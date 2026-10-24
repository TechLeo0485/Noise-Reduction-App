; Inno Setup 6 script for VoiceStudio. Invoked by scripts\package.ps1:
;   ISCC.exe /DAppVersion=0.2.0 /DSourceDir=<install dir> /O<output dir> installer\VoiceStudio.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\install\ninja-release"
#endif

[Setup]
AppId={{6E0B8E2C-6C47-4D0B-9E7B-3E1F2B9C1A55}
AppName=VoiceStudio
AppVersion={#AppVersion}
AppPublisher=VoiceStudio
DefaultDirName={autopf}\VoiceStudio
DefaultGroupName=VoiceStudio
OutputBaseFilename=VoiceStudio-{#AppVersion}-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\VoiceStudio.exe

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\VoiceStudio"; Filename: "{app}\VoiceStudio.exe"
Name: "{group}\Uninstall VoiceStudio"; Filename: "{uninstallexe}"
Name: "{autodesktop}\VoiceStudio"; Filename: "{app}\VoiceStudio.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\VoiceStudio.exe"; Description: "Launch VoiceStudio"; Flags: nowait postinstall skipifsilent

[Code]
// VB-CABLE is not bundled (redistribution needs VB-Audio's permission); point users to it.
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and not WizardSilent then
    MsgBox('VoiceStudio sends processed audio to a virtual audio cable.' + #13#10 +
           'If you have not installed VB-CABLE yet, download it from https://vb-audio.com/Cable/ ' +
           'and reboot before using VoiceStudio with Discord, Zoom or Teams.', mbInformation, MB_OK);
end;

// Ask whether to delete settings, logs and crash dumps (%LOCALAPPDATA%\VoiceStudio).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    if MsgBox('Also delete VoiceStudio settings, logs and crash dumps?', mbConfirmation, MB_YESNO) = IDYES then
      DelTree(ExpandConstant('{localappdata}\VoiceStudio'), True, True, True);
end;
