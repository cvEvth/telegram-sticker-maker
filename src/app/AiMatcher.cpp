#include "app/AiMatcher.h"

#include "core/AiEmoji.h"
#include "media/AiFrameGrabber.h"

#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace tsm {

AiMatcher::AiMatcher(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
    m_pool.setMaxThreadCount(1);
}

AiMatcher::~AiMatcher()
{
    if (m_cancel)
        m_cancel->store(true);
    if (m_reply)
        m_reply->abort();
    m_pool.waitForDone();
}

QString AiMatcher::validate(const AiConfig& config)
{
    const QUrl url(config.baseUrl.trimmed());
    if (!url.isValid() || (url.scheme() != u"http" && url.scheme() != u"https") || url.host().isEmpty())
        return QStringLiteral("Base URL 无效，请在 AI 设置中检查");
    if (config.model.trimmed().isEmpty())
        return QStringLiteral("请先在 AI 设置中填写模型名称");
    if (config.apiKey.trimmed().isEmpty() && !AiEmoji::isLocalBaseUrl(config.baseUrl))
        return QStringLiteral("请先在 AI 设置中填写 API Key");
    return {};
}

void AiMatcher::setTimings(int retryBaseMs, int gapMs)
{
    m_retryBaseMs = retryBaseMs;
    m_gapMs = gapMs;
}

void AiMatcher::start(const QList<StickerTask>& tasks, const AiConfig& config, const QString& workRoot)
{
    if (m_running || tasks.isEmpty())
        return;
    m_running = true;
    m_config = config;
    if (m_config.prompt.trimmed().isEmpty())
        m_config.prompt = AiEmoji::defaultPrompt();
    m_workRoot = workRoot;
    m_queue = tasks;
    m_matched = m_empty = m_failed = 0;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    next();
}

void AiMatcher::cancel()
{
    if (!m_running || !m_cancel)
        return;
    m_cancel->store(true);
    if (m_reply)
        m_reply->abort(); // onReply 里收尾
}

void AiMatcher::next()
{
    if (m_cancel->load() || m_queue.isEmpty()) {
        m_running = false;
        m_currentId.clear();
        emit finished(m_matched, m_empty, m_failed, m_cancel->load());
        return;
    }
    const StickerTask task = m_queue.takeFirst();
    m_currentId = task.id;
    m_attempt = 0;
    emit itemStarted(task.id);

    const QString workDir = QDir(m_workRoot).filePath(QStringLiteral("ai_") + QString(task.id).remove(u'{').remove(u'}'));
    const auto cancel = m_cancel;
    const QString id = task.id;
    m_pool.start([this, task, workDir, cancel, id] {
        const AiFrameGrabber::Result r = AiFrameGrabber::grab(task, workDir, cancel);
        QDir(workDir).removeRecursively();
        QMetaObject::invokeMethod(this, [this, id, r] { onGrabbed(id, r.ok, r.jpeg, r.error); }, Qt::QueuedConnection);
    });
}

void AiMatcher::onGrabbed(const QString& taskId, bool ok, const QByteArray& jpeg, const QString& error)
{
    if (taskId != m_currentId)
        return;
    if (m_cancel->load()) {
        next();
        return;
    }
    if (!ok) {
        finishItem({}, error);
        return;
    }
    m_body = AiEmoji::buildRequest(m_config.model.trimmed(), m_config.prompt, jpeg);
    send();
}

void AiMatcher::send()
{
    QNetworkRequest request(QUrl(AiEmoji::endpointFor(m_config.baseUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (const QString key = m_config.apiKey.trimmed(); !key.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    request.setTransferTimeout(AiEmoji::kTimeoutMs);
    m_reply = m_net->post(request, m_body);
    connect(m_reply, &QNetworkReply::finished, this, &AiMatcher::onReply);
}

void AiMatcher::onReply()
{
    QNetworkReply* reply = m_reply;
    if (!reply)
        return;
    m_reply = nullptr;
    reply->deleteLater();
    if (m_cancel->load()) {
        next();
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const bool networkError = reply->error() != QNetworkReply::NoError && status == 0;
    if (status >= 200 && status < 300) {
        QString error;
        const QString content = AiEmoji::parseReply(body, &error);
        if (!error.isEmpty())
            finishItem({}, error);
        else
            finishItem(AiEmoji::extractEmoji(content), {});
        return;
    }

    if (AiEmoji::isRetryable(status, networkError) && m_attempt < AiEmoji::kMaxRetries) {
        ++m_attempt;
        QTimer::singleShot(m_retryBaseMs * m_attempt, this, [this] {
            if (m_cancel->load())
                next();
            else
                send();
        });
        return;
    }

    QString error;
    if (status > 0) {
        QString message;
        AiEmoji::parseReply(body, &message);
        error = QStringLiteral("HTTP %1").arg(status);
        if (!message.isEmpty() && message != u"接口返回格式无效")
            error += QStringLiteral(": ") + message;
    } else if (reply->error() == QNetworkReply::OperationCanceledError) {
        error = QStringLiteral("请求超时（%1 秒）").arg(AiEmoji::kTimeoutMs / 1000);
    } else {
        error = reply->errorString();
    }
    finishItem({}, error);
}

void AiMatcher::finishItem(const QString& emoji, const QString& error)
{
    if (!error.isEmpty())
        ++m_failed;
    else if (emoji.isEmpty())
        ++m_empty;
    else
        ++m_matched;
    emit itemFinished(m_currentId, emoji, error);
    // 逐个进行，间隔 0.2 秒
    QTimer::singleShot(m_queue.isEmpty() ? 0 : m_gapMs, this, &AiMatcher::next);
}

} // namespace tsm
