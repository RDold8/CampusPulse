param([string]$QtRoot='D:\CampusPulseSDK\6.8.3\msvc2022_64')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$destination=Join-Path $projectRoot 'dist\CampusPulse'
if (-not (Test-Path -LiteralPath "$projectRoot\build\Release\CampusPulse.exe")) { throw 'Build the desktop app first' }
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-Item -LiteralPath "$projectRoot\build\Release\CampusPulse.exe" -Destination $destination
Copy-Item -LiteralPath "$projectRoot\configs" -Destination $destination -Recurse -Force
& "$QtRoot\bin\windeployqt.exe" --release --no-translations "$destination\CampusPulse.exe"
if ($LASTEXITCODE -ne 0) { throw 'Desktop packaging failed' }
Copy-Item -LiteralPath "$projectRoot\LICENSE" -Destination $destination
Copy-Item -LiteralPath "$projectRoot\THIRD_PARTY_NOTICES.md" -Destination $destination
Copy-Item -LiteralPath "$projectRoot\docs\desktop-prototype.md" -Destination "$destination\使用说明.md"
Copy-Item -LiteralPath "$projectRoot\docs\subscriptions.md" -Destination "$destination\subscriptions.md"
Copy-Item -LiteralPath "$projectRoot\docs\tasks.md" -Destination "$destination\tasks.md"
Copy-Item -LiteralPath "$projectRoot\docs\automatic-onboarding.md" -Destination "$destination\automatic-onboarding.md"
Copy-Item -LiteralPath "$projectRoot\docs\ai-supplement.md" -Destination "$destination\ai-supplement.md"
$lexborLicense=Join-Path $projectRoot 'build\_deps\lexbor-src\LICENSE'
Copy-Item -LiteralPath $lexborLicense -Destination "$destination\Lexbor-LICENSE.txt"
Write-Output "Packaged: $destination\CampusPulse.exe"

Copy-Item -LiteralPath "$projectRoot\docs\calendar.md" -Destination "$destination\calendar.md"
Copy-Item -LiteralPath "$projectRoot\docs\new-schools-validation.md" -Destination "$destination\new-schools-validation.md"
$libicalRoot=Join-Path $projectRoot 'build\_deps\libical-src'
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
