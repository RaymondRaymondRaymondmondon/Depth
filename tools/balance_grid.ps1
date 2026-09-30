# Balance grid: depth.exe --sim for the given locations at every Shallows tier (crew level = cave level).
# Usage: .\shots\grid.ps1 -Locs 1,2,3 -Runs 1000
param([int[]]$Locs = @(0, 1, 2, 3), [int]$Runs = 1000)
$lv = @(0, 1, 3, 5, 6)
$jobs = foreach ($l in $Locs) { foreach ($t in 0..4) {
    Start-Job -ArgumentList $l, $t, $lv[$t], $Runs -ScriptBlock {
        param($l, $t, $v, $n)
        Set-Location "C:\Users\phill\Downloads\Depth"
        $o = & .\build\Release\depth.exe --sim $n $v sensible $t $l 2>&1
        $w = ($o | Select-String "wins ([\d.]+)%").Matches[0].Groups[1].Value
        $r = ($o | Select-String "reached the boss ([\d.]+)%   beat it ([\d.]+)%").Matches[0]
        $k = ($o | Select-String "standing at the end:(.*)").Matches[0].Groups[1].Value
        [pscustomobject]@{ Loc = $l; Tier = $t; Lv = $v; Win = [double]$w; Reach = [double]$r.Groups[1].Value; BeatBoss = [double]$r.Groups[2].Value; Killers = $k.Trim() }
    }
} }
$jobs | Wait-Job | Receive-Job | Sort-Object Loc, Tier | Format-Table Loc, Lv, Win, Reach, BeatBoss, Killers -AutoSize -Wrap
