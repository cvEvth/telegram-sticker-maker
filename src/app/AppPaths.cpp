#include "app/AppPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace tsm {

namespace {
const QString kSessionFile = QStringLiteral("session.json");
const QString kPreviewCache = QStringLiteral("preview_cache");
const QString kImportCache = QStringLiteral("import_cache");

bool looksLikeDataDir(const QDir& dir)
{
    return dir.exists(kSessionFile) || dir.exists(kPreviewCache) || dir.exists(kImportCache);
}
} // namespace

QString AppPaths::configDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir;
}

QString AppPaths::defaultOutputDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)).filePath(QStringLiteral("TG_Stickers"));
}

QString AppPaths::resolveDataDir(const QString& userChoice)
{
    const QString choice = userChoice.trimmed();
    if (choice.isEmpty())
        return configDir();
    const QDir dir(choice);
    if (looksLikeDataDir(dir))
        return dir.absolutePath();
    return dir.absoluteFilePath(QString::fromLatin1(kDataFolderName));
}

QString AppPaths::sessionFile(const QString& dataDir)
{
    return QDir(dataDir).filePath(kSessionFile);
}

QString AppPaths::previewCacheDir(const QString& dataDir)
{
    return QDir(dataDir).filePath(kPreviewCache);
}

QString AppPaths::importCacheDir(const QString& dataDir)
{
    return QDir(dataDir).filePath(kImportCache);
}

} // namespace tsm
