#include "core/AiEmoji.h"

#include <QJsonArray>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextBoundaryFinder>
#include <QUrl>

namespace tsm::AiEmoji {

namespace {
bool isEmojiCodePoint(char32_t c)
{
    return (c >= 0x1F000 && c <= 0x1FAFF)  // 表情、符号、旗帜、扩展
        || (c >= 0x2600 && c <= 0x27BF)    // 杂项符号、装饰符号
        || (c >= 0x2300 && c <= 0x23FF)    // ⌚ ⏰ 等
        || (c >= 0x2B00 && c <= 0x2BFF)    // ⭐ ⬆ 等
        || (c >= 0x2190 && c <= 0x21FF)    // 箭头
        || c == 0x203C || c == 0x2049 || c == 0x00A9 || c == 0x00AE || c == 0x2122 || c == 0x3030 || c == 0x303D;
}

// 一个字形簇是否是 Emoji（含 ❤️、👍🏻、👨‍👩‍👧、1️⃣ 这类组合）
bool isEmojiCluster(const QString& cluster)
{
    const QList<uint> cps = cluster.toUcs4();
    if (cps.isEmpty())
        return false;
    if (isEmojiCodePoint(cps.first()))
        return true;
    // 键帽：数字 / # / * + FE0F + 20E3
    return cps.contains(0x20E3);
}

bool isDecorative(const QString& cluster)
{
    static const QStringList decorative{QStringLiteral("✨"), QStringLiteral("🌟"), QStringLiteral("💫")};
    QString base = cluster;
    base.remove(QChar(0xFE0F));
    return decorative.contains(base);
}
} // namespace

QString defaultPrompt()
{
    return QStringLiteral(
        "这是一张贴纸（如果是横向排列的多帧，表示同一段动画的不同时刻）。"
        "请判断画面中角色的情绪或反应，只回复 1~3 个最符合的表情 / 反应类 Emoji，例如 😂 😭 😡 🥺 😴 🤔 👍。"
        "不要使用 ✨ 🌟 💫 🎀 💖 这类装饰性符号，除非画面里真的是星星或魔法光效。"
        "不要输出任何文字、标点或解释。");
}

QList<double> sampleTimes(double d)
{
    if (d >= 1.5)
        return {d * 0.2, d * 0.5, d * 0.8};
    if (d >= 0.6)
        return {0.1, d * 0.6};
    return {std::max(0.0, d / 2)};
}

QString extractEmoji(const QString& reply)
{
    QStringList clusters;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, reply);
    qsizetype start = 0;
    while (finder.toNextBoundary() != -1) {
        const qsizetype end = finder.position();
        const QString cluster = reply.mid(start, end - start);
        start = end;
        if (isEmojiCluster(cluster))
            clusters << cluster;
    }
    // 同时有表情和装饰符号时去掉装饰符号
    const bool hasReal = std::any_of(clusters.cbegin(), clusters.cend(), [](const QString& c) { return !isDecorative(c); });
    if (hasReal)
        clusters.removeIf([](const QString& c) { return isDecorative(c); });

    QString result;
    for (const QString& c : std::as_const(clusters)) {
        if (result.size() + c.size() > kMaxEmojiChars)
            break;
        result += c;
    }
    return result;
}

QByteArray buildRequest(const QString& model, const QString& prompt, const QByteArray& jpeg)
{
    const QString dataUrl = QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(jpeg.toBase64());
    const QJsonArray content{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), prompt}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("image_url")},
                    {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), dataUrl}}}},
    };
    const QJsonObject root{
        {QStringLiteral("model"), model},
        {QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                            {QStringLiteral("content"), content}}}},
        {QStringLiteral("max_tokens"), 50},
        {QStringLiteral("temperature"), 0.2},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString parseReply(const QByteArray& body, QString* error)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    const QJsonValue content =
        doc.object().value(u"choices").toArray().at(0).toObject().value(u"message").toObject().value(u"content");
    if (err.error != QJsonParseError::NoError || !content.isString()) {
        if (error) {
            // 兼容接口常见的 {"error": {"message": ...}}
            const QString message = doc.object().value(u"error").toObject().value(u"message").toString();
            *error = message.isEmpty() ? QStringLiteral("接口返回格式无效") : message;
        }
        return {};
    }
    return content.toString();
}

QString endpointFor(const QString& baseUrl)
{
    QString base = baseUrl.trimmed();
    while (base.endsWith(u'/'))
        base.chop(1);
    if (base.endsWith(QStringLiteral("/chat/completions")))
        return base;
    return base + QStringLiteral("/chat/completions");
}

bool isLocalBaseUrl(const QString& baseUrl)
{
    const QString host = QUrl(baseUrl.trimmed()).host().toLower();
    return host == u"localhost" || host == u"127.0.0.1" || host == u"::1" || host == u"[::1]";
}

bool isRetryable(int httpStatus, bool networkError)
{
    if (httpStatus == 429 || (httpStatus >= 500 && httpStatus <= 599))
        return true;
    if (httpStatus > 0)
        return false;
    return networkError;
}

} // namespace tsm::AiEmoji
