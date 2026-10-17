#include "io/ZipWriter.h"

#include <QFile>
#include <zip.h>

namespace tsm::ZipWriter {

namespace {
QString zipErrorText(zip_error_t* err)
{
    return QString::fromLocal8Bit(zip_error_strerror(err));
}

zip_t* openForWrite(const QString& path, QString* error)
{
    zip_error_t err;
    zip_error_init(&err);
#ifdef Q_OS_WIN
    // zip_open 的 char* 路径在 Windows 上不可靠，改用宽字符源
    const std::wstring wide = path.toStdWString();
    zip_source_t* src = zip_source_win32w_create(wide.c_str(), 0, -1, &err);
#else
    const QByteArray local = QFile::encodeName(path);
    zip_source_t* src = zip_source_file_create(local.constData(), 0, -1, &err);
#endif
    zip_t* archive = src ? zip_open_from_source(src, ZIP_CREATE | ZIP_TRUNCATE, &err) : nullptr;
    if (!archive) {
        if (src)
            zip_source_free(src);
        if (error)
            *error = zipErrorText(&err);
    }
    zip_error_fini(&err);
    return archive;
}
} // namespace

bool writeStored(const QString& zipPath, const QList<Entry>& entries, QString* error)
{
    // 先读入内存：libzip 在 zip_close 时才真正读取数据源，缓冲区必须一直有效
    QList<QByteArray> buffers;
    buffers.reserve(entries.size());
    for (const Entry& entry : entries) {
        if (entry.filePath.isEmpty()) {
            buffers << entry.data;
            continue;
        }
        QFile file(entry.filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("无法读取 %1: %2").arg(entry.filePath, file.errorString());
            return false;
        }
        buffers << file.readAll();
    }

    zip_t* archive = openForWrite(zipPath, error);
    if (!archive)
        return false;

    for (int i = 0; i < entries.size(); ++i) {
        const QByteArray& data = buffers.at(i);
        zip_source_t* source = zip_source_buffer(archive, data.constData(), zip_uint64_t(data.size()), 0);
        const QByteArray name = entries.at(i).name.toUtf8();
        const zip_int64_t index = source ? zip_file_add(archive, name.constData(), source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) : -1;
        if (index < 0) {
            if (source)
                zip_source_free(source);
            if (error)
                *error = QString::fromLocal8Bit(zip_strerror(archive));
            zip_discard(archive);
            return false;
        }
        zip_set_file_compression(archive, zip_uint64_t(index), ZIP_CM_STORE, 0);
    }

    if (zip_close(archive) < 0) {
        if (error)
            *error = QString::fromLocal8Bit(zip_strerror(archive));
        zip_discard(archive);
        QFile::remove(zipPath);
        return false;
    }
    return true;
}

} // namespace tsm::ZipWriter
