#include "ui/Theme.h"

#include "core/TaskListModel.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace tsm {

namespace {
ThemeColors darkColors()
{
    ThemeColors c;
    c.dark = true;
    c.bg = QColor(0x12, 0x10, 0x14);
    c.bg2 = QColor(0x1b, 0x18, 0x1d);
    c.elevated = QColor(0x19, 0x17, 0x1b);
    c.soft = QColor(0x24, 0x20, 0x27);
    c.line = QColor(0x2a, 0x26, 0x2c);
    c.lineStrong = QColor(0x3a, 0x35, 0x3d);
    c.ink = QColor(0xec, 0xe5, 0xd9);
    c.ink2 = QColor(0xc9, 0xc2, 0xb8);
    c.muted = QColor(0x95, 0x8e, 0x98);
    c.shadow = QColor(0x07, 0x06, 0x08);
    c.accent = QColor(0xff, 0x6b, 0x95);
    c.accentHover = QColor(0xff, 0x85, 0xa8);
    c.accentBg = QColor(0x3a, 0x1d, 0x28);
    c.accentFg = QColor(0xff, 0xe1, 0xea);
    c.onAccent = QColor(0x2a, 0x0b, 0x16);
    c.chipBg = QColor(0x2a, 0x15, 0x20);
    c.chipLine = QColor(0x6b, 0x2a, 0x40);
    c.ok = QColor(0x46, 0xc4, 0x86);
    c.bad = QColor(0xff, 0x6b, 0x7f);
    c.badLine = QColor(0x4a, 0x25, 0x30);
    c.warn = QColor(0xf5, 0xb8, 0x2e);
    c.fresh = QColor(0x7e, 0xe0, 0xa8);
    c.canvas = QColor(0x0c, 0x0b, 0x0d);
    return c;
}

ThemeColors lightColors()
{
    ThemeColors c;
    c.dark = false;
    c.bg = QColor(0xf4, 0xef, 0xe6);
    c.bg2 = QColor(0xeb, 0xe3, 0xd4);
    c.elevated = QColor(0xfb, 0xf8, 0xf2);
    c.soft = QColor(0xef, 0xe8, 0xdc);
    c.line = QColor(0xdd, 0xd5, 0xc8);
    c.lineStrong = QColor(0xc9, 0xc0, 0xb3);
    c.ink = QColor(0x1c, 0x1a, 0x20);
    c.ink2 = QColor(0x3b, 0x37, 0x3f);
    c.muted = QColor(0x6f, 0x6a, 0x72);
    c.shadow = QColor(0x1c, 0x1a, 0x20);
    c.accent = QColor(0xff, 0x5c, 0x8a);
    c.accentHover = QColor(0xff, 0x7a, 0xa0);
    c.accentBg = QColor(0xff, 0xdb, 0xe5);
    c.accentFg = QColor(0x4a, 0x10, 0x22);
    c.onAccent = QColor(0x2a, 0x0b, 0x16);
    c.chipBg = QColor(0xff, 0xe8, 0xef);
    c.chipLine = QColor(0xf3, 0xa9, 0xbf);
    c.ok = QColor(0x1d, 0x9a, 0x5b);
    c.bad = QColor(0xd7, 0x37, 0x4f);
    c.badLine = QColor(0xea, 0xb0, 0xba);
    c.warn = QColor(0xb0, 0x76, 0x00);
    c.fresh = QColor(0x2f, 0xb3, 0x78);
    c.canvas = QColor(0xe4, 0xdd, 0xd1);
    return c;
}

ThemeColors g_theme = darkColors();
ThemeMode g_mode = ThemeMode::Dark;

// app.qss 里的 ${name} 换成当前配色
QString fillTemplate(QString qss, const ThemeColors& c)
{
    const std::pair<const char*, QColor> tokens[] = {
        {"bg", c.bg},           {"bg2", c.bg2},           {"elevated", c.elevated},
        {"soft", c.soft},       {"line", c.line},         {"lineStrong", c.lineStrong},
        {"ink", c.ink},         {"ink2", c.ink2},         {"muted", c.muted},
        {"shadow", c.shadow},   {"accent", c.accent},     {"accentHover", c.accentHover},
        {"accentBg", c.accentBg}, {"accentFg", c.accentFg}, {"onAccent", c.onAccent},
        {"chipBg", c.chipBg},   {"chipLine", c.chipLine}, {"ok", c.ok},
        {"bad", c.bad},         {"badLine", c.badLine},   {"warn", c.warn},
        {"fresh", c.fresh},     {"canvas", c.canvas},
    };
    for (const auto& [name, color] : tokens)
        qss.replace(QStringLiteral("${%1}").arg(QLatin1StringView(name)), color.name());
    return qss;
}

QPalette makePalette(const ThemeColors& c)
{
    QPalette p;
    p.setColor(QPalette::Window, c.bg);
    p.setColor(QPalette::WindowText, c.ink);
    p.setColor(QPalette::Base, c.elevated);
    p.setColor(QPalette::AlternateBase, c.bg2);
    p.setColor(QPalette::ToolTipBase, c.elevated);
    p.setColor(QPalette::ToolTipText, c.ink);
    p.setColor(QPalette::Text, c.ink);
    p.setColor(QPalette::Button, c.soft);
    p.setColor(QPalette::ButtonText, c.ink);
    p.setColor(QPalette::BrightText, c.dark ? Qt::white : Qt::black);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, c.onAccent);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::PlaceholderText, c.muted);
    p.setColor(QPalette::Light, c.lineStrong);
    p.setColor(QPalette::Midlight, c.line);
    p.setColor(QPalette::Mid, c.lineStrong);
    p.setColor(QPalette::Dark, c.shadow);
    QColor disabled = c.muted;
    disabled.setAlpha(150);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    return p;
}

bool systemIsDark()
{
    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}
} // namespace

ThemeMode themeModeFromString(const QString& text)
{
    if (text == u"light")
        return ThemeMode::Light;
    if (text == u"dark")
        return ThemeMode::Dark;
    return ThemeMode::System;
}

QString themeModeToString(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Light: return QStringLiteral("light");
    case ThemeMode::Dark: return QStringLiteral("dark");
    default: return QStringLiteral("system");
    }
}

const ThemeColors& theme()
{
    return g_theme;
}

void applyTheme(QApplication& app, ThemeMode mode)
{
    g_mode = mode;
    QStyleHints* hints = QGuiApplication::styleHints();
    // 标题栏跟着变；跟随系统时交还给系统再读取
    hints->setColorScheme(mode == ThemeMode::Light  ? Qt::ColorScheme::Light
                          : mode == ThemeMode::Dark ? Qt::ColorScheme::Dark
                                                    : Qt::ColorScheme::Unknown);
    const bool dark = mode == ThemeMode::Dark || (mode == ThemeMode::System && systemIsDark());
    g_theme = dark ? darkColors() : lightColors();

    static bool listening = false;
    if (!listening) {
        listening = true;
        QObject::connect(hints, &QStyleHints::colorSchemeChanged, &app, [&app] {
            if (g_mode == ThemeMode::System && systemIsDark() != g_theme.dark)
                applyTheme(app, ThemeMode::System);
        });
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QFont font(QStringLiteral("Microsoft YaHei UI"));
        font.setPointSizeF(9.5);
        app.setFont(font);
    }

    TaskListModel::setStatusColors(g_theme.ok, g_theme.bad, g_theme.muted);
    app.setPalette(makePalette(g_theme));
    QFile qss(QStringLiteral(":/styles/app.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(fillTemplate(QString::fromUtf8(qss.readAll()), g_theme));

    // 自绘控件读取 theme()，切换后重画
    for (QWidget* w : QApplication::allWidgets())
        w->update();
}

} // namespace tsm
