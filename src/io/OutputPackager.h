#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QStringList>

// 转换成功的贴纸写到最终位置（需求第 4 节）：
//  - 压缩包：全部贴纸 + stickers.json 打进输出目录下的 TG_Stickers_YYYYMMDD_HHMMSS.zip（仅存储），
//    失败时退回为把单个文件直接写到各自的目标目录；
//  - 文件夹：每个目标目录下新建时间戳文件夹，贴纸和 stickers.json 都放在里面。
namespace tsm::OutputPackager {

struct Item {
    QString taskId;
    QString stagedFile; // 已转换的临时文件
    QString fileName;   // 期望的文件名，如 "001_😂.webm"
    QString targetDir;  // 文件夹模式的目标目录
    QString emoji;
    QStringList keywords;
    double cropRadius = 0;
    bool keepSource = false; // stagedFile 是用户的原文件：复制而不是移动
    QString proxyFile;       // 附带的预览副本（写到 proxies/ 下，stickers.json 记录相对路径）
};

struct Options {
    QString prefix = QStringLiteral("TG_Stickers"); // 压缩包 / 时间戳文件夹名前缀
    QString noun = QStringLiteral("贴纸");           // 日志用词
};

struct Result {
    bool ok = false;
    QStringList locations;              // 生成的压缩包 / 文件夹
    QHash<QString, QString> finalPaths; // taskId → 最终文件（压缩包模式为压缩包路径）
    QStringList logLines;
    QString error;
};

// stickers.json 内容；fileNames 与 items 一一对应（去重后的实际文件名）
QByteArray stickersJson(const QList<Item>& items, const QStringList& fileNames);

Result package(const QList<Item>& items, bool asZip, const QString& zipDir, const QDateTime& now,
               const Options& options = {});

// 包内预览副本的相对路径，如 "proxies/001_😂.mp4.mp4"
QString proxyEntryName(const QString& fileName);

} // namespace tsm::OutputPackager
