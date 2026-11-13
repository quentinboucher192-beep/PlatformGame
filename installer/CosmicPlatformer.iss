; Cosmic Platformer - Windows installer (Inno Setup 6)
;
; Build: installer\build_installer.ps1   (or: iscc /DStageDir=<staging folder> CosmicPlatformer.iss)
; The staging folder is produced by `cmake --install <build> --prefix <stage>` and contains
; CosmicPlatformer.exe, the SDL3 DLLs, assets\, data\, docs\ and licenses\.
; The executable is linked against the static MSVC runtime: no redistributable needed.

#define MyAppName "Cosmic Platformer"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Cosmic Studio"
#define MyAppExeName "CosmicPlatformer.exe"
#ifndef StageDir
  #define StageDir "..\build\stage"
#endif

[Setup]
AppId={{8F3C2E71-5B1D-4C1A-9E3A-2C9D7B1F4E10}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=CosmicPlatformer-Setup-{#MyAppVersion}
SetupIconFile=app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes
LicenseFile=license_fr.txt
MinVersion=10.0

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Créer un raccourci sur le Bureau"; GroupDescription: "Raccourcis :"
Name: "association"; Description: "Ouvrir les packages de niveaux .cplanet avec Cosmic Platformer"; GroupDescription: "Fichiers :"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Éditeur de niveaux"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--editor"
Name: "{group}\Éditeur d'ennemis"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--enemy-editor"
Name: "{group}\Documentation"; Filename: "{app}\docs\README.md"
Name: "{group}\Sauvegardes et créations"; Filename: "{userappdata}\CosmicStudio\CosmicPlatformer"
Name: "{group}\Désinstaller {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.cplanet"; ValueType: string; ValueName: ""; ValueData: "CosmicPlatformer.Package"; Flags: uninsdeletevalue; Tasks: association
Root: HKA; Subkey: "Software\Classes\CosmicPlatformer.Package"; ValueType: string; ValueName: ""; ValueData: "Niveau Cosmic Platformer"; Flags: uninsdeletekey; Tasks: association
Root: HKA; Subkey: "Software\Classes\CosmicPlatformer.Package\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: association
Root: HKA; Subkey: "Software\Classes\CosmicPlatformer.Package\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" --import ""%1"""; Tasks: association

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Lancer {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Only files the game may create inside its own folder (none by design). Saves live in
; %APPDATA%\CosmicStudio\CosmicPlatformer and are preserved unless the user asks.
Type: filesandordirs; Name: "{app}\user"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  SaveDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    SaveDir := ExpandConstant('{userappdata}\CosmicStudio\CosmicPlatformer');
    if DirExists(SaveDir) and not UninstallSilent then
    begin
      if MsgBox('Voulez-vous aussi supprimer vos sauvegardes, records, niveaux créés et packages importés ?' + #13#10 +
                '(' + SaveDir + ')' + #13#10#13#10 + 'Choisissez « Non » pour les conserver.',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(SaveDir, True, True, True);
    end;
  end;
end;
