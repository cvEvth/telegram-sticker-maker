#pragma once

#include "core/StickerTask.h"

#include <QCache>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QThreadPool>

namespace tsm {

// 缩略图：取片段起点那一帧，应用镜像、裁切、圆角（需求 3.2 / 6.7 / 12）。
// 同一文件、时间点、尺寸、裁切、圆角、镜像只生成一次；源文件修改后失效。
// 内存缓存 + 磁盘缓存（数据目录下 preview_cache/thumbs）。
class ThumbnailService : public QObject {
    Q_OBJECT

public:
    struct Request {
        QString path;
        bool isVideo = false;
        bool sourceAlpha = false;
        QString videoCodec;
        double time = 0;
        std::optional<QRect> crop;
        double radius = 0;
        bool mirror = false;
        QSize box; // 缩略图最大尺寸（逻辑像素 × 设备像素比）
    };

    explicit ThumbnailService(QObject* parent = nullptr);
    ~ThumbnailService() override;

    static Request requestFor(const StickerTask& task, QSize box);

    void setCacheDir(const QString& dir);
    QString cacheDir() const { return m_cacheDir; }

    // 有缓存时直接返回，否则返回空并在后台生成，完成后发 ready(key)
    QImage thumbnail(const Request& request);
    static QString keyFor(const Request& request);

    // 同步生成（工作线程调用）：渲染一帧并应用圆角
    static QImage render(const Request& request, QString* error = nullptr);

    void clearMemory();

signals:
    void ready(const QString& key);

private:
    QString diskPath(const QString& key) const;

    QThreadPool m_pool;
    QCache<QString, QImage> m_memory;
    QSet<QString> m_pending;
    QSet<QString> m_failed;
    QString m_cacheDir;
};

} // namespace tsm
