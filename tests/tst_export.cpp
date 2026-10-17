// 导入导出（需求第 9 节）：zip 解压、清单定位、导入缓存复用、两种导出模式的端到端结果
#include "app/Exporter.h"
#include "app/ImportSource.h"
#include "app/ListLoader.h"
#include "core/StickerList.h"
#include "io/ZipReader.h"
#include "io/ZipWriter.h"
#include "media/FFmpegTools.h"
#include "media/MediaProbe.h"
#include "media/ProcessRunner.h"
#include "media/ProxyCache.h"
#include "media/SourceTrimmer.h"

#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace tsm;

Q_DECLARE_METATYPE(tsm::ExportSummary)

class TestExport : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_video;   // 4 秒 320x240 mp4
    QString m_image;   // 200x120 jpg
    QString m_png;     // 160x160 png
    QString m_proxies; // 假的预览缓存目录

    QString path(const QString& name) const { return m_dir.filePath(name); }

    bool generate(const QString& out, const QStringList& args)
    {
        QDir().mkpath(QFileInfo(out).absolutePath());
        QStringList full{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y")};
        full << args << out;
        const ProcessResult r = ProcessRunner::run(FFmpegTools::ffmpegPath(), full, 120000);
        if (!r.ok())
            qWarning("ffmpeg failed for %s: %s", qPrintable(out), qPrintable(r.errorSummary()));
        return r.ok();
    }

    static StickerTask taskFor(const QString& file)
    {
        const ProbeResult probe = MediaProbe::probe(file);
        if (!probe.ok)
            qWarning("probe failed: %s", qPrintable(probe.error));
        return StickerTask::fromMedia(file, probe.info);
    }

    static void writeFile(const QString& file, const QByteArray& data)
    {
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

    static QByteArray readFile(const QString& file)
    {
        QFile f(file);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    ExportSummary runExport(const QList<ExportJob>& jobs, const ExportSettings& settings, QHash<QString, QString>* texts = nullptr,
                            bool cancelAtStart = false)
    {
        Exporter exporter;
        QSignalSpy done(&exporter, &Exporter::finished);
        if (texts) {
            connect(&exporter, &Exporter::taskFinished, this,
                    [texts](const QString& id, TaskStatus, const QString& text, qint64) { texts->insert(id, text); });
        }
        exporter.start(jobs, settings);
        if (cancelAtStart)
            exporter.cancel();
        if (!done.wait(240000))
            return {};
        return done.first().first().value<ExportSummary>();
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<ExportSummary>();
        QVERIFY(m_dir.isValid());
        if (FFmpegTools::ffmpegPath().isEmpty() || FFmpegTools::ffprobePath().isEmpty())
            QSKIP("ffmpeg / ffprobe not found");
        m_video = path(QStringLiteral("素材/动作.mp4"));
        QVERIFY(generate(m_video, {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                   QStringLiteral("testsrc2=size=320x240:rate=25:duration=4"), QStringLiteral("-f"),
                                   QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=4"),
                                   QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-g"), QStringLiteral("5"),
                                   QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-shortest")}));
        m_image = path(QStringLiteral("素材/photo.jpg"));
        QVERIFY(generate(m_image, {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                   QStringLiteral("testsrc2=size=200x120"), QStringLiteral("-frames:v"), QStringLiteral("1")}));
        m_png = path(QStringLiteral("素材/icon.png"));
        QVERIFY(generate(m_png, {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                 QStringLiteral("testsrc2=size=160x160"), QStringLiteral("-frames:v"), QStringLiteral("1")}));
        m_proxies = path(QStringLiteral("proxies"));
        writeFile(ProxyCache::pathIn(m_proxies, QFileInfo(m_video).absoluteFilePath()), "fake proxy");
    }

    void zipRoundTripAndUnsafePaths()
    {
        const QString zip = path(QStringLiteral("rt/包.zip"));
        QDir().mkpath(QFileInfo(zip).absolutePath());
        QVERIFY(ZipWriter::writeStored(zip, {{QStringLiteral("中文/说明.txt"), {}, QByteArray("你好")},
                                             {QStringLiteral("icon.png"), m_png, {}}}));
        const QString out = path(QStringLiteral("rt/out"));
        QString error;
        QVERIFY2(ZipReader::extractAll(zip, out, &error), qPrintable(error));
        QCOMPARE(readFile(QDir(out).filePath(QStringLiteral("中文/说明.txt"))), QByteArray("你好"));
        QCOMPARE(readFile(QDir(out).filePath(QStringLiteral("icon.png"))), readFile(m_png));

        // 指向目标目录外的条目一律拒绝
        const QString evil = path(QStringLiteral("rt/evil.zip"));
        QVERIFY(ZipWriter::writeStored(evil, {{QStringLiteral("../escape.txt"), {}, QByteArray("x")}}));
        QVERIFY(!ZipReader::extractAll(evil, path(QStringLiteral("rt/evil_out")), &error));
        QVERIFY(error.contains(QStringLiteral("不安全")));
        QVERIFY(!QFileInfo::exists(path(QStringLiteral("rt/escape.txt"))));
    }

    void prepareFindsManifestsAndReportsErrors()
    {
        const QString cache = path(QStringLiteral("import_cache"));
        // 文件夹：下一级子文件夹里的 stickers.json
        const QString folder = path(QStringLiteral("out_folder"));
        writeFile(QDir(folder).filePath(QStringLiteral("TG_Stickers_1/stickers.json")),
                  R"({"stickers":[{"file":"001.webm","emoji":"😂","keywords":["a","b"],"crop_radius":0.3,"proxy":"proxies/001.webm.mp4"}]})");
        ImportSource::Prepared p = ImportSource::prepare(folder, cache);
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.list.kind, StickerList::Kind::StickersJson);
        QCOMPARE(p.list.entries.size(), 1);
        QCOMPARE(p.list.entries.first().task.keywords, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(p.list.entries.first().proxy, QStringLiteral("proxies/001.webm.mp4"));
        QVERIFY(p.baseDir.endsWith(QStringLiteral("TG_Stickers_1")));

        // 格式不对
        const QString bad = path(QStringLiteral("bad.json"));
        writeFile(bad, "{\"nope\": 1}");
        p = ImportSource::prepare(bad, cache);
        QVERIFY(!p.ok);
        QCOMPARE(p.error, QStringLiteral("列表文件格式无效"));

        // zip 里没有清单
        const QString empty = path(QStringLiteral("empty.zip"));
        QVERIFY(ZipWriter::writeStored(empty, {{QStringLiteral("readme.txt"), {}, QByteArray("x")}}));
        p = ImportSource::prepare(empty, cache);
        QVERIFY(!p.ok);
        QCOMPARE(p.error, QStringLiteral("未找到 sticker_list.json 或 stickers.json"));
    }

    void trimDecisions()
    {
        StickerTask video;
        video.inputPath = QStringLiteral("C:/a/clip.MKV");
        video.info.isVideo = true;
        video.info.duration = 10;
        QCOMPARE(SourceTrimmer::decide(video).method, SourceTrimmer::Method::CopyWhole);
        QCOMPARE(SourceTrimmer::decide(video).extension, QStringLiteral("mkv"));
        video.startTime = 1.0;
        video.endTime = 3.0;
        QCOMPARE(SourceTrimmer::decide(video).method, SourceTrimmer::Method::StreamCut);
        video.mirror = true;
        QCOMPARE(SourceTrimmer::decide(video).method, SourceTrimmer::Method::Reencode);
        QCOMPARE(SourceTrimmer::decide(video).extension, QStringLiteral("webm"));

        StickerTask image;
        image.inputPath = QStringLiteral("C:/a/pic.bmp");
        image.info.width = image.info.height = 100;
        QCOMPARE(SourceTrimmer::decide(image).method, SourceTrimmer::Method::CopyWhole);
        image.crop = QRect(0, 0, 50, 50);
        QCOMPARE(SourceTrimmer::decide(image).extension, QStringLiteral("png")); // bmp 转 png
        image.inputPath = QStringLiteral("C:/a/pic.JPG");
        QCOMPARE(SourceTrimmer::decide(image).extension, QStringLiteral("jpg"));
        image.cropRadius = 0.5;
        QCOMPARE(SourceTrimmer::decide(image).extension, QStringLiteral("png"));
    }

    void exportSourcesZipAndReimport()
    {
        StickerTask clipA = taskFor(m_video);
        clipA.emoji = QStringLiteral("😂");
        clipA.keywords = {QStringLiteral("哈哈"), QStringLiteral("笑死")};
        clipA.startTime = 0.5;
        clipA.endTime = 1.5;
        clipA.clipGroupId = QStringLiteral("clipgrp-1");
        clipA.clipId = QStringLiteral("clip-1");
        StickerTask clipB = taskFor(m_video);
        clipB.startTime = 2.0;
        clipB.endTime = 3.5;
        clipB.crop = QRect(40, 0, 240, 240);
        clipB.cropRadius = 0.3;
        clipB.mirror = true;
        clipB.clipGroupId = QStringLiteral("clipgrp-1");
        clipB.clipId = QStringLiteral("clip-2");
        StickerTask image = taskFor(m_image);
        StickerTask missing = taskFor(m_png);
        missing.inputPath = path(QStringLiteral("gone.png"));

        ExportSettings settings;
        settings.kind = ExportKind::Sources;
        settings.asZip = true;
        settings.destDir = path(QStringLiteral("bundle_out"));
        settings.proxyCacheDir = m_proxies;
        QHash<QString, QString> texts;
        const ExportSummary s = runExport({{clipA}, {clipB}, {image}, {missing}}, settings, &texts);
        QCOMPARE(s.exported, 3);
        QCOMPARE(s.missing, 1);
        QCOMPARE(texts.value(missing.id), QStringLiteral("源文件缺失"));
        QCOMPARE(s.locations.size(), 1);
        const QString zip = s.locations.first();
        QVERIFY(QFileInfo(zip).fileName().startsWith(QStringLiteral("sticker_bundle_")));
        QVERIFY(zip.endsWith(QStringLiteral(".zip")));

        // 再导入：同一个源文件只复制一次，相对路径，附带预览缓存
        const QString cache = path(QStringLiteral("import_cache"));
        const ImportSource::Prepared p = ImportSource::prepare(zip, cache);
        QVERIFY2(p.ok, qPrintable(p.error));
        QVERIFY(!p.reusedExtraction);
        QCOMPARE(p.list.kind, StickerList::Kind::StickerList);
        QCOMPARE(p.list.entries.size(), 3);
        QVERIFY(QFileInfo::exists(QDir(p.baseDir).filePath(QStringLiteral("sticker_list.json"))));
        QCOMPARE(QDir(QDir(p.baseDir).filePath(QStringLiteral("sources"))).entryList(QDir::Files).size(), 2);
        QCOMPARE(p.list.entries.at(0).task.inputPath, QStringLiteral("sources/动作.mp4"));
        QCOMPARE(p.list.entries.at(0).proxy, QStringLiteral("proxies/动作.mp4.mp4"));
        QVERIFY(p.list.entries.at(2).proxy.isEmpty());

        const QString json = QString::fromUtf8(readFile(QDir(p.baseDir).filePath(QStringLiteral("sticker_list.json"))));
        QVERIFY(json.contains(QStringLiteral("\"export_mode\": \"sources\"")));
        QVERIFY(json.contains(QStringLiteral("\"exported_at\"")));

        const ListLoader::Result a = ListLoader::load(p.list.entries.at(0), p.baseDir);
        QCOMPARE(a.outcome, ListLoader::Outcome::Loaded);
        QVERIFY(a.task.inputPath.startsWith(QDir::cleanPath(cache)));
        QCOMPARE(a.task.emoji, QStringLiteral("😂"));
        QCOMPARE(a.task.keywords, clipA.keywords);
        QCOMPARE(*a.task.startTime, 0.5);
        QCOMPARE(a.task.clipGroupId, QStringLiteral("clipgrp-1"));
        const ListLoader::Result b = ListLoader::load(p.list.entries.at(1), p.baseDir);
        QCOMPARE(b.outcome, ListLoader::Outcome::Loaded);
        QCOMPARE(*b.task.crop, QRect(40, 0, 240, 240));
        QCOMPARE(b.task.cropRadius, 0.3);
        QVERIFY(b.task.mirror);
        QCOMPARE(b.task.clipId, QStringLiteral("clip-2"));

        // 预览缓存装回缓存目录（按导入后的文件标识）
        const QString newCache = path(QStringLiteral("proxies_after_import"));
        const QString proxyFile = ListLoader::resolvePath(p.list.entries.at(0).proxy, p.baseDir, {});
        QVERIFY(ImportSource::installProxy(proxyFile, a.task.inputPath, newCache));
        QCOMPARE(readFile(ProxyCache::pathIn(newCache, a.task.inputPath)), QByteArray("fake proxy"));
        QVERIFY(!ImportSource::installProxy(proxyFile, a.task.inputPath, newCache)); // 已有则跳过

        // 同一个 zip 再次导入：直接复用，不重复解压
        const QDateTime extractedAt = QFileInfo(QDir(p.baseDir).filePath(QStringLiteral("sticker_list.json"))).lastModified();
        QTest::qWait(20);
        const ImportSource::Prepared again = ImportSource::prepare(zip, cache);
        QVERIFY(again.ok);
        QVERIFY(again.reusedExtraction);
        QCOMPARE(QFileInfo(QDir(again.baseDir).filePath(QStringLiteral("sticker_list.json"))).lastModified(), extractedAt);
    }

    void exportSourcesFolder()
    {
        StickerTask video = taskFor(m_video);
        StickerTask image = taskFor(m_image);
        ExportSettings settings;
        settings.kind = ExportKind::Sources;
        settings.asZip = false;
        settings.destDir = path(QStringLiteral("bundle_parent"));
        settings.proxyCacheDir = m_proxies;
        const ExportSummary s = runExport({{video}, {image}}, settings);
        QCOMPARE(s.exported, 2);
        const QDir folder(s.locations.value(0));
        QVERIFY(folder.dirName().startsWith(QStringLiteral("sticker_bundle_")));
        QVERIFY(folder.exists(QStringLiteral("sticker_list.json")));
        QVERIFY(folder.exists(QStringLiteral("sources/动作.mp4")));
        QVERIFY(folder.exists(QStringLiteral("sources/photo.jpg")));
        QVERIFY(folder.exists(QStringLiteral("proxies/动作.mp4.mp4")));
        // 文件夹形式也能直接导入
        const ImportSource::Prepared p = ImportSource::prepare(folder.absolutePath(), path(QStringLiteral("import_cache")));
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(ListLoader::load(p.list.entries.at(1), p.baseDir).outcome, ListLoader::Outcome::Loaded);
    }

    void exportTrimmedFolder()
    {
        StickerTask cut = taskFor(m_video); // 只截取 → 原扩展名直接剪切
        cut.emoji = QStringLiteral("😂");
        cut.startTime = 1.0;
        cut.endTime = 2.5;
        StickerTask shaped = taskFor(m_video); // 圆角 + 镜像 → 高画质 WebM（透明）
        shaped.startTime = 0.0;
        shaped.endTime = 1.0;
        shaped.crop = QRect(41, 0, 241, 240);
        shaped.cropRadius = 1.0;
        shaped.mirror = true;
        StickerTask whole = taskFor(m_video);  // 整段 → 原样复制 + 预览缓存
        StickerTask plain = taskFor(m_png);    // 图片无改动 → 原样复制
        StickerTask cropped = taskFor(m_image); // 只裁切的 jpg → jpg
        cropped.crop = QRect(10, 10, 100, 80);
        StickerTask round = taskFor(m_image);  // 有圆角 → png
        round.cropRadius = 0.5;

        const QString target = path(QStringLiteral("trim_out"));
        QList<ExportJob> jobs;
        int index = 0;
        for (const StickerTask& t : {cut, shaped, whole, plain, cropped, round}) {
            ++index;
            jobs << ExportJob{t, QStringLiteral("%1.webm").arg(index, 3, 10, QLatin1Char('0')), target};
        }
        jobs[0].fileName = QStringLiteral("001_😂.webm");

        ExportSettings settings;
        settings.kind = ExportKind::Trimmed;
        settings.asZip = false;
        settings.destDir = path(QStringLiteral("unused_zip_dir"));
        settings.workRoot = path(QStringLiteral("work"));
        settings.proxyCacheDir = m_proxies;
        QHash<QString, QString> texts;
        const ExportSummary s = runExport(jobs, settings, &texts);
        QCOMPARE(s.failed, 0);
        QCOMPARE(s.exported, 6);
        const QDir folder(s.locations.value(0));
        QVERIFY2(folder.dirName().startsWith(QStringLiteral("TG_Trimmed_")), qPrintable(folder.path()));
        QCOMPARE(QDir(folder.absoluteFilePath(QStringLiteral(".."))).absolutePath(), QDir(target).absolutePath());

        const ProbeResult cutProbe = MediaProbe::probe(folder.filePath(QStringLiteral("001_😂.mp4")));
        QVERIFY2(cutProbe.ok, qPrintable(cutProbe.error));
        QVERIFY2(cutProbe.info.duration > 1.0 && cutProbe.info.duration < 2.6, qPrintable(QString::number(cutProbe.info.duration)));
        QCOMPARE(cutProbe.info.width, 320); // 未重新编码

        const ProbeResult shapedProbe = MediaProbe::probe(folder.filePath(QStringLiteral("002.webm")));
        QVERIFY2(shapedProbe.ok, qPrintable(shapedProbe.error));
        QVERIFY(shapedProbe.info.hasAlpha);
        QCOMPARE(shapedProbe.info.width, 240); // 裁切框向下取偶数
        QCOMPARE(shapedProbe.info.height, 240);
        QVERIFY(shapedProbe.info.duration < 1.3);

        QCOMPARE(readFile(folder.filePath(QStringLiteral("003.mp4"))), readFile(m_video));
        QVERIFY(folder.exists(QStringLiteral("proxies/003.mp4.mp4")));
        QCOMPARE(readFile(folder.filePath(QStringLiteral("004.png"))), readFile(m_png));
        QCOMPARE(QImage(folder.filePath(QStringLiteral("005.jpg"))).size(), QSize(100, 80));
        const QImage rounded(folder.filePath(QStringLiteral("006.png")));
        QCOMPARE(rounded.size(), QSize(200, 120));
        QVERIFY(rounded.hasAlphaChannel());
        QCOMPARE(qAlpha(rounded.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(rounded.pixel(100, 60)), 255);
        QCOMPARE(texts.value(whole.id), QStringLiteral("已导出（整段复制）"));

        // stickers.json：按实际文件名，整段视频带预览缓存
        const QJsonArray stickers =
            QJsonDocument::fromJson(readFile(folder.filePath(QStringLiteral("stickers.json")))).object().value(u"stickers").toArray();
        QCOMPARE(stickers.size(), 6);
        QCOMPARE(stickers.at(0).toObject().value(u"file").toString(), QStringLiteral("001_😂.mp4"));
        QCOMPARE(stickers.at(1).toObject().value(u"shape").toString(), QStringLiteral("圆形"));
        QCOMPARE(stickers.at(2).toObject().value(u"proxy").toString(), QStringLiteral("proxies/003.mp4.mp4"));
        // 源文件不被移动
        QVERIFY(QFileInfo::exists(m_video));
        QVERIFY(QFileInfo::exists(m_png));
        // 临时目录已清理
        QCOMPARE(QDir(settings.workRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    }

    void exportTrimmedZip()
    {
        StickerTask image = taskFor(m_png);
        image.mirror = true;
        ExportSettings settings;
        settings.kind = ExportKind::Trimmed;
        settings.asZip = true;
        settings.destDir = path(QStringLiteral("trim_zip"));
        settings.workRoot = path(QStringLiteral("work"));
        const ExportSummary s = runExport({{image, QStringLiteral("001.webp"), path(QStringLiteral("ignored"))}}, settings);
        QCOMPARE(s.exported, 1);
        QVERIFY(QFileInfo(s.locations.value(0)).fileName().startsWith(QStringLiteral("TG_Trimmed_")));
        const QString out = path(QStringLiteral("trim_zip/x"));
        QVERIFY(ZipReader::extractAll(s.locations.value(0), out));
        QVERIFY(QFileInfo::exists(QDir(out).filePath(QStringLiteral("001.png"))));
        QVERIFY(QFileInfo::exists(QDir(out).filePath(QStringLiteral("stickers.json"))));
        QVERIFY(!QFileInfo::exists(path(QStringLiteral("ignored"))));
    }

    void cancelMarksRemaining()
    {
        StickerTask a = taskFor(m_video);
        a.mirror = true; // 需要编码，足够慢
        StickerTask b = taskFor(m_image);
        ExportSettings settings;
        settings.kind = ExportKind::Trimmed;
        settings.asZip = false;
        settings.destDir = path(QStringLiteral("cancel"));
        settings.workRoot = path(QStringLiteral("work"));
        QHash<QString, QString> texts;
        const ExportSummary s = runExport({{a, QStringLiteral("001.webm"), path(QStringLiteral("cancel"))},
                                           {b, QStringLiteral("002.webp"), path(QStringLiteral("cancel"))}},
                                          settings, &texts, true);
        QCOMPARE(s.exported, 0);
        QCOMPARE(s.cancelled, 2);
        QCOMPARE(texts.value(b.id), QStringLiteral("已取消"));
        QVERIFY(s.locations.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestExport)
#include "tst_export.moc"
