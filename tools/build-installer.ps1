<#
.SYNOPSIS
Build the CampusPulse per-user Windows x64 installer from a clean deployed package.
.EXAMPLE
./tools/build-installer.ps1 -PackageDir './dist/release-0.1.0/CampusPulse' -IsccPath 'C:\Program Files\Inno Setup 7\ISCC.exe'
.NOTES
Requires an installed Inno Setup compiler. This script does not install tools,
deploy Qt, download dependencies, launch CampusPulse, or run the installer.
#>
[CmdletBinding()]
param(
    [string]$PackageDir,
    [string]$OutputDir,
    [string]$Version,
    [string]$IsccPath,
    [string]$ChineseMessagesFile
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$installerScript = Join-Path $projectRoot 'installer\CampusPulse.iss'

if ([string]::IsNullOrWhiteSpace($Version)) {
    $cmakeContents = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
    $versionMatch = [regex]::Match($cmakeContents, 'project\(CampusPulse\s+VERSION\s+(\d+\.\d+\.\d+)\s')
    if (-not $versionMatch.Success) { throw 'Cannot read CampusPulse version from CMakeLists.txt.' }
    $Version = $versionMatch.Groups[1].Value
}
if ($Version -notmatch '^\d{1,5}\.\d{1,5}\.\d{1,5}$' -or
    @($Version.Split('.') | Where-Object { [long]$_ -gt 65535 }).Count -gt 0) {
    throw 'Version must be major.minor.patch with each component between 0 and 65535.'
}
if ([string]::IsNullOrWhiteSpace($PackageDir)) {
    $PackageDir = Join-Path $projectRoot "dist\release-$Version\CampusPulse"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $projectRoot 'dist\releases'
}
if (-not (Test-Path -LiteralPath $PackageDir -PathType Container)) {
    throw "Clean deployed package directory not found: $PackageDir"
}
$packagePath = (Resolve-Path -LiteralPath $PackageDir).ProviderPath.TrimEnd('\', '/')
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir).TrimEnd('\', '/')
if ($packagePath.Equals($projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'PackageDir must be the deployed application directory, not the project root.'
}
if ($outputPath.Equals($packagePath, [StringComparison]::OrdinalIgnoreCase) -or
    $outputPath.StartsWith($packagePath + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDir must be outside PackageDir to avoid including previous installers.'
}

$requiredFiles = @(
    'CampusPulse.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
    'Qt6Network.dll', 'Qt6Sql.dll', 'platforms\qwindows.dll',
    'sqldrivers\qsqlite.dll', 'configs\schools\neepu.example.json',
    'LICENSE', 'THIRD_PARTY_NOTICES.md', 'INSTALL-README.txt',
    'licenses\INSTALLATION-LICENSES.txt'
)
foreach ($relativePath in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $packagePath $relativePath) -PathType Leaf)) {
        throw "Package is incomplete; missing $relativePath"
    }
}
$licensePath = Join-Path $packagePath 'licenses'
if (-not (Test-Path -LiteralPath $licensePath -PathType Container) -or
    @(Get-ChildItem -LiteralPath $licensePath -File -Recurse -Force).Count -eq 0) {
    throw 'Package must include third-party distribution materials in a non-empty licenses directory.'
}
$packageEntries = @(Get-ChildItem -LiteralPath $packagePath -Recurse -Force)
foreach ($entry in $packageEntries) {
    $relativePath = $entry.FullName.Substring($packagePath.Length + 1)
    if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Package may not contain junctions or symbolic links: $relativePath"
    }
    if ($relativePath -match '^(?:\.git|\.venv|\.deps|evidence|build|tests)(?:\\|$)' -or
        (-not $entry.PSIsContainer -and
            ($entry.Name -match '(?:\.sqlite(?:-.*)?|\.db|\.log|\.pdb|\.user)$|^\.env(?:\..*)?$|_tests\.exe$|^Qt6.+d\.dll$' -or
             $entry.Name -eq 'Qt6Test.dll'))) {
        throw "Package contains runtime data or development material: $relativePath"
    }
}

if ([string]::IsNullOrWhiteSpace($IsccPath)) {
    $compilerCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($null -ne $compilerCommand) {
        $IsccPath = $compilerCommand.Source
    } else {
        $compilerCandidates = @(
            (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe'),
            (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
            (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe'),
            (Join-Path $projectRoot '.deps\release-tools\InnoSetup7\ISCC.exe')
        )
        $IsccPath = $compilerCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    }
}
if ([string]::IsNullOrWhiteSpace($IsccPath) -or
    -not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
    throw 'Inno Setup compiler not found. Install Inno Setup or pass -IsccPath with the path to ISCC.exe.'
}
$compilerPath = (Resolve-Path -LiteralPath $IsccPath).ProviderPath
if ([string]::IsNullOrWhiteSpace($ChineseMessagesFile)) {
    $ChineseMessagesFile = Join-Path (Split-Path -Parent $compilerPath) 'Languages\ChineseSimplified.isl'
}
if (-not (Test-Path -LiteralPath $ChineseMessagesFile -PathType Leaf)) {
    throw 'ChineseSimplified.isl was not found. Pass -ChineseMessagesFile with the matching Inno Setup language file.'
}
$chinesePath = (Resolve-Path -LiteralPath $ChineseMessagesFile).ProviderPath
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

# Each define is passed as one native argument; no expression evaluation or shell string is used.
$compilerArguments = @(
    "/DAppVersion=$Version",
    "/DProjectRoot=$projectRoot",
    "/DPackageDir=$packagePath",
    "/DReleaseOutputDir=$outputPath",
    "/DChineseMessagesFile=$chinesePath",
    $installerScript
)
& $compilerPath @compilerArguments
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed with exit code $LASTEXITCODE." }
$installerPath = Join-Path $outputPath "CampusPulse-$Version-windows-x64-setup.exe"
if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    throw "Compiler succeeded but the expected installer is missing: $installerPath"
}
$installer = Get-Item -LiteralPath $installerPath
if ($installer.Length -eq 0) { throw 'Compiler produced an empty installer.' }
$installerHash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "Installer: $installerPath"
Write-Output "Size: $($installer.Length) bytes"
Write-Output "SHA256: $installerHash"
