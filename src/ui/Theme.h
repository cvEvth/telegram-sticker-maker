#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace tsm {

enum class ThemeMode { System, Light, Dark };

ThemeMode themeModeFromString(const QString& text); // "light" / "dark"，其余为跟随系统
QString themeModeToString(ThemeMode mode);

// 一套主题的配色（参照 cvevth.top：暖色底 + 粉色强调 + 硬投影）
struct ThemeColors {
    bool dark = true;
    QColor bg;         // 窗口底色
    QColor bg2;        // 表头、日志栏等次级底色
    QColor elevated;   // 卡片、列表
    QColor soft;       // 按钮、空位
    QColor line;       // 细分隔线
    QColor lineStrong; // 控件描边
    QColor ink;        // 正文
    QColor ink2;       // 次要正文
    QColor muted;      // 提示文字
    QColor shadow;     // 硬投影 / 粗描边
    QColor accent;     // 粉色强调
    QColor accentHover;
    QColor accentBg;   // 强调色浅底（选中行、选中按钮）
    QColor accentFg;   // 强调浅底上的文字
    QColor onAccent;   // 强调色实底上的文字
    QColor chipBg;
    QColor chipLine;
    QColor ok;
    QColor bad;
    QColor badLine;
    QColor warn;
    QColor fresh;      // 第二强调色（渐变描边另一端）
    QColor canvas;     // 视频预览底色
};

// 当前生效的配色
const ThemeColors& theme();

// Fusion 样式 + 调色板 + resources/styles/app.qss；可在运行中切换。
// System 会跟随系统深浅色变化。
void applyTheme(QApplication& app, ThemeMode mode);

} // namespace tsm
