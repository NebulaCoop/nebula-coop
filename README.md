# Nebula Co-Op

**Online co-op for Super Mario Galaxy on PC.** Nebula Co-Op is a fork of
[**Nebula**](https://github.com/keepvaibin/nebula) by [**keepvaibin**](https://github.com/keepvaibin), the native PC port of Super Mario Galaxy, that adds online multiplayer in the spirit of [sm64coopdx](https://github.com/coop-deluxe/sm64coopdx): several players in the same galaxy, each with their own Mario or Luigi, over the internet.

You need your own copy of the game. Not affiliated with or endorsed by Nintendo. (Please don't hurt me Nintendo.)

<<<<<<< HEAD
[Download](https://github.com/NebulaCoop/nebula-coop/releases/latest) ·
[Playing online](#playing-online) ·
[Building from source](docs/BUILDING.md) ·
[Co-op design](docs/COOP_PLAN.md) ·
[Controls](docs/KEYBOARD_CONTROLS.md) ·
[Upstream Nebula](https://github.com/keepvaibin/nebula) ·
[Security](SECURITY.md) ·
[Discord](https://discord.gg/hNbgFtaRHv) ·
[Discord (Nebula)](https://discord.gg/2fJEdhAw2f)
=======
[Download](https://github.com/keepvaibin/nebula/releases/latest) ·
[Controls](docs/using-nebula/keyboard-and-controller-controls.md) ·
[Building from source](#building-from-source)
>>>>>>> upstream/main

## Credits

<<<<<<< HEAD
- **[Nebula](https://github.com/keepvaibin/nebula), by [keepvaibin](https://github.com/keepvaibin).** Everything that makes the game run on a PC is Nebula's: the host, the renderers, Setup and the launcher, controller and Wii Remote support, mods, achievements and the settings menu. Nebula Co-Op follows it closely and merges its changes in regularly. ([Discord](https://discord.gg/2fJEdhAw2f))
- **[sm64coopdx](https://github.com/coop-deluxe/sm64coopdx)** and its [CoopNet](https://github.com/coop-deluxe/coopnet) are the model for how co-op plays here: rooms, instances, player interaction, the bubble on death, chat commands, pop-ups and the Personal Star Counter. Its code is not used; only its design.
- **[Petari](https://github.com/SMGCommunity/Petari)**, the Super Mario Galaxy decompilation by SMGCommunity, is the
  game code Nebula builds natively.
- Other third-party code and its licenses are in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and
  [docs/PROVENANCE.md](docs/PROVENANCE.md).
=======
## Project status

- **Released: [Nebula 1.0.2](https://github.com/keepvaibin/nebula/releases/latest).** Playable from start to finish
  with the North American disc (RMGE01). Setup translates the game's PowerPC code to C++ and compiles it on your PC.
- **In development: Nebula 2** (this branch). The game is built as native code from the
  [Petari](https://github.com/SMGCommunity/Petari) decompilation, with more discs, controllers and features (below).
  It is not released; you can build it from source. Bugs remain; see [Known issues](#known-issues).

## How Nebula was made

Nebula's host, renderer, installer, tests and documentation were written with AI coding agents (Anthropic's
Claude), directed, reviewed and tested by the project's author. The game's own logic, Mario's physics included, is
the Petari decompilation by SMGCommunity, compiled for the PC with the changes a 64-bit, little-endian host needs.

## How it works

Nebula 2 (this branch; 1.0.2 works differently, see [Project status](#project-status)):

1. **Game module** (`native/`). Petari's game code and the engine code under it (layouts, sound, models, Miis), compiled with clang into
   `smg_game.dll`.
2. **Host** (`runtime/`, `native/host`). `Nebula.exe` loads the module and gives it its services:
   typed records drawn with Direct3D 12 or Vulkan, voices mixed and played, controllers and a real Wii Remote, the
   game's files from the store, saves, and the frame clock.
3. **Setup and launcher** (`installer/`, C#; `crates/nebula-import`, Rust). `Nebula-Setup.exe` reads your disc
   image, checks that it is the supported release and packs its files for Nebula; `NebulaLauncher.exe` opens the launcher and `Nebula.exe` is the game.
>>>>>>> upstream/main

## Features

### Playing together

- **Rooms of 2 to 16 players** (8 by default). One player hosts and the world runs on their PC; everyone else joins.
- **Join by room code through [NebulaNet](https://github.com/NebulaCoop/NebulaNet)**, Nebula Co-Op's own
  CoopNet-style matchmaker, so nobody has to forward a port. A host picks the LOBBY: **Public** rooms show to everyone
  under the Join page's ROOMS, **friends only** rooms are joined by their code (or found by their password), as
  sm64coopdx's public and private lobbies.
- **Or connect directly** to `address:port` (UDP 7777 by default), with a forwarded port or a VPN such as Tailscale,
  ZeroTier or Radmin VPN.
- **Mario or Luigi**, picked per player and switchable live, each with their own animations.
- **Per-mission instances**, as in sm64coopdx: you see and play with the players on the same galaxy *and* star. The
  galaxy and star selects show who is where.
- **Mods are the room's**: the host picks them, and joiners download any they don't have from the host. The game
  version, disc and mods are checked as a player joins.

### A shared world

- The host's game decides the save, collectibles, enemies and objects; the pause and settings menus no longer pause
  the game.
- **Shared enemies and objects**: hits and squashes play for everyone, enemies follow the nearest player, and
  switches and blocks are shared.
- **Coins and star bits** are each player's own, but gone for everyone once taken. Star Chips, the Observatory's star
  bit bank and high scores are shared.
- **Cutscenes and stars**: each player's cutscenes are their own, and a Grand Star takes everyone in its mission along.
- **Boss fights are the room's**: a boss's phases are shared, its opening plays once, and players arriving late join
  the fight in progress.
- **Races** are each player's own race against the rival, with the other players see-through.
- **A mission resets** only once everyone in it has missed or left. A game over sends you back to the Observatory and
  keeps you in the room.
- **Achievements** unlocked in a save are every player's who plays in it.

<<<<<<< HEAD
### Room options

Set on the Host page, and changed by the host from the in-game ONLINE tab:

- **Player Interaction**: non-solid, solid or friendly fire, with **Knockback Strength** and a **PvP mode**.
- **Bubble on Death** (on by default): instead of missing, float in a bubble and fly to another player to get back in.
- **Cheats**, which allow the `/tp`, `/teleport` and `/pos` commands.
- **Personal Star Counter**, sm64coopdx's mod built in: each player's stars this room and all time.

### In the room

- **Nametags** over each player's head, and a **player list** while Tab (keyboard) or Share (controller) is held.
- **Chat**: T opens a line, Enter sends it.
- **Chat commands**, as sm64coopdx's: `/help`, `/players`, `/kick`, `/ban`, `/permban`, `/moderator` and `/confirm`, with Tab completion. See [Chat commands](docs/COOP_PLAN.md#chat-commands).
- **Pop-ups** when players join, leave, change galaxy, die or get a star, each toggled on the PLAYER page.
- **Any number of saves**, each with its own name, Power Stars and achievements. Joiners play the host's save; their own saves are untouched.

### Still to come

From [TODO.md](TODO.md): a relay fallback through TURN when a direct path can't be made, a Discord Join button
carrying the room code, bosses' movement and thrown-object hits shared, story demos with several players, and a
dedicated server.

Everything Nebula itself offers is here too: keyboard and mouse, Xbox, PlayStation, Switch and Steam controllers and
a real Wii Remote; Direct3D 12 and Vulkan; upscalers and anti-aliasing; `.luma` mods; and more. See
[Nebula's README](https://github.com/keepvaibin/nebula#readme) for the full list.
=======
- The European (RMGP01) and Japanese (RMGJ01) discs and all their languages ([Languages](#languages)).
- The game built natively from the Petari decompilation: no compiling during install; a Vulkan renderer beside
  Direct3D 12.
- More controllers: PlayStation (DualShock 4, DualSense, DualSense Edge), the Steam Controller and generic HID
  controllers; gyro pointing and tilt on any controller with motion sensors.
- MSAA, AMD FSR 3, ambient occlusion.
- Skippable cutscenes (+ during a cutscene) and Quit on the file select.
- 34 achievements, each with a pop-up when it is reached. From the Gateway's Grand Star on, a Luma near Mario's start
  in the Comet Observatory shows them all: tabs, filters and play stats.
- Instant text speed.
- Mii import for the file icons from a Wii, DS, 3DS, Wii U, Switch, Mii Studio or Mii Creator. Settings > GENERAL
  > IMPORT MIIS, or put the files in the `miis` folder next to the settings; imported files are kept there and read
  at every start.
- Mods: one `.luma` file for a mod, on every version of the game. A mod brings files, table patches, models, textures,
  sound, stages and code (LumaScript or C, built to native code). [LumaLink](https://github.com/keepvaibin/lumalink)
  makes, checks, packs and runs mods and converts Riivolution mods and texture packs; the modding documentation starts
  at [docs/modding/index.md](docs/modding/index.md). Import from Settings > MODS or drop a file on the game window. Among enabled mods the higher one wins a file or texture. Raw mods, disc files and texture packs in the `mods` folder still work.
- Nebula's own text (its buttons, questions and achievements) in all 14 of the game's languages, changeable or
  translatable by a mod: [docs/modding/how-tos/changing-nebulas-text.md](docs/modding/how-tos/changing-nebulas-text.md).
- When Nebula stops, an error code ([docs/using-nebula/error-codes.md](docs/using-nebula/error-codes.md)) naming the cause and a report in
  `%LOCALAPPDATA%\Nebula\crashes`.
>>>>>>> upstream/main

## Requirements

- Your own copy of **Super Mario Galaxy**, revision 0, as an ISO, RVZ or extracted folder: North America (RMGE01), Europe (RMGP01) or Japan (RMGJ01). Everyone in a room needs the same game build and a supported disc.
- Windows 10 (version 1803 or newer) or Windows 11, 64-bit
<<<<<<< HEAD
- A GPU with Direct3D 12 support and a CPU with AVX2
- 8 GB of RAM
- An internet connection to play online
=======
- A GPU with Direct3D 12 support
- A CPU with SSE4.2 (2.0; AVX2 CPUs run a build for them)
- 8 GB of RAM; for 1.0.2, 16 GB of free disk space and an internet connection during installation, about 5.5 GB
  afterwards
>>>>>>> upstream/main

## Install

1. Download `Nebula-Setup.exe` from the [latest release](https://github.com/NebulaCoop/nebula-coop/releases/latest). 
It isn't code-signed, so Windows SmartScreen shows "Unknown publisher": choose **More info**, then **Run anyway**.
2. Select your copy of the game and choose **Install**.
3. Start **Nebula** from the Start menu.

Run Setup again to update, repair or uninstall. Updates come from this repository's releases and are signed; Setup checks the signature before installing.

## Playing online

1. Set your name and character in **Options > PLAYER**.
2. **To host:** choose **Host**, pick or create a save, and leave NETWORK on **NebulaNet** (or pick **Direct** to share your address and port instead). Set LOBBY to **Public** to list the room for anyone, or keep it **Friends only**; a password makes joining need it (and finds the room by it). Then choose **Host**. The room code shows in your chat and on the ONLINE tab.
3. **To join:** choose **Join**, then enter the room code or the host's `address:port`, or pick a room under ROOMS: **Public** for everyone's public rooms, **Private** for the rooms with the password you typed.

To leave, use the pause menu's **Quit**. Only the host's Quit saves the room's game; if the host leaves, everyone goes back to the main menu.

## Building from source

<<<<<<< HEAD
[docs/BUILDING.md](docs/BUILDING.md) covers the tools, the game module, the host, Setup and the tests. On Windows,
[`setup.ps1`](setup.ps1) checks the tools and builds everything in one go:
=======
You need these tools:

* CMake 3.24 or later, and Ninja
* LLVM 21 or later, for the game module
* Rust stable with the MSVC target, for the disc importer and LumaLink
* The .NET 8 SDK, for Setup and the launcher
* Visual Studio 2022 C++ tools and the Windows SDK, for the Windows host

In PowerShell, run `. .\tools\enter_msvc_environment.ps1` first. CMake downloads the other dependencies when it
configures, from pinned archives it checks by SHA-256.

### Importing the game

```powershell
cargo build --release --locked -p nebula-import
.\target\release\nebula-import.exe --key common.key identify D:\Games\SMG.rvz
.\target\release\nebula-import.exe --key common.key import D:\Games\SMG.rvz --output D:\nebula\content
```

`identify` must report RMGE01, RMGP01 or RMGJ01. An extracted game folder works in place of the disc image. The
content folder then holds `game.pak`.

### Building the host

```powershell
cmake -S . -B build\host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build\host --target Nebula
```

`-DNEBULA_VULKAN=ON` also builds `NebulaVk.exe`. The host is built for AVX2. For processors without AVX2,
configure a second folder with `-DNEBULA_HOST_BASELINE=ON`: it builds `Nebula-x86-64-v2.exe`, which goes
beside `Nebula.exe` and is started in its place on those processors.

### Building the game module

```powershell
.\native\build.ps1 -Petari native\petari -Build build\game
```

This builds `build\game\smg_game.dll`, and `build\game\x86-64-v2\smg_game.dll` for processors without AVX2. Add
`-Pgo` for a player build: it links with ThinLTO and the profile in `native/pgo`.

On Linux, configure `native` with clang from LLVM 21 or later. It builds `libsmg_game.so` and
`libsmg_game_plain.so`.

### Running

Put `Nebula.exe` and `smg_game.dll` in one folder, then run:

```powershell
.\Nebula.exe --pak D:\nebula\content\game.pak --saves "$env:LOCALAPPDATA\Nebula\saves"
```

### Running the tests

The test runs' parts (the test extension and its tests) are built only when the configure asks for them:

```powershell
cmake -S . -B build\host -DNEBULA_EXTRA_CMAKE=tools/test_driver/test_driver.cmake
cmake --build build\host
ctest --test-dir build\host --output-on-failure
cargo test -p nebula-import
sh native/tests/run_tests.sh
.\tools\audit_repo.ps1
```

### Building Setup and the Android app

`installer\build-native-setup.ps1` builds the launcher and Setup and writes `Nebula-Setup.exe`.
`platform\android\build_apk.ps1` builds the Android app. Each script lists its arguments at the top. Both take
`-ShippedMods`, a folder holding `lumakit.luma`, which LumaLink builds and packs from its `lumakit` folder.

### Packaging for Linux and macOS

`installer/package-linux.sh --bin <build folder> --version <version> --shipped <folder>` packs a Linux build as
`Nebula-<version>-linux-<arch>.tar.gz`. Players unpack it and run `./install.sh`, which reads their disc image and
installs Nebula for the user or as a portable folder. `platform/apple/build_macos.sh` builds `Nebula.app` for macOS.
>>>>>>> upstream/main

```powershell
.\setup.ps1
```

You still need your own copy of the game to run what you build. The co-op design and its progress are in
[docs/COOP_PLAN.md](docs/COOP_PLAN.md) and [TODO.md](TODO.md).

## Related repositories

- [keepvaibin/nebula](https://github.com/keepvaibin/nebula): upstream Nebula, which this repository follows
- [NebulaCoop/NebulaNet](https://github.com/NebulaCoop/NebulaNet): the room-code matchmaker (a Cloudflare Worker)
- [NebulaCoop/lumalink-coop](https://github.com/NebulaCoop/lumalink-coop): LumaLink, which converts mods and texture
  packs to `.luma` files

## Reporting problems

Report co-op problems on this repository's [issue tracker](https://github.com/NebulaCoop/nebula-coop/issues). If the
game closed unexpectedly, attach its crash report from `%LOCALAPPDATA%\NebulaCoop\crashes`. Please don't report co-op
problems to upstream Nebula.

## How it was made

Nebula's host, renderer, installer, tests and documentation were written with AI coding agents (Anthropic's Claude),
directed, reviewed and tested by its author. Much of Nebula Co-Op's co-op code was written the same way. The game's own
logic is the Petari decompilation, compiled for the PC.

## License

<<<<<<< HEAD
Nebula Co-Op, like Nebula, is licensed under the GNU General Public License v3.0 only ([LICENSE](LICENSE)).
=======
Nebula is licensed under the GNU General Public License v3.0 only ([LICENSE](LICENSE)). Third-party code and its
licenses are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Nebula builds on the Petari decompilation (SMGCommunity), Aurora, Dusklight, SMAA, AMD FidelityFX, NVIDIA
Streamline and the `nod` disc library.
>>>>>>> upstream/main

"Super Mario Galaxy", "Wii" and "Nintendo" are trademarks of Nintendo.
