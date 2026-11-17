/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's coop service (docs/modding/reference/services/coop.md): NebulaCoop's online room for mods, as sm64coopdx's
   Lua API gives it: the players and where they play, a line beside each in the player list, chat commands and the
   chat's answers, counters under the HUD's Power Star counter, and Nebula's all-time counts. Outside a room this
   player is index 0 of a room of one. It needs no permission. A plain Nebula has no such service: a mod that imports
   it plays only in NebulaCoop (a .lumacoop with "requires": ["coop"]).
   Import: LUMA_SERVICE(slot, "coop", "1.0"). */
#ifndef LUMA_COOP_H
#define LUMA_COOP_H

#include <stdint.h>

#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_COOP_NAME "coop"
#define LUMA_COOP_MAJOR 1u
#define LUMA_COOP_MINOR 0u

/* Players a room has at most (indexes 0 to 15). */
#define LUMA_COOP_MOST_PLAYERS 16u
/* The lines hud_counter has under the Power Star counter (1 to 4). */
#define LUMA_COOP_HUD_LINES 4u
/* The chat commands one mod adds at most. */
#define LUMA_COOP_COMMANDS_MOST 8u

/* LumaCoopPlayer.flags' bits. */
enum {
    LUMA_COOP_PLAYER_YOU = 1,   /* this player */
    LUMA_COOP_PLAYER_HOST = 2,  /* the room's host */
    LUMA_COOP_PLAYER_LUIGI = 4, /* their character's base is Luigi (else Mario) */
    LUMA_COOP_PLAYER_HERE = 8   /* they play in this player's galaxy and mission (this player too) */
};

/* One player of the room, as player gives it. */
typedef struct LumaCoopPlayer {
    uint32_t size;      /* sizeof(LumaCoopPlayer) as the mod was built */
    uint32_t index;     /* 0 to LUMA_COOP_MOST_PLAYERS - 1, kept while they stay */
    uint32_t flags;     /* LUMA_COOP_PLAYER_* */
    uint32_t stars;     /* the Power Stars they got in this room, new to its save */
    uint32_t ping;      /* the round trip to them in milliseconds (to the host, for a joiner); 0 for this player or unknown */
    uint32_t mission;   /* where they play: the mission (0 where none was chosen) */
    char name[32];      /* as the player list shows it, NUL-terminated UTF-8 */
    char character[32]; /* the character they play by id ("mario", "luigi", a mod's) */
    char stage[48];     /* the stage they play ("" until they reach one) */
} LumaCoopPlayer;       /* 1.0: 136 bytes */

typedef struct LumaCoopService {
    LumaServiceTable base;
    /* 1.0. Game thread. 1 while this player is in a room (hosting or joined), else 0. */
    uint32_t (*in_room)(const LumaMod* mod);
    /* 1.0. Game thread. 1 when this player is the room's host, or in no room; else 0. */
    uint32_t (*is_host)(const LumaMod* mod);
    /* 1.0. Game thread. This player's index (0 outside a room). */
    uint32_t (*me)(const LumaMod* mod);
    /* 1.0. Game thread. The room's size as its host set it (1 outside a room). */
    uint32_t (*max_players)(const LumaMod* mod);
    /* 1.0. Game thread. The player at `index` into *out. LUMA_OK; LUMA_E_NOT_FOUND when nobody has that index (an
       answer, not reported); a null out: LUMA_E_ARGUMENT, a size below 1.0's: LUMA_E_ARGUMENT (Service.StructSize),
       both reported. */
    int32_t (*player)(const LumaMod* mod, uint32_t index, LumaCoopPlayer* out);
    /* 1.0. Game thread. The colour the player at `index` wears for a part (a NEBULA_PALETTE_* number: 0 the cap),
       0xRRGGBB into *out (this player's own outside a room, as index 0). LUMA_OK; LUMA_E_NOT_FOUND when nobody has
       that index or there is no such part (an answer, not reported); a null out: LUMA_E_ARGUMENT, reported. */
    int32_t (*palette_color)(const LumaMod* mod, uint32_t index, uint32_t part, uint32_t* out);
    /* 1.0. Game thread. Text beside the player at `index` in this player's player list (sm64coopdx's
       network_player_set_description), in `colour`; message tags allowed ("[icon 7]3": a Power Star and 3), cut to 95
       bytes. "" takes it away; the player leaving, or the room closing, takes it away. Only this player's list shows
       it. LUMA_OK; LUMA_E_NOT_FOUND for an index past the room's most (an answer, not reported); a null text:
       LUMA_E_ARGUMENT, reported. */
    int32_t (*set_description)(const LumaMod* mod, uint32_t index, const char* text, LumaColor colour);
    /* 1.0. Game thread. A line in the chat for this player alone, as a command's answer (sm64coopdx's
       djui_chat_message_create): a text that is one of the game's chat texts' ids ("CoopCmd...") shows in the language
       played. Nothing outside a room. LUMA_OK; a null text: LUMA_E_ARGUMENT, reported. */
    int32_t (*chat_say)(const LumaMod* mod, const char* text);
    /* 1.0. Game thread. A counter under the HUD's Power Star counter, in its look with its star tinted `colour`
       (0xFFFFFFFF keeps the game's): `line` 1 is the first under it, to LUMA_COOP_HUD_LINES. It comes and goes with
       the HUD's counters and counts up with their flash as `value` grows; a smaller value, or a new colour, shows at
       once. `value` below 0 takes the line away; values above 2147483647 show as it. Kept across stages until the mod
       changes it or stops. LUMA_OK; a line outside 1 to LUMA_COOP_HUD_LINES: LUMA_E_ARGUMENT, reported. */
    int32_t (*hud_counter)(const LumaMod* mod, uint32_t line, LumaColor colour, int64_t value);
    /* 1.0. Game thread. One of Nebula's all-time counts (stats.ini, the achievement board's stats page) by its key:
       "power_stars", "coins"... into *out. LUMA_OK; LUMA_E_NOT_FOUND for no such count (an answer, not reported); a
       null name or out: LUMA_E_ARGUMENT, reported. */
    int32_t (*stat)(const LumaMod* mod, const char* name, uint64_t* out);
    /* 1.0. Game thread. A chat command (sm64coopdx's hook_chat_command), shown in a room for the rest of the run:
       /`name` (1 to 24 of a-z A-Z 0-9 _ -) with `description` as its /help line, `options` its words split by spaces
       ("on off"; "": a "[On|Off]" in the description gives them). Each time this player types it, the mod's watch fn
       gets a COMMAND event. The number it is given into *out (from 1, in the order the mod adds them). LUMA_OK;
       LUMA_E_EXISTS when the name is taken (another mod's, or the game's own: an answer, not reported); a name or a
       word outside the rule, a null text or out: LUMA_E_ARGUMENT, reported; LUMA_COOP_COMMANDS_MOST added already:
       LUMA_E_LIMIT, reported. A mod reloaded keeps its commands: adding one of its own again gives its number. */
    int32_t (*add_command)(const LumaMod* mod, const char* name, const char* description, const char* options,
                           uint32_t* out);
    /* 1.0. Game thread. `fn` gets the room's events (at the start of a frame, never inside this call) while the mod
       runs. A later call replaces fn and user; a null fn stops the events. LUMA_OK; 32 mods watching already:
       LUMA_E_LIMIT, reported; LUMA_E_MEMORY when there is no memory for it. */
    /* events: JOINED(value), CLOSED(), PLAYER_JOIN(value), PLAYER_LEAVE(value), PLAYER_PLACE(value), COMMAND(value, flags) */
    int32_t (*watch)(const LumaMod* mod, LumaEventFn fn, void* user);
} LumaCoopService;

/* Events of coop (LumaEvent.kind) to watch's fn; `handle` is 0, `status` LUMA_OK. */
typedef enum LumaCoopEvent {
    LUMA_COOP_EVENT_JOINED = 1,       /* this player is in a room: it opened one, or a host let it in. value: this
                                         player's index. The players already there come with it: no PLAYER_JOIN */
    LUMA_COOP_EVENT_CLOSED = 2,       /* this player's room ended: it left, or the host closed it */
    LUMA_COOP_EVENT_PLAYER_JOIN = 3,  /* another player came into the room. value: their index */
    LUMA_COOP_EVENT_PLAYER_LEAVE = 4, /* another player left the room. value: the index they had */
    LUMA_COOP_EVENT_PLAYER_PLACE = 5, /* a player (this one too) reached another galaxy or mission. value: their index */
    LUMA_COOP_EVENT_COMMAND = 6       /* this player typed one of the mod's commands. value: its number (add_command's);
                                         flags: the word after it, by its place in the command's options (1 the first; 0
                                         no word). A word that is none of them never comes: the chat says the command
                                         doesn't take it */
} LumaCoopEvent;

#ifdef __cplusplus
}
#endif

#endif /* LUMA_COOP_H */
