Telegram 贴纸批量制作工具：把视频、动图、图片一键转换成符合 Telegram 规范的贴纸。

## 更新内容

{{CHANGES}}

## 下载

| 文件 | 说明 |
|---|---|
| `TelegramStickerMaker-{{VERSION}}-win64-setup.exe` | 安装程序。默认安装到当前用户，不需要管理员权限 |
| `TelegramStickerMaker-{{VERSION}}-win64.zip` | 便携版，解压后双击 `TelegramStickerMaker.exe` 即可运行 |
| `TelegramStickerMaker-{{VERSION}}-macos-arm64.dmg` | macOS 版。打开后把程序拖进「应用程序」文件夹 |

系统要求：Windows 10 / 11 64 位（两个包都自带 VC++ 运行库，不需要另外安装）；macOS 12 及以上，Apple Silicon（M 系列芯片）。

## 说明

- 程序没有代码签名，首次运行时 Windows SmartScreen 可能提示"未知发布者"，点"更多信息" → "仍要运行"即可。
- macOS 版没有经过 Apple 公证，第一次打开会提示无法验证开发者：在「系统设置 → 隐私与安全性」里点「仍要打开」，或在终端执行 `xattr -dr com.apple.quarantine /Applications/TelegramStickerMaker.app`。
- 本程序使用了 Qt、FFmpeg（LGPL）、libwebp、libzip 等开源组件，许可证见安装目录下的 `THIRD_PARTY_NOTICES.txt` 和 `licenses` 文件夹（macOS 版在 `TelegramStickerMaker.app/Contents/Resources/THIRD_PARTY_NOTICES.txt`）。
