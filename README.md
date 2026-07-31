# SuperMarioAdvance4Recomp

An early static recompilation of **Super Mario Advance 4: Super Mario Bros. 3**
(Game Boy Advance, USA/Australia revision 1, `AX4E`) built on
[`gbarecomp`](https://github.com/mstan/gbarecomp).

This repository contains no ROM, Nintendo GBA BIOS, save data, or generated
translation of ROM code. You must supply your own legally dumped copies.

## Status

The native build boots through the original BIOS, reaches the title sequence,
and runs Super Mario Bros. 3 gameplay. The tested 12,000-frame passive boot and
deterministic input/fuzz routes execute with full static AOT coverage: zero
dispatch misses, interpreted instructions, self-healed functions, or
unhandled I/O.

The ROM intentionally performs deterministic open-bus reads. Acceptance pins
the current counts—73,600 on the passive route and 117,760 on the fuzz
route—so behavioral drift still fails.

## Launcher and in-game menu

A normal windowed launch opens the `recomp-ui` setup screen for ROM/BIOS,
display, audio, and controls. `--no-launcher` skips it once; `--launcher`
forces it to reopen even when **Skip launcher on boot** is enabled.

During play, press **Escape** or a controller's Guide button to open the
in-game menu. Navigate with arrow keys or D-pad, accept with Enter/A, and go
back with Escape/B. Scripted and headless runs bypass the launcher.

## Required ROM and BIOS

| Input | Local path | Required SHA-1 |
|---|---|---|
| Super Mario Advance 4 (USA/Australia, Rev 1) ROM | `roms/super_mario_advance_4_usa_rev1.gba` | `532f3307021637474b6dd37da059ca360f612337` |
| Game Boy Advance BIOS | `gbarecomp/bios/gba_bios.bin` | `300c20df6731a33952ded8c436f7f186d25d3492` |

The runtime refuses an unrecognized ROM. Both inputs are ignored by Git.

## Building from source

Prerequisites on Windows are CMake 3.20+, Ninja, and an MSYS2 MinGW64
toolchain with SDL2. Clone both pinned public dependencies:

```powershell
git clone --recurse-submodules git@github.com:mstan/SuperMarioAdvance4Recomp.git
cd SuperMarioAdvance4Recomp
```

After supplying the ROM and BIOS:

```powershell
cmake -S gbarecomp -B gbarecomp/build -G Ninja
cmake --build gbarecomp/build --target gba_recompile
pwsh tools/regen.ps1
cmake -S . -B build -G Ninja
cmake --build build --target SuperMarioAdvance4Recomp
```

Generated translation units stay local because they are derived from the
user's ROM.

## Controls

| GBA control | Keyboard |
|---|---|
| D-Pad | Arrow keys |
| A / B | Z / X |
| Start | Enter |
| Select | Backspace |

Save states use **Shift+F1-F9** to save and **F1-F9** to load.

## Static acceptance

The acceptance script runs bounded passive and deterministic input routes with
the interpreter, cached overlay, and self-healing fallback disabled:

```powershell
pwsh tools/test-attract-fuzz.ps1
```

A pass requires the requested frame count, a non-empty capture, zero dispatch
misses or interpreted instructions, and the pinned open-bus observations.

## Legal

This project is an unaffiliated, non-commercial preservation and research
effort. Mario and related names and artwork are trademarks or copyrights of
their respective owners. No copyrighted game or BIOS data is distributed
here.
