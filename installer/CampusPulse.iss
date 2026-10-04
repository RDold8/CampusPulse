; Compile with tools/build-installer.ps1. PackageDir must be a clean, deployed release.
; Keep this AppId stable across versions so Setup upgrades the same per-user application.
#ifndef AppVersion
  #define AppVersion "0.1.2"
#endif
#ifndef ProjectRoot
  #define ProjectRoot AddBackslash(SourcePath) + ".."
#endif
#ifndef PackageDir
  #define PackageDir AddBackslash(ProjectRoot) + "dist\CampusPulse"
#endif
#ifndef ReleaseOutputDir
  #define ReleaseOutputDir AddBackslash(ProjectRoot) + "dist\releases"
#endif
#ifndef ChineseMessagesFile
  #define ChineseMessagesFile AddBackslash(CompilerPath) + "Languages\ChineseSimplified.isl"
#endif

[Setup]
AppId={{2E9EA6BE-1F6A-4E9A-A782-B7DD99C51E83}
AppName=CampusPulse
AppVersion={#AppVersion}
AppVerName=CampusPulse {#AppVersion}
AppPublisher=CampusPulse contributors
AppPublisherURL=https://github.com/RDold8/CampusPulse
AppSupportURL=https://github.com/RDold8/CampusPulse/issues
AppUpdatesURL=https://github.com/RDold8/CampusPulse/releases
DefaultDirName={localappdata}\Programs\CampusPulse
DefaultGroupName=CampusPulse
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.17763
OutputDir={#ReleaseOutputDir}
OutputBaseFilename=CampusPulse-{#AppVersion}-windows-x64-setup
SetupIconFile={#ProjectRoot}\assets\campuspulse.ico
UninstallDisplayIcon={app}\CampusPulse.exe
LicenseFile={#PackageDir}\licenses\INSTALLATION-LICENSES.txt
InfoAfterFile={#PackageDir}\INSTALL-README.txt
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes
CloseApplications=no
RestartApplications=no
Uninstallable=yes
UninstallDisplayName=CampusPulse {#AppVersion}
VersionInfoVersion={#AppVersion}
VersionInfoDescription=CampusPulse Windows x64 installer
VersionInfoCopyright=CampusPulse contributors

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "{#ChineseMessagesFile}"

[CustomMessages]
english.CreateDesktopIcon=Create a desktop shortcut
english.AdditionalIcons=Shortcuts:
chinesesimp.CreateDesktopIcon=创建桌面快捷方式
chinesesimp.AdditionalIcons=快捷方式：

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\CampusPulse"; Filename: "{app}\CampusPulse.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\CampusPulse"; Filename: "{app}\CampusPulse.exe"; WorkingDir: "{app}"; Tasks: desktopicon

; There is intentionally no [Run] section: installing never starts the app.
; There is no [UninstallDelete] or registry cleanup of application settings.
; The app stores SQLite and discovered-school data in AppLocalDataLocation,
; outside {app}; uninstall removes deployed files and shortcuts only.
