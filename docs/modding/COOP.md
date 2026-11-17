# Co-op mods

NebulaCoop's multiplayer calls for mods, modeled on sm64coopdx's Lua API. A co-op mod says `"requires": ["coop"]` in
its luma.json and is packed as a `.lumacoop` (LumaLink does both), so upstream Nebula refuses it.

Who is in the room, where they play and when that changes; what the player list shows for them; and the mod's own
chat commands. Packets and synced variables come next (the plan: the "Co-op LumaLink API" proposal).

## Players

A room's players go by index: the host is 0, then 1, 2... as they joined. A player keeps their index while they stay,
and a later joiner takes the lowest free one (sm64coopdx's global index). Outside a room the game is a room of one:
this player is index 0 and the host, with no name.

| C (NebulaModApi) | LumaScript | Gives |
|---|---|---|
| `coop_in_room()` | `coop.in_room()` | 1 in a room, 0 outside one |
| `coop_is_host()` | `coop.is_host()` | 1 for the host (and outside a room) |
| `coop_me()` | `coop.me()` | this player's index |
| `coop_count()` | `coop.count()` | how many players the room has now |
| `coop_max_players()` | `coop.max_players()` | how many it takes |
| `coop_connected(i)` | `coop.connected(i)` | 1 when someone has index `i` |
| `coop_name(i)` | `coop.name(i)` | their name, as the player list shows it |
| `coop_character(i)` | `coop.character(i)` | 0 Mario, 1 Luigi (a mod character's base), -1 nobody |
| `coop_character_id(i)` | `coop.character_id(i)` | their character's id: "mario", "luigi" or a mod's ([CHARACTERS.md](CHARACTERS.md)); "" for nobody |
| `coop_stage(i)` | `coop.stage(i)` | their stage's name ("" before they reach one) |
| `coop_mission(i)` | `coop.mission(i)` | their mission (0 where none was chosen) |
| `coop_same_place(i)` | `coop.same_place(i)` | 1 when they play in this player's galaxy and mission |
| `coop_stars(i)` | `coop.stars(i)` | Power Stars they got in this room (new to the room's save) |
| `coop_ping(i)` | `coop.ping(i)` | round trip in milliseconds (0 for this player or unknown) |

The values are read once a frame, on the game's thread. A C plugin checks that
`api->coop_count` is not null before using it.

## Events

| Event | LumaScript handler | When |
|---|---|---|
| `NEBULA_MOD_EVENT_COOP_ROOM_JOINED` | `room_joined` | this player is in a room (opened one, or was let in). value: its index |
| `NEBULA_MOD_EVENT_COOP_ROOM_CLOSED` | `room_closed` | this player's room ended |
| `NEBULA_MOD_EVENT_COOP_PLAYER_JOIN` | `player_join` | another player came in. value: index, name: name |
| `NEBULA_MOD_EVENT_COOP_PLAYER_LEAVE` | `player_leave` | another player left. value: the index they had, name: name |
| `NEBULA_MOD_EVENT_COOP_PLAYER_PLACE` | `player_place` | a player (this one too) reached another galaxy or mission. value: index, name: stage |
| `NEBULA_MOD_EVENT_COOP_COMMAND` | `chat_command` | this player typed one of the mod's chat commands. value: the word's place in its options (1 the first, 0 none), name: the command |

On joining, the players already in the room don't each get a `player_join`: `room_joined` comes, then a
`player_place` for each player already in a stage. These events only run while a mod with code is loaded.

```
var others: int

on room_joined(me) {
    others = 0
    let i = 0
    while i < coop.max_players() {
        if coop.connected(i) && i != me { others = others + 1 }
        i = i + 1
    }
}

on player_join(index, name) { others = others + 1 }
on player_leave(index, name) { others = others - 1 }
```

The `coop.*` names in LumaScript are LumaCore toolkit rows (tools/lumacore/gen_lumacore.py names a `coop_<name>`
field's row `coop.<name>`); `gen_lumacore.py --api-only` rewrites only the API rows, without clang, so a new call
needs no Windows toolkit build.

## The player list and the chat

| C (NebulaModApi) | LumaScript | Does |
|---|---|---|
| `coop_set_description(i, text, rgba)` | `coop.set_description(i, text, rgba)` | the player list's last column for player `i`, on this player's screen (sm64coopdx's `network_player_set_description`). Text tags work (`[icon 7]` is the Power Star); `rgba` is 0xRRGGBBAA (in LumaScript written in decimal); "" clears it. It goes when the player leaves. |
| `coop_chat_say(text)` | `coop.chat_say(text)` | a line in this player's chat, for them alone; a NebulaText id (`CoopCmdPscOn`) shows that text in the played language. Only in a room. |

## Chat commands

A co-op mod's chat commands are luma.json rows, as sm64coopdx's `hook_chat_command`:

```json
"commands": [{ "name": "psc", "description": "CoopCmdPscHelp", "options": ["on", "off"] }]
```

`/help` lists each with its description (a NebulaText id or plain words), Tab completes its options, and they are
there only in a room playing the mod. Typing `/psc off` fires `chat_command` with 2 and `"psc"` in the mod's scripts;
a word that isn't one of the options is refused by the chat. A name the game or an earlier mod already has keeps
theirs.

## The HUD and stats

These are Nebula calls (`nebula.*`), not only for co-op:

| C (NebulaModApi) | LumaScript | Does |
|---|---|---|
| `hud_counter(line, rgba, number)` | `nebula.hud_counter(line, rgba, number)` | a counter on line 1 to `NEBULA_HUD_COUNTER_LINES` (4) under the HUD's star counter, its look with the star tinted `rgba`; it comes and goes with the game's counter and counts up as it does. A number below 0 hides the line. Answers -1 for no such line. |
| `stat(name)` | `nebula.stat(name)` | one of the save's all-time stats (the achievement board's Stats page, `stats.ini`) by its key, such as `"power_stars"`; -1 for a key it doesn't know |

The built-in Personal Star Counter (LumaLink-coop's `builtin/personal-star-counter`) is made of these: its red line is
`coop.stars(coop.me())`, its green one `nebula.stat("power_stars")`, and the player list shows each player's
`coop.stars`.
