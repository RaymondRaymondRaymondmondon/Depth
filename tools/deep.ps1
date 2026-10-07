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
    "run" { Start-Process $exe -ArgumentList $Arg }
}
