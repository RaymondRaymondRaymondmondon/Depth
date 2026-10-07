# The Deep (Unity) from the command line, no Editor window:
#   tools\deep.ps1 build            configure + build games\TheDeep\TheDeep.exe (prints C# and shader errors)
#   tools\deep.ps1 shots [names]    build, then render TheDeep\shots\deep_<name>.png (all if no names)
#   tools\deep.ps1 test             the EditMode tests (TheDeep\Assets\Deep\Tests), results in TheDeep\Logs\tests.xml
#   tools\deep.ps1 run [args]       launch the built game
param([string]$What = "build", [string]$Arg = "")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$unity = "C:\Program Files\Unity\Hub\Editor\6000.6.4f1\Editor\Unity.exe"
$proj = Join-Path $root "TheDeep"
$exe = Join-Path $root "games\TheDeep\TheDeep.exe"
$log = Join-Path $env:TEMP "deep_build.log"

function Build {
    & $unity -batchmode -quit -projectPath $proj -executeMethod DeepBuild.BuildWindows -logFile $log | Out-Null
    $bad = Select-String -Path $log -Pattern "error CS\d+|Shader error|DEEP BUILD: Failed" | Select-Object -First 20
    $ok = Select-String -Path $log -Pattern "DEEP BUILD:" | Select-Object -Last 1
    if ($bad) { $bad | ForEach-Object { $_.Line }; throw "build failed" }
    $ok.Line
}

switch ($What) {
    "build" { Build }
    "shots" {
        Build
        $dir = Join-Path $proj "shots"; New-Item -ItemType Directory -Force $dir | Out-Null
        $names = if ($Arg) { $Arg } else { "all" }
        $p = Start-Process $exe -ArgumentList "-shot", $names, "-shotdir", "`"$dir`"", "-logFile", "`"$env:TEMP\deep_run.log`"" -PassThru -Wait
        Select-String -Path "$env:TEMP\deep_run.log" -Pattern "DEEP |Exception" | ForEach-Object { $_.Line }
    }
    "test" {
        $res = Join-Path $proj "Logs\tests.xml"
        & $unity -batchmode -projectPath $proj -runTests -testPlatform EditMode -testResults $res -logFile "$env:TEMP\deep_test.log" | Out-Null
        if (Test-Path $res) { [xml]$x = Get-Content $res; $r = $x.'test-run'; "tests: $($r.passed) passed, $($r.failed) failed of $($r.total)"; $x.SelectNodes("//test-case[@result='Failed']") | ForEach-Object { "FAILED " + $_.fullname + ": " + $_.failure.message.'#cdata-section' } }
        else { "no results"; Select-String -Path "$env:TEMP\deep_test.log" -Pattern "error CS\d+" | Select-Object -First 10 | ForEach-Object { $_.Line } }
    }
    "nettest" {
        # a host and two crewmates on this PC over UDP (127.0.0.1): the host runs the script and exits 0 if all passed
        $port = "47789"
        $common = @("-skipopening", "-nettest", "-port", $port, "-screen-width", "480", "-screen-height", "270", "-screen-fullscreen", "0")
        if ($Arg -ne "windowed") { $common += @("-batchmode", "-nographics") }
        $hl = "$env:TEMP\deep_net_host.log"; $al = "$env:TEMP\deep_net_a.log"; $bl = "$env:TEMP\deep_net_b.log"
        $h = Start-Process $exe -ArgumentList (@("-role", "host", "-name", "Host", "-seat", "0", "-logFile", "`"$hl`"") + $common) -PassThru
        Start-Sleep -Seconds 4
        $a = Start-Process $exe -ArgumentList (@("-role", "join", "-addr", "127.0.0.1", "-name", "Ann", "-seat", "1", "-logFile", "`"$al`"") + $common) -PassThru
        $b = Start-Process $exe -ArgumentList (@("-role", "join", "-addr", "127.0.0.1", "-name", "Bo", "-seat", "1", "-logFile", "`"$bl`"") + $common) -PassThru
        if (-not $h.WaitForExit(300000)) { Stop-Process -Id $h.Id -Force; "the host timed out" }
        foreach ($p in @($a, $b)) { if (-not $p.WaitForExit(20000)) { Stop-Process -Id $p.Id -Force } }
        Select-String -Path $hl -Pattern "DEEP NETTEST|DEEP NET:|Exception" | ForEach-Object { $_.Line }
        foreach ($l in @($al, $bl)) { Select-String -Path $l -Pattern "Exception|DEEP NET:" | Select-Object -First 8 | ForEach-Object { "  guest: " + $_.Line } }
    }
    "savetest" {
        # the campaign's save: capture, scramble, restore, compare (headless; a scratch save file, never the real one)
        $sl = "$env:TEMP\deep_savetest.log"
        $p = Start-Process $exe -ArgumentList @("-savetest", "-skipopening", "-batchmode", "-nographics", "-save", "`"$env:TEMP\deep_savetest.json`"", "-logFile", "`"$sl`"") -PassThru
        if (-not $p.WaitForExit(240000)) { Stop-Process -Id $p.Id -Force; "the save test timed out" }
        Select-String -Path $sl -Pattern "DEEP SAVETEST|Exception" | ForEach-Object { $_.Line }
    }
    "run" { Start-Process $exe -ArgumentList $Arg }
}
