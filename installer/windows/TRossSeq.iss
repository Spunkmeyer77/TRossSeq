; Inno Setup script for the Windows installer.
; Build:  ISCC /DAppVersion=0.1.0 installer\windows\TRossSeq.iss
; (paths are relative to this file; expects a Release build in ..\..\build)

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#define Artefacts "..\..\build\TRossSeq_artefacts\Release"

[Setup]
AppId={{5E9B2C71-3A4D-4F18-B6C2-7D1E0A9F4C36}
AppName=TRoss Seq
AppVersion={#AppVersion}
AppVerName=TRoss Seq {#AppVersion}
AppPublisher=Ticco Ross
DefaultDirName={autopf}\Ticco Ross\TRoss Seq
DefaultGroupName=Ticco Ross
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename=TRossSeq-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=TRoss Seq

[Types]
Name: "full";   Description: "VST3 plug-in and standalone app"
Name: "custom"; Description: "Choose components"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Ableton Live, Bitwig, Reaper, Cubase...)"; Types: full custom
Name: "app";  Description: "Standalone app";                                         Types: full custom

[Files]
Source: "{#Artefacts}\VST3\TRoss Seq.vst3\*"; DestDir: "{commoncf64}\VST3\TRoss Seq.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\TRoss Seq.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\TRoss Seq"; Filename: "{app}\TRoss Seq.exe"; Components: app

[Messages]
FinishedLabel=TRoss Seq is installed.%n%nRescan plug-ins in your DAW (Ableton Live: Settings > Plug-Ins > Rescan).
