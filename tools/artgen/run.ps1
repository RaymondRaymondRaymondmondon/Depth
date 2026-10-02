# Regenerates Depth's baked art (the Trawl Visual Overhaul Spec, section 4) by running every generator in this folder
# through Blender headless. The outputs (glTF .glb with embedded PNG texture sets) are committed, so the game builds
# without Blender; run this after changing a generator.
#     .\tools\artgen\run.ps1                 every generator
#     .\tools\artgen\run.ps1 test_carbine    just the ones whose name contains the text
param([string]$Only = "")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$blender = $env:DEPTH_BLENDER
if (-not $blender) {
    $cands = @("C:\Program Files\Blender Foundation\Blender 5.2\blender.exe")
    $cands += Get-ChildItem "C:\Program Files\Blender Foundation" -Recurse -Filter blender.exe -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
    $blender = $cands | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
}
if (-not $blender) { throw "Blender not found: install it or set DEPTH_BLENDER to blender.exe" }

# generator -> output folder (relative to the repo root)
$jobs = @(
    @{ script = "test_carbine.py"; out = "assets\shared\test" },
    @{ script = "test_head.py";    out = "assets\shared\test" }
)
$procs = @()
foreach ($j in $jobs) {
    if ($Only -and $j.script -notlike "*$Only*") { continue }
    $out = Join-Path $root $j.out
    New-Item -ItemType Directory -Force $out | Out-Null
    $log = Join-Path $env:TEMP ("artgen_" + [IO.Path]::GetFileNameWithoutExtension($j.script) + ".log")
    Write-Host "artgen: $($j.script) -> $($j.out)"
    $procs += @{ p = (Start-Process -PassThru -NoNewWindow -FilePath $blender -WorkingDirectory $root `
        -ArgumentList "-b --factory-startup -P `"$(Join-Path $PSScriptRoot $j.script)`" -- --out `"$out`"" `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err"); log = $log; name = $j.script }
}
$failed = 0
foreach ($q in $procs) {
    $q.p.WaitForExit()
    $lines = Get-Content $q.log | Select-String "artgen:"
    $lines | ForEach-Object { Write-Host "  $($_.Line)" }
    if (-not ($lines | Select-String "wrote")) { $failed++; Write-Host "  FAILED: $($q.name) (see $($q.log) and .err)" -ForegroundColor Red }
}
if ($failed) { exit 1 }
