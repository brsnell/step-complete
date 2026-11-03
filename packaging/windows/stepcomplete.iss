; Inno Setup 6 script. Compile with:
;   ISCC /DAppVersion=1.0.0 /DSourceDir=C:\path\to\stage\StepComplete /DOutputDir=C:\out stepcomplete.iss
; (packaging/windows/build-package.ps1 does this.)

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z
#endif
#ifndef SourceDir
  #error Pass /DSourceDir=<deployed application folder>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define AppName "STEP Complete"
#define AppExe "StepComplete.exe"

[Setup]
; Fixed GUID: identifies the app for upgrades/uninstall. Never change it.
AppId={{6C1B7E5A-3D52-4B0E-9C8F-5E0A7D2F41B9}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=STEP Complete contributors
AppPublisherURL=https://github.com/brsnell/step-complete
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Per-user install by default (no admin prompt); the user may choose "all users" in the dialog.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#SourceDir}\licenses\LICENSE
SetupIconFile={#SourcePath}\stepcomplete.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename=StepComplete-{#AppVersion}-windows-x64-setup
Compression=lzma2/ultra
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
MinVersion=10.0

[Tasks]
; Open-with registration is always done; this only makes us the default where nothing else is.
Name: "assoc"; Description: "Use {#AppName} as the default program for .step, .stp, .p21 and .stpz files (does not replace an existing default)"; GroupDescription: "File associations:"; Flags: unchecked
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; ProgIDs
Root: HKA; Subkey: "Software\Classes\StepComplete.step"; ValueType: string; ValueName: ""; ValueData: "STEP CAD model"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\StepComplete.step\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\StepComplete.step\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\StepComplete.stpz"; ValueType: string; ValueName: ""; ValueData: "Compressed STEP CAD model"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\StepComplete.stpz\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\StepComplete.stpz\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""

; "Open with" list entry
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".step"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".stp"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".p21"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".stpz"; ValueData: ""

; Offer our ProgID for each extension without changing the current default
Root: HKA; Subkey: "Software\Classes\.step\OpenWithProgids"; ValueType: string; ValueName: "StepComplete.step"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.stp\OpenWithProgids"; ValueType: string; ValueName: "StepComplete.step"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.p21\OpenWithProgids"; ValueType: string; ValueName: "StepComplete.step"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.stpz\OpenWithProgids"; ValueType: string; ValueName: "StepComplete.stpz"; ValueData: ""; Flags: uninsdeletevalue

; Optional default handler: only where no default exists yet
Root: HKA; Subkey: "Software\Classes\.step"; ValueType: string; ValueName: ""; ValueData: "StepComplete.step"; Flags: createvalueifdoesntexist uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.stp"; ValueType: string; ValueName: ""; ValueData: "StepComplete.step"; Flags: createvalueifdoesntexist uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.p21"; ValueType: string; ValueName: ""; ValueData: "StepComplete.step"; Flags: createvalueifdoesntexist uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.stpz"; ValueType: string; ValueName: ""; ValueData: "StepComplete.stpz"; Flags: createvalueifdoesntexist uninsdeletevalue; Tasks: assoc

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
