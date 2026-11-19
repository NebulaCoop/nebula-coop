# Characters

NebulaCoop's characters work like sm64coopdx's character table. Mario is index 0 and Luigi is 1, and mods add more after
them. A player picks one on Options' PLAYER page (the CHARACTER row lists every character the running mods add). The
others in an online room see the character's model.

A character is a **model on one of the game's two players**: its base, `like`. The base gives everything the game's
code does for Mario or Luigi: the animations (MarioAnime.arc, with Luigi's swaps), voice, effects, the life icon and the
game's own Luigi code. The character gives:

- **its model**, `ObjectData/<archive>.arc`, with the model inside named as the archive (case doesn't matter). It needs
  the base's skeleton (its joint names), as a model that replaces `Mario.arc` does.
- **its other models**, found by the game's names with the base's name swapped for the archive's: `MarioFace` becomes
  `WarioFace`, `MarioShadow` `WarioShadow`, `MarioHandL` `WarioHandL`, `BeeMario` `BeeWario`, `IceMario` `IceWario`,
  `InvincibleMario` `InvincibleWario`, `HopperMario` `HopperWario`, `BoneMario` `BoneWario`, `TeresaMario`
  `TeresaWario`, `MarioTornado` `WarioTornado`, `BeeMarioWing` `BeeWarioWing`. A Luigi-based character swaps `Luigi`
  (`LuigiFace` becomes `WaluigiFace`), and `Mario` in the names both share (the hands, the bee's wings). One the mod
  doesn't ship is the base's. The files inside an archive keep the game's names (`InvincibleMario.brk` stays).
- **its own player numbers**, a copy of the base's as the game has them (before any mod changed them). `player_constants`
  and `set_player_constant` change them by the character's id or index, so a movesets mod can give each character its
  own jump, speed and traction.

## In luma.json

A layer's `characters` adds them, and the mod says `"requires": ["characters"]` (Nebula without characters refuses it):

```json
{
  "requires": ["characters"],
  "layers": [{
    "path": "layers/base",
    "characters": [
      { "id": "wario", "name": "WARIO", "like": "mario", "archive": "Wario" },
      { "id": "waluigi", "like": "luigi" }
    ],
    "player_constants": {
      "wario": { "mJumpHeight": [20.0, null, 30.0] }
    }
  }]
}
```

| Field | |
|---|---|
| `id` | 1 to 31 of `a-z`, `0-9`, `_` and `-`; not `mario` or `luigi`. How saves, `player_constants` and online co-op name it |
| `name` | As the Options page shows it; the id in capitals when left out |
| `like` | `mario` (the default) or `luigi` |
| `archive` | The model's archive in ObjectData, without `.arc`; the id with a capital first letter when left out (`waluigi` reads `Waluigi.arc`) |
| `palette` | The colours its model's textures are painted in, by part (see [Colours](#colours)): `{"cap": "5a22a0", "overalls": "202020"}`. A part left out is its base's |

The layer's `files/ObjectData/` holds the archives. An id another mod already took is refused and named in the log. A
`player_constants` key that isn't `mario`, `luigi` or `both` is a character's id: its numbers are set once the
character is added (by this mod or another one running).

## From code

| C (NebulaModApi) | Gives |
|---|---|
| `add_character(self, &class)` | Only in `nebula_plugin_start`: adds a character (NebulaCharacterClass: id, name, like, archive). Its index, or -1 |
| `character_count()` | how many characters there are (2 before any mod's) |
| `character_id(i)`, `character_name(i)` | a character's id and name; null past the last |
| `character_like(i)` | its base: 0 Mario, 1 Luigi; -1 past the last |
| `character_find(id)` | its index by id, or -1 |
| `player_character()` | the index the player plays now |
| `set_player_character_id(id)` | picks the player's character instead of the Options page's, as sm64coopdx's `overrideModelIndex`; null or "" gives the page's back. In a stage the player's spot reloads as it (as a switch on the page does), and the room sees it. 1 done, 0 no such character |
| `set_player_character(luigi)` | the same for Mario (0) or Luigi (1) |
| `set_player_constant(i, name, place, value)`, `player_constant(i, name, place)` | a character's numbers by its index |
| `coop_character_id(i)` | the id of the character the room's player `i` plays; `coop_character(i)` is its base |
| `registry_value("character", id)` | its index too: the `character` registry lists every character |
| `character_palette_color(i, part)` | a character's own colour for a part (0xRRGGBB), -1 past the last |
| `player_palette_color(part)` | the colour the player wears now for a part (theirs with a mod's over it) |
| `set_player_palette_color(part, color)` | a mod's colour for a part over the player's, as sm64coopdx's `network_player_set_override_palette_color`; a negative colour takes it off. The room sees it. 1 done, 0 no such part |
| `reset_player_palette()` | takes every mod colour off |
| `coop_palette_color(i, part)` | the colour the room's player `i` wears for a part, -1 for no such player |

A script reaches these as `nebula.<name>` and `coop.character_id` once LumaCore's toolkit is generated again
(tools/lumacore/gen_lumacore.py, on Windows).

## Colours

A player's colours work like sm64coopdx's palettes: eight parts, each a colour. `part` numbers and luma.json's names:

| # | Name | |
|---|---|---|
| 0 | `pants` (or `overalls`) | the overalls |
| 1 | `shirt` | |
| 2 | `gloves` | |
| 3 | `shoes` | |
| 4 | `hair` | the hair, moustache and eyebrows |
| 5 | `skin` | the face, ears and eyelids |
| 6 | `cap` | |
| 7 | `emblem` | the letter on the cap |

A player picks theirs on Options' PLAYER page, under COLOURS: SET gives a whole set (CUSTOM, their own edit; OWN for
their character's own, the default; then any character's, or a few more: FIRE MARIO, WARIO, TOAD and others), and a
row per part steps through named colours, moving ADVANCED COLOURS to that part. ADVANCED COLOURS gives any colour, as
sm64coopdx's palette editor: PART picks a part (its colour shown as `#RRGGBB`), and RED, GREEN and BLUE slide its
channels from 0 to 255 in fives. Any change to a part's colour starts from what is worn and makes it CUSTOM, kept in
the settings (`coop_custom_palette`) so SET's CUSTOM brings it back after trying a set. Each
player's colours travel with their character, so everyone in a room sees them.

The game's player models carry their colours in their textures, so a palette is drawn by recoloured copies of those
textures, made as the player's actor is made and remade when the colours change. Every frame of a texture gets a
copy (`MarioBody.0` to `.2`, `MarioCap.0` and `.1`, the eyelids' blink), since the player's texture animations
(MarioAnime's `ElementEnd` and `ColorChange`) draw other frames than the model's own. The material's name says which parts
its texture may hold: `Cap` the cap and emblem, `Body` the overalls, shirt and shoes, `Hair` the hair, `Face` and
`EyeLid` the skin, `Hand` the gloves (`Eye` none). Each texel near a part's colour in the character's own palette takes
the wanted colour at its own lightness; texels near none (the buttons, the eyes, outlines, the white around the
emblem) stay. The emblem is a patch of the cap's or emblem's colour ringed by light texels, away from the texture's
edges. A mod character's model works the same when its materials use those names and its `palette` says the colours
its textures are painted in (a part it leaves out is taken to be its base's).

## Online co-op

The room's players run the same mods (a joiner gets the room's), so each has every character. A player's character
travels by its id with their player record, and their snapshot's Luigi flag says its base at once. A copy of a player
shows their character's model when this game has the character and its base is the one their snapshot plays;
otherwise it shows the base. Copies of the characters the room plays are made as each stage starts: a player who
switches to a mod's character mid-stage shows as its base until the next start.

## Not yet

- A character's own animations, voice and life icon: it uses its base's.
