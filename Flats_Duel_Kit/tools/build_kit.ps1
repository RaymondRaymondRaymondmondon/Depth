# Builds the kit's checker (tools/duel_check.cpp) and, with -Previews, the art preview renderer, using Visual Studio's
# compiler. Nothing here touches the Depth folder: vendor/ holds copies of the headless Flats engine (refresh them
# with -RefreshVendor), and the preview renderer links the raylib.lib already built in Depth\build (read only).
#   .\tools\build_kit.ps1                 build and run every duel check
#   .\tools\build_kit.ps1 -Previews       also build and run the preview renderer (writes previews\*.png)
#   .\tools\build_kit.ps1 -RefreshVendor  first re-copy flats_card/flats_board/net_msg/arcade_game from Depth\src
param([switch]$Previews, [switch]$RefreshVendor, [string]$Depth = "C:\Users\phill\Downloads\Depth")
$ErrorActionPreference = "Stop"
$kit = Split-Path $PSScriptRoot -Parent
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"   # (Launch-VsDevShell calls vswhere by name)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -SkipAutomaticLocation | Out-Null
if ($RefreshVendor) {
    foreach ($f in "flats_card.h","flats_card.cpp","flats_board.h","flats_board.cpp","net_msg.h","arcade_game.h") { Copy-Item "$Depth\src\$f" "$kit\vendor\$f" -Force }
}
New-Item -ItemType Directory -Force "$kit\build" | Out-Null
Push-Location "$kit\build"
try {
    cl /nologo /std:c++17 /O2 /EHsc /W3 /I"$kit\src" /I"$kit\vendor" `
        "$kit\tools\duel_check.cpp" "$kit\src\flats_duel.cpp" "$kit\src\flats_duel_data.cpp" `
        "$kit\vendor\flats_card.cpp" "$kit\vendor\flats_board.cpp" /Fe:duel_check.exe
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    .\duel_check.exe
    $checks = $LASTEXITCODE
    if ($Previews) {
        $ray = "$Depth\build\_deps\raylib-build\raylib"
        cl /nologo /std:c++17 /O2 /EHsc /MD /I"$kit\src" /I"$kit\vendor" /I"$ray\include" `
            "$kit\tools\render_previews.cpp" "$kit\src\flats_duel_art.cpp" "$kit\src\flats_duel.cpp" "$kit\src\flats_duel_data.cpp" `
            "$kit\vendor\flats_card.cpp" "$kit\vendor\flats_board.cpp" `
            /Fe:render_previews.exe /link "$ray\Release\raylib.lib" winmm.lib gdi32.lib user32.lib shell32.lib opengl32.lib
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        .\render_previews.exe "$kit\previews"
    }
    exit $checks
} finally { Pop-Location }
