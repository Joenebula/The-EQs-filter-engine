; Inno Setup script for the ParaEQ Windows installer.
; Built by CI (see .github/workflows/windows-build.yml):
;   ISCC.exe /DAppVersion=1.0.0 /DVST3Dir=<folder holding ParaEQ.vst3> /DManual=<ParaEQ-Manual.html> /DOutDir=<output folder> installer\ParaEQ.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef VST3Dir
  #define VST3Dir "..\build\Source\ParaEQ_artefacts\Release\VST3"
#endif
#ifndef Manual
  #define Manual "..\docs\ParaEQ-Manual.html"
#endif
#ifndef OutDir
  #define OutDir "..\build\installer"
#endif

[Setup]
; Keep this AppId forever: it is how Windows recognises updates of ParaEQ.
AppId={{6F2B7C4E-3A8D-4E1B-9C55-2D7E0A9B1F34}
AppName=ParaEQ
AppVersion={#AppVersion}
AppVerName=ParaEQ {#AppVersion}
AppPublisher=Home Studio
DefaultDirName={autopf}\Home Studio\ParaEQ
DisableDirPage=yes
DefaultGroupName=ParaEQ
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutDir}
OutputBaseFilename=ParaEQ-Setup-Windows-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=ParaEQ {#AppVersion} (VST3)
CloseApplications=yes

[Messages]
WelcomeLabel2=This installs ParaEQ {#AppVersion}, a 24-band parametric EQ (VST3, 64-bit).%n%nThe plugin goes into the standard VST3 folder (C:\Program Files\Common Files\VST3) where Cubase finds it.%n%nPlease close Cubase before continuing.

[InstallDelete]
; Clear out any earlier ParaEQ first. A manual install may have left
; ParaEQ.vst3 as a single file (the plugin binary copied on its own) instead
; of the bundle folder, and Windows then refuses to create the folder
; (error 183). Removing it here, before files are copied, also drops files an
; older version had that this one no longer ships.
Type: files;          Name: "{commoncf64}\VST3\ParaEQ.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\ParaEQ.vst3"

[Files]
Source: "{#VST3Dir}\ParaEQ.vst3\*"; DestDir: "{commoncf64}\VST3\ParaEQ.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Manual}"; DestDir: "{app}"; DestName: "ParaEQ-Manual.html"; Flags: ignoreversion

[Icons]
Name: "{group}\ParaEQ manual"; Filename: "{app}\ParaEQ-Manual.html"
Name: "{group}\Uninstall ParaEQ"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\ParaEQ-Manual.html"; Description: "Open the ParaEQ manual"; Flags: postinstall shellexec skipifsilent unchecked

[UninstallDelete]
Type: dirifempty; Name: "{commoncf64}\VST3\ParaEQ.vst3"
