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
#define NEBULA_MOD_API_VERSION 4u

/* An object in the game (a LiveActor in the game's code), as an event names it. */
typedef struct NebulaActor NebulaActor;

/* Callbacks run on the thread where the event happens: objects created while a stage loads are reported on the
   loading thread, everything else on the game thread. */
typedef enum NebulaModEvent {
    /* Every frame, in every scene including menus. */
    NEBULA_MOD_EVENT_FRAME = 0,
    /* Every frame of a stage, before objects move. */
    NEBULA_MOD_EVENT_STAGE_FRAME,
    /* A stage started. name: the stage. value: the mission. */
    NEBULA_MOD_EVENT_STAGE_START,
    /* The player is about to lose one health. Return 0 to cancel the hit. */
    NEBULA_MOD_EVENT_PLAYER_DAMAGE,
    /* The player is about to die. value: the cause (0 is falling out of the level). Return -1 to cancel. */
    NEBULA_MOD_EVENT_PLAYER_MISS,
    /* The player's power-up is about to change. value: the new one (NEBULA_POWER_UP_*). The return value is used. */
    NEBULA_MOD_EVENT_POWER_UP,
    /* Coins are about to be added. value: how many. The return value is added instead. */
    NEBULA_MOD_EVENT_COIN,
    /* Star bits are about to be added. value: how many. The return value is added instead. */
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
    /* The game is about to write the save. */
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
    /* A sound effect is about to play. value: its number. name: its name. The returned number plays instead (the same,
       another sound's, or -1 for silence). Costs one call per sound. */
    NEBULA_MOD_EVENT_SOUND_START,
    /* An object started an effect. actor: the object. name: the effect's name in the object's effect table. */
    NEBULA_MOD_EVENT_EFFECT_START,
    /* A screen layout (a counter, a menu, a message box) was shown (value 1) or removed (value 0). name: its name in
       the game's code. */
    NEBULA_MOD_EVENT_SCREEN,
    /* An object received a message from another: an attack, a stomp, a push, a grab (numbers in
       docs/modding/reference/events.md). actor: the receiver. name: its name. value: the message. Return -1 to cancel it. */
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
    NEBULA_MOD_EVENT_COUNT
} NebulaModEvent;

typedef struct NebulaModEventData {
    int32_t value;
    NebulaActor* actor;
    const char* name;
} NebulaModEventData;

/* Callbacks subscribed to the same event run in the order they subscribed; each receives the previous one's return
   value. Return data->value to change nothing. */
typedef int32_t (*NebulaModCallback)(const NebulaModEventData* data, void* user);

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

<<<<<<< HEAD
/* A playable character (add_character), as sm64coopdx's character table has them: a model on one of the game's two
   players, its base (`like`), which gives the animations, voice, effects and the game's own Luigi code. Its model is
   ObjectData/<archive>.arc, the model in it named as the archive (case doesn't matter), with the base's skeleton. Its
   other models follow the game's names with the base's name swapped for the archive's ("MarioFace" becomes
   "WarioFace", "BeeMario" "BeeWario", "MarioHandL" "WarioHandL"); one the mod doesn't ship is the base's. The files
   inside keep the game's names. It has its own player numbers (set_player_constant), first its base's as the game
   has them. docs/modding/CHARACTERS.md says more. */
typedef struct NebulaCharacterClass {
    uint32_t size;       /* sizeof(NebulaCharacterClass) */
    const char* id;      /* 1-31 of a-z, 0-9, '_', '-' ("wario"); how luma.json, saves and online co-op name it */
    const char* name;    /* as menus show it ("WARIO"); null: the id in capitals */
    const char* like;    /* "mario" or "luigi"; null: "mario" */
    const char* archive; /* null: the id with a capital first letter ("Wario") */
} NebulaCharacterClass;

/* A new power-up. Give it to the player with set_value(NEBULA_SET_POWER_UP, its number). */
=======
/* A new power-up (luma.json "forms"). */
>>>>>>> upstream/main
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

    /* Calls `callback` at each `event` (NEBULA_MOD_EVENT_*). Its return value matters where the event says so;
       otherwise it returns data->value. */
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
<<<<<<< HEAD
    /* Sets a value in the player's constants table by name (mJumpHeight, mFireModeTime). `character`: 0 Mario, 1 Luigi, or a
       mod's character's index (character_find). `index`: position in a list, else 0. Any time. 0: done; -1: no such
       value or character. */
=======
    /* Sets a value in the player's constants table by name (mJumpHeight, mFireModeTime). `character`: 0 Mario,
       1 Luigi. `index`: position in a list, else 0. -1: no such value. */
>>>>>>> upstream/main
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
<<<<<<< HEAD
    /* Memory from the game's C library, for plugins built without one (as portable plugins are, so they have no
       malloc). alloc returns null for 0 bytes or when memory runs out; the memory is aligned for any type.
       release(null) does nothing. Memory stays valid until released and is never moved. */
    void* (*alloc)(uint64_t bytes);
    void (*release)(void* memory);
    /* printf-style formatting into `buffer` of `size` bytes, always zero-terminated when size is above 0. Returns what
       snprintf would: the length the full text needs, or below 0 for a bad format. */
    int32_t (*format)(char* buffer, uint64_t size, const char* format, ...);
    /* The camera's near plane in units: nothing closer is drawn. 0: the game's (100; 10 in its own first-person view).
       Kept until set again. The distances at which the game hides objects near its camera are tunables:
       ClippingJudge.cNearClip (500), ClippingJudge.cNearClipSubjective (100) and ObjUtil.cNearClipScale (1). */
    void (*set_camera_near)(float distance);
    /* Unlocks one of the mod's achievements (luma.json "achievements", by id). Returns 1 when newly unlocked, 0 when
       already unlocked or the mod has no such id. Its pop-up shows and the board lists it under Mods. */
    int32_t (*unlock_achievement)(const NebulaPlugin* self, const char* id);
    /* The game's own camera this frame, before set_camera changes it: its position and the point it looks at (either
       may be null). In a cutscene or a conversation this is the shot the game frames. 1 in a stage, 0 otherwise. */
    int32_t (*game_camera)(float* position, float* target);
    /* The character in conversation with the player now, or null when there is none. */
    NebulaActor* (*talking_actor)(void);
    /* While `on` is 1, the large talk box (the one with the A button, also used by signs) is centred on x, y in
       screen_position's coordinates, kept inside the screen, and its pointer turns to the speaker. 0 puts it back in its
       own place. Small balloons already follow their speaker. Reset when the stage ends. */
    void (*place_talk_box)(float x, float y, int32_t on);
    /* `on` 0 hides one joint of an actor's model and every joint under it, on that actor only; 1 shows them again.
       The player keeps it in every form, and his face and hands go with the joint they hang from ("Head" takes his
       head, cap and face). Joint names as in get_joint_position. A new stage starts with all shown. */
    void (*show_joint)(NebulaActor* actor, const char* joint, int32_t on);
    /* The name of joint `index` of an actor's model (0 is the root), or null past the last joint or without a model.
       Names are what get_joint_position and show_joint take. */
    const char* (*joint_name)(NebulaActor* actor, int32_t index);
    /* A joint's place and turn this frame: 12 floats, the rows of a 3x4 matrix. Columns 0, 1 and 2 are the joint's own
       x, y and z axes in the world (scaled with the model), column 3 its position. Without the joint or a model: no
       turn, at the actor's position. */
    void (*get_joint_matrix)(NebulaActor* actor, const char* joint, float* matrix);

    /* A star of the stage played won, as the player wins it: its result counted and its get demo (the Grand Star's with
       `grand` 1) once the stage is in play (after its opening camera and starter). `galaxy`: null or the stage played.
       0: refused (no galaxy is played, or another one is named); 1: done. */
    int32_t (*give_star)(const char* galaxy, int32_t star, int32_t grand);
    /* The save holds Grand Stars 1 to `count` (1-7), and the stage played starts again, so the Observatory's domes are
       built with them. 0: refused; 1: done. */
    int32_t (*give_grand_stars)(int32_t count);
    /* The player plays Luigi (1) or Mario (0) instead of Options' PLAYER page's character, as set_player_character_id
       ("luigi" or "mario") does. 0: refused; 1: done. */
    int32_t (*set_player_character)(int32_t luigi);
    /* The materials of the first living actor whose model is `model` (any case), each given to `callback`: how many. */
    int32_t (*list_materials)(const char* model, NebulaMaterialCallback callback, void* user);
    /* A point inside the stage's `index`-th body of water (its oceans first, then its water areas; the index wraps):
       how many bodies of water the stage has (0: none, `xyz` untouched). */
    int32_t (*water_point)(int32_t index, float* xyz);
    /* The circle wipe closes (`open` 0) or opens (1) over `frames` frames. 0: refused (no stage); 1: done. */
    int32_t (*circle_wipe)(int32_t open, int32_t frames);
    /* The story's grant of the spin and the Star Pointer (the Gateway Luma's), which a new file has not reached. 0:
       refused (no stage); 1: done. */
    int32_t (*grant_spin_and_pointer)(void);

    /* Version 3: LumaCore. */
    /* The toolkit: every game function scripts and plugins call (docs/modding/TOOLKIT.md). Its index by name
       ("MR::startBck"; an overloaded name with its value letters after ':'), or -1. */
    int32_t (*toolkit_find)(const char* name);
    /* Calls it. `out` gets the result, then each value an in-out parameter gives back; returns how many values were
       written, or -1 when the call was refused (a missing or stale handle, a wrong class, too few arguments: logged once
       per reason). */
    int32_t (*toolkit_call)(int32_t index, const NebulaValue* args, uint32_t count, NebulaValue* out, uint32_t out_count);
    /* The value for an actor the API gave, and back (null for anything but a living object). */
    NebulaValue (*actor_value)(NebulaActor* actor);
    NebulaActor* (*value_actor)(const NebulaValue* value);
    /* A field of an object's class (or a base's) by name ("mVelocity"): its index for field_get and field_set, or -1
       (docs/modding/FIELDS.md). Handles, text and pointers are read-only. 1: done; 0: refused. */
    int32_t (*field_find)(const NebulaValue* object, const char* name);
    int32_t (*field_get)(const NebulaValue* object, int32_t field, uint32_t element, NebulaValue* out);
    int32_t (*field_set)(const NebulaValue* object, int32_t field, uint32_t element, const NebulaValue* value);
    /* A class's name and its parent's id, by id (0 is NameObj; null past the last), and whether an object is one. */
    const char* (*class_name)(uint32_t id, uint32_t* parent);
    int32_t (*is_a)(const NebulaValue* object, const char* class_name);
    /* The host's: the table it completed (log, store, format), once, before any mod row. Plugins never call it. */
    void (*luma_start)(const struct NebulaModApi* api);
    /* An object a script runs (luma.json "objects"): the mod's /LumaScripts/<mod id>/<script>.lumascript on `shape`,
       whose callbacks the script's handlers take. 0: added; -1: refused (logged with the reason). */
    int32_t (*add_script_object)(const NebulaPlugin* mod, const NebulaObjectClass* shape, const char* script);
    /* The host's: a mod-wide script (luma.json "scripts"), the mod's /LumaScripts/<mod id>/<script>.lumascript, one
       instance for the whole run. 0: started; -1: refused (logged with the reason). */
    int32_t (*add_mod_script)(const NebulaPlugin* mod, const char* script);
    /* The registries: every kind of thing the game has, its own rows and the mods' by name. `kind` is "object",
       "form", "area", "camera", "gravity", "message", "floor", "comet", "hud", "character", "player_state",
       "attack", "star", "ride", "item" or "projectile" (docs/modding/REGISTRIES.md lists each kind's rows).
       registry_count: how many rows the kind has (0 for an unknown kind). registry_name: the i-th one's name (null
       past the last). registry_find: a name's place in the kind (0 to count - 1), or -1. registry_value: the game's
       number for a row (a form's id, a message's number, a floor code; a table's index), or -1 without a row.
       registry_mod: the mod that added the i-th row, or null for the game's own. A later row of the same name
       replaces an earlier one (the later mod wins, logged). */
    int32_t (*registry_count)(const char* kind);
    const char* (*registry_name)(const char* kind, int32_t index);
    int32_t (*registry_find)(const char* kind, const char* name);
    int32_t (*registry_value)(const char* kind, const char* name);
    const char* (*registry_mod)(const char* kind, int32_t index);
    /* Only in nebula_plugin_start. What runs on every actor placed by `name` (the game's or a mod's object name): the
       mod's script (scripts/<script>.lumascript; its handlers start, frame, control, appear, kill, message and touch)
       or hooks. Several mods may name one class; each runs, in load order. 0: done. */
    int32_t (*set_class_script)(const NebulaPlugin* mod, const char* name, const char* script);
    int32_t (*set_class_hooks)(const NebulaPlugin* mod, const char* name, const NebulaClassHooks* hooks);
    /* Only in nebula_plugin_start. A power-up as add_power_up, its form's row named by the mod (registry "form"), and
       a script (scripts/<script>.lumascript, or null) on the player while he is in it: start when he gets it, frame
       every frame, end when he leaves it. The power-up's number, or -1. */
    int32_t (*add_form)(const NebulaPlugin* mod, const NebulaPowerUpClass* form, const char* script);

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
=======
    /* Changes the game's story: "first_stage" (with "first_mission", a number as text) is where a new file starts
       instead of the prologue; "hub" is where the player returns after a galaxy instead of the Comet Observatory;
       "story_scenes" "off" skips the game's story scenes. */
    int32_t (*set_story)(const char* key, const char* value);
>>>>>>> upstream/main
} NebulaModApi;

#ifdef __cplusplus
}
#endif

#endif /* NEBULA_MOD_API_H */
