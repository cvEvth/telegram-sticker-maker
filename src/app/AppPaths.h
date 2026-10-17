#pragma once

#include <QString>

namespace tsm {

class AppPaths {
public:
    static constexpr auto kDataFolderName = "telegram_sticker_maker";

    // 用户目录下的应用配置文件夹（settings.ini 所在处）
    static QString configDir();
    // 桌面/TG_Stickers
    static QString defaultOutputDir();
    // 数据目录：留空 = 配置目录；所选路径本身已是数据目录则直接用，否则用其下的 telegram_sticker_maker
    static QString resolveDataDir(const QString& userChoice);

    static QString sessionFile(const QString& dataDir);
    static QString previewCacheDir(const QString& dataDir);
    static QString importCacheDir(const QString& dataDir);
};

} // namespace tsm
