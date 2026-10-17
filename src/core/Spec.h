#pragma once

#include <QtGlobal>

namespace tsm {

enum class OutputMode { Sticker = 0, Emoji = 1 };
enum class EncodePreset { Anime = 0, LiveAction = 1, Fast = 2 };
enum class CropAspect { Square = 0, Free, Original, Landscape16x9, Classic4x3, Portrait9x16 };

// Telegram 官方规格与本工具的规划常量（见需求第 1、5 节）
namespace spec {
inline constexpr int kStickerSide = 512;
inline constexpr int kEmojiSide = 100;

inline constexpr qint64 kStickerVideoLimit = 256 * 1024;
inline constexpr qint64 kStickerStaticLimit = 512 * 1024;
inline constexpr qint64 kEmojiLimit = 64 * 1024;
// 视频规划时留一点余量，硬上限仍是上面的值
inline constexpr qint64 kStickerVideoTarget = 252 * 1024;
inline constexpr qint64 kEmojiVideoTarget = 60 * 1024;

inline constexpr double kMaxFps = 30.0;
inline constexpr double kOfficialMaxDuration = 3.0;
inline constexpr double kPatchedDuration = 2.99;
inline constexpr double kDefaultDuration = 3.0;

inline constexpr int kMaxParallelJobs = 2;
inline constexpr int kMaxKeywords = 20;
inline constexpr int kMaxKeywordChars = 64;
} // namespace spec

} // namespace tsm
