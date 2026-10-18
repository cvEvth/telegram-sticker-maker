# 开发指南

技术栈：C++20 · Qt 6.8 Widgets / Multimedia · FFmpeg 8.1（LGPL 共享库）· libwebp · libzip · CMake + Ninja + vcpkg。
目前只做了 Windows 版，代码按跨平台写（没有用 Windows 专有界面 API），macOS / Linux 需要补构建脚本与打包。

版本改动见 [CHANGELOG.md](../CHANGELOG.md)。

## 环境

| 依赖 | 说明 |
|---|---|
| Visual Studio 2022 Build Tools | 勾选「使用 C++ 的桌面开发」，自带 CMake 与 Ninja |
| Qt 6.8（MSVC 2022 64 位） | 需要 Qt Multimedia、Qt Image Formats，以及基础包里的 Qt Tools（构建时用 lrelease 编译翻译）；路径用环境变量 `QT_ROOT` 指定，如 `D:\Qt\6.8.3\msvc2022_64` |
| FFmpeg 8.1 LGPL 共享库 | [BtbN 构建](https://github.com/BtbN/FFmpeg-Builds/releases) 的 `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip`，解压目录用 `FFMPEG_ROOT` 指定 |
| vcpkg | 用 `VCPKG_ROOT` 指定；依赖（libwebp、libzip）由 `vcpkg.json` 自动安装 |
| Inno Setup 6（可选） | 只在打安装程序时需要，`ISCC` 指向 `ISCC.exe` |

`scripts\build.bat` 里有作者机器上的默认路径，设置了同名环境变量就会用你的。

## 构建与测试

```bat
scripts\build.bat              :: dev（RelWithDebInfo）
scripts\build.bat dev test     :: 构建并运行全部测试
scripts\build.bat release      :: Release
scripts\build.bat release test
```

产物在 `build\<预设>\bin\`。构建后会自动部署 Qt 与 FFmpeg 运行库，`TelegramStickerMaker.exe` 可以直接双击运行。

测试分三类：

- `tst_core` / `tst_plan` / `tst_editor`：规则单元测试（命名、画质规划、裁切计算、时间轴等）
- `tst_media` / `tst_convert` / `tst_export` / `tst_ai`：用 FFmpeg 现场生成素材，测试解析、转换、导入导出、AI 请求（本机假接口）
- `tst_editor_ui` / `tst_mainlist_ui`：打开真实窗口模拟点击和按键；设置 `TSM_SNAPSHOT_DIR` 会把界面截图存到该目录

### 重新生成 README 截图

`tst_readme_shots` 平时自动跳过。准备一个放了 4 段短视频的目录，然后：

```bat
set TSM_README_MEDIA=D:\素材目录
set TSM_SNAPSHOT_DIR=D:\Code\s\docs\screenshots
build\dev\bin\tst_readme_shots.exe
```

它会转换这些素材并保存 `main-window.png`、`editor.png`、`done-dialog.png`。另设 `TSM_README_LANG=en` 或 `ru` 时界面换成英语 / 俄语。输出目录和数据目录临时放在 `TSM_README_ROOT`（默认 `D:\StickerDemo`，必须是不存在的目录）下，截图里不会出现个人路径，结束后自动删除。

## 打包

```bat
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

在 `dist\` 下生成：

- `TelegramStickerMaker-<版本>-win64.zip`：便携版，自带 VC++ 运行库与第三方许可证
- `TelegramStickerMaker-<版本>-win64-setup.exe`：Inno Setup 安装程序（找不到 `ISCC.exe` 时跳过）；默认按当前用户安装，不需要管理员权限

版本号默认取 `CMakeLists.txt` 里的 `set(TSM_VERSION x.y.z)`，设置环境变量 `TSM_VERSION` 可以覆盖。

## 发版

推送 `v` 开头的版本标签，GitHub Actions（`.github/workflows/release.yml`）会在 Windows 上构建、跑全部测试、打包，并创建 GitHub Release 上传安装程序和便携 zip（附 SHA-256）。版本号直接取自标签。

1. 在 [CHANGELOG.md](../CHANGELOG.md) 加一节 `## vX.Y.Z`，写这一版的改动（会放在 Release 说明开头）；没写时自动列出上一版以来的提交标题
2. 提交并推送
3. 打标签并推送：

```bat
git tag v0.2.0
git push origin v0.2.0
```

在 Actions 页面手动运行这个工作流（填一个试用版本号）只构建打包，产物作为构建附件保留 7 天，不发布 Release。发布说明模板在 `packaging/release-notes.md`。

## 目录结构

```
src/
  app/        配置与数据目录、持久化设置、日志、批量转换与导出调度、会话保存、导入、AI 匹配
  core/       数据模型与规则：任务、主列表模型、输出命名、Telegram 规格、画质规划、关键词、清单格式、文件头解析
  io/         ZIP 读写、输出打包（压缩包 / 时间戳文件夹 + stickers.json）
  media/      FFmpeg 调用：素材解析、滤镜链、贴纸编码、WebM 时长改写、WebP 编码、动态 WebP 解码、缩略图、预览副本
  ui/         主窗口、右侧设置面板、拖放区、日志、列表委托、各类弹窗、深色主题
  ui/editor/  截取 / 裁切窗口：预览画布、时间轴、片段列表、窗口设置
resources/    样式表、程序图标
translations/ 英语 / 俄语界面翻译（Qt Linguist .ts），更新流程见其中的 README
packaging/    安装脚本（Inno Setup）、简体中文语言文件、第三方许可证清单、发布说明模板
tests/        单元测试与界面测试
docs/         开发文档与 README 截图
```

`tsm_core` 静态库不依赖 Widgets，规则测试直接链接它；界面代码在 `tsm_ui` 静态库，主程序与界面测试共用。

## 界面翻译

界面文字直接用简体中文写在代码里，统一用 `tr("…")`（QObject 子类）或 `QCoreApplication::translate("上下文", "…")`（其他代码）包起来；带数量的句子用 `%n` 写成复数形式。英语和俄语译文在 `translations/tsm_en.ts`、`tsm_ru.ts`，构建时编译成 `.qm` 嵌入程序。改了界面文字后运行 `cmake --build --preset dev --target update_translations` 更新 `.ts` 并补上译文，详见 [translations/README.md](../translations/README.md)。
