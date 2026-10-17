#include "media/WebmDuration.h"

#include <QFile>
#include <QtEndian>
#include <cstring>

namespace tsm::WebmDuration {

namespace {
constexpr quint32 kEbmlHeader = 0x1A45DFA3;
constexpr quint32 kSegment = 0x18538067;
constexpr quint32 kInfo = 0x1549A966;
constexpr quint32 kTimecodeScale = 0x2AD7B1;
constexpr quint32 kDuration = 0x4489;
constexpr quint32 kCluster = 0x1F43B675;
constexpr qint64 kUnknownSize = -1;

struct Element {
    quint32 id = 0;
    qint64 dataOffset = 0;
    qint64 size = 0; // kUnknownSize 表示未知长度
};

class Reader {
public:
    explicit Reader(const QByteArray& data) : m_data(data) {}

    // EBML 变长整数。id 保留长度标记位，size 去掉标记位
    bool readVint(qint64& pos, quint64& value, int& length, bool keepMarker) const
    {
        if (pos >= m_data.size())
            return false;
        const quint8 first = quint8(m_data.at(pos));
        length = 1;
        while (length <= 8 && !(first & (0x80 >> (length - 1))))
            ++length;
        if (length > 8 || pos + length > m_data.size())
            return false;
        value = keepMarker ? first : (first & (0xFF >> length));
        for (int i = 1; i < length; ++i)
            value = (value << 8) | quint8(m_data.at(pos + i));
        pos += length;
        return true;
    }

    bool readElement(qint64& pos, Element& el) const
    {
        quint64 id = 0;
        quint64 size = 0;
        int idLen = 0;
        int sizeLen = 0;
        if (!readVint(pos, id, idLen, true) || idLen > 4 || !readVint(pos, size, sizeLen, false))
            return false;
        el.id = quint32(id);
        el.dataOffset = pos;
        const quint64 allOnes = (quint64(1) << (7 * sizeLen)) - 1;
        el.size = size == allOnes ? kUnknownSize : qint64(size);
        return true;
    }

    quint64 readUInt(qint64 offset, qint64 size) const
    {
        quint64 v = 0;
        for (qint64 i = 0; i < size && offset + i < m_data.size(); ++i)
            v = (v << 8) | quint8(m_data.at(offset + i));
        return v;
    }

    qint64 size() const { return m_data.size(); }

private:
    const QByteArray& m_data;
};

struct DurationField {
    qint64 offset = -1;
    int size = 0;
    quint64 timecodeScale = 1000000;
};

// 只需要文件开头一段：Info 总在第一个 Cluster 之前
std::optional<DurationField> locate(const QByteArray& data)
{
    const Reader r(data);
    qint64 pos = 0;
    Element el;
    if (!r.readElement(pos, el) || el.id != kEbmlHeader || el.size < 0)
        return std::nullopt;
    pos = el.dataOffset + el.size;
    if (!r.readElement(pos, el) || el.id != kSegment)
        return std::nullopt;

    qint64 cursor = el.dataOffset;
    while (cursor < r.size()) {
        Element child;
        if (!r.readElement(cursor, child) || child.id == kCluster)
            return std::nullopt;
        if (child.id == kInfo) {
            if (child.size < 0)
                return std::nullopt;
            DurationField field;
            qint64 p = child.dataOffset;
            const qint64 end = std::min(child.dataOffset + child.size, r.size());
            while (p < end) {
                Element item;
                if (!r.readElement(p, item) || item.size < 0)
                    return std::nullopt;
                if (item.id == kTimecodeScale)
                    field.timecodeScale = r.readUInt(item.dataOffset, item.size);
                else if (item.id == kDuration && (item.size == 4 || item.size == 8))
                    field = {item.dataOffset, int(item.size), field.timecodeScale};
                p = item.dataOffset + item.size;
            }
            if (field.offset < 0 || field.timecodeScale == 0 || field.offset + field.size > r.size())
                return std::nullopt;
            return field;
        }
        if (child.size < 0)
            return std::nullopt;
        cursor = child.dataOffset + child.size;
    }
    return std::nullopt;
}

QByteArray readHead(QFile& file)
{
    return file.read(1024 * 1024);
}
} // namespace

std::optional<double> read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray head = readHead(file);
    const auto field = locate(head);
    if (!field)
        return std::nullopt;
    double ticks = 0;
    if (field->size == 4) {
        const quint32 bits = qFromBigEndian<quint32>(head.constData() + field->offset);
        float f;
        std::memcpy(&f, &bits, sizeof f);
        ticks = f;
    } else {
        const quint64 bits = qFromBigEndian<quint64>(head.constData() + field->offset);
        std::memcpy(&ticks, &bits, sizeof ticks);
    }
    return ticks * double(field->timecodeScale) / 1e9;
}

bool write(const QString& path, double seconds, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray head = readHead(file);
    const auto field = locate(head);
    if (!field) {
        if (error)
            *error = QStringLiteral("未找到 WebM 时长字段");
        return false;
    }
    const double ticks = seconds * 1e9 / double(field->timecodeScale);
    char bytes[8];
    if (field->size == 4) {
        const float f = float(ticks);
        quint32 bits;
        std::memcpy(&bits, &f, sizeof bits);
        qToBigEndian(bits, bytes);
    } else {
        quint64 bits;
        std::memcpy(&bits, &ticks, sizeof bits);
        qToBigEndian(bits, bytes);
    }
    if (!file.seek(field->offset) || file.write(bytes, field->size) != field->size) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace tsm::WebmDuration
