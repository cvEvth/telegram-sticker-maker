# Telegram Sticker Maker

把视频 / 动图 / 图片批量转换成符合 Telegram 官方规格的贴纸（WebM VP9 / WebP）。完整需求见 [功能需求说明.md](功能需求说明.md)。

技术栈：C++20 · Qt 6.8 Widgets · FFmpeg 8.1（LGPL 共享库）· libwebp · libzip · CMake + vcpkg

## 构建（Windows）

依赖：Visual Studio 2022 Build Tools（C++）、Qt 6.8 MSVC 2022 64 位（含 Multimedia）、FFmpeg 共享库、vcpkg。
路径通过环境变量 `QT_ROOT`、`FFMPEG_ROOT`、`VCPKG_ROOT` 指定；`scripts\build.bat` 里有本机默认值。

```bat
scripts\build.bat            :: dev（RelWithDebInfo）
scripts\build.bat dev test   :: 构建并运行单元测试
scripts\build.bat release
```

产物在 `build\<preset>\bin\`，构建后会自动部署 Qt 与 FFmpeg 运行库，`TelegramStickerMaker.exe` 可直接双击运行。

## 目录结构

```
src/
  app/     配置目录 / 数据目录、持久化设置、运行日志、批量转换调度
  core/    数据模型与规则：任务、主列表模型、输出命名、时间格式、Telegram 规格、画质规划
  io/      ZIP 写入、输出打包（压缩包 / 时间戳文件夹 + stickers.json）
  media/   FFmpeg 调用：子进程（无黑窗口）、素材解析、滤镜链、贴纸编码、WebM 时长改写、WebP 编码
  ui/      主窗口、右侧设置面板、拖放区、日志面板、列表委托、深色主题
  ui/editor/ 截取 / 裁切窗口：预览画布、时间轴、片段列表、窗口设置
resources/ 样式表
tests/     tst_core / tst_plan / tst_editor（规则单元测试）、tst_media / tst_convert（用 ffmpeg 现场生成素材的解析与转换测试）、
           tst_editor_ui / tst_mainlist_ui（打开真实窗口模拟操作；设 TSM_SNAPSHOT_DIR 可保存截图）
```

`tsm_core` 静态库不依赖 Widgets，规则测试直接链接它；界面代码在 `tsm_ui` 静态库，主程序与界面测试共用。

## 进度

- [x] 工程骨架、主窗口布局、深色主题、设置持久化
- [x] 添加文件 / 文件夹 / 拖拽，后台解析素材（ffprobe）
- [x] 主列表：规格、目标文件名（序号自动重排）、双击改 Emoji、删除 / 清空、忙碌锁
- [ ] 解析细节：动图逐帧时长、HEIC 方向、调色板透明
- [x] 主列表：缩略图（懒加载 + 缓存）、行内 ✂️ / 📂 / 🗑️ 操作
- [x] 主列表：选择规则、快捷键、右键菜单（3.3 / 3.4），转换选中项
- [x] 关键词弹窗（第 8 节）
- [x] 转换核心（第 5 节）：画质规划、运动量探测、两遍编码、超限 / 偏小重试、3 秒破限、静态图逐级压缩、压缩包 / 文件夹输出
- [x] 截取 / 裁切编辑窗口（第 6 节）：时间轴与总览条、片段列表、裁切框（比例锁定 / 圆角 / 三分线）、镜像联动、快捷键、窗口设置、缓存代理、片段组写回
- [ ] 导入导出、会话自动保存与恢复、AI 匹配 Emoji
- [ ] 发布打包
