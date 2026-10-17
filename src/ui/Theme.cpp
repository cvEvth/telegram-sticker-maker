#include "ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

namespace tsm {

void applyDarkTheme(QApplication& app)
{
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark); // Windows 11 深色标题栏
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const QColor window(0x14, 0x16, 0x1b);
    const QColor base(0x1a, 0x1d, 0x24);
    const QColor text(0xe6, 0xe8, 0xee);
    const QColor disabled(0x5c, 0x63, 0x75);
    const QColor accent(0x2a, 0xab, 0xee);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x1e, 0x21, 0x28));
    p.setColor(QPalette::ToolTipBase, QColor(0x26, 0x2a, 0x33));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x23, 0x27, 0x31));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x6c, 0xc6, 0xf5));
    p.setColor(QPalette::PlaceholderText, QColor(0x6b, 0x73, 0x85));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    app.setPalette(p);

    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSizeF(9.5);
    app.setFont(font);

    QFile qss(QStringLiteral(":/styles/dark.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
}

} // namespace tsm
