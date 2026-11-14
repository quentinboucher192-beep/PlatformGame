; Cosmic Platformer - Windows installer (Inno Setup 6)
;
; Build: installer\build_installer.ps1   (or: iscc /DStageDir=<staging folder> CosmicPlatformer.iss)
; The staging folder is produced by `cmake --install <build> --prefix <stage>` and contains
; CosmicPlatformer.exe, the SDL3 DLLs, assets\, data\, docs\ and licenses\.
; The executable is linked against the static MSVC runtime: no redistributable needed.

#define MyAppName "Cosmic Platformer"
#define MyAppVersion "2.4.0"
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
; Saves live in the user's profile; the shortcut to them and the uninstall question
; intentionally refer to the installing user's folder.
UsedUserAreasWarning=no

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
french.TaskDesktop=Créer un raccourci sur le Bureau
english.TaskDesktop=Create a desktop shortcut
french.GroupShortcuts=Raccourcis :
english.GroupShortcuts=Shortcuts:
french.TaskAssoc=Ouvrir les packages de niveaux .cplanet avec Cosmic Platformer
english.TaskAssoc=Open .cplanet level packages with Cosmic Platformer
french.GroupFiles=Fichiers :
english.GroupFiles=Files:
french.LevelEditor=Éditeur de niveaux
english.LevelEditor=Level editor
french.EnemyEditor=Éditeur d'ennemis
english.EnemyEditor=Enemy editor
french.Docs=Documentation
english.Docs=Documentation
french.SavesFolder=Sauvegardes et créations
english.SavesFolder=Saves and creations
french.PackageType=Niveau Cosmic Platformer
english.PackageType=Cosmic Platformer level
french.DeleteSaves=Voulez-vous aussi supprimer vos sauvegardes, records, niveaux créés et packages importés ?
english.DeleteSaves=Do you also want to delete your saves, records, created levels and imported packages?
french.KeepSavesHint=Choisissez « Non » pour les conserver.
english.KeepSavesHint=Choose "No" to keep them.

[Tasks]
Name: "desktopicon"; Description: "{cm:TaskDesktop}"; GroupDescription: "{cm:GroupShortcuts}"
Name: "association"; Description: "{cm:TaskAssoc}"; GroupDescription: "{cm:GroupFiles}"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:LevelEditor}"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--editor"
Name: "{group}\{cm:EnemyEditor}"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--enemy-editor"
Name: "{group}\{cm:Docs}"; Filename: "{app}\docs\README.md"
Name: "{group}\{cm:SavesFolder}"; Filename: "{userappdata}\CosmicStudio\CosmicPlatformer"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.cplanet"; ValueType: string; ValueName: ""; ValueData: "CosmicPlatformer.Package"; Flags: uninsdeletevalue; Tasks: association
Root: HKA; Subkey: "Software\Classes\CosmicPlatformer.Package"; ValueType: string; ValueName: ""; ValueData: "{cm:PackageType}"; Flags: uninsdeletekey; Tasks: association
Root: HKA; Subkey: "Software\Classes\CosmicPlatformer.Package\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: association
Root: HKA; Subkey: "Software\Classes\CosmicPlatformer.Package\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" --import ""%1"""; Tasks: association

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

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
      if MsgBox(CustomMessage('DeleteSaves') + #13#10 + '(' + SaveDir + ')' + #13#10#13#10 +
                CustomMessage('KeepSavesHint'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(SaveDir, True, True, True);
    end;
  end;
end;
