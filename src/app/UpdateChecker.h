#pragma once

#include <QObject>
#include <QString>
#include <optional>

class QNetworkAccessManager;

namespace tsm {

// 启动时查询 GitHub 上的最新 Release，有新版本时通知界面。只提醒，不下载。
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    struct Release {
        QString version; // "1.2.0"（已去掉标签前的 v）
        QString pageUrl; // Release 页面
    };

    explicit UpdateChecker(QObject* parent = nullptr);

    static QString latestReleaseApiUrl();
    static QString releasesPageUrl();

    // 比较 "x.y.z"（可带前缀 v，缺的段按 0 算）：a < b 返回负数，相等 0，a > b 正数
    static int compareVersions(const QString& a, const QString& b);
    // 解析 releases/latest 接口的 JSON；草稿 / 预发布或字段缺失时返回空
    static std::optional<Release> parseRelease(const QByteArray& json);

    // 发起一次检查；结果通过 updateAvailable / upToDate / failed 之一返回
    void check(const QString& currentVersion);

signals:
    void updateAvailable(const tsm::UpdateChecker::Release& release);
    void upToDate();
    void failed(const QString& error);

private:
    QNetworkAccessManager* m_net = nullptr;
    bool m_running = false;
};

} // namespace tsm
