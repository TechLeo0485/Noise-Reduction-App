; Inno Setup 6 script for VoiceStudio. Invoked by scripts\package.ps1:
;   ISCC.exe /DAppVersion=0.3.0 /DSourceDir=<install dir> /DVCRedist=<vc_redist.x64.exe>
;            /DVBCableDir=<extracted VB-CABLE driver pack> /O<output dir> installer\VoiceStudio.iss
;
; Produces one self-contained setup .exe: the app, Qt, ONNX Runtime, the VoxLock models, the Microsoft
; Visual C++ runtime (installed only if the PC lacks it) and VB-CABLE (installed only if missing).
;
; VB-CABLE is donationware by VB-Audio Software. VB-Audio allows embedding it with a silent installation
; when the user can see and identify it as a VB-Audio product and is in a position to donate
; (https://vb-audio.com/Services/licensing.htm). Setup therefore names it in its own task with the
; www.vb-cable.com credit and offers a link to the author on the last page. Professional / company
; deployments where users cannot donate need licenses from VB-Audio's webshop.

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
AppVerName=VoiceStudio {#AppVersion}
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
; Administrator install: the Visual C++ runtime goes to System32 and VB-CABLE is a driver.
PrivilegesRequired=admin
UninstallDisplayIcon={app}\VoiceStudio.exe
SetupIconFile=..\ui\icons\voicestudio.ico
; Updating while VoiceStudio runs would fail on locked files.
CloseApplications=yes
RestartApplications=no

[Tasks]
#ifdef VBCableDir
Name: "vbcable"; Description: "Install VB-CABLE, the virtual microphone that sends your voice to Zoom, Teams and Discord (required). Free donationware by VB-Audio Software, www.vb-cable.com"; Check: not HasVBCable
#endif
Name: "autostart"; Description: "Start VoiceStudio with Windows (runs in the notification area)"
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Registry]
; Same entry as Settings > "Start with Windows" in the app.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "VoiceStudio"; ValueData: """{app}\VoiceStudio.exe"" --minimized"; Flags: uninsdeletevalue; Tasks: autostart

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion
#ifdef VCRedist
Source: "{#VCRedist}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: deleteafterinstall; Check: NeedsVCRedist
#endif
#ifdef VBCableDir
; The setup program must run from an extracted folder (VB-CABLE reference manual).
Source: "{#VBCableDir}\*"; DestDir: "{tmp}\vbcable"; Flags: recursesubdirs deleteafterinstall; Tasks: vbcable
#endif

[Icons]
Name: "{group}\VoiceStudio"; Filename: "{app}\VoiceStudio.exe"
Name: "{group}\VoiceStudio User Guide"; Filename: "{app}\VoiceStudio.exe"; Parameters: "--guide"
Name: "{group}\Uninstall VoiceStudio"; Filename: "{uninstallexe}"
Name: "{autodesktop}\VoiceStudio"; Filename: "{app}\VoiceStudio.exe"; Tasks: desktopicon

[Run]
#ifdef VCRedist
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Microsoft Visual C++ runtime..."; Flags: waituntilterminated; Check: NeedsVCRedist
#endif
#ifdef VBCableDir
; -i installs, -h hides VB-CABLE's own dialogs. Windows still asks once to allow the VB-Audio driver.
Filename: "{tmp}\vbcable\VBCABLE_Setup_x64.exe"; Parameters: "-i -h"; WorkingDir: "{tmp}\vbcable"; StatusMsg: "Installing VB-CABLE by VB-Audio Software. If Windows asks whether to install this device software, click Install."; Flags: waituntilterminated; Tasks: vbcable
Filename: "https://vb-audio.com/Cable/"; Description: "VB-CABLE is donationware by VB-Audio Software: open www.vb-cable.com to learn more or support its author"; Flags: shellexec postinstall skipifsilent nowait unchecked; Tasks: vbcable
#endif
Filename: "https://vb-audio.com/Cable/"; Description: "Open the VB-CABLE download page (required for Zoom, Teams, Discord)"; Flags: shellexec postinstall skipifsilent nowait; Check: not HasVBCable
Filename: "{app}\VoiceStudio.exe"; Description: "Launch VoiceStudio"; Flags: nowait postinstall skipifsilent

[Code]
// The app and ONNX Runtime are built with MSVC 14.44: the system runtime must be at least that new.
function RuntimeFileOk(const Name: String): Boolean;
var
  Version: String;
begin
  Result := GetVersionNumbersString(ExpandConstant('{sys}\') + Name, Version) and
            (CompareStr(Copy(Version, 1, 5), '14.44') >= 0) and (Copy(Version, 1, 3) = '14.');
end;

function NeedsVCRedist: Boolean;
begin
  Result := not (RuntimeFileOk('msvcp140.dll') and RuntimeFileOk('msvcp140_1.dll') and
                 RuntimeFileOk('vcruntime140.dll') and RuntimeFileOk('vcruntime140_1.dll'));
end;

function HasVBCable: Boolean;
begin
  Result := RegKeyExists(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\VB:VBCABLE {87459874-1236-4469}') or
            RegKeyExists(HKLM32, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\VB:VBCABLE {87459874-1236-4469}');
end;

// VB-CABLE's driver is only complete after a restart.
function NeedRestart: Boolean;
begin
#ifdef VBCableDir
  Result := WizardIsTaskSelected('vbcable') and HasVBCable;
#else
  Result := False;
#endif
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and not WizardSilent and not HasVBCable then
    MsgBox('VoiceStudio sends your cleaned voice to Zoom, Teams or Discord through the free VB-CABLE ' +
           'virtual microphone, which is not installed on this PC.' + #13#10#13#10 +
           'On the last page, leave "Open the VB-CABLE download page" ticked, install VB-CABLE ' +
           '(run VBCABLE_Setup_x64.exe as administrator) and restart Windows. Or run this setup again ' +
           'and keep "Install VB-CABLE" ticked.', mbInformation, MB_OK);
end;

// Ask whether to delete settings, logs, crash dumps and the saved voice sample (%LOCALAPPDATA%\VoiceStudio).
// VB-CABLE stays installed: other applications may use it (remove it under Settings > Apps).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // The app can also enable "Start with Windows" itself; never leave a dangling entry behind.
  if CurUninstallStep = usUninstall then
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'VoiceStudio');
  if CurUninstallStep = usPostUninstall then
    // Silent uninstalls (/SUPPRESSMSGBOXES) keep the user's data: the default answer is No.
    if SuppressibleMsgBox('Also delete your VoiceStudio settings, logs and saved voice sample?', mbConfirmation, MB_YESNO,
                          IDNO) = IDYES then
      DelTree(ExpandConstant('{localappdata}\VoiceStudio'), True, True, True);
end;
