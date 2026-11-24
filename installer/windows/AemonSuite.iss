; Aemon Suite: one Windows installer for all Aemon plugins (Inno Setup 6.3 or later). AGPL-3.0-or-later.
; Use build-suite.ps1, which passes: /DAppVer=0.1.0 /DVst3Root=<folder holding the .vst3 bundles> /DOutDir=<dir>
; A plugin whose bundle is not in Vst3Root is left out of the installer.

#ifndef AppVer
  #define AppVer "0.0.0"
#endif
#ifndef Vst3Root
  #error Pass /DVst3Root=<folder with the .vst3 bundles>
#endif
#ifndef OutDir
  #define OutDir "..\..\build\installer"
#endif
#define Site "https://github.com/aemon-audio/aemon"
; 1.2.3-dev.4 -> 1.2.3 for the Windows version resource
#define NumVer Pos("-", AppVer) > 0 ? Copy(AppVer, 1, Pos("-", AppVer) - 1) : AppVer
#define Has(str Name) DirExists(AddBackslash(Vst3Root) + Name + ".vst3")

[Setup]
AppId={{6E7B1C52-4A0E-4E3B-9F7C-AE0A0C1D7E00}
AppName=Aemon Suite
AppVersion={#AppVer}
AppVerName=Aemon Suite {#AppVer}
AppPublisher=Aemon
AppPublisherURL={#Site}
AppSupportURL={#Site}
VersionInfoVersion={#NumVer}
VersionInfoDescription=Aemon Suite Setup
VersionInfoProductName=Aemon Suite
DefaultDirName={autopf}\Aemon Suite
DisableWelcomePage=no
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
AlwaysShowComponentsList=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; Plugins go to the standard shared VST3 folder, which needs administrator rights.
; "Setup.exe /CURRENTUSER" installs to the standard per-user VST3 folder without them.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
OutputDir={#OutDir}
OutputBaseFilename=Aemon-Suite-Setup
Compression=lzma2/max
SolidCompression=yes
SetupIconFile=art\aemon.ico
WizardImageFile=art\wizard-1.bmp,art\wizard-2.bmp
WizardSmallImageFile=art\small-1.bmp,art\small-2.bmp
#if Ver >= 0x06060000
WizardStyle=modern dark
#else
WizardStyle=modern
#endif
UninstallDisplayName=Aemon Suite
UninstallDisplayIcon={app}\aemon.ico
; never close a DAW for the user (unsaved work): see PluginInUse below
CloseApplications=no
RestartApplications=no
SetupLogging=yes

[Messages]
WelcomeLabel1=Aemon Suite
WelcomeLabel2=Mixing and mastering plugins for any VST3 host. Free and open source.%n%nPlease close your DAW before you continue. Your own presets are kept.
SelectComponentsDesc=Which plugins would you like?
SelectComponentsLabel2=Everything is selected. Each plugin works on its own.
WizardSelectComponents=Choose your plugins
FinishedHeadingLabel=Aemon is installed
FinishedLabel=The plugins are in the standard VST3 folder.%n%nOpen your DAW, scan for new plugins, and look for Aemon in the effects list.
FinishedLabelNoIcons=The plugins are in the standard VST3 folder.%n%nOpen your DAW, scan for new plugins, and look for Aemon in the effects list.

[Types]
; a single custom type: only the tick boxes are shown, all ticked
Name: "custom"; Description: "Choose plugins"; Flags: iscustom

[Components]
#if Has("Aemon Utility")
Name: "utility"; Description: "Aemon Utility: level, width, phase and loudness"; Types: custom
#endif
#if Has("Aemon EQ")
Name: "eq"; Description: "Aemon EQ: sixteen bands, dynamic and linear phase"; Types: custom
#endif
#if Has("Aemon Compressor")
Name: "compressor"; Description: "Aemon Compressor: punch, glue and control"; Types: custom
#endif
#if Has("Aemon Limiter")
Name: "limiter"; Description: "Aemon Limiter: loud and safe, true peak"; Types: custom
#endif

[InstallDelete]
; a clean bundle on every update
#if Has("Aemon Utility")
Type: filesandordirs; Name: "{autocf}\VST3\Aemon Utility.vst3"; Components: utility
#endif
#if Has("Aemon EQ")
Type: filesandordirs; Name: "{autocf}\VST3\Aemon EQ.vst3"; Components: eq
#endif
#if Has("Aemon Compressor")
Type: filesandordirs; Name: "{autocf}\VST3\Aemon Compressor.vst3"; Components: compressor
#endif
#if Has("Aemon Limiter")
Type: filesandordirs; Name: "{autocf}\VST3\Aemon Limiter.vst3"; Components: limiter
#endif

[Files]
#if Has("Aemon Utility")
Source: "{#Vst3Root}\Aemon Utility.vst3\*"; DestDir: "{autocf}\VST3\Aemon Utility.vst3"; Components: utility; Flags: recursesubdirs createallsubdirs ignoreversion
#endif
#if Has("Aemon EQ")
Source: "{#Vst3Root}\Aemon EQ.vst3\*"; DestDir: "{autocf}\VST3\Aemon EQ.vst3"; Components: eq; Flags: recursesubdirs createallsubdirs ignoreversion
#endif
#if Has("Aemon Compressor")
Source: "{#Vst3Root}\Aemon Compressor.vst3\*"; DestDir: "{autocf}\VST3\Aemon Compressor.vst3"; Components: compressor; Flags: recursesubdirs createallsubdirs ignoreversion
#endif
#if Has("Aemon Limiter")
Source: "{#Vst3Root}\Aemon Limiter.vst3\*"; DestDir: "{autocf}\VST3\Aemon Limiter.vst3"; Components: limiter; Flags: recursesubdirs createallsubdirs ignoreversion
#endif
Source: "art\aemon.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "THIRD-PARTY.txt"; DestDir: "{app}"; Flags: ignoreversion

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\';

{ The test releases had one installer per plugin. Remove the ones being replaced, so that Windows Apps
  shows a single entry. They only own their .vst3 folder; presets live in Documents and are not touched. }
procedure RemoveBeta (const Component, Guid: String);
var
  Root, Code, Tries: Integer;
  Key, Cmd: String;
begin
  if not WizardIsComponentSelected (Component) then Exit;
  if IsAdminInstallMode then Root := HKLM64 else Root := HKCU;
  Key := UninstallKey + '{6E7B1C52-4A0E-4E3B-9F7C-' + Guid + '}_is1';
  if not RegQueryStringValue (Root, Key, 'UninstallString', Cmd) then Exit;
  Cmd := RemoveQuotes (Cmd);
  Log ('Removing the single-plugin test version: ' + Cmd);
  if Exec (Cmd, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, Code) then
  begin
    { the uninstaller continues from a temporary copy: wait until it has really finished }
    Tries := 0;
    while (RegKeyExists (Root, Key) or FileExists (Cmd)) and (Tries < 100) do
    begin
      Sleep (200);
      Tries := Tries + 1;
    end;
  end;
end;

{ A plugin that is loaded in a DAW cannot be opened for writing. }
function PluginInUse (const Name: String): Boolean;
var
  Path: String;
  Stream: TFileStream;
begin
  Result := False;
  Path := ExpandConstant ('{autocf}\VST3\') + Name + '.vst3\Contents\x86_64-win\' + Name + '.vst3';
  if not FileExists (Path) then Exit;
  try
    Stream := TFileStream.Create (Path, fmOpenReadWrite or fmShareExclusive);
    Stream.Free;
  except
    Result := True;
  end;
end;

function AnyInUse (OnlySelected: Boolean): String;
var
  Names, Comps: TArrayOfString;
  I: Integer;
begin
  Result := '';
  Names := ['Aemon Utility', 'Aemon EQ', 'Aemon Compressor', 'Aemon Limiter'];
  Comps := ['utility', 'eq', 'compressor', 'limiter'];
  for I := 0 to GetArrayLength (Names) - 1 do
    if (not OnlySelected) or WizardIsComponentSelected (Comps[I]) then
      if PluginInUse (Names[I]) then
      begin
        Result := Names[I];
        Exit;
      end;
end;

function AskToClose (const Name: String): Boolean;
begin
  Result := SuppressibleMsgBox (Name + ' is open in another program.' + #13#10#13#10 +
    'Save your work and close your DAW, then choose Retry. Nothing will be closed for you.', mbInformation, MB_RETRYCANCEL, IDCANCEL) = IDRETRY;
end;

function PrepareToInstall (var NeedsRestart: Boolean): String;
var
  Name: String;
begin
  Result := '';
  Name := AnyInUse (True);
  while Name <> '' do
  begin
    if not AskToClose (Name) then
    begin
      Result := Name + ' is open in another program. Close your DAW and run Setup again. Nothing was changed.';
      Exit;
    end;
    Name := AnyInUse (True);
  end;
end;

function InitializeUninstall: Boolean;
var
  Name: String;
begin
  Result := True;
  Name := AnyInUse (False);
  while Name <> '' do
  begin
    if not AskToClose (Name) then
    begin
      Result := False;
      Exit;
    end;
    Name := AnyInUse (False);
  end;
end;

procedure CurStepChanged (CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    RemoveBeta ('utility', 'AE0A0C1D7E01');
    RemoveBeta ('eq', 'AE0A0C1D7E02');
    RemoveBeta ('compressor', 'AE0A0C1D7E03');
    RemoveBeta ('limiter', 'AE0A0C1D7E04');
  end;
end;

procedure CurPageChanged (CurPageID: Integer);
begin
  if CurPageID = wpSelectComponents then
    WizardForm.NextButton.Caption := SetupMessage (msgButtonInstall);
end;

function NextButtonClick (CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectComponents) and (WizardSelectedComponents (False) = '') then
  begin
    SuppressibleMsgBox ('Choose at least one plugin.', mbInformation, MB_OK, IDOK);
    Result := False;
  end;
end;
