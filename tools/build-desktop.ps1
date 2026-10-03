param([string]$QtRoot='D:\CampusPulseSDK\6.8.3\msvc2022_64')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if (-not (Test-Path -LiteralPath "$QtRoot\lib\cmake\Qt6\Qt6Config.cmake")) { throw "Qt SDK not found: $QtRoot" }
cmake -S $projectRoot -B "$projectRoot\build" -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build "$projectRoot\build" --config Release --parallel 6
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed' }
& "$QtRoot\bin\windeployqt.exe" --release --no-translations --no-compiler-runtime "$projectRoot\build\Release\CampusPulse.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
$env:PATH="$QtRoot\bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH="$QtRoot\plugins\platforms"
if ($env:QT_QPA_PLATFORM -eq "offscreen") { $env:QT_QPA_FONTDIR="$env:WINDIR\Fonts" }
ctest --test-dir "$projectRoot\build" -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Prototype tests failed' }
