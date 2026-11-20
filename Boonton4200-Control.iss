; Build Boonton4200-Control.exe and deploy its Qt runtime with build.ps1 before compiling.
; The NI-488.2 driver is installed separately.
#define ReadQuoted(Text) Copy(Text, Pos('"', Text) + 1, Pos('"', Copy(Text, Pos('"', Text) + 1, Len(Text))) - 1)
#define VersionHeader FileOpen(SourcePath + "version.h")
#expr FileRead(VersionHeader)
#expr FileRead(VersionHeader)
#define Boonton4200Version ReadQuoted(FileRead(VersionHeader))
#expr FileClose(VersionHeader)
#define IconResource FileOpen(SourcePath + "assets\app.rc")
#define Boonton4200Icon ReadQuoted(FileRead(IconResource))
#expr FileClose(IconResource)
#define Boonton4200Name "Boonton 4200 Control"
#define Boonton4200Exe "Boonton4200-Control.exe"

[Setup]
AppId={{6FC1B2AE-DC1B-4F3A-A889-E992E5CF27A0}
AppName={#Boonton4200Name}
AppVersion={#Boonton4200Version}
AppVerName={#Boonton4200Name} {#Boonton4200Version}
AppPublisher=Ralf Tralow
VersionInfoVersion={#Boonton4200Version}
VersionInfoDescription={#Boonton4200Name} Setup
DefaultDirName={pf}\{#Boonton4200Name}
DefaultGroupName={#Boonton4200Name}
AllowNoIcons=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
; Inno 5.3.5 can see modern Windows as 6.2 due to its legacy manifest.
SetupIconFile=assets\{#Boonton4200Icon}
UninstallDisplayIcon={app}\{#Boonton4200Exe}
OutputDir=installer
OutputBaseFilename=Boonton4200-Control-Setup-{#Boonton4200Version}-x64
Compression=lzma
SolidCompression=yes

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
Source: "build\{#Boonton4200Exe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\generic\*.dll"; DestDir: "{app}\generic"; Flags: ignoreversion
Source: "build\iconengines\*.dll"; DestDir: "{app}\iconengines"; Flags: ignoreversion
Source: "build\imageformats\*.dll"; DestDir: "{app}\imageformats"; Flags: ignoreversion
Source: "build\networkinformation\*.dll"; DestDir: "{app}\networkinformation"; Flags: ignoreversion
Source: "build\platforms\*.dll"; DestDir: "{app}\platforms"; Flags: ignoreversion
Source: "build\styles\*.dll"; DestDir: "{app}\styles"; Flags: ignoreversion
Source: "build\tls\*.dll"; DestDir: "{app}\tls"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "licenses\*.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\{#Boonton4200Name}"; Filename: "{app}\{#Boonton4200Exe}"; WorkingDir: "{app}"
Name: "{commondesktop}\{#Boonton4200Name}"; Filename: "{app}\{#Boonton4200Exe}"; WorkingDir: "{app}"; Tasks: desktopicon
