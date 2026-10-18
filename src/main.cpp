#include "app/AppSettings.h"
#include "app/Language.h"
#include "app/Logger.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TelegramStickerMaker"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    // 窗口与弹窗标题栏图标（多尺寸 .ico：大尺寸为完整人物，16~32px 只取甜甜圈和脸）
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));

    tsm::Language::install(tsm::Language::resolve(tsm::AppSettings().language()));

    tsm::Logger::instance(); // 在主线程创建
    tsm::applyDarkTheme(app);

    tsm::MainWindow window;
    window.show();
    if (const QStringList paths = QApplication::arguments().mid(1); !paths.isEmpty())
        window.openPaths(paths);
    return app.exec();
}
