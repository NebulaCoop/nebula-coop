/* SPDX-License-Identifier: GPL-3.0-only */
/* The rows of mods that are not code (luma.json), as the host hands them to the game module: galaxies, text, music,
   progression, object changes, power-ups, sounds, player animations and constants, story, tunables, and the events an
   achievement waits for. A mod's code is a LumaCore mod (luma.h); the old plugin interface, the script interpreter
   and its toolkit are gone.

   Passing null where a call needs a name makes the call do nothing and return -1. Passing NaN or infinity as a value
   also does nothing. */
#ifndef NEBULA_MOD_API_H
#define NEBULA_MOD_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The table's layout: the host and the game module are built together, so a different number is refused. */
#define NEBULA_MOD_API_VERSION 5u

/* An object in the game (a LiveActor in the game's code), as an event names it. */
typedef struct NebulaActor NebulaActor;

/* The game's events. An achievement waits for one (luma.json); a mod's code watches them with the events service
   (luma_events.h), which tells it at the next frame's start; to act at the moment itself, a mod hooks the function
   that raises the event. Callbacks run on the thread that raises the event: the game thread, where stages load too. */
typedef enum NebulaModEvent {
    /* Every frame, in every scene including menus. */
    NEBULA_MOD_EVENT_FRAME = 0,
    /* Every frame of a stage, before objects move. */
    NEBULA_MOD_EVENT_STAGE_FRAME,
    /* A stage started. name: the stage. value: the mission. */
    NEBULA_MOD_EVENT_STAGE_START,
    /* The player lost one health. */
    NEBULA_MOD_EVENT_PLAYER_DAMAGE,
    /* The player lost a life. value: the cause (0 is falling out of the level). */
    NEBULA_MOD_EVENT_PLAYER_MISS,
    /* The player's power-up changed. value: the new one (NEBULA_POWER_UP_*). */
    NEBULA_MOD_EVENT_POWER_UP,
    /* Coins were added. value: how many. */
    NEBULA_MOD_EVENT_COIN,
    /* Star bits were added. value: how many. */
    NEBULA_MOD_EVENT_STAR_BIT,
    /* A star was collected. name: the galaxy. value: the star number. */
    NEBULA_MOD_EVENT_STAR_GET,
    /* A cutscene started or ended. name: the cutscene. */
    NEBULA_MOD_EVENT_CUTSCENE_START,
    NEBULA_MOD_EVENT_CUTSCENE_END,
    /* An object appeared or was killed. actor: the object. name: its name. Costs one call per object. */
    NEBULA_MOD_EVENT_ACTOR_APPEAR,
    NEBULA_MOD_EVENT_ACTOR_KILL,
    /* The player started a spin. */
    NEBULA_MOD_EVENT_PLAYER_SPIN,
    /* The player jumped off the ground, or landed. */
    NEBULA_MOD_EVENT_PLAYER_JUMP,
    NEBULA_MOD_EVENT_PLAYER_LAND,
    /* The pause menu opened (value 1) or closed (value 0). */
    NEBULA_MOD_EVENT_PAUSE,
    /* The stage is ending; all its actors and layouts are destroyed after this. name: the stage. */
    NEBULA_MOD_EVENT_STAGE_END,
    /* The game wrote the save. */
    NEBULA_MOD_EVENT_GAME_SAVE,
    /* A power star appeared in the stage. value: its number. */
    NEBULA_MOD_EVENT_POWER_STAR_APPEAR,
    /* A line of dialogue opened in its balloon. actor: the speaker. name: the message id, as luma.json "text" uses it
       (null for a line without an id). */
    NEBULA_MOD_EVENT_TALK_LINE,
    /* The player answered a yes/no question. value: 1 yes, 0 no. actor: who asked. name: the question's message id. */
    NEBULA_MOD_EVENT_TALK_CHOICE,
    /* A part of a cutscene started. value: the part's index, from 0. name: the part's name in the cutscene's
       time sheet. */
    NEBULA_MOD_EVENT_CUTSCENE_PART,
    /* The camera changed. value: 0 for an area camera (also when the last event camera ends), 1 for an event camera.
       name: its id in the stage's camera table. */
    NEBULA_MOD_EVENT_CAMERA_CHANGE,
    /* A sound effect started. value: its number. name: its name. Costs one call per sound. */
    NEBULA_MOD_EVENT_SOUND_START,
    /* An object started an effect. actor: the object. name: the effect's name in the object's effect table. */
    NEBULA_MOD_EVENT_EFFECT_START,
    /* A screen layout (a counter, a menu, a message box) was shown (value 1) or removed (value 0). name: its name in
       the game's code. */
    NEBULA_MOD_EVENT_SCREEN,
    /* An object received a message from another: an attack, a stomp, a push, a grab (numbers in
       docs/modding/reference/events.md). actor: the receiver. name: its name. value: the message. */
    NEBULA_MOD_EVENT_ACTOR_MESSAGE,
    /* The player started an action: swimming, hanging, sliding, falling (numbers in docs/modding/reference/events.md).
       value: the action. */
    NEBULA_MOD_EVENT_PLAYER_ACTION,
    /* Online co-op (NebulaCoop; coop_* below). Another player came into the room. value: their index (coop_name).
       name: their name. Not sent for the players already there when this player joins: room_joined comes then. */
    NEBULA_MOD_EVENT_COOP_PLAYER_JOIN,
    /* Another player left the room. value: the index they had. name: their name. */
    NEBULA_MOD_EVENT_COOP_PLAYER_LEAVE,
    /* This player is in a room: it opened one, or a host let it in. value: this player's index (coop_me). */
    NEBULA_MOD_EVENT_COOP_ROOM_JOINED,
    /* This player's room ended: it left, or the host closed it. */
    NEBULA_MOD_EVENT_COOP_ROOM_CLOSED,
    /* A player (this one too) reached another galaxy or mission. value: their index. name: the stage (coop_stage). */
    NEBULA_MOD_EVENT_COOP_PLAYER_PLACE,
    /* This player typed one of the mod's chat commands (sm64coopdx's hook_chat_command; the coop service's
       add_command). value: the word after it, by its place in the command's options (1 the first; 0 no word). name:
       the command. A word that is none of its options never comes: the chat says the command doesn't take it. */
    NEBULA_MOD_EVENT_COOP_COMMAND,
    NEBULA_MOD_EVENT_COUNT
} NebulaModEvent;

typedef struct NebulaModEventData {
    int32_t value;
    NebulaActor* actor;
    const char* name;
} NebulaModEventData;

/* Callbacks subscribed to the same event run in the order they subscribed. They are told; nothing they do changes
   the event. */
typedef void (*NebulaModCallback)(const NebulaModEventData* data, void* user);

/* The player's power-ups. */
typedef enum NebulaPowerUp {
    NEBULA_POWER_UP_NONE = 0,
    NEBULA_POWER_UP_INVINCIBLE = 1,
    NEBULA_POWER_UP_FIRE = 2,
    NEBULA_POWER_UP_ICE = 3,
    NEBULA_POWER_UP_BEE = 4,
    NEBULA_POWER_UP_SPRING = 5,
    NEBULA_POWER_UP_BOO = 6,
    NEBULA_POWER_UP_FLYING = 7,
    NEBULA_POWER_UP_TORNADO = 9
} NebulaPowerUp;

/* A new galaxy. Its files go in StageData/<name>/ like any galaxy's. */
typedef struct NebulaGalaxy {
    uint32_t size;
    const char* name;
    const char* map_pane;           /* its icon's pane in the map layout (MapGrandGalaxy). Null: not on the map */
    const char* open_condition[3];  /* event flags that open it, or null */
    int32_t stars_to_open;          /* power stars needed to open it */
    int32_t dome;                   /* its dome: 1 to 6, 0 for none */
    int32_t has_map_position;       /* 1: the game creates the icon's pane at map_position */
    float map_position[2];          /* right and up from the middle of the dome's page (or the grand map with no dome), on a 608 x 456 page */
} NebulaGalaxy;

/* A playable character (add_character), as sm64coopdx's character table has them: a model on one of the game's two
   players, its base (`like`), which gives the animations, voice, effects and the game's own Luigi code. Its model is
   ObjectData/<archive>.arc, the model in it named as the archive (case doesn't matter), with the base's skeleton. Its
   other models follow the game's names with the base's name swapped for the archive's ("MarioFace" becomes
   "WarioFace", "BeeMario" "BeeWario", "MarioHandL" "WarioHandL"); one the mod doesn't ship is the base's. The files
   inside keep the game's names. It has its own player numbers (set_player_constant), first its base's as the game
   has them. docs/modding/CHARACTERS.md says more. */
/* The mod adding a character (opaque; null for a luma.json "characters" row). */
typedef struct NebulaPlugin NebulaPlugin;

typedef struct NebulaCharacterClass {
    uint32_t size;       /* sizeof(NebulaCharacterClass) */
    const char* id;      /* 1-31 of a-z, 0-9, '_', '-' ("wario"); how luma.json, saves and online co-op name it */
    const char* name;    /* as menus show it ("WARIO"); null: the id in capitals */
    const char* like;    /* "mario" or "luigi"; null: "mario" */
    const char* archive; /* null: the id with a capital first letter ("Wario") */
    /* Its own colours (sm64coopdx's character palette): 0xRRGGBB for each NEBULA_PALETTE_* part, as its model's
       textures have them, which the parts are found by and a player's colours drawn against. Null: its base's. */
    const int32_t* palette;
} NebulaCharacterClass;

/* A player's colours, sm64coopdx's PlayerPart: the parts of a palette, in its order. A colour is 0xRRGGBB. */
enum {
    NEBULA_PALETTE_PANTS = 0, /* the overalls */
    NEBULA_PALETTE_SHIRT = 1,
    NEBULA_PALETTE_GLOVES = 2,
    NEBULA_PALETTE_SHOES = 3,
    NEBULA_PALETTE_HAIR = 4,
    NEBULA_PALETTE_SKIN = 5,
    NEBULA_PALETTE_CAP = 6,
    NEBULA_PALETTE_EMBLEM = 7, /* the cap's letter */
    NEBULA_PALETTE_PARTS = 8
};

/* A new power-up. Give it to the player with set_value(NEBULA_SET_POWER_UP, its number). */
typedef struct NebulaPowerUpClass {
    uint32_t size;
    const char* name;
    int32_t frames;       /* how long it lasts, at 60 per second. 0: until something ends it */
    const char* music;    /* music while it lasts: a BGM_ name, or null */
    int32_t color_frame;  /* the player's colours: a frame of Mario.arc's ColorChange (0 normal, 1 Flying, 2 Fire). 0: those of
                             looks_like. A mod's Mario.arc can add more frames. */
    const char* model;    /* a whole-body model, ObjectData/<model>.arc, on the player's skeleton. Null: the normal model */
    int32_t looks_like;   /* without a model: look like this power-up (NEBULA_POWER_UP_*) */
    int32_t acts_like;    /* what the player can do in it: one of the game's power-ups (NEBULA_POWER_UP_*): Fire's throw,
                             Ice's skate, Bee's flight... Its music and time too, where the class gives none (frames 0,
                             music null). 0: the normal player. One the game can't give in this stage refuses the power-up. */
} NebulaPowerUpClass;

/* A material's texture coordinate (nebula_mod_list_materials, NebulaModCalls.hpp): how it is made (the game's TexGen
   type, source and matrix), its matrix's kind and projection (255: no matrix), and whether a stage of the material
   reads it. */
typedef struct NebulaMaterialCoord {
    uint32_t type, source, matrix;
    uint32_t matrix_kind, projection;
    uint32_t used;
} NebulaMaterialCoord;
/* A model's material (nebula_mod_list_materials). Valid during the callback. */
typedef struct NebulaMaterialInfo {
    /* sizeof(NebulaMaterialInfo): fields are only added at the end. */
    uint32_t size;
    /* Its place in the model and its name. */
    int32_t index;
    const char* name;
    /* Its texture coordinates: coord_count of them at coords. */
    int32_t coord_count;
    const NebulaMaterialCoord* coords;
} NebulaMaterialInfo;
/* Called once for each material of the model, with the call's `user`. */
typedef void (*NebulaMaterialCallback)(const NebulaMaterialInfo* material, void* user);

/* Where an object change applies. */
typedef enum NebulaObjectsIn {
    NEBULA_OBJECTS_PLACED = 0,        /* anything the game creates by name: a level's objects, areas */
    NEBULA_OBJECTS_DISPLAY_MODEL = 1, /* the still models shown before an item is collected */
    NEBULA_OBJECTS_TREASURE_BOX = 2,  /* what a treasure box holds */
    NEBULA_OBJECTS_IN_COUNT
} NebulaObjectsIn;

/* The game module's table (nebula_game_mod_api). The host applies every mod's rows through it once, before the first
   stage; each call returns 0 when done unless it says otherwise. */
typedef struct NebulaModApi {
    uint32_t size;
    uint32_t version;

    /* Calls `callback` at each `event` (NEBULA_MOD_EVENT_*). */
    int32_t (*subscribe)(NebulaModEvent event, NebulaModCallback callback, void* user);
    /* Adds a galaxy. -2: no scenario archive (StageData/<name>/<name>Scenario.arc in the mod, StageData/<name>Scenario
       in the store), so it is skipped; -1: any other problem (its name is already one of the game's). */
    int32_t (*add_galaxy)(const NebulaGalaxy* galaxy);
    /* A hungry Luma: placed in a level as TicoGalaxy with Arg7 set to `slot`, it opens `galaxy` when fed
       `star_bits`. The game uses slots 0 to 6; using one replaces the game's Luma. */
    int32_t (*add_hungry_luma)(const char* galaxy, int32_t slot, int32_t star_bits);
    /* Replaces or adds a message by id: GalaxyName_<galaxy>, ScenarioName_<galaxy><n>, a conversation's id. An
       existing line keeps its talk settings. The text may use the game's message tags in brackets (nebula_text_tags.h). */
    int32_t (*add_text)(const char* id, const char* utf8);
    /* Like add_text; a new message also copies the camera, sound, talk and balloon settings of the game's message
       `like_id` (null or "" behaves like add_text). */
    int32_t (*add_text_like)(const char* id, const char* utf8, const char* like_id);
    /* Adds a power-up: its number, or below 0 when it can't be added. */
    int32_t (*add_power_up)(const NebulaPowerUpClass* power_up);
    /* Adds a music track: `file` is its path in the game, AudioRes/Stream/MyTrack.wav (.ogg, .ast). Any `name` not
       starting with SE_, BGM_, MBGM_, STM_ or ME_, which are the game's. */
    int32_t (*add_music)(const char* name, const char* file);
    /* The music a stage plays: `scenario` is a mission, 0 for every mission without its own entry. */
    int32_t (*set_stage_music)(const char* stage, int32_t scenario, const char* name);
    /* The music a ChangeBgmCube with Obj_arg1 equal to `area` starts in the stage. */
    int32_t (*set_area_music)(const char* stage, int32_t area, const char* name);
    /* Changes or adds a progression row by name: what unlocks what, and what each hungry Luma costs. `kind`: 1 stars,
       2 a galaxy's stars, 3 a special star, 4 two flags, 5 story, 6 a galaxy, 7 a comet, 8 a hungry Luma. `a` and `b`
       depend on the kind: a star number, or a Luma's slot and price. */
    int32_t (*set_event_flag)(const char* name, int32_t kind, int32_t a, int32_t b, const char* first, const char* second);
    /* A dome's full comet schedule, in order (domes 1 to 6 are the game's; 7, 8 and up add domes as long as the
       numbers have no gap): `galaxies[i]`, with `coins[i]` 1 for the purple coin comet. */
    int32_t (*set_comet_schedule)(int32_t dome, const char* const* galaxies, const int32_t* coins, int32_t count);
    /* Replaces one of the game's objects with another, or removes it (`becomes` null). Where: NEBULA_OBJECTS_*. */
    int32_t (*set_object_change)(int32_t where, const char* object, const char* becomes);
    /* A comet mission's time limit in seconds. */
    int32_t (*set_comet_time_limit)(const char* galaxy, int32_t mission, int32_t seconds);
    /* Makes a stage follow another stage's rules: the game's stage-specific rules (a Bowser arena's star, a hub's
       counters, ocean swimming) treat it as `acts_as`. own_rules 0: its own rules no longer apply. `scenario`: one
       mission, 0 for all. */
    int32_t (*set_stage_rules)(const char* stage, int32_t scenario, const char* acts_as, int32_t own_rules);
    /* Sets a value in the player's constants table by name (mJumpHeight, mFireModeTime). `character`: 0 Mario, 1 Luigi, or a
       mod's character's index (character_find). `index`: position in a list, else 0. Any time. 0: done; -1: no such
       value or character. */
    int32_t (*set_player_constant)(int32_t character, const char* name, int32_t index, float value);
    /* A new object name levels can place, built like the game's object `like`, with its files from
       ObjectData/<name>.arc. */
    int32_t (*set_object_class)(const char* name, const char* like);
    /* A sound effect played from a file (AudioRes/Stream/Boom.wav, .ogg, .ast): a new name, or one of the game's
       (SE_OJ_COIN_GET) to replace it everywhere, whether played by name or by number (player voices, SE_PV_*).
       "<name>@luigi" replaces it only while playing as Luigi. */
    int32_t (*add_sound_effect)(const char* name, const char* file);
    /* A player animation: `name` plays the bck `file` from the player's animations (ObjectData/MarioAnime.arc: the
       game's, or one the mod adds there as a .bck or inside a .glb) at `rate` (1: the bck's own speed), blended in over
       `blend_frames` frames (0: the game's 6); a name the game already uses replaces that animation wherever the game
       plays it. -1: bad name or file, or too late (the player is built). */
    int32_t (*add_player_animation)(const char* name, const char* file, float rate, int32_t blend_frames);
    /* Changes the game's story: "first_stage" (with "first_mission", a number as text) is where a new file starts
       instead of the prologue; "hub" is where the player returns after a galaxy instead of the Comet Observatory;
       "story_scenes" "off" skips the game's story scenes. */
    int32_t (*set_story)(const char* key, const char* value);

    /* Online co-op (NebulaCoop only; a mod using these says "requires": ["coop"] and is a .lumacoop). A room's players
       by index: the host 0, then 1, 2... as they joined, an index kept while its player stays (a later joiner takes
       the lowest free one), as sm64coopdx's global index. Scripts call these as coop.<name> (coop.count()...).
       Outside a room the game is a room of one: this player is index 0 and the host, with no name.
       Read once a frame, on the game's thread. */
    /* 1 in a room, 0 outside one. */
    int32_t (*coop_in_room)(void);
    /* This player is the room's host (or outside a room): 1, else 0. */
    int32_t (*coop_is_host)(void);
    /* This player's index. */
    int32_t (*coop_me)(void);
    /* How many players the room has now (1 outside a room). */
    int32_t (*coop_count)(void);
    /* How many players the room takes (1 outside a room). */
    int32_t (*coop_max_players)(void);
    /* 1 when a player has `index` now, else 0. The calls below give "", -1 or 0 for an index nobody has. */
    int32_t (*coop_connected)(int32_t index);
    /* Their name, as the player list shows it ("" outside a room). Valid until the next frame. */
    const char* (*coop_name)(int32_t index);
    /* The character they play: 0 Mario, 1 Luigi (a mod character's base); -1 nobody. coop_character_id names it. */
    int32_t (*coop_character)(int32_t index);
    /* Where they play: the stage's name ("" until they reach one) and its mission (0 where none was chosen). Their
       instance is the pair: coop_same_place says whether it is this player's. Valid until the next frame. */
    const char* (*coop_stage)(int32_t index);
    int32_t (*coop_mission)(int32_t index);
    /* 1 when they play in this player's galaxy and mission (this player too), else 0. */
    int32_t (*coop_same_place)(int32_t index);
    /* The Power Stars they got in this room, new to its save (the Personal Star Counter's count); 0 outside a room. */
    int32_t (*coop_stars)(int32_t index);
    /* The round trip to them in milliseconds (to the host, for a joiner's view of the others); 0 for this player or
       when unknown. */
    int32_t (*coop_ping)(int32_t index);

    /* Characters (docs/modding/CHARACTERS.md), sm64coopdx's character table: Mario is 0, Luigi 1, and the mods'
       follow in the order they were added. The registry "character" lists them too (its value: the index). */
    /* Only in nebula_plugin_start. Adds a character: its index, or -1 (a bad or taken id, a `like` other than mario or
       luigi, a bad archive name, or 30 mod characters already). */
    int32_t (*add_character)(const NebulaPlugin* mod, const NebulaCharacterClass* character);
    /* How many characters there are, and one by index: its id and its name (null past the last), its base (0 Mario,
       1 Luigi; -1 past the last). */
    int32_t (*character_count)(void);
    const char* (*character_id)(int32_t index);
    const char* (*character_name)(int32_t index);
    int32_t (*character_like)(int32_t index);
    /* A character's index by id (any case), or -1. */
    int32_t (*character_find)(const char* id);
    /* The character the player plays in this stage (a switch takes effect at a reload), or the one they will. */
    int32_t (*player_character)(void);
    /* Picks the player's character for them instead of Options' PLAYER page's, as sm64coopdx's overrideModelIndex:
       null or "" gives the page's back. In a stage the player's spot reloads as it, as a switch on the page does; the
       others in a room see it. 1: done; 0: no such character. */
    int32_t (*set_player_character_id)(const char* id);
    /* Online co-op: the character a player plays, by id ("mario", "luigi", a mod's); "" for an index nobody has.
       coop_character gives its base. Valid until the next frame. */
    const char* (*coop_character_id)(int32_t index);

    /* Colours (docs/modding/CHARACTERS.md "Colours"), sm64coopdx's palettes: `part` is a NEBULA_PALETTE_*, a colour
       0xRRGGBB. A player wears the Options page's colours, else their character's own, and a mod's parts over them. */
    /* A character's own colour for a part (its model's: luma.json "palette", NebulaCharacterClass's); -1 for no such
       character or part. */
    int32_t (*character_palette_color)(int32_t index, int32_t part);
    /* The colour the player wears for a part now; -1 for no such part. */
    int32_t (*player_palette_color)(int32_t part);
    /* Puts a colour on a part of the player over their own, as sm64coopdx's network_player_set_override_palette_color;
       the others in a room see it. A negative colour takes it off. 1: done; 0: no such part. */
    int32_t (*set_player_palette_color)(int32_t part, int32_t color);
    /* Every part back to the player's own colours (network_player_reset_override_palette). */
    void (*reset_player_palette)(void);
    /* Online co-op: the colour a player wears for a part (this player's own outside a room, as index 0); -1 for an
       index nobody has or no such part. */
    int32_t (*coop_palette_color)(int32_t index, int32_t part);
    /* A counter under the HUD's Power Star counter, in its look (the StarCounter layout) with its star tinted `rgba`
       (0xRRGGBBAA; 0xFFFFFFFF keeps the game's colours): `line` 1 is the first under it, to NEBULA_HUD_COUNTER_LINES.
       It comes and goes with the HUD's counters, and counts up with their flash as `number` grows; a smaller number,
       or a new colour, shows at once. `number` below 0 removes the line. Kept across stages until removed or changed.
       0: done; -1: no such line. */
    int32_t (*hud_counter)(int32_t line, uint32_t rgba, int32_t number);
    /* One of Nebula's all-time counts (stats.ini, the achievement board's stats page) by its key: "power_stars",
       "coins"... As high as int32_t goes. -1: no such count. */
    int32_t (*stat)(const char* name);
    /* Online co-op: text beside a player in this player's player list (sm64coopdx's network_player_set_description),
       in `rgba` (0xRRGGBBAA), message tags allowed ("[icon 7]3": a Power Star and 3). Null or "" takes it away; a
       player leaving takes theirs away. Only this player's list shows it: each player's mods set their own. */
    void (*coop_set_description)(int32_t index, const char* utf8, uint32_t rgba);
    /* Online co-op: a line in the chat for this player alone, as a command's answer (sm64coopdx's
       djui_chat_message_create). A text that is one of the game's chat texts' ids ("CoopCmd...") shows in the
       language played. Nothing outside a room. */
    void (*coop_chat_say)(const char* utf8);
} NebulaModApi;

/* The lines hud_counter has under the Power Star counter. */
#define NEBULA_HUD_COUNTER_LINES 4

#ifdef __cplusplus
}
#endif

#endif /* NEBULA_MOD_API_H */
