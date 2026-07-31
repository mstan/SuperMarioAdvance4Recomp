param(
    [string]$Rom = (Join-Path $PSScriptRoot '..\roms\super_mario_advance_4_usa_rev1.gba'),
    [string]$GbarecompRoot = (Join-Path $PSScriptRoot '..\gbarecomp'),
    [int]$MaxFunctions = 65536
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$romPath = (Resolve-Path $Rom).Path
$engine = (Resolve-Path $GbarecompRoot).Path
$tool = Join-Path $engine 'build\gba_recompile.exe'
if (-not (Test-Path -LiteralPath $tool)) {
    throw "Missing $tool. Configure and build this game's gbarecomp worktree first."
}
$actual = (Get-FileHash -LiteralPath $romPath -Algorithm SHA1).Hash.ToLowerInvariant()
$expected = '532f3307021637474b6dd37da059ca360f612337'
if ($actual -ne $expected) {
    throw "Super Mario Advance 4 ROM SHA-1 mismatch: got $actual expected $expected"
}
& $tool --rom $romPath --config (Join-Path $root 'game.toml') `
    --out (Join-Path $root 'generated') --max-functions $MaxFunctions
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
