#include "app/UpdateChecker.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace tsm {

namespace {
constexpr int TimeoutMs = 10000;

// 走系统代理（很多用户靠系统代理访问 GitHub）；只作用于检查更新这一个 QNetworkAccessManager
class SystemProxyFactory : public QNetworkProxyFactory {
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override
    {
        return systemProxyForQuery(query);
    }
};

QList<int> versionParts(QString version)
{
    version = version.trimmed();
    if (version.startsWith(u'v') || version.startsWith(u'V'))
        version.remove(0, 1);
    QList<int> parts;
    for (const QString& part : version.split(u'.'))
        parts << part.toInt(); // 非数字段按 0 算
    return parts;
}
} // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
    m_net->setProxyFactory(new SystemProxyFactory);
}

QString UpdateChecker::latestReleaseApiUrl()
{
    return QStringLiteral("https://api.github.com/repos/cvEvth/telegram-sticker-maker/releases/latest");
}

QString UpdateChecker::releasesPageUrl()
{
    return QStringLiteral("https://github.com/cvEvth/telegram-sticker-maker/releases/latest");
}

int UpdateChecker::compareVersions(const QString& a, const QString& b)
{
    const QList<int> pa = versionParts(a);
    const QList<int> pb = versionParts(b);
    for (qsizetype i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int x = pa.value(i);
        const int y = pb.value(i);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

std::optional<UpdateChecker::Release> UpdateChecker::parseRelease(const QByteArray& json)
{
    const QJsonObject obj = QJsonDocument::fromJson(json).object();
    if (obj.value(u"draft").toBool() || obj.value(u"prerelease").toBool())
        return std::nullopt;
    QString tag = obj.value(u"tag_name").toString().trimmed();
    if (tag.startsWith(u'v') || tag.startsWith(u'V'))
        tag.remove(0, 1);
    if (tag.isEmpty() || !tag.front().isDigit())
        return std::nullopt;
    QString page = obj.value(u"html_url").toString();
    if (!page.startsWith(u"https://github.com/"))
        page = releasesPageUrl();
    return Release{tag, page};
}

void UpdateChecker::check(const QString& currentVersion)
{
    if (m_running)
        return;
    m_running = true;

    QNetworkRequest request{QUrl(latestReleaseApiUrl())};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("TelegramStickerMaker/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(TimeoutMs);

    QNetworkReply* reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, currentVersion] {
        reply->deleteLater();
        m_running = false;
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }
        const std::optional<Release> release = parseRelease(reply->readAll());
        if (!release) {
            emit failed(tr("无法识别的版本信息"));
            return;
        }
        if (compareVersions(release->version, currentVersion) > 0)
            emit updateAvailable(*release);
        else
            emit upToDate();
    });
}

} // namespace tsm
