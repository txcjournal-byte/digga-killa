; TrapWin Sampler - Windows installer (Inno Setup 6)
; Built by CI:  iscc /DAppVersion=1.0.0 /DSourceDir=<out> installer\TrapWinSampler.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\out"
#endif

[Setup]
AppId={{3C0E5B7A-9D41-4F2E-A6B8-71C2D9E4F5A3}
AppName=TrapWin Sampler
AppVersion={#AppVersion}
AppVerName=TrapWin Sampler {#AppVersion}
AppPublisher=TrapWin
AppPublisherURL=https://trapwin.com
DefaultDirName={autopf}\TrapWin\TrapWin Sampler
DefaultGroupName=TrapWin
DisableProgramGroupPage=yes
LicenseFile=..\EULA.txt
OutputDir=..\installer-out
OutputBaseFilename=TrapWinSampler-{#AppVersion}-Windows-Setup
SetupIconFile=..\installer\app.ico
UninstallDisplayIcon={app}\TrapWin Sampler.exe
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=TrapWin Sampler

[Types]
Name: "full"; Description: "VST3 + Standalone"
Name: "vst3"; Description: "VST3 only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (C:\Program Files\Common Files\VST3)"; Types: full vst3 custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#SourceDir}\TrapWin Sampler.vst3\*"; DestDir: "{commoncf64}\VST3\TrapWin Sampler.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\TrapWin Sampler.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
; stem separation model, shared by the VST3 and the standalone app
Source: "{#SourceDir}\spleeter4.bin"; DestDir: "{commonappdata}\TrapWin\TrapWin Sampler"; Flags: ignoreversion
Source: "..\EULA.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD-PARTY-LICENSES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\packaging\INSTALL - Windows.txt"; DestDir: "{app}"; DestName: "Read me.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\TrapWin Sampler"; Filename: "{app}\TrapWin Sampler.exe"; Components: standalone
Name: "{autodesktop}\TrapWin Sampler"; Filename: "{app}\TrapWin Sampler.exe"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for the standalone app"; Components: standalone; Flags: unchecked

[Run]
Filename: "{app}\TrapWin Sampler.exe"; Description: "Start TrapWin Sampler (standalone)"; Components: standalone; Flags: postinstall nowait skipifsilent unchecked
