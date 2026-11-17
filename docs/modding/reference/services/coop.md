---
title: coop service
shortTitle: coop
intro: NebulaCoop's online room for mods, as sm64coopdx's Lua API gives it.
contentType: reference
---

## About the coop service

The `coop` service is NebulaCoop's own. It gives a mod the online room: its players and where they play, a line beside each player in the player list, chat commands and the chat's answers, counters under the HUD's Power Star counter, and Nebula's all-time counts. Outside a room, this player is index 0 of a room of one.

Upstream Nebula has no such service, so a mod that uses it doesn't start there. Ship it as a `.lumacoop`.

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `coop` | 1.0 | None | `LUMA_SERVICE(slot, "coop", "1.0")` | `use svc.coop` |

The header is `native/include/luma_coop.h`, and every function's comment there is its contract. All of them run on the game thread.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_COOP_MOST_PLAYERS` | `16` | Players a room has at most (indexes 0 to 15). |
| `LUMA_COOP_HUD_LINES` | `4` | Lines `hud_counter` has under the Power Star counter. |
| `LUMA_COOP_COMMANDS_MOST` | `8` | Chat commands one mod adds at most. |

## Functions

| Function | What it does | sm64coopdx |
|---|---|---|
| `in_room()` | 1 while this player is in a room. | `network_is_server() or network_is_client()` |
| `is_host()` | 1 for the host, or outside a room. | `network_is_server()` |
| `me()` | This player's index. | `gNetworkPlayers[0].globalIndex` |
| `max_players()` | The room's size (1 outside one). | `gServerSettings.maxPlayers` |
| `player(index)` | A `coop.Player` (`LumaCoopPlayer`), or none for nobody: flags (you, host, Luigi, here), stars got in the room, ping, mission, name, character id and stage. | `gNetworkPlayers[i]` |
| `palette_color(index, part)` | The colour a player wears for a part, 0xRRGGBB. | `network_player_get_palette_color` |
| `set_description(index, text, colour)` | Text beside a player in this player's list, with message tags. `""` takes it away. | `network_player_set_description` |
| `chat_say(text)` | A line in the chat for this player alone. A chat text's id (`CoopCmd...`) shows in the language played. | `djui_chat_message_create` |
| `hud_counter(line, colour, value)` | A counter in the star counter's look under it, its star tinted. A value below 0 takes the line away. The line goes when the mod stops. | (the Personal Star Counter's HUD) |
| `stat(name)` | One of Nebula's all-time counts (`stats.ini`), such as `"power_stars"`. | (the mod's storage) |
| `add_command(name, description, options)` | `/name` in the chat for the rest of the run, `description` its `/help` line, `options` its words split by spaces. Gives its number, from 1. | `hook_chat_command` |
| `watch(fn)` | The room's events: `Joined(value)`, `Closed`, `PlayerJoin(value)`, `PlayerLeave(value)`, `PlayerPlace(value)` and `Command(value, flags)`, where value is the command's number and flags the word's place (0 none). | `hook_event` |

## Example

The built-in Personal Star Counter (`lumalink/builtin/personal-star-counter/source/main.lum`) uses most of the service:

```lumascript
use svc.coop

var psc: u32 = 0

fn heard(e: coop.Event) {
    match e {
        coop.Event.Command(value, flags) => {
            if value == psc as u64 {
                coop.chat_say("CoopCmdPscOn")
            }
        }
        else => return
    }
}

on start {
    if let n = coop.add_command("psc", "CoopCmdPscHelp", "on off") {
        psc = n
    }
    coop.watch(heard)
}
```

## Further reading

* [Online co-op for mods](../../COOP.md)
* [Calling services](calling-services.md)
* [Services reference](index.md)
