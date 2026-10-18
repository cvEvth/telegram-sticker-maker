# 界面翻译

界面文字直接用简体中文写在代码里（`tr("…")` / `QCoreApplication::translate("…", "…")`），这里是英语和俄语的 Qt Linguist 翻译文件。构建时 lrelease 把它们编译成 `.qm` 并嵌入程序资源 `:/i18n`。

## 改了界面文字之后

1. 从源码更新 `.ts`（新增的文字会标为未完成，删掉的文字会被移除）：

   ```bat
   cmake --build --preset dev --target update_translations
   ```

2. 用 Qt Linguist（`linguist.exe`）或直接编辑 `.ts` 补上 `type="unfinished"` 的条目。
3. 带 `%n` 的是复数条目：英语要填 2 个 `<numerusform>`（单数、复数），俄语填 3 个（1 / 2–4 / 5 及以上）。
4. 重新构建。翻译缺失的文字会显示中文原文。
