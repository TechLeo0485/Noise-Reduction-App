; Inno Setup 6 script for VoiceStudio. Invoked by scripts\package.ps1:
;   ISCC.exe /DAppVersion=1.0.0 /DSourceDir=<install dir> /DVCRedist=<vc_redist.x64.exe>
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
; VoiceStudio Virtual Microphone (driver\, built and test-signed by scripts\build-driver.ps1): offered when the
; signed package is part of the install tree. Test-signed drivers only load while Windows Test Mode is on, which
; setup turns on only after asking (and never while Secure Boot is on, where Windows does not allow it).
#if FileExists(AddBackslash(SourceDir) + "driver\vsvirtualmic.inf")
  #define VirtualMic
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
; One choice (radio buttons): how the voice reaches other apps.
#define MicGroup "How your voice reaches Discord, Zoom, Teams and games (choose one):"
#ifdef VBCableDir
Name: "vbcable"; Description: "{code:VBCableChoiceText}"; GroupDescription: "{#MicGroup}"; Flags: exclusive
#endif
#ifdef VirtualMic
Name: "virtualmic"; Description: "VoiceStudio Virtual Microphone - VoiceStudio's own free driver. Needs Secure Boot off and Windows Test Mode: setup guides you and asks before each change"; GroupDescription: "{#MicGroup}"; Flags: exclusive unchecked
#endif
Name: "nomic"; Description: "Neither - I use another virtual microphone, or set one up later"; GroupDescription: "{#MicGroup}"; Flags: exclusive unchecked
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
Source: "{#VBCableDir}\*"; DestDir: "{tmp}\vbcable"; Flags: recursesubdirs deleteafterinstall; Tasks: vbcable; Check: VBCableWasMissing
#endif

[Icons]
Name: "{group}\VoiceStudio"; Filename: "{app}\VoiceStudio.exe"
Name: "{group}\VoiceStudio User Guide"; Filename: "{app}\VoiceStudio.exe"; Parameters: "--guide"
Name: "{group}\Uninstall VoiceStudio"; Filename: "{uninstallexe}"
#ifdef VirtualMic
Name: "{group}\Finish VoiceStudio Virtual Microphone setup"; Filename: "{app}\driver\vs_vmic.exe"; Parameters: "finish"; Tasks: virtualmic
#endif
Name: "{autodesktop}\VoiceStudio"; Filename: "{app}\VoiceStudio.exe"; Tasks: desktopicon

[Run]
#ifdef VCRedist
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing the Microsoft Visual C++ runtime..."; Flags: waituntilterminated; Check: NeedsVCRedist
#endif
#ifdef VBCableDir
; -i installs, -h hides VB-CABLE's own dialogs. Windows still asks once to allow the VB-Audio driver.
Filename: "{tmp}\vbcable\VBCABLE_Setup_x64.exe"; Parameters: "-i -h"; WorkingDir: "{tmp}\vbcable"; StatusMsg: "Installing VB-CABLE by VB-Audio Software. If Windows asks whether to install this device software, click Install."; Flags: waituntilterminated; Tasks: vbcable; Check: VBCableWasMissing
Filename: "https://vb-audio.com/Cable/"; Description: "VB-CABLE is donationware by VB-Audio Software: open www.vb-cable.com to learn more or support its author"; Flags: shellexec postinstall skipifsilent nowait unchecked; Tasks: vbcable
#endif
#ifdef VirtualMic
; Trust the test certificate (machine Root + TrustedPublisher) so Windows installs the driver without a warning,
; then create the device. Removed again on uninstall.
Filename: "{sys}\certutil.exe"; Parameters: "-f -addstore Root ""{app}\driver\vsvirtualmic-test.cer"""; StatusMsg: "Installing the VoiceStudio Virtual Microphone..."; Flags: runhidden waituntilterminated; Tasks: virtualmic
Filename: "{sys}\certutil.exe"; Parameters: "-f -addstore TrustedPublisher ""{app}\driver\vsvirtualmic-test.cer"""; Flags: runhidden waituntilterminated; Tasks: virtualmic
Filename: "{app}\driver\vs_vmic.exe"; Parameters: "install ""{app}\driver\vsvirtualmic.inf"""; StatusMsg: "Installing the VoiceStudio Virtual Microphone..."; Flags: runhidden waituntilterminated; Tasks: virtualmic
; Guided finish: explains, asks, and if agreed restarts into the firmware settings (Secure Boot) and/or turns on
; Test Mode; resumes by itself after each restart. Changes nothing when the user says no.
Filename: "{app}\driver\vs_vmic.exe"; Parameters: "finish"; StatusMsg: "Checking what the virtual microphone needs..."; Flags: waituntilterminated skipifsilent; Tasks: virtualmic
#endif
Filename: "https://vb-audio.com/Cable/"; Description: "Open the VB-CABLE download page (required for Zoom, Teams, Discord)"; Flags: shellexec postinstall skipifsilent nowait; Check: not HasVBCable and VBCableSelected
Filename: "{app}\VoiceStudio.exe"; Description: "Launch VoiceStudio"; Flags: nowait postinstall skipifsilent runasoriginaluser
; Updates from inside the app run setup with /SILENT /RELAUNCH=1: start VoiceStudio again (as the user, not as
; administrator), where it then shows what is new.
Filename: "{app}\VoiceStudio.exe"; Flags: nowait runasoriginaluser; Check: RelaunchAfterUpdate

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

// The uninstall entry can be missing while the driver is installed and working; reinstalling it then removes the
// working cable until the next restart. So the driver service counts too.
function HasVBCable: Boolean;
begin
  Result := RegKeyExists(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\VB:VBCABLE {87459874-1236-4469}') or
            RegKeyExists(HKLM32, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\VB:VBCABLE {87459874-1236-4469}') or
            RegKeyExists(HKLM64, 'SYSTEM\CurrentControlSet\Services\VBAudioVACMME');
end;

// ---- VoiceStudio Virtual Microphone --------------------------------------------------------------------------
const
  VirtualMicCertName = 'VoiceStudio Virtual Microphone Test Signing';
  VirtualMicKey = 'Software\VoiceStudio\VirtualMicrophone';

var
  MissingVBCable: Boolean; // VB-CABLE was not installed when setup started

function InitializeSetup(): Boolean;
begin
  MissingVBCable := not HasVBCable;
  Result := True;
end;

function RelaunchAfterUpdate: Boolean;
begin
  Result := WizardSilent and (ExpandConstant('{param:RELAUNCH|0}') = '1');
end;

function VBCableWasMissing: Boolean;
begin
  Result := MissingVBCable;
end;

function VBCableChoiceText(Param: String): String;
begin
  if HasVBCable then
    Result := 'VB-CABLE (already installed on this PC) - works with Secure Boot on'
  else
    Result := 'VB-CABLE - free donationware by VB-Audio Software (www.vb-cable.com), installed now; works with Secure Boot on';
end;

function VBCableSelected: Boolean;
begin
#ifdef VBCableDir
  Result := WizardIsTaskSelected('vbcable');
#else
  Result := not WizardIsTaskSelected('nomic');
#endif
end;

function VirtualMicSelected: Boolean;
begin
#ifdef VirtualMic
  Result := WizardIsTaskSelected('virtualmic');
#else
  Result := False;
#endif
end;

// VB-CABLE's driver is only complete after a restart. (The virtual microphone's restarts are asked for by
// "vs_vmic finish" itself.)
function NeedRestart: Boolean;
begin
#ifdef VBCableDir
  Result := WizardIsTaskSelected('vbcable') and MissingVBCable and HasVBCable; // installed just now
#else
  Result := False;
#endif
end;

// ---- Closing the running app ----------------------------------------------------------------------------------
// Files in use cannot be replaced or deleted. "VoiceStudio.exe --quit" asks the running VoiceStudio (often hidden in
// the notification area, where Windows' "close applications" only hides it) to quit and save its settings, and waits
// for it. Versions without --quit, or a hung app, are then ended - only the copy in this folder, never other copies.
procedure CloseVoiceStudio(const AppDir: String);
var
  Code: Integer;
begin
  if not FileExists(AddBackslash(AppDir) + 'VoiceStudio.exe') then
    exit;
  Exec(AddBackslash(AppDir) + 'VoiceStudio.exe', '--quit', AppDir, SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
       '-NoProfile -NonInteractive -Command "Get-Process VoiceStudio -ErrorAction SilentlyContinue | ' +
       'Where-Object { $_.Path -eq ''' + AddBackslash(AppDir) + 'VoiceStudio.exe'' } | Stop-Process -Force"',
       '', SW_HIDE, ewWaitUntilTerminated, Code);
  Sleep(500); // let Windows release the files
end;

// Updating: close the installed copy before its files are replaced.
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  CloseVoiceStudio(ExpandConstant('{app}'));
  Result := '';
end;

// Uninstalling: close it first, or its files (and the folder) would stay behind.
function InitializeUninstall(): Boolean;
begin
  CloseVoiceStudio(ExpandConstant('{app}'));
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and not WizardSilent and not HasVBCable and VBCableSelected then
    MsgBox('VoiceStudio sends your cleaned voice to Zoom, Teams or Discord through the free VB-CABLE ' +
           'virtual microphone, which is not installed on this PC.' + #13#10#13#10 +
           'On the last page, leave "Open the VB-CABLE download page" ticked, install VB-CABLE ' +
           '(run VBCABLE_Setup_x64.exe as administrator) and restart Windows. Or run this setup again ' +
           'and keep "Install VB-CABLE" ticked.', mbInformation, MB_OK);
end;

// Ask whether to delete settings, logs, crash dumps and the saved voice sample (%LOCALAPPDATA%\VoiceStudio).
// VB-CABLE stays installed: other applications may use it (remove it under Settings > Apps).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Code: Integer;
  Flag: Cardinal;
begin
  // The app can also enable "Start with Windows" itself; never leave a dangling entry behind.
  if CurUninstallStep = usUninstall then
  begin
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'VoiceStudio');
    // The virtual microphone: a pending resume step, device, driver package and the trusted test certificate.
    RegDeleteValue(HKLM, 'Software\Microsoft\Windows\CurrentVersion\RunOnce', 'VoiceStudioVirtualMic');
    if FileExists(ExpandConstant('{app}\driver\vs_vmic.exe')) then
    begin
      Exec(ExpandConstant('{app}\driver\vs_vmic.exe'), 'uninstall', '', SW_HIDE, ewWaitUntilTerminated, Code);
      Exec(ExpandConstant('{sys}\certutil.exe'), '-delstore Root "' + VirtualMicCertName + '"', '', SW_HIDE, ewWaitUntilTerminated, Code);
      Exec(ExpandConstant('{sys}\certutil.exe'), '-delstore TrustedPublisher "' + VirtualMicCertName + '"', '', SW_HIDE, ewWaitUntilTerminated, Code);
    end;
    // Test Mode is only turned off if this setup turned it on, and only when the user agrees.
    if RegQueryDWordValue(HKLM, VirtualMicKey, 'TestModeEnabledBySetup', Flag) and (Flag = 1) then
    begin
      if SuppressibleMsgBox('VoiceStudio setup turned on Windows Test Mode for the virtual microphone. Turn it off ' +
                            'again? (Takes effect after a restart. Choose No if other test-signed drivers need it.)',
                            mbConfirmation, MB_YESNO, IDNO) = IDYES then
        Exec(ExpandConstant('{sys}\bcdedit.exe'), '/set testsigning off', '', SW_HIDE, ewWaitUntilTerminated, Code);
      RegDeleteKeyIncludingSubkeys(HKLM, VirtualMicKey);
    end;
  end;
  if CurUninstallStep = usPostUninstall then
  begin
    // Whatever is left in the program folder (files created after setup, e.g. Qt caches) goes too. Only a folder
    // named VoiceStudio is removed, so a custom install location like "C:\Tools" is never wiped.
    if DirExists(ExpandConstant('{app}')) and
       (CompareText(ExtractFileName(RemoveBackslashUnlessRoot(ExpandConstant('{app}'))), 'VoiceStudio') = 0) then
      DelTree(ExpandConstant('{app}'), True, True, True);
    // Silent uninstalls (/SUPPRESSMSGBOXES) keep the user's data: the default answer is No.
    if SuppressibleMsgBox('Also delete your VoiceStudio settings, logs and saved voice sample?', mbConfirmation, MB_YESNO,
                          IDNO) = IDYES then
      DelTree(ExpandConstant('{localappdata}\VoiceStudio'), True, True, True);
  end;
end;
