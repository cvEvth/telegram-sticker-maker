; Telegram Sticker Maker 安装程序（Inno Setup 6）
; 由 scripts\package.ps1 调用：ISCC /DAppVersion=x.y.z /DSourceDir=<打包目录> /DOutputDir=<输出目录> installer.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir is required
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define AppName "Telegram Sticker Maker"
#define AppExe "TelegramStickerMaker.exe"
#define SiteUrl "https://www.cvevth.top/sticker"

[Setup]
AppId={{6F3C2B9E-4D1A-4E7B-9C55-2A8F1D7E3B60}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=cvEvth
AppPublisherURL={#SiteUrl}
AppSupportURL={#SiteUrl}
DefaultDirName={autopf}\TelegramStickerMaker
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; 默认为当前用户安装，不需要管理员权限；安装时可改为所有用户
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile=..\resources\icons\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir={#OutputDir}
OutputBaseFilename=TelegramStickerMaker-{#AppVersion}-win64-setup
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
WizardStyle=modern
CloseApplications=yes
; 按系统界面语言自动选择，匹配不到时才弹出语言选择框
ShowLanguageDialog=auto

[Languages]
; 第一项是系统语言都不匹配时的默认语言
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "ChineseSimplified.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
english.VisitSite=Visit cvevth.top to learn how to create a Telegram sticker pack
chinesesimp.VisitSite=访问 cvevth.top，了解如何创建 Telegram 贴纸包
russian.VisitSite=Открыть cvevth.top и узнать, как создать набор стикеров Telegram

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
Filename: "{#SiteUrl}"; Description: "{cm:VisitSite}"; Flags: shellexec nowait postinstall skipifsilent unchecked

; 卸载时保留 %LOCALAPPDATA%\TelegramStickerMaker 下的设置与会话，重新安装后可继续使用
