param(
    # stock: the unmodified cartridge -> generated/
    # vc:    VC Edition image (e-Reader content) -> generated_vc/. -Rom may be
    #        the already-patched VC Edition ROM, or the stock ROM, in which
    #        case -Patch (the VC Edition .xdelta) is applied first.
    [ValidateSet('stock', 'vc')][string]$Variant = 'stock',
    [string]$Rom = (Join-Path $PSScriptRoot '..\roms\super_mario_advance_4_usa_rev1.gba'),
    [string]$Patch = (Join-Path $PSScriptRoot '..\roms\SMA4_USA_Australia_Rev_1_to_USA_VC_Edition.xdelta'),
    [string]$GbarecompRoot = (Join-Path $PSScriptRoot '..\gbarecomp'),
    [string]$Bios = '',
    [int]$MaxFunctions = 65536
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$romPath = (Resolve-Path $Rom).Path
$engine = (Resolve-Path $GbarecompRoot).Path
if (-not $Bios) { $Bios = Join-Path $engine 'bios\gba_bios.bin' }
$biosPath = (Resolve-Path $Bios).Path
$tool = Join-Path $engine 'build\gba_recompile.exe'
if (-not (Test-Path -LiteralPath $tool)) {
    throw "Missing $tool. Configure and build this game's gbarecomp worktree first."
}
$actual = (Get-FileHash -LiteralPath $romPath -Algorithm SHA1).Hash.ToLowerInvariant()
$expected = '532f3307021637474b6dd37da059ca360f612337'
$vcConfig = Join-Path $root 'game_vc.toml'
$vcExpected = ''
if ($Variant -eq 'vc') {
    $vcMatch = [regex]::Match((Get-Content -LiteralPath $vcConfig -Raw),
        '(?ms)^\[identity\]\s*\r?\nsha1\s*=\s*"([0-9a-f]{40})"')
    if (-not $vcMatch.Success) {
        throw 'game_vc.toml is unpinned. Run tools\vc_edition.py pin first.'
    }
    $vcExpected = $vcMatch.Groups[1].Value
}
$romIsVc = $Variant -eq 'vc' -and $actual -eq $vcExpected
if ($actual -ne $expected -and -not $romIsVc) {
    $want = if ($Variant -eq 'vc') { "$expected (stock) or $vcExpected (VC Edition)" } else { $expected }
    throw "Super Mario Advance 4 ROM SHA-1 mismatch: got $actual expected $want"
}

$biosActual = (Get-FileHash -LiteralPath $biosPath -Algorithm SHA1).Hash.ToLowerInvariant()
$biosExpected = '300c20df6731a33952ded8c436f7f186d25d3492'
if ($biosActual -ne $biosExpected) {
    throw "GBA BIOS SHA-1 mismatch: got $biosActual expected $biosExpected"
}

if ($Variant -eq 'vc') {
    if (-not $romIsVc) {
        $patchPath = (Resolve-Path $Patch).Path
        $vcRom = Join-Path $root 'roms\super_mario_advance_4_usa_vc.gba'
        # Applies the VCDIFF without xdelta3, checks the patch's Adler-32 and
        # the SHA-1 pinned in game_vc.toml before anything is generated.
        & (Get-Command python.exe).Source (Join-Path $root 'tools\vc_edition.py') apply `
            --rom $romPath --patch $patchPath --out $vcRom
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $romPath = $vcRom
    }
    $gameConfig = $vcConfig
    $outDir = Join-Path $root 'generated_vc'
} else {
    $gameConfig = Join-Path $root 'game.toml'
    $outDir = Join-Path $root 'generated'
}

$biosConfig = Join-Path $engine 'bios\gba_bios.toml'
& $tool --bios $biosPath --config $biosConfig `
    --out (Join-Path $engine 'src\runtime\generated_bios')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $tool --rom $romPath --config $gameConfig `
    --out $outDir --max-functions $MaxFunctions
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
