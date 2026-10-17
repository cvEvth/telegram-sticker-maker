#pragma once

#include <QHash>
#include <QObject>
#include <QThreadPool>
#include <atomic>
#include <memory>

namespace tsm {

// 截取窗口的流畅预览副本（需求 6.2）：对 MP4 / WebM / Ogg 以外的格式在后台生成 480p 副本。
// 按文件内容标识（路径 + 修改时间 + 大小）缓存，最多保留 60 个，超过删最旧的。
class ProxyCache : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxEntries = 60;

    explicit ProxyCache(QObject* parent = nullptr);
    ~ProxyCache() override;

    void setCacheDir(const QString& dir);
    QString cacheDir() const { return m_dir; }

    // 浏览器式播放器能直接流畅拖动的格式
    static bool needsProxy(const QString& path);
    // 播放器根本解不了、必须先生成副本的格式（动态 WebP）
    static bool playerCannotDecode(const QString& path);

    QString proxyPathFor(const QString& source) const { return pathIn(m_dir, source); }
    // 缓存目录 dir 下 source 对应的副本路径（文件不一定存在）
    static QString pathIn(const QString& dir, const QString& source);
    // 已有缓存时返回副本路径（并刷新其"最近使用"时间），否则为空
    QString cached(const QString& source) const;

    bool isGenerating(const QString& source) const { return m_jobs.contains(source); }
    void generate(const QString& source, double duration);
    void cancelAll();

signals:
    void progress(const QString& source, int percent);
    void finished(const QString& source, bool ok, const QString& proxyPath, const QString& error);

private:
    void prune();

    QString m_dir;
    QThreadPool m_pool;
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_jobs;
};

// 预览缓存（缩略图 + 预览副本）占用
struct CacheUsage {
    int files = 0;
    qint64 bytes = 0;
    QString text() const;
};
CacheUsage measurePreviewCache(const QString& previewCacheDir);
bool clearPreviewCache(const QString& previewCacheDir);

} // namespace tsm
