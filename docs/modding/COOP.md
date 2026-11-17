# Co-op mods

NebulaCoop's multiplayer for mods, modeled on sm64coopdx's Lua API, is the LumaCore service `coop`
([reference/services/coop.md](reference/services/coop.md), header `native/include/luma_coop.h`). A mod imports it
like any other service (`use svc.coop` in LumaScript, `LUMA_SERVICE(slot, "coop", "1.0")` in C). Upstream Nebula has
no such service, so a mod that uses it doesn't start there; ship it as a `.lumacoop`.

It covers who is in the room, where they play and when that changes; what the player list shows for them; the mod's
own chat commands and the chat's answers; counters under the HUD's star counter; and Nebula's all-time counts. Packets
and synced variables come next (the plan: the "Co-op LumaLink API" proposal).

## Players

A room's players go by index: the host is 0, then 1, 2... as they joined. A player keeps their index while they stay,
and a later joiner takes the lowest free one (sm64coopdx's global index). Outside a room the game is a room of one:
this player is index 0 and the host, with no name.

`coop.player(i)` gives a `coop.Player` (none for nobody): its flags (you, host, Luigi, in this player's galaxy and
mission), the Power Stars they got in this room, their ping, mission, name, character id
([CHARACTERS.md](CHARACTERS.md)) and stage. The room is read once a frame, on the game's thread.

## Events

`coop.watch(fn)` gives the mod's function the room's events, at the start of a frame:

| Event | When |
|---|---|
| `Joined(value)` | this player is in a room (opened one, or was let in). value: its index |
| `Closed` | this player's room ended |
| `PlayerJoin(value)` | another player came in. value: their index |
| `PlayerLeave(value)` | another player left. value: the index they had |
| `PlayerPlace(value)` | a player (this one too) reached another galaxy or mission. value: their index |
| `Command(value, flags)` | this player typed one of the mod's chat commands. value: its number, flags: the word's place in its options (1 the first, 0 none) |

On joining, the players already in the room don't each get a `PlayerJoin`: `Joined` comes, then a `PlayerPlace` for
each player already in a stage.

## Chat commands

`coop.add_command(name, description, options)` adds `/name` for the rest of the run, as sm64coopdx's
`hook_chat_command`. `/help` lists it with its description (a NebulaText id or plain words), Tab completes its options
(words split by spaces), and it shows only in a room. A word that isn't one of the options is refused by the chat. A
name the game or another mod already has keeps theirs (`LUMA_E_EXISTS`). A mod reloaded keeps its numbers.

The old luma.json `"commands"` rows and the script interpreter's `chat_command` handler are gone with Nebula 2's old
scripts.

## The built-in mods

LumaLink-coop's `builtin/` holds NebulaCoop's built-in mods, embedded from the `lumalink` submodule
(cmake/nebula_builtin_mods.cmake). The Personal Star Counter is LumaScript on this service: its red line is
`coop.hud_counter(1, ..., coop.player(coop.me()).stars)`, its green one `coop.stat("power_stars")`, and the player
list shows each player's stars with `coop.set_description`. Character Movesets is data only: Luigi's player constants.
