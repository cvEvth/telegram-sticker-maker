Telegram 贴纸批量制作工具：把视频、动图、图片一键转换成符合 Telegram 规范的贴纸。

## 更新内容

{{CHANGES}}

## 下载

| 文件 | 说明 |
|---|---|
| `TelegramStickerMaker-{{VERSION}}-win64-setup.exe` | 安装程序。默认安装到当前用户，不需要管理员权限 |
| `TelegramStickerMaker-{{VERSION}}-win64.zip` | 便携版，解压后双击 `TelegramStickerMaker.exe` 即可运行 |

系统要求：Windows 10 / 11 64 位。两个包都自带 VC++ 运行库，不需要另外安装。

## 主要功能

- **批量转换**：视频 → WebM（VP9）视频贴纸，图片 → WebP 静态贴纸，也支持 100×100 自定义表情；自动规划画质、两遍编码，超限或偏小时自动重试
- **支持的素材**：MP4、MKV、WebM、MOV、AVI、FLV；GIF、APNG、动态 WebP；PNG、JPG、WebP、BMP、TIFF、HEIC
- **截取 / 裁切编辑窗口**：时间轴打点截取多个片段，点击片段自动循环播放；裁切框支持比例锁定、圆角、镜像
- **Emoji 与关键词**：双击设置 Emoji，批量设置关键词（写入压缩包内的 `stickers.json`）；可用兼容 OpenAI 的视觉接口自动匹配 Emoji
- **导入 / 导出**：导出原片 + 参数清单，换电脑后继续编辑；也可导出已裁切的高画质源文件
- **自动保存**：关闭后再打开，列表和所有编辑都会恢复

## 创建贴纸包

转换完成后，前往 **https://www.cvevth.top/sticker** 创建你的 Telegram 贴纸包，也可以把压缩包直接发给 Telegram 机器人 **@cvEvthBot**。

## 说明

- 程序没有代码签名，首次运行时 Windows SmartScreen 可能提示"未知发布者"，点"更多信息" → "仍要运行"即可。
- 本程序使用了 Qt、FFmpeg（LGPL）、libwebp、libzip 等开源组件，许可证见安装目录下的 `THIRD_PARTY_NOTICES.txt` 和 `licenses` 文件夹。
