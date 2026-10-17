#include "core/ImageHeaders.h"

#include <QFile>
#include <algorithm>

namespace tsm::ImageHeaders {

namespace {
quint16 le16(const uchar* p)
{
    return quint16(p[0] | (p[1] << 8));
}

quint32 le24(const uchar* p)
{
    return quint32(p[0] | (p[1] << 8) | (p[2] << 16));
}

quint32 le32(const uchar* p)
{
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint32 be32(const uchar* p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint16 be16(const uchar* p)
{
    return quint16((p[0] << 8) | p[1]);
}

qint64 orDefault(qint64 ms)
{
    return ms > 0 ? ms : kDefaultFrameMs;
}
} // namespace

Info scanGif(const QByteArray& data)
{
    Info info;
    const auto* p = reinterpret_cast<const uchar*>(data.constData());
    const qsizetype n = data.size();
    if (n < 13 || !(data.startsWith("GIF87a") || data.startsWith("GIF89a")))
        return info;
    info.width = le16(p + 6);
    info.height = le16(p + 8);
    qsizetype pos = 13;
    if (p[10] & 0x80)
        pos += 3 * (qsizetype(1) << ((p[10] & 0x07) + 1)); // 全局调色板

    // 跳过一串数据子块，返回结束后的位置；越界返回 -1
    const auto skipSubBlocks = [&](qsizetype at) -> qsizetype {
        while (at < n) {
            const int size = p[at];
            ++at;
            if (size == 0)
                return at;
            at += size;
        }
        return -1;
    };

    // 透明看合成后的画面：编码器常把后续帧没变化的像素标成透明（表示"保留上一帧"），这不算透明。
    // 算透明的情况：首帧带透明色或没铺满画布、或某帧处理方式为"恢复为背景"（露出透明背景）
    qint64 pendingDelay = -1;
    bool pendingTransparent = false;
    int pendingDisposal = 0;
    while (pos < n) {
        const uchar marker = p[pos];
        if (marker == 0x3B) // 结束
            break;
        if (marker == 0x21 && pos + 1 < n) {
            const uchar label = p[pos + 1];
            if (label == 0xF9 && pos + 7 < n && p[pos + 2] == 4) {
                // 图形控制扩展：透明标记 + 帧延迟（1/100 秒）
                const uchar packed = p[pos + 3];
                pendingDelay = qint64(le16(p + pos + 4)) * 10;
                pendingTransparent = packed & 0x01;
                pendingDisposal = (packed >> 2) & 0x07;
            }
            pos = skipSubBlocks(pos + 2);
            if (pos < 0)
                break;
            continue;
        }
        if (marker == 0x2C && pos + 10 <= n) {
            const uchar packed = p[pos + 9];
            if (info.frames == 0) {
                const bool coversCanvas = le16(p + pos + 1) == 0 && le16(p + pos + 3) == 0
                    && le16(p + pos + 5) >= info.width && le16(p + pos + 7) >= info.height;
                if (pendingTransparent || !coversCanvas)
                    info.hasAlpha = true;
            }
            if (pendingDisposal == 2)
                info.hasAlpha = true;
            pos += 10;
            if (packed & 0x80)
                pos += 3 * (qsizetype(1) << ((packed & 0x07) + 1)); // 局部调色板
            pos += 1;                                                 // LZW 最小码长
            pos = skipSubBlocks(pos);
            ++info.frames;
            info.durationMs += orDefault(pendingDelay);
            pendingDelay = -1;
            pendingTransparent = false;
            pendingDisposal = 0;
            if (pos < 0)
                break;
            continue;
        }
        break; // 无法识别的块：截断或损坏，按已读到的为准
    }
    info.parsed = info.frames > 0;
    info.animated = info.frames > 1;
    if (!info.animated)
        info.durationMs = 0;
    return info;
}

Info scanPng(const QByteArray& data)
{
    Info info;
    const auto* p = reinterpret_cast<const uchar*>(data.constData());
    const qsizetype n = data.size();
    if (n < 33 || !data.startsWith("\x89PNG\r\n\x1a\n"))
        return info;
    int declaredFrames = 0;
    qsizetype pos = 8;
    while (pos + 8 <= n) {
        const quint32 length = be32(p + pos);
        const QByteArray type = data.mid(pos + 4, 4);
        const qsizetype body = pos + 8;
        if (body + qsizetype(length) > n)
            break;
        const uchar* d = p + body;
        if (type == "IHDR" && length >= 13) {
            info.width = int(be32(d));
            info.height = int(be32(d + 4));
            const uchar colorType = d[9];
            if (colorType == 4 || colorType == 6) // 灰度 + Alpha / RGBA
                info.hasAlpha = true;
            info.parsed = true;
        } else if (type == "tRNS") {
            info.hasAlpha = true; // 调色板透明或单色透明
        } else if (type == "acTL" && length >= 8) {
            declaredFrames = int(be32(d));
        } else if (type == "fcTL" && length >= 26) {
            const quint16 num = be16(d + 20);
            quint16 den = be16(d + 22);
            if (den == 0)
                den = 100;
            ++info.frames;
            info.durationMs += orDefault(qint64(num) * 1000 / den);
        } else if (type == "IEND") {
            break;
        }
        pos = body + qsizetype(length) + 4; // + CRC
    }
    info.animated = declaredFrames > 1 && info.frames > 1;
    if (!info.animated) {
        info.frames = info.parsed ? 1 : 0;
        info.durationMs = 0;
    }
    return info;
}

Info scanWebp(const QByteArray& data)
{
    Info info;
    const auto* p = reinterpret_cast<const uchar*>(data.constData());
    const qsizetype n = data.size();
    if (n < 20 || !data.startsWith("RIFF") || data.mid(8, 4) != "WEBP")
        return info;
    qsizetype pos = 12;
    while (pos + 8 <= n) {
        const QByteArray type = data.mid(pos, 4);
        const quint32 size = le32(p + pos + 4);
        const qsizetype body = pos + 8;
        const uchar* d = p + body;
        const qsizetype available = std::min<qsizetype>(size, n - body);
        if (type == "VP8X" && available >= 10) {
            if (d[0] & 0x10)
                info.hasAlpha = true;
            if (d[0] & 0x02)
                info.animated = true;
            info.width = int(le24(d + 4)) + 1;
            info.height = int(le24(d + 7)) + 1;
            info.parsed = true;
        } else if (type == "ANMF" && available >= 16) {
            ++info.frames;
            info.durationMs += orDefault(le24(d + 12));
        } else if (type == "ALPH") {
            info.hasAlpha = true;
        } else if (type == "VP8L" && available >= 5 && d[0] == 0x2F) {
            const quint32 bits = le32(d + 1);
            if (!info.parsed) {
                info.width = int(bits & 0x3FFF) + 1;
                info.height = int((bits >> 14) & 0x3FFF) + 1;
                info.parsed = true;
            }
            if ((bits >> 28) & 1)
                info.hasAlpha = true;
        } else if (type == "VP8 " && available >= 10 && !info.parsed) {
            info.width = le16(d + 6) & 0x3FFF;
            info.height = le16(d + 8) & 0x3FFF;
            info.parsed = true;
        }
        pos = body + qsizetype(size) + (size & 1); // 块按偶数字节对齐
    }
    if (info.animated && info.frames <= 1)
        info.animated = false;
    if (!info.animated) {
        info.frames = info.parsed ? 1 : 0;
        info.durationMs = 0;
    }
    return info;
}

Info scan(const QByteArray& data)
{
    if (data.startsWith("GIF8"))
        return scanGif(data);
    if (data.startsWith("\x89PNG"))
        return scanPng(data);
    if (data.startsWith("RIFF"))
        return scanWebp(data);
    return {};
}

Info scanFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray magic = file.peek(16);
    if (!magic.startsWith("GIF8") && !magic.startsWith("\x89PNG") && !magic.startsWith("RIFF"))
        return {};
    // 动图需要遍历所有块；文件头格式都不大，上限 256MB 防止异常文件
    if (file.size() > (qint64(256) << 20))
        return {};
    return scan(file.readAll());
}

} // namespace tsm::ImageHeaders
