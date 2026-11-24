# Building Nebula from source

`Nebula-Setup.exe` installs a prebuilt Nebula. Build by hand to develop Nebula or to check a release.

## Tools

- Visual Studio 2022 C++ tools and the Windows SDK, for the host.
- LLVM/clang 19 or newer (`C:\Program Files\LLVM`), for the game module.
- CMake 3.24+ and Ninja.
- Rust stable (MSVC target), for the disc import tool.
- LLVM 22 for a release's game module (`native\build.ps1 -Llvm`); Setup itself is plain C++ built with the MSVC tools.

In PowerShell, dot-source `tools\enter_msvc_environment.ps1` first. CMake downloads the remaining dependencies at
configure time from pinned archives checked by SHA-256: the FidelityFX SDK (FSR 3), and for the Vulkan renderer
Vulkan-Headers, volk, VulkanMemoryAllocator, glslang, SPIRV-Tools and SPIRV-Headers, and the DirectX Shader Compiler's
release (a build tool: it compiles the ray tracing shaders to SPIR-V; `GALAXY_DXC_SPIRV` names another dxc that can).

## 1. Game files

```powershell
cargo build --release --locked -p nebula-import
.\target\release\nebula-import.exe --key common.key identify D:\Games\SMG.rvz      # must report RMGE01, RMGP01 or RMGJ01
.\target\release\nebula-import.exe --key common.key import D:\Games\SMG.rvz --output D:\nebula\content
```

An extracted folder works in place of the disc image; `--features folders-only` builds an importer that takes folders only. The content folder holds `game.pak`.

## 2. Host

```powershell
cmake -S . -B build\host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build\host --target Nebula
```

`-DNEBULA_VULKAN=ON` also builds `NebulaVk.exe`; `-DNEBULA_FSR3=OFF` leaves out FSR 3. DLSS uses NVIDIA's
signed Streamline libraries, which the host loads at run time from the vendor folder.

## 3. Game module

```powershell
.\native\build.ps1 -Petari native\petari -Build build\game
```

Builds `build\game\smg_game.dll` with clang and Ninja.

A build for players adds `-Pgo`: ThinLTO with the profile in `native/pgo` (`tools/verify/pgo_profile.sh` makes it). It takes longer to link and spends about 17 % less CPU on the game.

## 4. Run

Put `Nebula.exe` and `smg_game.dll` in one folder with the `content` folder from step 1, then:

```powershell
.\Nebula.exe
```

With no arguments it finds `content\game.pak` next to it (or in the current folder) and uses the installed game's saves,
`%LOCALAPPDATA%\NebulaCoop\saves`. `--pak <game.pak>` and `--nand <folder>` name others:

```powershell
.\Nebula.exe --pak D:\nebula\content\game.pak --saves "$env:LOCALAPPDATA\Nebula\saves"
```

The other arguments are in [using-nebula/command-line-arguments.md](using-nebula/command-line-arguments.md).

## Tests

```powershell
ctest --test-dir build\host --output-on-failure
.\native\build.ps1 -Petari native\petari -Build build\game -Target native_tests
ctest --test-dir build\game --output-on-failure
cargo test -p nebula-import -p luma-validate
.\tools\audit_repo.ps1
```

On Linux and macOS, `cmake -S tests/unit -B build/unit -G Ninja` builds the same unit tests (the co-op ones included);
`ctest --test-dir build/unit` runs them.

The Build workflow runs only what a change can affect: a change under `installer/` compiles Setup (`installer/native`)
and not the game builds, and any other change to the code runs the game builds and their tests and not
the installer's. `installer/assets/`, `runtime/include/galaxy/store_version.inc` and `build.yml` itself run both;
documentation, `tools/`, the `nebulanet` and `web` submodules and the other workflows run neither. The **Build result**
check reports either way, so it is the one to require before merging. It covers the Windows build (host, game module,
disc import tool) and Setup's compile; the Linux, Android and macOS builds run and show their own checks but don't block
a merge.

A push doesn't cancel a build that is still going for an earlier push to the same branch or pull request unless it
changes what that build is building, part by part (the game builds, Setup, Android). An installer build keeps going
when the next push changes only the game, and the next push's run takes the earlier run's installer result instead of
building it again (its **Earlier build** job). A push that changes nothing built, such as a submodule bump or
documentation, cancels nothing and waits for the earlier run's result. If that earlier run is re-run after a failure,
re-run the later one too.

## Setup

```powershell
.\installer\build-native-setup.ps1 -Version <version> -Dxc <unpacked DirectX Shader Compiler release> -Bin <folder with Nebula.exe and smg_game.dll> -Names <names> -ShippedMods <folder with lumakit.luma>
```

Builds the disc import tool, the install root's `Nebula.exe` and Setup (`installer/native`), and writes `Nebula-Setup.exe` with the host and game module
inside it.

## Releases

Pushing a version tag builds a release (`.github/workflows/release.yml`), as upstream Nebula's 2.0 releases are built.
Write its notes first, in `.github/release-notes/<version>.md` (a tag without them is refused), and tag a commit on main
whose Build result passed:

```powershell
git tag -a v2.0.0-beta.1 -m "Nebula 2.0.0-beta.1"
git push origin v2.0.0-beta.1
```

The tag's version is `CMakeLists.txt`'s project version, with `-beta.<n>` for a beta. The workflow makes the draft release
`Nebula <version>` with those notes (a beta is a pre-release) and builds every package into it: the Windows Setup
(`Nebula-Setup.exe`, checked by `tools/release_check.ps1` without Defender), the Linux package
(`Nebula-Linux-x64.tar.gz`), the Mac app (`Nebula-macOS.zip`, Intel and Apple silicon) and the Android app
(`Nebula-Android-unsigned.apk`), each with the manifest its update reads. A beta's files are `Nebula-Beta-...`, and every
build carries the label (`NEBULA_VERSION_SUFFIX`), so a beta reports `2.0.0-beta.1` and only betas update to it.
Unlike upstream's betas, ours keep mod support. Each package is started once on GitHub's machines before it is added.
Then `installer/sign-release.ps1` signs the draft (below), and the release is published by hand on GitHub. The
`Nebula.pdb` files are kept as the run's `release-symbols-*` downloads for 90 days.

**Actions** > **Release** > **Run workflow** with **dry run** builds every package from a branch and keeps them as the
run's downloads (no tag, no release); its **version** builds as that version (and needs its notes). A pull request
that changes how a release is built runs a dry run too.

Setup installs NebulaCoop beside an installed upstream Nebula, never over it: `%LOCALAPPDATA%\Programs\NebulaCoop`,
data in `%LOCALAPPDATA%\NebulaCoop`, its own Start menu folder, `HKCU\Software\NebulaCoop` and Add/Remove Programs entry
(`installer/native/common.h`, `paths::kProductName`; the game's own `galaxy::kDataFolderName`). It registers no `.luma`
file type and no `nebula:` links: those stay Nebula's. When a first install finds Nebula (its registered install folder
or its data folder), Setup offers to copy its saves and settings, and its packed game when this version can read it,
so the disc is not asked for. Nebula's folders are only read. Updates never ask.

Updates take only what the release key signs: the private half of `installer/release-public-key.xml`. Put that private
key (RSA XML) in the repository secret `NEBULA_RELEASE_KEY`, and the workflow signs the draft itself: the Setup, every
manifest and `SHA256SUMS`. The Android app is signed with the app signing key when the repository has it as the secrets
`NEBULA_ANDROID_KEYSTORE` (the keystore, base64), `NEBULA_ANDROID_STORE_PASSWORD`, `NEBULA_ANDROID_KEY_ALIAS` and
`NEBULA_ANDROID_KEY_PASSWORD`. Whatever the workflow had no key for, sign on the PC that has it:

```powershell
.\installer\sign-release.ps1 -Tag v2.0.0-beta.1 -AndroidSigning <signing.properties>
```

To make a new key pair (keep `nebula-release-private.xml` out of the repository):

```powershell
$rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider 4096
$rsa.PersistKeyInCsp = $false
New-Item -ItemType Directory -Force "$env:USERPROFILE\.nebula-release-key" | Out-Null
[IO.File]::WriteAllText("$env:USERPROFILE\.nebula-release-key\nebula-release-private.xml", $rsa.ToXmlString($true))
[IO.File]::WriteAllText("$PWD\installer\release-public-key.xml", $rsa.ToXmlString($false))
```

Run it from the repository's root.
