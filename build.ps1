# Builds Depth with Visual Studio's compiler, and runs it with -Run.
# Usage (from this folder, in any PowerShell):  .\build.ps1 -Run
param([switch]$Run)

$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with 'Desktop development with C++' not found." }

& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -SkipAutomaticLocation | Out-Null
# CMake needs git to download raylib; fall back to the copy bundled with Visual Studio.
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    $env:PATH = "$vs\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd;$env:PATH"
}

Set-Location $PSScriptRoot
# If a different CMake configured the build folder (say a new CMake was installed), start it afresh.
$cmake = (Get-Command cmake).Source -replace '\\', '/'
if (Test-Path build\CMakeCache.txt) {
    $cached = (Select-String -Path build\CMakeCache.txt -Pattern '^CMAKE_COMMAND:INTERNAL=(.*)$').Matches.Groups[1].Value
    if ($cached -and $cached -ne $cmake) {
        Write-Host "CMake changed ($cached -> $cmake); reconfiguring the build folder."
        Remove-Item -Recurse -Force build\CMakeCache.txt, build\CMakeFiles
    }
}
if (-not (Test-Path build\CMakeCache.txt)) { cmake -B build; if ($LASTEXITCODE) { exit $LASTEXITCODE } }
cmake --build build --config Release
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($Run) { & .\build\Release\depth.exe }
