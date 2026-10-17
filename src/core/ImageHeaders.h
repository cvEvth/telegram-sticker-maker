#pragma once

#include <QByteArray>
#include <QString>

// 读 GIF / PNG(APNG) / WebP 文件头（需求第 2 节）：
//  - 透明：GIF 调色板透明（图形控制扩展的透明标记）、PNG 带 Alpha 的颜色类型或 tRNS、WebP 的 Alpha 标记；
//  - 动图：逐帧时长累加（单帧缺省或为 0 时按 100ms）。
namespace tsm::ImageHeaders {

inline constexpr int kDefaultFrameMs = 100;

struct Info {
    bool parsed = false;
    bool hasAlpha = false;
    bool animated = false;
    int width = 0;
    int height = 0;
    int frames = 0;
    qint64 durationMs = 0; // 动图逐帧时长之和

    double durationSeconds() const { return durationMs / 1000.0; }
    double fps() const { return durationMs > 0 ? frames * 1000.0 / durationMs : 0.0; }
};

Info scanGif(const QByteArray& data);
Info scanPng(const QByteArray& data);
Info scanWebp(const QByteArray& data);

// 按文件头魔数选择解析器；不是这三种格式时 parsed = false
Info scan(const QByteArray& data);
Info scanFile(const QString& path);

} // namespace tsm::ImageHeaders
