#include "io/ZipReader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <zip.h>

namespace tsm::ZipReader {

namespace {
zip_t* openForRead(const QString& path, QString* error)
{
    zip_error_t err;
    zip_error_init(&err);
#ifdef Q_OS_WIN
    const std::wstring wide = path.toStdWString();
    zip_source_t* src = zip_source_win32w_create(wide.c_str(), 0, -1, &err);
#else
    const QByteArray local = QFile::encodeName(path);
    zip_source_t* src = zip_source_file_create(local.constData(), 0, -1, &err);
#endif
    zip_t* archive = src ? zip_open_from_source(src, ZIP_RDONLY, &err) : nullptr;
    if (!archive) {
        if (src)
            zip_source_free(src);
        if (error)
            *error = QString::fromLocal8Bit(zip_error_strerror(&err));
    }
    zip_error_fini(&err);
    return archive;
}

// 包内路径 → 目标目录下的安全相对路径；不安全时返回空
QString safeRelativePath(const QString& name)
{
    QString clean = QDir::cleanPath(QString(name).replace(u'\\', u'/'));
    if (clean.isEmpty() || clean == u"." || QDir::isAbsolutePath(clean) || clean.contains(u':'))
        return {};
    for (const QString& part : clean.split(u'/')) {
        if (part == u"..")
            return {};
    }
    return clean;
}
} // namespace

bool extractAll(const QString& zipPath, const QString& destDir, QString* error,
                const std::shared_ptr<std::atomic_bool>& cancel)
{
    const auto fail = [error](const QString& text) {
        if (error)
            *error = text;
        return false;
    };

    zip_t* archive = openForRead(zipPath, error);
    if (!archive)
        return false;
    QDir().mkpath(destDir);
    const QDir root(destDir);

    const zip_int64_t count = zip_get_num_entries(archive, 0);
    QByteArray buffer(1 << 16, Qt::Uninitialized);
    for (zip_int64_t i = 0; i < count; ++i) {
        if (cancel && cancel->load()) {
            zip_discard(archive);
            return fail(QCoreApplication::translate("ZipReader", "已取消"));
        }
        const char* rawName = zip_get_name(archive, zip_uint64_t(i), ZIP_FL_ENC_GUESS);
        if (!rawName)
            continue;
        const QString name = QString::fromUtf8(rawName);
        if (name.endsWith(u'/') || name.endsWith(u'\\')) {
            if (const QString dir = safeRelativePath(name); !dir.isEmpty())
                root.mkpath(dir);
            continue;
        }
        const QString relative = safeRelativePath(name);
        if (relative.isEmpty()) {
            zip_discard(archive);
            return fail(QCoreApplication::translate("ZipReader", "压缩包含有不安全的路径: %1").arg(name));
        }
        const QString target = root.filePath(relative);
        QDir().mkpath(QFileInfo(target).absolutePath());

        zip_file_t* entry = zip_fopen_index(archive, zip_uint64_t(i), 0);
        if (!entry) {
            const QString text = QString::fromLocal8Bit(zip_strerror(archive));
            zip_discard(archive);
            return fail(QCoreApplication::translate("ZipReader", "无法读取 %1: %2").arg(name, text));
        }
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly)) {
            zip_fclose(entry);
            zip_discard(archive);
            return fail(QCoreApplication::translate("ZipReader", "无法写入 %1: %2").arg(QDir::toNativeSeparators(target), out.errorString()));
        }
        bool ok = true;
        for (;;) {
            const zip_int64_t n = zip_fread(entry, buffer.data(), zip_uint64_t(buffer.size()));
            if (n < 0) {
                ok = false;
                break;
            }
            if (n == 0)
                break;
            if (out.write(buffer.constData(), n) != n) {
                ok = false;
                break;
            }
            if (cancel && cancel->load())
                break;
        }
        zip_fclose(entry);
        out.close();
        if (!ok) {
            zip_discard(archive);
            return fail(QCoreApplication::translate("ZipReader", "解压 %1 失败").arg(name));
        }
    }
    zip_discard(archive);
    if (cancel && cancel->load())
        return fail(QCoreApplication::translate("ZipReader", "已取消"));
    return true;
}

} // namespace tsm::ZipReader
