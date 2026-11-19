# Base ROM

Provide the unmodified USA/Australia revision 1 ROM as
`roms/super_mario_advance_4_usa_rev1.gba`.

- Game code: `AX4E`
- Revision: 1
- Size: 4,194,304 bytes
- SHA-1: `532f3307021637474b6dd37da059ca360f612337`
- MD5: `605286b3aedeffba70bf46b834b120b1`

The ROM is not tracked.

## VC Edition (optional, e-Reader content)

`SuperMarioAdvance4VCRecomp` is generated from the stock ROM above plus
`SMA4_USA_Australia_Rev_1_to_USA_VC_Edition.xdelta`, which restores the
content that originally required the Nintendo e-Reader. Place the patch at
`roms/SMA4_USA_Australia_Rev_1_to_USA_VC_Edition.xdelta` (or pass `-Patch`).

- Format: VCDIFF (xdelta3 / Delta Patcher), one window, Adler-32 verified
- Patch size: 51,046 bytes
- Output size: 8,388,608 bytes, game code still `AX4E`
- Output SHA-1: `dd2879329ec52bd5372f26b75297a67f1a81215a`
- Output MD5: `e7a2792c5913a8420a419f2d01358487`

What the patch changes, decoded from the delta itself:

| Range | Change |
|---|---|
| `0x08039B98`-`0x0803E72C` | Literal and BL edits in the e-Reader card loader; six calls redirected to new code |
| `0x08071492` | BL to `0x083F0000` |
| `0x080B92F8` | One byte |
| `0x083F0000`-`0x083FFFFF` | New Thumb routine, then data (previously free space) |
| `0x08400000`-`0x08426B5F` | e-card level blobs (`ASR0`) plus loader stubs at `0x08424900`-`0x08424A20` |
| `0x08426B60`-`0x087FFFFF` | Zero fill |

`tools/regen.ps1 -Variant vc` accepts either input:

```powershell
# Already-patched VC Edition ROM
pwsh tools/regen.ps1 -Variant vc -Rom roms/sma4_vc_edition.gba
# Stock ROM + patch (applied by tools/vc_edition.py, no xdelta3 needed)
pwsh tools/regen.ps1 -Variant vc -Rom roms/super_mario_advance_4_usa_rev1.gba `
    -Patch roms/SMA4_USA_Australia_Rev_1_to_USA_VC_Edition.xdelta
```

Generated code goes to `generated_vc/` (ignored). The identity above is
already pinned in `game_vc.toml` and the preloaded mod manifests; for a
different patch release, reset it to `UNPINNED` and run
`python tools/vc_edition.py pin --rom <stock> --patch <xdelta>`.
