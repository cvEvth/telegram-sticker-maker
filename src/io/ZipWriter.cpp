#include "io/ZipWriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

bool writeStored(const QString& zipPath, const QList<Entry>& entries, QString* error, const ProgressFn& progress)
{
    for (const Entry& entry : entries) {
        if (!entry.filePath.isEmpty() && !QFileInfo(entry.filePath).isFile()) {
            if (error)
                *error = QStringLiteral("找不到 %1").arg(QDir::toNativeSeparators(entry.filePath));
            return false;
        }
    }

    zip_t* archive = openForWrite(zipPath, error);
    if (!archive)
        return false;

    // 内存数据的缓冲区要一直有效到 zip_close
    const QList<Entry> held = entries;
    for (const Entry& entry : held) {
        zip_source_t* source = nullptr;
        if (entry.filePath.isEmpty()) {
            source = zip_source_buffer(archive, entry.data.constData(), zip_uint64_t(entry.data.size()), 0);
        } else {
#ifdef Q_OS_WIN
            const std::wstring wide = entry.filePath.toStdWString();
            source = zip_source_win32w(archive, wide.c_str(), 0, -1);
#else
            const QByteArray local = QFile::encodeName(entry.filePath);
            source = zip_source_file(archive, local.constData(), 0, -1);
#endif
        }
        const QByteArray name = entry.name.toUtf8();
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

    struct State {
        const ProgressFn* fn;
        bool cancelled = false;
    } state{&progress};
    if (progress) {
        zip_register_progress_callback_with_state(
            archive, 0.01,
            [](zip_t*, double fraction, void* ud) {
                auto* st = static_cast<State*>(ud);
                if (!(*st->fn)(fraction))
                    st->cancelled = true;
            },
            nullptr, &state);
        zip_register_cancel_callback_with_state(
            archive, [](zip_t*, void* ud) { return static_cast<State*>(ud)->cancelled ? 1 : 0; }, nullptr, &state);
    }

    if (zip_close(archive) < 0) {
        if (error)
            *error = state.cancelled ? QStringLiteral("已取消") : QString::fromLocal8Bit(zip_strerror(archive));
        zip_discard(archive);
        QFile::remove(zipPath);
        return false;
    }
    return true;
}

} // namespace tsm::ZipWriter
