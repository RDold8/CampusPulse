param(
    [string]$QtRoot='D:\CampusPulseSDK\6.8.3\msvc2022_64',
    [string]$BuildDir='',
    [string]$Destination='',
    [string]$VcRuntimeDir=''
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir=Join-Path $projectRoot 'build' }
if (-not $Destination) { $Destination=Join-Path $projectRoot 'dist\CampusPulse' }
if (-not (Test-Path -LiteralPath "$BuildDir\Release\CampusPulse.exe")) { throw 'Build the desktop app first' }
if (Test-Path -LiteralPath $Destination) {
    if (Get-ChildItem -LiteralPath $Destination -Force | Select-Object -First 1) {
        throw 'Package destination must be empty; select a new directory instead of mixing old and new files'
    }
}
$destination=$Destination
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-Item -LiteralPath "$BuildDir\Release\CampusPulse.exe" -Destination $destination
Copy-Item -LiteralPath "$projectRoot\configs" -Destination $destination -Recurse -Force
# Widgets uses raster rendering; ship only the SQLite backend used by the application.
& "$QtRoot\bin\windeployqt.exe" --release --no-translations --no-compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --exclude-plugins qsqlmimer,qsqlodbc,qsqlpsql "$destination\CampusPulse.exe"
if ($LASTEXITCODE -ne 0) { throw 'Desktop packaging failed' }
if ($VcRuntimeDir) {
    if (-not (Test-Path -LiteralPath "$VcRuntimeDir\vcruntime140.dll")) { throw 'VC redistributable CRT directory is invalid' }
    Copy-Item -Path "$VcRuntimeDir\*.dll" -Destination $destination
}
@('[Paths]','Plugins = .') | Set-Content -LiteralPath "$destination\qt.conf" -Encoding ascii
Copy-Item -LiteralPath "$projectRoot\LICENSE" -Destination $destination
Copy-Item -LiteralPath "$projectRoot\THIRD_PARTY_NOTICES.md" -Destination $destination
Copy-Item -LiteralPath "$projectRoot\docs\desktop-prototype.md" -Destination "$destination\使用说明.md"
Copy-Item -LiteralPath "$projectRoot\docs\subscriptions.md" -Destination "$destination\subscriptions.md"
Copy-Item -LiteralPath "$projectRoot\docs\tasks.md" -Destination "$destination\tasks.md"
Copy-Item -LiteralPath "$projectRoot\docs\automatic-onboarding.md" -Destination "$destination\automatic-onboarding.md"
Copy-Item -LiteralPath "$projectRoot\docs\ai-supplement.md" -Destination "$destination\ai-supplement.md"
Copy-Item -LiteralPath "$projectRoot\docs\ai-provider-management.md" -Destination "$destination\ai-provider-management.md"
Copy-Item -LiteralPath "$projectRoot\docs\universal-onboarding-validation.md" -Destination "$destination\universal-onboarding-validation.md"
$lexborLicense=Join-Path $BuildDir '_deps\lexbor-src\LICENSE'
if (-not (Test-Path -LiteralPath $lexborLicense)) { $lexborLicense=Join-Path $projectRoot 'build\_deps\lexbor-src\LICENSE' }
Copy-Item -LiteralPath $lexborLicense -Destination "$destination\Lexbor-LICENSE.txt"
Write-Output "Packaged: $destination\CampusPulse.exe"

Copy-Item -LiteralPath "$projectRoot\docs\calendar.md" -Destination "$destination\calendar.md"
Copy-Item -LiteralPath "$projectRoot\docs\new-schools-validation.md" -Destination "$destination\new-schools-validation.md"
$libicalRoot=Join-Path $BuildDir '_deps\libical-src'
if (-not (Test-Path -LiteralPath $libicalRoot)) { $libicalRoot=Join-Path $projectRoot 'build\_deps\libical-src' }
foreach ($licenseName in @('LICENSE','COPYING','LICENSE.MPL2.txt','LICENSE.LGPL21.txt')) {
    $licensePath=Join-Path $libicalRoot $licenseName
    if (Test-Path -LiteralPath $licensePath) { Copy-Item -LiteralPath $licensePath -Destination "$destination\libical-$licenseName.txt" }
}

Copy-Item -LiteralPath "$projectRoot\docs\stage4-validation.md" -Destination "$destination\stage4-validation.md"
Copy-Item -LiteralPath "$projectRoot\docs\source-access.md" -Destination "$destination\source-access.md"
Copy-Item -LiteralPath "$projectRoot\docs\school-resources.md" -Destination "$destination\school-resources.md"
Copy-Item -LiteralPath "$projectRoot\docs\school-resources-validation.md" -Destination "$destination\school-resources-validation.md"
Copy-Item -LiteralPath "$projectRoot\docs\library-access-validation.md" -Destination "$destination\library-access-validation.md"
Copy-Item -LiteralPath "$projectRoot\docs\ai-resource-contract.md" -Destination "$destination\ai-resource-contract.md"
New-Item -ItemType Directory -Path "$destination\schemas" -Force | Out-Null
Copy-Item -LiteralPath "$projectRoot\schemas\ai-resource-input.schema.json" -Destination "$destination\schemas"
Copy-Item -LiteralPath "$projectRoot\schemas\ai-resource-output.schema.json" -Destination "$destination\schemas"
