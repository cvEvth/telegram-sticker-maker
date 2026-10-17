#pragma once

#include "core/StickerTask.h"

#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <atomic>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace tsm {

struct AiConfig {
    QString baseUrl;
    QString model;
    QString apiKey;
    QString prompt; // 为空时用默认提示词
};

// 批量 AI 匹配 Emoji（需求 7.2）：逐个识别，间隔 0.2 秒；超时 25 秒；
// 429 / 5xx / 网络错误最多重试 2 次（等 1s、2s），其他 HTTP 错误直接放弃。所有信号在主线程发出。
class AiMatcher : public QObject {
    Q_OBJECT

public:
    explicit AiMatcher(QObject* parent = nullptr);
    ~AiMatcher() override;

    // 检查配置；不可用时返回原因
    static QString validate(const AiConfig& config);

    bool isRunning() const { return m_running; }
    void start(const QList<StickerTask>& tasks, const AiConfig& config, const QString& workRoot);
    void cancel();

    // 测试用：重试等待的基数（默认 1000ms）与批量间隔
    void setTimings(int retryBaseMs, int gapMs);

signals:
    void itemStarted(const QString& taskId);
    void itemFinished(const QString& taskId, const QString& emoji, const QString& error);
    void finished(int matched, int empty, int failed, bool cancelled);

private:
    void next();
    void onGrabbed(const QString& taskId, bool ok, const QByteArray& jpeg, const QString& error);
    void send();
    void onReply();
    void finishItem(const QString& emoji, const QString& error);

    QNetworkAccessManager* m_net = nullptr;
    QThreadPool m_pool;
    QPointer<QNetworkReply> m_reply;
    std::shared_ptr<std::atomic_bool> m_cancel;
    AiConfig m_config;
    QString m_workRoot;
    QList<StickerTask> m_queue;
    QString m_currentId;
    QByteArray m_body;
    int m_attempt = 0;
    int m_retryBaseMs = 1000;
    int m_gapMs = 200;
    int m_matched = 0;
    int m_empty = 0;
    int m_failed = 0;
    bool m_running = false;
};

} // namespace tsm
