#pragma once

#include <nebula_types.h>

// Online co-op for mods (nebula_mod_api.h's coop_* calls and NEBULA_MOD_EVENT_COOP_*), as sm64coopdx's Lua has
// gNetworkPlayers and its player hooks: the room's players by index (the host 0, then as they joined), read from the
// host's player list once a frame, and the events of what changed since the last frame.
namespace NebulaCoopMods {
    // Each frame on the game's thread (GameSystem::update), before the mods' frame: the room's players read again,
    // and room_joined, player_join, player_leave, player_place and room_closed sent for what changed, then the
    // mods' chat commands this player typed (chat_command); each also goes to the coop service's watchers (LumaCoop).
    // Nothing while no mod runs.
    void update();

    s32 inRoom();
    s32 isHost();
    s32 me();
    s32 count();
    s32 maxPlayers();
    s32 connected(s32 index);
    const char* name(s32 index);
    s32 character(s32 index);
    const char* stage(s32 index);
    s32 mission(s32 index);
    s32 samePlace(s32 index);
    s32 stars(s32 index);
    s32 ping(s32 index);
    // The id of the character they play ("" for nobody).
    const char* characterId(s32 index);
    // Their NEBULA_COOP_LIST_* flags (this player's outside a room, as index 0: you and host); 0 for nobody.
    u32 listFlags(s32 index);

    // What this player's mods show beside a player in the list (coop_set_description, sm64coopdx's
    // network_player_set_description): null or "" takes it away. Kept by index until its player leaves or the room
    // ends.
    void setDescription(s32 index, const char* pUtf8, u32 rgba);
    // A player's, with its colour (0xRRGGBBAA) in pColor: null for none.
    const char* description(s32 index, u32* pColor);
    // A line in the chat for this player alone (coop_chat_say); nothing outside a room.
    void chatSay(const char* pUtf8);
    // The colour player `index` wears for a part (0xRRGGBB): this player's own as theirs; -1 for nobody or no such part.
    s32 paletteColor(s32 index, s32 part);
};  // namespace NebulaCoopMods
