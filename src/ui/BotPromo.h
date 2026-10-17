#pragma once

#include <QString>

// 配合使用的 Telegram 机器人与贴纸库：转换好的压缩包直接发给机器人即可创建贴纸包
namespace tsm::BotPromo {

inline QString handle() { return QStringLiteral("@cvEvthBot"); }
inline QString botUrl() { return QStringLiteral("https://t.me/cvEvthBot"); }
inline QString galleryUrl() { return QStringLiteral("https://www.cvevth.top/sticker"); }

void openBot();
void openGallery();

} // namespace tsm::BotPromo
