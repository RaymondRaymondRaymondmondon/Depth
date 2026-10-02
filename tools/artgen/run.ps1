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
$weapons = @("revolver,derringer,pepperbox,flarepistol,speargun,twinspear,harpistol,airpistol,captainpistol,shotgun",
             "carbine,chatter,rifle,nitro,longbow,puntgun,riveter,blunderbuss,airrifle,prod",
             "rocketharpoon,priest,knife,gaff,knuckles,pin,boathook,spike,cleaver,cutlass",
             "coralclub,sharkblade,flenser,lance,maul,obsidian,crackerjack,lampoil,dynamite,depthcharge")
$jobs = @(
    @{ script = "test_carbine.py"; out = "assets\shared\test" },
    @{ script = "test_head.py";    out = "assets\shared\test" },
    @{ script = "crew.py";         out = "assets\shared\crew" },
    @{ script = "rt_divers.py";    out = "assets\shared\divers" },
    @{ script = "rt_fphands.py";   out = "assets\shared\divers" },
    @{ script = "costumes.py";     out = "assets\shared\costumes" },
    @{ script = "weapons_rt.py";   out = "assets\redtide\weapons" },
    @{ script = "creatures_rt.py"; out = "assets\redtide\creatures" },
    @{ script = "flora_rt.py";     out = "assets\redtide\flora" },
    @{ script = "stations_rt.py";  out = "assets\redtide\stations" },
    @{ script = "ship_rt.py";      out = "assets\redtide\ship" },
    @{ script = "maps_rt.py";      out = "assets\redtide\maps" },
    @{ script = "attachments.py";  out = "assets\shared\attachments" },
    @{ script = "boat.py";         out = "assets\trawl" },
    @{ script = "dock.py";         out = "assets\trawl" },
    @{ script = "fish.py";         out = "assets\trawl\fish" },
    @{ script = "props.py";        out = "assets\trawl\props" }
)
foreach ($w in $weapons) { $jobs += @{ script = "weapons.py"; out = "assets\shared\weapons"; extra = "--only $w"; tag = $w.Split(",")[0] } }
$procs = @()
foreach ($j in $jobs) {
    if ($Only -and $j.script -notlike "*$Only*" -and -not ($j.extra -and $j.extra -like "*$Only*")) { continue }
    $out = Join-Path $root $j.out
    New-Item -ItemType Directory -Force $out | Out-Null
    $log = Join-Path $env:TEMP ("artgen_" + [IO.Path]::GetFileNameWithoutExtension($j.script) + $(if ($j.tag) { "_" + $j.tag } else { "" }) + ".log")
    $extra = if ($j.extra) { $j.extra } else { "" }
    if ($Only -and $j.extra -and $j.extra -like "*$Only*") { $extra = "--only $Only" }
    Write-Host "artgen: $($j.script) $extra -> $($j.out)"
    $procs += @{ p = (Start-Process -PassThru -NoNewWindow -FilePath $blender -WorkingDirectory $root `
        -ArgumentList "-b --factory-startup -P `"$(Join-Path $PSScriptRoot $j.script)`" -- --out `"$out`" $extra" `
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
