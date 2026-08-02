param(
    [string]$Rom = "",
    [string]$Bios = "",
    [string]$BuildDir = "build",
    [ValidateSet("Both", "Attract", "Fuzz")][string]$Route = "Both",
    [int]$AttractFrames = 12000,
    [int]$FuzzFrames = 12000,
    [int]$ExpectedAttractUnmapped = 73600,
    [int]$ExpectedFuzzUnmapped = 117760
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "$BuildDir\SuperMarioAdvance4Recomp.exe"
$config = Join-Path $root "game.toml"
$replay = Join-Path $root "tests\basic-fuzz.input"
$resultDir = Join-Path $root "build\acceptance"
if (-not $Rom) { $Rom = Join-Path $root "roms\super_mario_advance_4_usa_rev1.gba" }
if (-not $Bios) { $Bios = Join-Path $root "gbarecomp\bios\gba_bios.bin" }

foreach ($path in @($exe, $config, $replay, $Rom, $Bios)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file is missing: $path"
    }
}
New-Item -ItemType Directory -Path $resultDir -Force | Out-Null

function Invoke-StrictRoute {
    param(
        [string]$Name,
        [int]$Frames,
        [int]$ExpectedUnmapped,
        [string]$InputReplay = ""
    )
    $log = Join-Path $resultDir "$Name.log"
    $png = Join-Path $resultDir "$Name.png"
    $save = Join-Path $resultDir "$Name.sav"
    Remove-Item -LiteralPath $log, $png, $save -Force -ErrorAction SilentlyContinue
    $oldStrict = $env:GBARECOMP_STRICT_STATIC
    $oldHeal = $env:GBARECOMP_SELFHEAL_RECOMPILE
    $oldReplay = $env:GBARECOMP_INPUT_REPLAY
    $oldWatchdog = $env:GBARECOMP_HANG_WATCHDOG
    try {
        $env:GBARECOMP_STRICT_STATIC = "1"
        $env:GBARECOMP_SELFHEAL_RECOMPILE = "0"
        # This is a deterministic frame-bounded acceptance run; the wall-clock
        # no-HALT watchdog is diagnostic and can false-trigger on slow hosts.
        $env:GBARECOMP_HANG_WATCHDOG = "0"
        if ($InputReplay) { $env:GBARECOMP_INPUT_REPLAY = $InputReplay }
        else { Remove-Item Env:GBARECOMP_INPUT_REPLAY -ErrorAction SilentlyContinue }
        $oldErrorAction = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            & $exe --bios $Bios --rom $Rom --frames $Frames --no-window `
                --save-path $save --dump-png $png $config *> $log
            $nativeExitCode = $LASTEXITCODE
        } finally { $ErrorActionPreference = $oldErrorAction }
        if ($nativeExitCode -ne 0) {
            throw "$Name exited with code $nativeExitCode. See $log"
        }
    } finally {
        $env:GBARECOMP_STRICT_STATIC = $oldStrict
        $env:GBARECOMP_SELFHEAL_RECOMPILE = $oldHeal
        $env:GBARECOMP_INPUT_REPLAY = $oldReplay
        $env:GBARECOMP_HANG_WATCHDOG = $oldWatchdog
    }
    $text = Get-Content -LiteralPath $log -Raw
    foreach ($needle in @(
        "ppu_frames=$Frames",
        "unmapped=$ExpectedUnmapped io_unhandled=0",
        "self_heal_coverage=FULLY_STATIC dispatch_misses=0 interpreted_insns=0 healed_native=0"
    )) {
        if (-not $text.Contains($needle)) {
            throw "$Name did not satisfy '$needle'. See $log"
        }
    }
    if (-not (Test-Path -LiteralPath $png -PathType Leaf) -or
        (Get-Item -LiteralPath $png).Length -eq 0) {
        throw "$Name did not produce a non-empty screenshot: $png"
    }
    Write-Host "PASS $Name ($Frames frames)"
}

if ($Route -in @("Both", "Attract")) {
    Invoke-StrictRoute -Name "attract-$AttractFrames" -Frames $AttractFrames `
        -ExpectedUnmapped $ExpectedAttractUnmapped
}
if ($Route -in @("Both", "Fuzz")) {
    Invoke-StrictRoute -Name "basic-fuzz-$FuzzFrames" -Frames $FuzzFrames `
        -ExpectedUnmapped $ExpectedFuzzUnmapped -InputReplay $replay
}
