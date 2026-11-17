#include "Game/System/LumaCoop.hpp"

#include "Game/Player/NebulaPalette.hpp"
#include "Game/Screen/NebulaHudCounter.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/System/NebulaCoopMods.hpp"
#include "Game/System/NebulaHost.hpp"
#include <luma.h>
#include <luma_coop.h>
#include <luma_service_provider.h>
#include <luma_services.h>
#include <nebula_format.h>
#include <nebula_game_api.h>
#include <nebula_mod_api.h>
#include <cstring>

// The coop service (docs/modding/reference/services/coop.md). Game thread throughout: the calls, the provider, the
// room's events (NebulaCoopMods::update).
//
// No cost with no mod: NebulaCoopMods reads the room only once a mod has called the service (or has the old rows'
// table); event() and command() are one load while no mod watches.

namespace luma {
    namespace coop {
        namespace {
            // Events kept for one mod between two frames' starts; past it the oldest wait is dropped.
            const u32 cQueued = 32;
            // Mods that watch at once.
            const u32 cWatches = 32;
            // Commands added in a run, over every mod.
            const u32 cCommands = 64;

            struct Pending {
                u32 kind;
                u32 flags;
                u64 value;
            };

            // One mod's watch: its node first, so the kernel's node is the watch.
            struct Watch {
                LumaEventNode node;
                LumaEventFn fn;  // null: stopped (the slot frees when the node is idle)
                void* user;
                Pending queued[cQueued];
                u32 first;
                u32 count;
            };

            // A chat command: kept for the run under its mod's id, so a reloaded mod gets its numbers back.
            struct Command {
                char modId[68];
                char name[25];
                const LumaMod* mod;  // null while its mod is not running
                u32 number;          // from 1, in its mod's order
            };

            const LumaServiceKernel* sKernel = nullptr;
            bool sUsed = false;
            Watch* sWatches[cWatches];
            u32 sWatching = 0;  // watches with a fn
            Command sCommands[cCommands];
            u32 sCommandCount = 0;
            // Who set each HUD line (it goes with its mod).
            const LumaMod* sHudOwners[LUMA_COOP_HUD_LINES];

            void report(const LumaMod* pMod, const char* pWhere, u32 code, const char* pArg0, const char* pArg1,
                        const char* pArg2) {
                const char* args[3] = {pArg0, pArg1, pArg2};
                u32 count = 0;
                while (count < 3 && args[count] != nullptr) {
                    count++;
                }
                sKernel->report(pMod, pWhere, code, args, count);
            }

            s32 enter(const LumaMod* pMod, const char* pWhere) {
                if (pMod == nullptr || sKernel->mod_id(pMod) == nullptr) {
                    sKernel->unknown_mod(pWhere);
                    return LUMA_E_ARGUMENT;
                }
                if (sKernel->mod_live(pMod) == 0) {
                    return LUMA_E_CANCELLED;
                }
                if (sKernel->game_thread() == 0) {
                    report(pMod, pWhere, 100u, nullptr, nullptr, nullptr);
                    return LUMA_E_THREAD;
                }
                sUsed = true;
                return LUMA_OK;
            }

            s32 wrong(const LumaMod* pMod, const char* pWhere, const char* pArgument) {
                report(pMod, pWhere, 101u, pArgument, nullptr, nullptr);
                return LUMA_E_ARGUMENT;
            }

            void limit(const LumaMod* pMod, const char* pWhere, u32 most) {
                char text[16];
                nebula_snprintf(text, sizeof(text), "%u", most);
                report(pMod, pWhere, 104u, text, nullptr, nullptr);
            }

            void copyText(char* pTo, u32 capacity, const char* pFrom) {
                nebula_snprintf(pTo, capacity, "%s", pFrom != nullptr ? pFrom : "");
            }

            bool sameText(const char* pA, const char* pB) {
                return std::strcmp(pA, pB) == 0;
            }

            // A command's name or word: 1 to 24 of a-z A-Z 0-9 _ -.
            bool wordOk(const char* pWord, u32 length) {
                if (length == 0 || length > 24) {
                    return false;
                }
                for (u32 i = 0; i < length; i++) {
                    const char c = pWord[i];
                    const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                       c == '_' || c == '-';
                    if (!plain) {
                        return false;
                    }
                }
                return true;
            }

            // Case aside, as the chat matches them.
            bool sameName(const char* pA, const char* pB) {
                for (;; pA++, pB++) {
                    const char a = *pA >= 'A' && *pA <= 'Z' ? static_cast< char >(*pA - 'A' + 'a') : *pA;
                    const char b = *pB >= 'A' && *pB <= 'Z' ? static_cast< char >(*pB - 'A' + 'a') : *pB;
                    if (a != b) {
                        return false;
                    }
                    if (a == '\0') {
                        return true;
                    }
                }
            }

            Watch* watchOf(const LumaMod* pMod) {
                for (u32 i = 0; i < cWatches; i++) {
                    if (sWatches[i] != nullptr && sWatches[i]->node.mod == pMod) {
                        return sWatches[i];
                    }
                }
                return nullptr;
            }

            void freeWatch(Watch* pWatch) {
                for (u32 i = 0; i < cWatches; i++) {
                    if (sWatches[i] == pWatch) {
                        sWatches[i] = nullptr;
                    }
                }
                sKernel->free(pWatch->node.mod, pWatch);
            }

            void stopWatch(Watch* pWatch) {
                if (pWatch->fn != nullptr) {
                    pWatch->fn = nullptr;
                    sWatching--;
                }
                pWatch->count = 0;
            }

            void deliver(LumaEventNode* pNode) {
                Watch* const pWatch = reinterpret_cast< Watch* >(pNode);
                // A post from inside a call queues the node for the next frame: these are this frame's.
                const u32 count = pWatch->count;
                for (u32 i = 0; i < count && pWatch->fn != nullptr && pWatch->count > 0; i++) {
                    const Pending pending = pWatch->queued[pWatch->first];
                    pWatch->first = (pWatch->first + 1) % cQueued;
                    pWatch->count--;
                    LumaEvent event = {};
                    event.size = sizeof(LumaEvent);
                    event.kind = pending.kind;
                    event.status = LUMA_OK;
                    event.flags = pending.flags;
                    event.value = pending.value;
                    (void)sKernel->call(pWatch->node.mod, LUMA_COOP_NAME, pWatch->fn, pWatch->user, &event);
                }
            }

            void drop(LumaEventNode* pNode) {
                reinterpret_cast< Watch* >(pNode)->count = 0;
            }

            void queue(Watch* pWatch, u32 kind, u64 value, u32 flags) {
                if (pWatch == nullptr || pWatch->fn == nullptr) {
                    return;
                }
                if (pWatch->count == cQueued) {
                    pWatch->first = (pWatch->first + 1) % cQueued;
                    pWatch->count--;
                }
                Pending& rPending = pWatch->queued[(pWatch->first + pWatch->count) % cQueued];
                rPending.kind = kind;
                rPending.flags = flags;
                rPending.value = value;
                pWatch->count++;
                (void)sKernel->post(&pWatch->node);
            }

            // ---- The table ----

            uint32_t svcInRoom(const LumaMod* pMod) {
                return enter(pMod, "coop.in_room") == LUMA_OK ? static_cast< u32 >(NebulaCoopMods::inRoom()) : 0u;
            }

            uint32_t svcIsHost(const LumaMod* pMod) {
                return enter(pMod, "coop.is_host") == LUMA_OK ? static_cast< u32 >(NebulaCoopMods::isHost()) : 1u;
            }

            uint32_t svcMe(const LumaMod* pMod) {
                return enter(pMod, "coop.me") == LUMA_OK ? static_cast< u32 >(NebulaCoopMods::me()) : 0u;
            }

            uint32_t svcMaxPlayers(const LumaMod* pMod) {
                return enter(pMod, "coop.max_players") == LUMA_OK ? static_cast< u32 >(NebulaCoopMods::maxPlayers()) : 1u;
            }

            int32_t svcPlayer(const LumaMod* pMod, uint32_t index, LumaCoopPlayer* pOut) {
                const char* const pWhere = "coop.player";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pOut == nullptr) {
                    return wrong(pMod, pWhere, "out");
                }
                if (pOut->size < sizeof(LumaCoopPlayer)) {
                    char found[16];
                    char expected[16];
                    nebula_snprintf(found, sizeof(found), "%u", static_cast< u32 >(pOut->size));
                    nebula_snprintf(expected, sizeof(expected), "%u", static_cast< u32 >(sizeof(LumaCoopPlayer)));
                    report(pMod, pWhere, 103u, "LumaCoopPlayer", found, expected);
                    return LUMA_E_ARGUMENT;
                }
                const s32 i = index < LUMA_COOP_MOST_PLAYERS ? static_cast< s32 >(index) : -1;
                if (i < 0 || NebulaCoopMods::connected(i) == 0) {
                    return LUMA_E_NOT_FOUND;
                }
                const u32 size = pOut->size;
                std::memset(pOut, 0, sizeof(LumaCoopPlayer));
                pOut->size = size;
                pOut->index = index;
                const u32 list = NebulaCoopMods::listFlags(i);
                pOut->flags = ((list & NEBULA_COOP_LIST_YOU) != 0 ? LUMA_COOP_PLAYER_YOU : 0u) |
                              ((list & NEBULA_COOP_LIST_HOST) != 0 ? LUMA_COOP_PLAYER_HOST : 0u) |
                              (NebulaCoopMods::character(i) == 1 ? LUMA_COOP_PLAYER_LUIGI : 0u) |
                              (NebulaCoopMods::samePlace(i) != 0 ? LUMA_COOP_PLAYER_HERE : 0u);
                pOut->stars = static_cast< u32 >(NebulaCoopMods::stars(i));
                pOut->ping = static_cast< u32 >(NebulaCoopMods::ping(i));
                pOut->mission = static_cast< u32 >(NebulaCoopMods::mission(i));
                copyText(pOut->name, sizeof(pOut->name), NebulaCoopMods::name(i));
                copyText(pOut->character, sizeof(pOut->character), NebulaCoopMods::characterId(i));
                copyText(pOut->stage, sizeof(pOut->stage), NebulaCoopMods::stage(i));
                return LUMA_OK;
            }

            int32_t svcPaletteColor(const LumaMod* pMod, uint32_t index, uint32_t part, uint32_t* pOut) {
                const char* const pWhere = "coop.palette_color";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pOut == nullptr) {
                    return wrong(pMod, pWhere, "out");
                }
                if (index >= LUMA_COOP_MOST_PLAYERS || part >= nebula_palette::PartCount) {
                    return LUMA_E_NOT_FOUND;
                }
                const s32 color = NebulaCoopMods::paletteColor(static_cast< s32 >(index), static_cast< s32 >(part));
                if (color < 0) {
                    return LUMA_E_NOT_FOUND;
                }
                *pOut = static_cast< u32 >(color);
                return LUMA_OK;
            }

            int32_t svcSetDescription(const LumaMod* pMod, uint32_t index, const char* pText, LumaColor colour) {
                const char* const pWhere = "coop.set_description";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pText == nullptr) {
                    return wrong(pMod, pWhere, "text");
                }
                if (index >= LUMA_COOP_MOST_PLAYERS) {
                    return LUMA_E_NOT_FOUND;
                }
                NebulaCoopMods::setDescription(static_cast< s32 >(index), pText, colour);
                return LUMA_OK;
            }

            int32_t svcChatSay(const LumaMod* pMod, const char* pText) {
                const char* const pWhere = "coop.chat_say";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pText == nullptr) {
                    return wrong(pMod, pWhere, "text");
                }
                NebulaCoopMods::chatSay(pText);
                return LUMA_OK;
            }

            int32_t svcHudCounter(const LumaMod* pMod, uint32_t line, LumaColor colour, int64_t value) {
                const char* const pWhere = "coop.hud_counter";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (line < 1 || line > LUMA_COOP_HUD_LINES) {
                    return wrong(pMod, pWhere, "line");
                }
                const s32 number = value < 0 ? -1 : (value > 0x7FFFFFFFll ? 0x7FFFFFFF : static_cast< s32 >(value));
                (void)NebulaHudCounter::setLine(static_cast< s32 >(line), colour, number);
                sHudOwners[line - 1] = number < 0 ? nullptr : pMod;
                return LUMA_OK;
            }

            int32_t svcStat(const LumaMod* pMod, const char* pName, uint64_t* pOut) {
                const char* const pWhere = "coop.stat";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pName == nullptr || pOut == nullptr) {
                    return wrong(pMod, pWhere, pName == nullptr ? "name" : "out");
                }
                const s32 stat = NebulaAchievements::findStat(pName);
                if (stat < 0 || stat >= NebulaStat_Num) {
                    return LUMA_E_NOT_FOUND;
                }
                *pOut = NebulaAchievements::getStat(stat);
                return LUMA_OK;
            }

            int32_t svcAddCommand(const LumaMod* pMod, const char* pName, const char* pDescription, const char* pOptions,
                                  uint32_t* pOut) {
                const char* const pWhere = "coop.add_command";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                if (pName == nullptr || !wordOk(pName, static_cast< u32 >(std::strlen(pName)))) {
                    return wrong(pMod, pWhere, "name");
                }
                if (pDescription == nullptr) {
                    return wrong(pMod, pWhere, "description");
                }
                if (pOut == nullptr) {
                    return wrong(pMod, pWhere, "out");
                }
                if (pOptions == nullptr) {
                    return wrong(pMod, pWhere, "options");
                }
                for (const char* pWord = pOptions; *pWord != '\0';) {
                    const char* pEnd = pWord;
                    while (*pEnd != '\0' && *pEnd != ' ') {
                        pEnd++;
                    }
                    if (pEnd != pWord && !wordOk(pWord, static_cast< u32 >(pEnd - pWord))) {
                        return wrong(pMod, pWhere, "options");
                    }
                    pWord = *pEnd == ' ' ? pEnd + 1 : pEnd;
                }
                const char* const pId = sKernel->mod_id(pMod);
                u32 mine = 0;
                for (u32 i = 0; i < sCommandCount; i++) {
                    Command& rCommand = sCommands[i];
                    if (!sameText(rCommand.modId, pId)) {
                        continue;
                    }
                    mine++;
                    if (sameName(rCommand.name, pName)) {
                        // Its own again (a reload): the chat has it already.
                        rCommand.mod = pMod;
                        *pOut = rCommand.number;
                        return LUMA_OK;
                    }
                }
                if (mine >= LUMA_COOP_COMMANDS_MOST || sCommandCount >= cCommands) {
                    limit(pMod, pWhere, mine >= LUMA_COOP_COMMANDS_MOST ? LUMA_COOP_COMMANDS_MOST : cCommands);
                    return LUMA_E_LIMIT;
                }
                if (!NebulaHost::coopCommandAdd(pName, pDescription, pOptions)) {
                    return LUMA_E_EXISTS;
                }
                Command& rCommand = sCommands[sCommandCount++];
                copyText(rCommand.modId, sizeof(rCommand.modId), pId);
                copyText(rCommand.name, sizeof(rCommand.name), pName);
                rCommand.mod = pMod;
                rCommand.number = mine + 1;
                *pOut = rCommand.number;
                return LUMA_OK;
            }

            int32_t svcWatch(const LumaMod* pMod, LumaEventFn fn, void* pUser) {
                const char* const pWhere = "coop.watch";
                if (const s32 status = enter(pMod, pWhere); status != LUMA_OK) {
                    return status;
                }
                Watch* pWatch = watchOf(pMod);
                if (fn == nullptr) {
                    if (pWatch != nullptr) {
                        stopWatch(pWatch);
                    }
                    return LUMA_OK;
                }
                if (pWatch == nullptr) {
                    u32 slot = cWatches;
                    for (u32 i = 0; i < cWatches && slot == cWatches; i++) {
                        slot = sWatches[i] == nullptr ? i : slot;
                    }
                    if (slot == cWatches) {
                        limit(pMod, pWhere, cWatches);
                        return LUMA_E_LIMIT;
                    }
                    pWatch = static_cast< Watch* >(sKernel->alloc(pMod, sizeof(Watch), alignof(Watch)));
                    if (pWatch == nullptr) {
                        return LUMA_E_MEMORY;
                    }
                    std::memset(pWatch, 0, sizeof(Watch));
                    pWatch->node.mod = pMod;
                    pWatch->node.deliver = deliver;
                    pWatch->node.drop = drop;
                    sWatches[slot] = pWatch;
                }
                if (pWatch->fn == nullptr) {
                    sWatching++;
                }
                pWatch->fn = fn;
                pWatch->user = pUser;
                return LUMA_OK;
            }

            const LumaCoopService cTable = {
                {sizeof(LumaCoopService), LUMA_COOP_MAJOR, LUMA_COOP_MINOR},
                svcInRoom,
                svcIsHost,
                svcMe,
                svcMaxPlayers,
                svcPlayer,
                svcPaletteColor,
                svcSetDescription,
                svcChatSay,
                svcHudCounter,
                svcStat,
                svcAddCommand,
                svcWatch,
            };

            // ---- The provider ----

            // The mod's stop: no more events, its HUD lines go, its commands wait for it to come back.
            void modGone(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                if (Watch* const pWatch = watchOf(pMod); pWatch != nullptr) {
                    stopWatch(pWatch);
                }
                for (u32 line = 0; line < LUMA_COOP_HUD_LINES; line++) {
                    if (sHudOwners[line] == pMod) {
                        (void)NebulaHudCounter::setLine(static_cast< s32 >(line + 1), 0xFFFFFFFF, -1);
                        sHudOwners[line] = nullptr;
                    }
                }
                for (u32 i = 0; i < sCommandCount; i++) {
                    if (sCommands[i].mod == pMod) {
                        sCommands[i].mod = nullptr;
                    }
                }
            }

            // Its memory goes after this: the watch with it.
            void modEnd(void* pUser, const LumaMod* pMod) {
                (void)pUser;
                if (Watch* const pWatch = watchOf(pMod); pWatch != nullptr) {
                    freeWatch(pWatch);
                }
            }

            void registered(void* pUser, const LumaServiceKernel* pKernel) {
                (void)pUser;
                sKernel = pKernel;
            }

            const LumaServiceProvider cProvider = {
                sizeof(LumaServiceProvider),
                LUMA_COOP_NAME,
                &cTable.base,
                nullptr,
                nullptr,
                0,
                nullptr,
                nullptr,
                modGone,
                modEnd,
                registered,
            };
        }  // namespace

        bool used() {
            return sUsed;
        }

        void event(u32 kind, s32 value) {
            if (sWatching == 0) {
                return;
            }
            for (u32 i = 0; i < cWatches; i++) {
                queue(sWatches[i], kind, static_cast< u64 >(value < 0 ? 0 : value), 0);
            }
        }

        void command(const char* pName, s32 word) {
            if (sWatching == 0 || pName == nullptr) {
                return;
            }
            for (u32 i = 0; i < sCommandCount; i++) {
                const Command& rCommand = sCommands[i];
                if (rCommand.mod != nullptr && sameName(rCommand.name, pName)) {
                    queue(watchOf(rCommand.mod), LUMA_COOP_EVENT_COMMAND, rCommand.number,
                          static_cast< u32 >(word < 0 ? 0 : word));
                    return;
                }
            }
        }

        const LumaServiceProvider* provider() {
            return &cProvider;
        }
    }
}
