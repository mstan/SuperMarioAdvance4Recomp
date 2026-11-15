# Super Mario Advance 4 Recomp

> **Experimental preview.** This recompilation is a byproduct of developing
> [gbarecomp](https://github.com/mstan/gbarecomp): the games are the proving
> ground, while the reusable framework is the larger goal. This is not a
> finished commercial port, so expect rough edges and please report problems.
> For more context, read
> [Recomp + AI: 5 Months Later »](https://1379.tech/recomp-ai-5-months-later/).

Static recompilation of **Super Mario Advance 4: Super Mario Bros. 3** for
Windows. The original Game Boy Advance code runs as a native desktop
application with a modern launcher, controller support, fullscreen display,
audio settings, and save states.

The game ROM and Nintendo GBA BIOS are **not included**. You must provide your
own legally obtained dumps.

## Status

The game boots, reaches the title screen, and runs Super Mario Bros. 3
gameplay. The initial `v0.0.1` build is an experimental release; back up
important saves and expect game-specific issues that have not been discovered
yet.

## Quick start

1. Download the Windows zip from [Releases](../../releases) and extract it.
2. Run `SuperMarioAdvance4Recomp.exe`.
3. In the launcher, select your **Super Mario Advance 4: Super Mario Bros. 3
   (USA/Australia, Rev 1)** ROM and retail GBA BIOS.
4. Configure display, audio, and controls, then select **Play**.

The launcher remembers valid files after the first setup. Enable **Skip
launcher on boot** if you want later launches to go directly into the game.

## Features

For smoother pixel edges, enable **Pixel Smoothing** in the launcher's **Mods**
page and select HQ2x, HQ3x or HQ4x before Play. It is disabled by default;
existing HQ2x selections keep HQ2x. The replaceable filter library and its
LGPL source are included under `third-party/hqx`.

- Native Windows x64 application
- ROM and BIOS setup through the shared
  [recomp-ui](https://github.com/mstan/recomp-ui) launcher
- Keyboard and modern game-controller support
- Windowed and fullscreen play with configurable presentation
- In-game settings menu
- Cartridge saves and save states
- Experimental adaptive widescreen for horizontal and vertical SMB3 rooms, enabled by
  default for fresh settings. The HUD stays anchored and entry wipes cover
  the wide canvas. Enemy activation and visibility use a fixed wider area,
  independent of window size. Cyclic vertical rooms continue their native
  240-pixel world period into the margins, preserving movement and collision
  rather than adding playable columns. Stage and enemy coverage remains
  partial. Menus keep native framing. The mod can be disabled in the Mods catalog.

## Controls

| GBA control | Keyboard |
|---|---|
| D-Pad | Arrow keys |
| A / B | X / Z |
| Start | Enter |
| Select | Right Shift |
| L / R | C / V |

Use **Shift+F1-F9** to save a state and **F1-F9** to load one. Controls can be
changed from the launcher.

## Building from source

Windows development requires CMake, Ninja, MSYS2 MinGW64, and SDL2:

```powershell
git clone --recurse-submodules `
  https://github.com/mstan/SuperMarioAdvance4Recomp.git
cd SuperMarioAdvance4Recomp

cmake -S gbarecomp -B gbarecomp/build -G Ninja
cmake --build gbarecomp/build --target gba_recompile
pwsh tools/regen.ps1
cmake -S . -B build -G Ninja
cmake --build build --target SuperMarioAdvance4Recomp
```

Generation requires the supported ROM revision and a retail GBA BIOS. Their
identities and local development paths are documented in
[`baserom.md`](baserom.md) and [`game.toml`](game.toml). ROM-derived generated
code, copyrighted inputs, saves, and build output remain local and are never
included in releases.

Contributors can run `pwsh tools/test-attract-fuzz.ps1` for the automated
native acceptance routes and `pwsh tools/make_release.ps1 -Version 0.0.1` to
build a sanitized Windows package.

## License

For this project's code covered by the [PolyForm Noncommercial License](LICENSE),
[PERMISSIONS.md](PERMISSIONS.md) grants standing permission for voluntary
donations, sponsorships, and advertising revenue, including support for broader
development initiatives. Payment must not buy access to the software, games
built with it, or their updates, features, or game content, including early access.

Matthew Stanley has authority from the relevant copyright holders to grant
these permissions for the project's PolyForm-covered code. Third-party
components and code explicitly licensed under other terms retain those terms.

PolyForm Noncommercial 1.0.0 — see [`LICENSE`](LICENSE). Third-party
components retain their own licenses.

## Legal

This is an unofficial, non-commercial preservation and research project. It
is not affiliated with or endorsed by Nintendo. Mario and related names,
characters, artwork, and game data are trademarks or copyrights of their
respective owners.

No copyrighted game ROM or Nintendo BIOS data is distributed by this project.

---

Part of the **R.A.I.D. — Retro AI Development** static-recompilation
community.
