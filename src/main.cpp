#include "app/Logger.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TelegramStickerMaker"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    // Qt 自带对话框 / 按钮的中文翻译：优先程序目录下的 translations（部署后），其次 Qt 安装目录
    QTranslator qtTranslator;
    const QLocale zhCN(QLocale::Chinese, QLocale::China);
    for (const QString& dir : {QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
                               QLibraryInfo::path(QLibraryInfo::TranslationsPath)}) {
        if (qtTranslator.load(zhCN, QStringLiteral("qtbase"), QStringLiteral("_"), dir)) {
            QApplication::installTranslator(&qtTranslator);
            break;
        }
    }

    tsm::Logger::instance(); // 在主线程创建
    tsm::applyDarkTheme(app);

    tsm::MainWindow window;
    window.show();
    if (const QStringList paths = QApplication::arguments().mid(1); !paths.isEmpty())
        window.openPaths(paths);
    return app.exec();
}
