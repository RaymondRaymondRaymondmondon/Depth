# Red Tide balance: runs --redtide-sim once per seed in parallel processes and sums them up.
#   .\tools\rt_balance.ps1 -Map ship -Tides 40 -Style careful -Players 4 -Seeds 8
# Each process is one run (a careful team can take 5-10 minutes of CPU on a busy map). Prints every run's line and the
# average tide reached; extra environment (tuning overrides) is passed through.
param(
    [string]$Map = "ship",
    [int]$Tides = 40,
    [string]$Style = "careful",
    [int]$Players = 4,
    [int]$Seeds = 8,
    [int]$Base = 1000
)
$exe = Join-Path $PSScriptRoot "..\build\Release\depth.exe"
$out = Join-Path $PSScriptRoot "..\shots\rt_balance"
New-Item -ItemType Directory -Force $out | Out-Null
$procs = @()
for ($k = 0; $k -lt $Seeds; $k++) {
    $seed = $Base + $k * 7919
    $log = Join-Path $out "$Map-$Style-$k.txt"
    $env:DEPTH_SEED = "$seed"
    $procs += Start-Process -FilePath $exe -ArgumentList "--redtide-sim", $Map, $Tides, $Style, 1, $Players -RedirectStandardOutput $log -NoNewWindow -PassThru
}
Remove-Item env:DEPTH_SEED -ErrorAction SilentlyContinue
$procs | Wait-Process
$reached = @(); $wipes = 0
for ($k = 0; $k -lt $Seeds; $k++) {
    $log = Join-Path $out "$Map-$Style-$k.txt"
    $line = Select-String -Path $log -Pattern '^\s+run\s+1:' | Select-Object -First 1
    if ($line) {
        Write-Host $line.Line.Trim()
        if ($line.Line -match 'tide (\d+)') { $reached += [int]$Matches[1] }
        if ($line.Line -match 'wiped') { $wipes++ }
    }
    Select-String -Path $log -Pattern 'downed by:' | ForEach-Object { Write-Host ("      " + $_.Line.Trim()) }
}
if ($reached.Count) {
    $avg = ($reached | Measure-Object -Average).Average
    $sorted = $reached | Sort-Object
    Write-Host ("{0} {1} x{2}: average tide {3:N1}, median {4}, range {5}-{6}, wiped {7} of {8}" -f $Map, $Style, $Players, $avg, $sorted[[int]($sorted.Count / 2)], $sorted[0], $sorted[-1], $wipes, $reached.Count)
}
