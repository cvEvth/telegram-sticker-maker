<p align="center">
  <img src="resources/icons/app_256.png" width="128" alt="Telegram Sticker Maker">
</p>

<h1 align="center">Telegram Sticker Maker</h1>

<p align="center">
  <a href="README.md">简体中文</a> · <b>English</b> · <a href="README.ru.md">Русский</a>
</p>

<p align="center">
  A Windows desktop tool that turns videos, GIFs and images into Telegram stickers in bulk<br>
  Trim clips, crop the frame, set emoji and keywords, then convert everything to spec-compliant WebM / WebP stickers in one click
</p>

<p align="center">
  <a href="https://github.com/cvEvth/telegram-sticker-maker/releases/latest"><img src="https://img.shields.io/github/v/release/cvEvth/telegram-sticker-maker?label=latest" alt="Latest release"></a>
  <a href="https://github.com/cvEvth/telegram-sticker-maker/releases"><img src="https://img.shields.io/github/downloads/cvEvth/telegram-sticker-maker/total?label=downloads" alt="Downloads"></a>
  <img src="https://img.shields.io/badge/Windows-10%20%2F%2011-0078d4" alt="Windows 10 / 11">
</p>

<p align="center">
  <a href="https://github.com/cvEvth/telegram-sticker-maker/releases/latest"><b>⬇️ Download latest</b></a>
  ·
  <a href="https://www.cvevth.top/sticker"><b>🌐 Create a sticker pack online</b></a>
</p>

> The app's interface is in Simplified Chinese only. Button names below are given in Chinese with an English translation so you can find them.

![Main window](docs/screenshots/main-window.png)

## Features

- **Telegram specs, handled for you**: videos and GIFs become WebM (VP9) video stickers, images become WebP static stickers; size, frame rate and file size are taken care of, no settings to tune
- **Best possible quality, never over the limit**: bitrate and frame rate are planned from duration and motion, with two-pass encoding; anything over 256 KB is retried at a lower bitrate or frame rate, and undersized results get a higher bitrate to "fill up" the quality
- **Longer than 3 seconds still works**: all frames are kept and only the duration in the file header is rewritten to 2.99 s, which passes the @Stickers bot's duration check
- **Trim and crop**: mark points on the timeline to cut several clips from one video; the crop box supports aspect-ratio lock, rounded corners, circles and mirroring; click a clip to preview it on loop
- **Emoji and keywords**: double-click to set an emoji, or right-click to set emoji and keywords in bulk; you can also connect an OpenAI-compatible vision model to match emoji to the mood of each frame automatically
- **Lots of input formats**: MP4, MKV, WebM, MOV, AVI, FLV, GIF, APNG, animated WebP, PNG, JPG, WebP, BMP, TIFF, HEIC; transparency in videos, GIFs and PNGs is preserved
- **Custom emoji**: switch to 100×100 mode to make a whole Custom Emoji set at once
- **Safe from interruptions**: the list and every edit are saved automatically and restored exactly when you reopen the app; you can also export the source files with a settings manifest and continue on another computer

## Download

Get it from [Releases](https://github.com/cvEvth/telegram-sticker-maker/releases/latest):

| File | Description |
|---|---|
| `TelegramStickerMaker-x.y.z-win64-setup.exe` | Installer; installs for the current user by default, no admin rights needed |
| `TelegramStickerMaker-x.y.z-win64.zip` | Portable version; unzip and double-click `TelegramStickerMaker.exe` |

Requirements: Windows 10 / 11, 64-bit. Both packages bundle their runtimes, nothing else to install.

## Quick start

1. **Drag** videos, GIFs or images **into the window**, or click 「添加文件」 (Add files) / 「添加文件夹」 (Add folder)
2. To trim clips or crop the frame, click ✂️ on a row to open the editor
3. **Double-click 「目标文件名」 (Target file name)** to set an emoji; the right-click menu sets emoji and keywords in bulk, or lets AI match emoji
4. Click 「🚀 开始转换」 (Start conversion). When it finishes, the output folder contains `TG_Stickers_<date>_<time>.zip` (the stickers plus a `stickers.json` with their emoji and keywords)

### Trim and crop

One video can be cut into several clips, each with its own start and end, crop box, rounded corners, mirroring and emoji. Clicking a clip card or its block on the timeline loops playback within that clip; `[` / `]` set the start and end points, and `Space` plays or pauses.

![Trim and crop window](docs/screenshots/editor.png)

### Create a sticker pack

After converting, go to **[www.cvevth.top/sticker](https://www.cvevth.top/sticker)** to create your Telegram sticker pack, or send the zip straight to the Telegram bot **@cvEvthBot**.

<p align="center"><img src="docs/screenshots/done-dialog.png" width="540" alt="Conversion finished"></p>

## Telegram sticker specs

Output automatically follows the official requirements:

| Type | Size | Format | Max file size |
|---|---|---|---|
| Video sticker | 512 px on the long side | WebM (VP9), no audio, ≤ 3 s | 256 KB |
| Static sticker | 512 px on the long side | WebP, transparency allowed | 512 KB |
| Custom emoji | 100×100 | WebM video / WebP static | 64 KB |

## FAQ

<details>
<summary><b>Windows says "Windows protected your PC" when I open it</b></summary>

The app isn't signed with a paid code-signing certificate, so Windows SmartScreen reports an "unknown publisher". Click "More info" → "Run anyway". Every release lists SHA-256 checksums in its notes so you can verify your download.
</details>

<details>
<summary><b>How do I use AI emoji matching?</b></summary>

Click 「⚙️ AI 设置」 (AI settings) in the toolbar and fill in the API key, base URL and model name. Any vision model that speaks the OpenAI Chat Completions format works (OpenAI, DeepSeek, Moonshot, OneAPI-style relays and so on). If the base URL points to your own machine (localhost / 127.0.0.1), the key can be left empty. Recognition only looks at the final sticker area (with crop, rounded corners and mirroring applied); for videos, several frames are combined into one image.
</details>

<details>
<summary><b>Conversion fails or says the file is too large</b></summary>

Long clips with complex, fast-moving content may not fit in 256 KB even at the lowest frame rate. Try trimming the clip or cropping away extra area. If 「编码预设」 (Encoding preset) is set to 「⚡ 快速导出」 (Fast export), switch back to the default 「✨ 高画质」 (High quality), which uses two-pass encoding with light denoising for better quality at the same size. The reason for a failure is shown in the status bar and the log.
</details>

<details>
<summary><b>Where are settings and caches stored?</b></summary>

By default in `%LOCALAPPDATA%\TelegramStickerMaker` (settings, the auto-saved list and preview caches). You can move it to another drive with 「数据目录」 (Data folder) on the right, and 「清除预览缓存」 (Clear preview cache) frees up space. Uninstalling keeps this data, so you can pick up where you left off after reinstalling.
</details>

## Development

C++20 · Qt 6.8 · FFmpeg 8.1 · libwebp · libzip · CMake + vcpkg. See [docs/development.md](docs/development.md) (Chinese) for building, testing, packaging and releasing, and [CHANGELOG.md](CHANGELOG.md) for version history.

## Acknowledgements

This app uses open-source components including [Qt](https://www.qt.io/) (LGPL v3), [FFmpeg](https://ffmpeg.org/) (LGPL), [libwebp](https://chromium.googlesource.com/webm/libwebp) and [libzip](https://libzip.org/). Their licenses ship with the installer; see `THIRD_PARTY_NOTICES.txt`.
