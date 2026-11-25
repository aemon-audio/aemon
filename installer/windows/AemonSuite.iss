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
#if Has("Aemon Clipper")
Name: "clipper"; Description: "Aemon Clipper: shave the peaks before the limiter"; Types: custom
#endif
#if Has("Aemon Saturation")
Name: "saturation"; Description: "Aemon Saturation: warmth, weight and grit"; Types: custom
#endif
#if Has("Aemon De-esser")
Name: "deesser"; Description: "Aemon De-esser: smooth S sounds, vowels untouched"; Types: custom
#endif
#if Has("Aemon Imager")
Name: "imager"; Description: "Aemon Imager: width that never changes the mono sum"; Types: custom
#endif
#if Has("Aemon Multiband")
Name: "multiband"; Description: "Aemon Multiband: every band in its place"; Types: custom
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
#if Has("Aemon Clipper")
Type: filesandordirs; Name: "{autocf}\VST3\Aemon Clipper.vst3"; Components: clipper
#endif
#if Has("Aemon Saturation")
Type: filesandordirs; Name: "{autocf}\VST3\Aemon Saturation.vst3"; Components: saturation
#endif
#if Has("Aemon De-esser")
Type: filesandordirs; Name: "{autocf}\VST3\Aemon De-esser.vst3"; Components: deesser
#endif
#if Has("Aemon Imager")
Type: filesandordirs; Name: "{autocf}\VST3\Aemon Imager.vst3"; Components: imager
#endif
#if Has("Aemon Multiband")
Type: filesandordirs; Name: "{autocf}\VST3\Aemon Multiband.vst3"; Components: multiband
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
#if Has("Aemon Clipper")
Source: "{#Vst3Root}\Aemon Clipper.vst3\*"; DestDir: "{autocf}\VST3\Aemon Clipper.vst3"; Components: clipper; Flags: recursesubdirs createallsubdirs ignoreversion
#endif
#if Has("Aemon Saturation")
Source: "{#Vst3Root}\Aemon Saturation.vst3\*"; DestDir: "{autocf}\VST3\Aemon Saturation.vst3"; Components: saturation; Flags: recursesubdirs createallsubdirs ignoreversion
#endif
#if Has("Aemon De-esser")
Source: "{#Vst3Root}\Aemon De-esser.vst3\*"; DestDir: "{autocf}\VST3\Aemon De-esser.vst3"; Components: deesser; Flags: recursesubdirs createallsubdirs ignoreversion
#endif
#if Has("Aemon Imager")
Source: "{#Vst3Root}\Aemon Imager.vst3\*"; DestDir: "{autocf}\VST3\Aemon Imager.vst3"; Components: imager; Flags: recursesubdirs createallsubdirs ignoreversion
#endif
#if Has("Aemon Multiband")
Source: "{#Vst3Root}\Aemon Multiband.vst3\*"; DestDir: "{autocf}\VST3\Aemon Multiband.vst3"; Components: multiband; Flags: recursesubdirs createallsubdirs ignoreversion
#endif
Source: "art\aemon.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "THIRD-PARTY.txt"; DestDir: "{app}"; Flags: ignoreversion

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\';

var
  Names, Comps: TArrayOfString;

procedure FillNames;
begin
  if GetArrayLength (Names) > 0 then Exit;
  Names := ['Aemon Utility', 'Aemon EQ', 'Aemon Compressor', 'Aemon Limiter', 'Aemon Clipper', 'Aemon Saturation', 'Aemon De-esser', 'Aemon Imager', 'Aemon Multiband'];
  Comps := ['utility', 'eq', 'compressor', 'limiter', 'clipper', 'saturation', 'deesser', 'imager', 'multiband'];
end;

function PluginFile (const Name: String): String;
begin
  Result := ExpandConstant ('{autocf}\VST3\') + Name + '.vst3\Contents\x86_64-win\' + Name + '.vst3';
end;

{ One installer for everybody: it looks at what is there, updates it and adds what is new. Only plugins that were
  deliberately left out before stay left out. Nothing is remembered for plugins that did not exist then, so
  without this they would never be ticked on an update. }
procedure LookAround;
var
  I, K, Found, Fresh, Root: Integer;
  Left, Note: String;
  Notes: TArrayOfString;
  InPackage: Boolean;
begin
  FillNames;
  if IsAdminInstallMode then Root := HKLM64 else Root := HKCU;
  Left := '';
  RegQueryStringValue (Root, UninstallKey + '{6E7B1C52-4A0E-4E3B-9F7C-AE0A0C1D7E00}_is1', 'Inno Setup: Deselected Components', Left);
  Left := ',' + Lowercase (Left) + ',';
  Found := 0; Fresh := 0;
  SetArrayLength (Notes, WizardForm.ComponentsList.Items.Count);
  for I := 0 to GetArrayLength (Names) - 1 do
  begin
    Note := '';
    InPackage := False;
    for K := 0 to WizardForm.ComponentsList.Items.Count - 1 do
      if Pos (Names[I] + ':', WizardForm.ComponentsList.ItemCaption[K]) = 1 then
      begin
        InPackage := True;
        if FileExists (PluginFile (Names[I])) then
        begin
          Note := 'update';
          Found := Found + 1;
        end
        else Note := 'new';
        Notes[K] := Note;
      end;
    if InPackage then
    begin
      if (Note = 'new') and (Pos (',' + Comps[I] + ',', Left) = 0) then Fresh := Fresh + 1;
      { what is there is updated, what is new is added; a choice on the command line wins }
      if (ExpandConstant ('{param:components|}') = '') and ((Note = 'update') or (Pos (',' + Comps[I] + ',', Left) = 0)) then
        WizardSelectComponents (Comps[I]);
      Log ('Found: ' + Names[I] + ' = ' + Note);
    end;
  end;
  if Found > 0 then
  begin
    for K := 0 to GetArrayLength (Notes) - 1 do
      if Notes[K] <> '' then
        WizardForm.ComponentsList.ItemCaption[K] := WizardForm.ComponentsList.ItemCaption[K] + '   (' + Notes[K] + ')';
    if Fresh = 1 then
      WizardForm.WelcomeLabel2.Caption := 'Aemon is already on this computer. Setup will update the plugins you have and add the new one.'
    else if Fresh > 0 then
      WizardForm.WelcomeLabel2.Caption := 'Aemon is already on this computer. Setup will update the plugins you have and add the ' + IntToStr (Fresh) + ' new ones.'
    else
      WizardForm.WelcomeLabel2.Caption := 'Aemon is already on this computer. Setup will update the plugins you have.';
    WizardForm.WelcomeLabel2.Caption := WizardForm.WelcomeLabel2.Caption + #13#10#13#10 + 'Please close your DAW before you continue. Your own presets and your projects are kept.';
    WizardForm.SelectComponentsLabel.Caption := 'What you have is updated, what is new is ticked for you. Each plugin works on its own.';
  end;
  Log ('Found ' + IntToStr (Found) + ', new ' + IntToStr (Fresh));
end;

procedure InitializeWizard;
begin
  LookAround;
end;

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
  Path := PluginFile (Name);
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
  I: Integer;
begin
  Result := '';
  FillNames;
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
