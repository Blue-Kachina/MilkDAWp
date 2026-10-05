; packaging/windows/MilkDAWp.iss -- the Windows installer (6.2), Inno Setup 6.
;
; Built by scripts/release/package.sh from the staged release, never by hand:
;
;   ISCC /DVersion=2.0.0-beta.1 /DNumericVersion=2.0.0 /DStage=<staged release>
;        /DContent=<build>/content /DRedist=<vc_redist.x64.exe> /DVCMinor=44
;        /DOutputDir=<dist> /DOutputBase=MilkDAWp-2.0.0-beta.1-windows-x64-setup MilkDAWp.iss
;
; Installs, for all users (needs admin: Common Files and ProgramData are shared):
;   the VST3      -> C:\Program Files\Common Files\VST3\MilkDAWp.vst3
;   the app       -> C:\Program Files\MilkDAWp\ (MilkDAWp.exe, projectM-4.dll, licences)
;   the presets   -> C:\ProgramData\MilkDAWp\{Presets,Textures} (engine::BundledContent
;                    looks there, so the app and every plugin instance find them)
;   the Microsoft Visual C++ 2015-2022 Redistributable (x64) when it is missing or
;   older than the toolset that built MilkDAWp (3.10: everything links the dynamic CRT)
; plus a Start menu shortcut, an optional desktop shortcut, an optional .milk file
; association (§4.6), and an uninstaller. Settings, ratings and crash reports in
; %APPDATA%\MilkDAWp are the user's and stay on uninstall.
;
; Unsigned until SignPath accepts the project (6.10); then the release workflow
; signs this installer and the binaries in it.

#ifndef Version
  #error Pass /DVersion=<label>, e.g. 2.0.0-beta.1
#endif
#ifndef NumericVersion
  #error Pass /DNumericVersion=<major.minor.patch>
#endif
#ifndef Stage
  #error Pass /DStage=<staged release folder>
#endif
#ifndef Content
  #error Pass /DContent=<bundled content folder>
#endif
#ifndef Redist
  #error Pass /DRedist=<vc_redist.x64.exe>
#endif
#ifndef VCMinor
  #define VCMinor "40"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputBase
  #define OutputBase "MilkDAWp-" + Version + "-windows-x64-setup"
#endif

[Setup]
; Never change: it is how an upgrade finds the installed copy.
AppId={{D0DE081F-BDE1-4B7F-AE27-A4F1C4DA92FF}
AppName=MilkDAWp
AppVersion={#Version}
AppVerName=MilkDAWp {#Version}
AppPublisher=Otitis Media
AppPublisherURL=https://github.com/Blue-Kachina/MilkDAWp2
AppSupportURL=https://github.com/Blue-Kachina/MilkDAWp2/issues
AppUpdatesURL=https://github.com/Blue-Kachina/MilkDAWp2/releases
VersionInfoVersion={#NumericVersion}.0
VersionInfoProductVersion={#NumericVersion}.0
VersionInfoProductTextVersion={#Version}
DefaultDirName={autopf}\MilkDAWp
DefaultGroupName=MilkDAWp
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; D9: Windows 10 21H2+.
MinVersion=10.0.19044
LicenseFile={#Stage}\LICENSE
SetupIconFile={#Stage}\MilkDAWp.ico
UninstallDisplayIcon={app}\MilkDAWp.exe
UninstallDisplayName=MilkDAWp
WizardStyle=modern
Compression=lzma2/ultra64
SolidCompression=yes
LZMANumBlockThreads=4
ChangesAssociations=yes
CloseApplications=yes
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBase}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "Open .milk preset files with MilkDAWp"; GroupDescription: "File types:"

[Components]
Name: "app"; Description: "MilkDAWp app"; Types: full compact custom
Name: "vst3"; Description: "VST3 plugin (Common Files\VST3)"; Types: full compact custom
Name: "presets"; Description: "Cream of the Crop presets (~9,800) and textures"; Types: full custom

[Types]
Name: "full"; Description: "Everything"
Name: "compact"; Description: "App and plugin, without the presets"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Files]
; The app folder: MilkDAWp.exe and projectM-4.dll side by side.
Source: "{#Stage}\MilkDAWp\*"; DestDir: "{app}"; Components: app; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\LICENSE"; DestDir: "{app}\licenses"; DestName: "LICENSE.txt"; Components: app vst3
Source: "{#Stage}\LICENSES\*"; DestDir: "{app}\licenses"; Components: app vst3; Flags: recursesubdirs
Source: "{#Stage}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}\licenses"; Components: app vst3
Source: "{#Stage}\README.txt"; DestDir: "{app}"; Components: app vst3; Flags: isreadme

; The VST3 bundle, projectM inside its binary folder.
Source: "{#Stage}\MilkDAWp.vst3\*"; DestDir: "{commoncf64}\VST3\MilkDAWp.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

; The bundled content (6.1), where engine::BundledContent looks first among the
; shared locations.
Source: "{#Content}\Presets\*"; DestDir: "{commonappdata}\MilkDAWp\Presets"; Components: presets; Excludes: ".milkdawp-*"; Flags: recursesubdirs createallsubdirs
Source: "{#Content}\Textures\*"; DestDir: "{commonappdata}\MilkDAWp\Textures"; Components: presets; Excludes: ".milkdawp-*"; Flags: recursesubdirs createallsubdirs

; Run from {tmp} only when needed (see VCRedistNeeded).
Source: "{#Redist}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: deleteafterinstall; Check: VCRedistNeeded

[Icons]
Name: "{autoprograms}\MilkDAWp"; Filename: "{app}\MilkDAWp.exe"; Components: app
Name: "{autodesktop}\MilkDAWp"; Filename: "{app}\MilkDAWp.exe"; Components: app; Tasks: desktopicon

[Registry]
; §4.6: double-clicking a .milk file opens it in the app (Main.cpp takes the path).
Root: HKA; Subkey: "Software\Classes\.milk"; ValueType: string; ValueName: ""; ValueData: "MilkDAWp.Preset"; Flags: uninsdeletevalue; Tasks: associate; Components: app
Root: HKA; Subkey: "Software\Classes\MilkDAWp.Preset"; ValueType: string; ValueName: ""; ValueData: "MilkDrop preset"; Flags: uninsdeletekey; Tasks: associate; Components: app
Root: HKA; Subkey: "Software\Classes\MilkDAWp.Preset\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\MilkDAWp.exe"",0"; Tasks: associate; Components: app
Root: HKA; Subkey: "Software\Classes\MilkDAWp.Preset\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\MilkDAWp.exe"" ""%1"""; Tasks: associate; Components: app

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Microsoft Visual C++ Redistributable..."; Flags: waituntilterminated; Check: VCRedistNeeded
Filename: "{app}\MilkDAWp.exe"; Description: "{cm:LaunchProgram,MilkDAWp}"; Components: app; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Folders the installer created, if they are empty after its files are gone.
Type: dirifempty; Name: "{commonappdata}\MilkDAWp"

[Code]
// True when the VC++ 2015-2022 x64 runtime is missing or older than the
// toolset that built MilkDAWp (14.<VCMinor>). The redistributable itself also
// refuses to downgrade, so a false "needed" only costs a few seconds.
function VCRedistNeeded: Boolean;
var
  Installed, Major, Minor: Cardinal;
  Key: String;
begin
  Key := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  Result := True;
  if RegQueryDWordValue(HKLM64, Key, 'Installed', Installed) and (Installed = 1) and
     RegQueryDWordValue(HKLM64, Key, 'Major', Major) and RegQueryDWordValue(HKLM64, Key, 'Minor', Minor) then
    Result := (Major < 14) or ((Major = 14) and (Minor < {#VCMinor}));
end;
