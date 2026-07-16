#define MyAppName "MCDeploy"
#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#define MyAppPublisher "MCDeploy"
#define MyAppExeName "mcdeploy.exe"
#define MyAppId "{{2D3825DD-B7D6-4A25-AAB7-CE422D32B36E}"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=MCDeploy Windows Installer
VersionInfoProductName={#MyAppName}
DefaultDirName={autopf}\MCDeploy
DefaultGroupName=MCDeploy
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\MCDeploy\resources\mcdeploy.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir=output
OutputBaseFilename=MCDeploy-{#MyAppVersion}-x64-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UsePreviousTasks=yes
ChangesEnvironment=no
MinVersion=10.0.17763

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startmenu"; Description: "Add MCDeploy to the Start Menu"; GroupDescription: "Shortcuts:"
Name: "desktop"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "datafolder"; Description: "Add a Start Menu shortcut to the MCDeploy data folder"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "startup"; Description: "Start MCDeploy when I sign in to Windows"; GroupDescription: "Windows integration:"; Flags: unchecked
Name: "lanaccess"; Description: "Allow the MCDeploy dashboard on my private network"; GroupDescription: "Windows integration:"; Flags: unchecked
[Files]
Source: "stage\app\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "stage\prerequisites\vc_redist.x64.exe"; Flags: dontcopy
Source: "stage\prerequisites\MicrosoftEdgeWebView2Setup.exe"; Flags: dontcopy

[Dirs]
Name: "{localappdata}\MCDeploy"
Name: "{localappdata}\MCDeploy\mcdeploy_webview_data"
Name: "{localappdata}\MCDeploy\uploads"
Name: "{%USERPROFILE}\MCDeploy"
Name: "{%USERPROFILE}\MCDeploy\Servers"
Name: "{%USERPROFILE}\MCDeploy\Backups"

[Icons]
Name: "{group}\MCDeploy"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{localappdata}\MCDeploy"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: startmenu
Name: "{group}\MCDeploy Data"; Filename: "{%USERPROFILE}\MCDeploy"; Tasks: startmenu and datafolder
Name: "{autodesktop}\MCDeploy"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{localappdata}\MCDeploy"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktop
Name: "{userstartup}\MCDeploy"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{localappdata}\MCDeploy"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: startup

[Run]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""MCDeploy Dashboard"""; Flags: runhidden waituntilterminated; Tasks: lanaccess
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""MCDeploy Dashboard"" dir=in action=allow program=""{app}\{#MyAppExeName}"" enable=yes profile=private"; Flags: runhidden waituntilterminated; Tasks: lanaccess
Filename: "{app}\{#MyAppExeName}"; Description: "Launch MCDeploy"; WorkingDir: "{localappdata}\MCDeploy"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""MCDeploy Dashboard"""; Flags: runhidden waituntilterminated; RunOnceId: "RemoveMCDeployFirewallRule"

[Code]
function JsonEscape(Value: String): String;
begin
  StringChangeEx(Value, '\', '\\', True);
  StringChangeEx(Value, '"', '\"', True);
  Result := Value;
end;

function NewSecret(): String;
var
  Seed: String;
begin
  Seed := GetDateTimeString('yyyy-mm-dd hh:nn:ss.zzz', '-', ':') + '|' +
    GetComputerNameString + '|' + GetUserNameString + '|' +
    IntToStr(Random(2147483647)) + '|' + IntToStr(Random(2147483647));
  Result := GetSHA256OfString(Seed);
end;

function ValidPrerequisiteExitCode(Code: Integer): Boolean;
begin
  Result := (Code = 0) or (Code = 1638) or (Code = 3010) or (Code = 1641);
end;
function RunPrerequisite(FileName, Parameters, DisplayName: String; var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  ExtractTemporaryFile(FileName);
  WizardForm.StatusLabel.Caption := 'Installing ' + DisplayName + '...';
  if not Exec(ExpandConstant('{tmp}\' + FileName), Parameters, '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode) then
  begin
    Result := 'Setup could not start the ' + DisplayName + ' installer.';
    Exit;
  end;
  if not ValidPrerequisiteExitCode(ResultCode) then
  begin
    Result := DisplayName + ' installation failed with exit code ' + IntToStr(ResultCode) + '.';
    Exit;
  end;
  if (ResultCode = 3010) or (ResultCode = 1641) then
    NeedsRestart := True;
  Result := '';
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := RunPrerequisite('vc_redist.x64.exe', '/install /quiet /norestart',
    'Microsoft Visual C++ Runtime', NeedsRestart);
  if Result <> '' then Exit;
  Result := RunPrerequisite('MicrosoftEdgeWebView2Setup.exe', '/silent /install',
    'Microsoft Edge WebView2 Runtime', NeedsRestart);
end;

procedure WriteInitialConfig();
var
  ConfigPath, ServersPath, BackupsPath, Host, Config: String;
begin
  ConfigPath := ExpandConstant('{localappdata}\MCDeploy\config.json');
  if FileExists(ConfigPath) then Exit;

  ServersPath := ExpandConstant('{%USERPROFILE}\MCDeploy\Servers');
  BackupsPath := ExpandConstant('{%USERPROFILE}\MCDeploy\Backups');
  if WizardIsTaskSelected('lanaccess') then Host := '0.0.0.0'
  else Host := '127.0.0.1';

  Config := '{' + #13#10 +
    '  "mcdeploy": {' + #13#10 +
    '    "host": "' + Host + '",' + #13#10 +
    '    "port": 8082,' + #13#10 +
    '    "ssl": {"enabled": false, "cert_path": "", "key_path": ""},' + #13#10 +
    '    "database": {"path": "mcdeploy.db"},' + #13#10 +
    '    "servers_dir": "' + JsonEscape(ServersPath) + '",' + #13#10 +
    '    "backups_dir": "' + JsonEscape(BackupsPath) + '",' + #13#10 +
    '    "jwt_secret": "' + NewSecret() + '",' + #13#10 +
    '    "jwt_expiry_hours": 24,' + #13#10 +
    '    "public_url": "https://mcdeploy.online",' + #13#10 +
    '    "webpanel_url": "https://mcdeploy.online",' + #13#10 +
    '    "email": {"resend_api_key": "", "from": "MCDeploy <info@mcdeploy.online>"},' + #13#10 +
    '    "oauth": {' + #13#10 +
    '      "google": {"client_id": "", "client_secret": ""},' + #13#10 +
    '      "github": {"client_id": "", "client_secret": ""},' + #13#10 +
    '      "discord": {"client_id": "", "client_secret": ""}' + #13#10 +
    '    },' + #13#10 +
    '    "java_paths": ["java", "C:\\Program Files\\Java\\jdk-21\\bin\\java.exe", "C:\\Program Files\\Java\\jdk-17\\bin\\java.exe"],' + #13#10 +
    '    "ai": {' + #13#10 +
    '      "provider": "gemini",' + #13#10 +
    '      "api_url": "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions",' + #13#10 +
    '      "api_key": "",' + #13#10 +
    '      "model": "gemini-2.5-flash"' + #13#10 +
    '    }' + #13#10 +
    '  }' + #13#10 +
    '}' + #13#10;

  if not SaveStringToFile(ConfigPath, Config, False) then
    RaiseException('MCDeploy configuration could not be created.');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    WriteInitialConfig();
end;
