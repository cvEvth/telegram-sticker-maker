#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// AI 匹配 Emoji 的纯逻辑部分
namespace tsm::AiEmoji {

inline constexpr int kMaxEmojiChars = 12;
inline constexpr int kTimeoutMs = 25000;
inline constexpr int kMaxRetries = 2;
inline constexpr int kBatchGapMs = 200;

// 默认提示词：只要表情 / 反应类 Emoji
QString defaultPrompt();

// 片段内的取帧时刻（相对片段起点，秒）：≥1.5s 取 20%/50%/80%；0.6~1.5s 取 0.1s 与 60%；更短取一帧
QList<double> sampleTimes(double clipDuration);

// 只提取 Emoji（最多 12 个字符，不截断组合 Emoji）。
// dropDecorative：AI 回复里同时有表情和 ✨🌟💫 时去掉装饰符号；手动输入时保留
QString extractEmoji(const QString& reply, bool dropDecorative = true);

// Chat Completions 请求体（文字提示 + 一张 JPEG）
QByteArray buildRequest(const QString& model, const QString& prompt, const QByteArray& jpeg);

// 取 choices[0].message.content；格式不对时返回空并写 error
QString parseReply(const QByteArray& body, QString* error = nullptr);
// 回复里既没有内容也没有 error.message 时 parseReply 给出的提示
QString invalidReplyText();

// "https://api.openai.com/v1" → ".../v1/chat/completions"
QString endpointFor(const QString& baseUrl);

// Base URL 指向本机时允许不填 Key
bool isLocalBaseUrl(const QString& baseUrl);

// 429、5xx、网络错误重试；400/401/403/404 等直接放弃
bool isRetryable(int httpStatus, bool networkError);

} // namespace tsm::AiEmoji
