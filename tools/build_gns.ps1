# Builds Valve's GameNetworkingSockets for the Deep Arcade (docs/design/5_Depth_Arcade_Networking.md) into external\gns.
# Run once (it takes a while the first time: protobuf and Abseil are compiled by VS's bundled vcpkg). Afterwards
# .\build.ps1 finds external\gns and links networking into depth.exe; without it Depth builds with networking off.
#   external\GameNetworkingSockets   the source (unzip GameNetworkingSockets-master.zip there)
#   external\deps\vcpkg.json         the manifest: protobuf (with Abseil), pinned baseline
param([switch]$Clean)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vs = "C:\Program Files\Microsoft Visual Studio\18\Community"
$ext = Join-Path $root 'external'
$src = Join-Path $ext 'GameNetworkingSockets'
$bld = Join-Path $ext 'gns_build'
$out = Join-Path $ext 'gns'
$vcpkgRoot = Join-Path $ext 'vcpkg_installed'
$triplet = 'x64-windows-static-md'

if (-not (Test-Path "$src\CMakeLists.txt")) {
    $zip = Join-Path $root 'GameNetworkingSockets-master.zip'
    if (-not (Test-Path $zip)) { throw "No GameNetworkingSockets source: put GameNetworkingSockets-master.zip in $root" }
    Expand-Archive $zip -DestinationPath $ext -Force
    Rename-Item (Join-Path $ext 'GameNetworkingSockets-master') 'GameNetworkingSockets'
}

& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -SkipAutomaticLocation | Out-Null
$env:PATH = "$vs\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd;$env:PATH"
$cmake = if (Test-Path 'C:\Program Files\CMake\bin\cmake.exe') { 'C:\Program Files\CMake\bin\cmake.exe' } else { 'cmake' }

if (-not (Test-Path "$vcpkgRoot\$triplet\share\protobuf")) {
    Write-Host '== protobuf (vcpkg) =='
    & "$vs\VC\vcpkg\vcpkg.exe" install "--x-manifest-root=$ext\deps" "--x-install-root=$vcpkgRoot" "--triplet=$triplet"
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg failed' }
}
$protoc = Get-ChildItem "$vcpkgRoot" -Recurse -Filter protoc.exe | Where-Object { $_.FullName -notmatch '\\blds\\' } | Select-Object -First 1
if (-not $protoc) { throw 'protoc.exe not found under external\vcpkg_installed' }

if ($Clean -and (Test-Path $bld)) { [IO.Directory]::Delete($bld, $true) }
Write-Host '== GameNetworkingSockets =='
& $cmake -S $src -B $bld -G 'Visual Studio 18 2026' -A x64 `
    "-DCMAKE_PREFIX_PATH=$vcpkgRoot\$triplet" "-DProtobuf_PROTOC_EXECUTABLE=$($protoc.FullName)" `
    -DUSE_CRYPTO=BCrypt -DBUILD_STATIC_LIB=ON -DBUILD_SHARED_LIB=OFF -DProtobuf_USE_STATIC_LIBS=ON `
    -DENABLE_ICE=ON -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF -DBUILD_TOOLS=OFF "-DCMAKE_INSTALL_PREFIX=$out"
if ($LASTEXITCODE -ne 0) { throw 'GNS configure failed' }
& $cmake --build $bld --config Release --target install --parallel
if ($LASTEXITCODE -ne 0) { throw 'GNS build failed' }
Write-Host "GameNetworkingSockets installed to $out. Run .\build.ps1 to link it into Depth."
