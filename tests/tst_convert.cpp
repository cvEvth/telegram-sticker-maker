// 端到端转换：用 ffmpeg 现场生成素材，跑真实编码并检查输出规格
#include "io/OutputPackager.h"
#include "media/FFmpegTools.h"
#include "media/MediaProbe.h"
#include "media/ProcessRunner.h"
#include "media/StickerEncoder.h"
#include "media/WebmDuration.h"

#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace tsm;

class TestConvert : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_counter = 0;

    QString generate(const QString& name, const QStringList& args)
    {
        const QString out = m_dir.filePath(name);
        QStringList full{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y")};
        full << args << out;
        const ProcessResult r = ProcessRunner::run(FFmpegTools::ffmpegPath(), full, 120000);
        if (!r.ok())
            qWarning("ffmpeg failed for %s: %s", qPrintable(name), qPrintable(r.errorSummary()));
        return out;
    }

    StickerTask taskFor(const QString& path)
    {
        const ProbeResult probe = MediaProbe::probe(path);
        if (!probe.ok)
            qWarning("probe failed: %s", qPrintable(probe.error));
        return StickerTask::fromMedia(path, probe.info);
    }

    EncodeResult convert(const StickerTask& task, OutputMode mode, EncodePreset preset, QString* outPath,
                         QStringList* stages = nullptr)
    {
        ++m_counter;
        EncodeRequest req;
        req.task = task;
        req.mode = mode;
        req.preset = preset;
        req.workDir = m_dir.filePath(QStringLiteral("work_%1").arg(m_counter));
        req.outputFile = m_dir.filePath(QStringLiteral("result_%1.%2").arg(m_counter).arg(task.outputExtension()));
        *outPath = req.outputFile;
        StickerEncoder encoder(req, std::make_shared<std::atomic_bool>(false), [stages](int, const QString& text) {
            if (stages && (stages->isEmpty() || stages->last() != text))
                *stages << text;
        });
        return encoder.run();
    }

    QString testsrc(const QString& name, const QString& size, double duration, const QString& rate = QStringLiteral("30"))
    {
        return generate(name, {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                               QStringLiteral("testsrc2=size=%1:rate=%2:duration=%3").arg(size, rate).arg(duration),
                               QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
                               QStringLiteral("-q:v"), QStringLiteral("2")});
    }

private slots:
    void initTestCase()
    {
        if (FFmpegTools::ffmpegPath().isEmpty() || FFmpegTools::ffprobePath().isEmpty())
            QSKIP("ffmpeg / ffprobe not found");
        QVERIFY(m_dir.isValid());
    }

    void shortVideoSticker()
    {
        const QString src = testsrc(QStringLiteral("short.mp4"), QStringLiteral("1280x720"), 2.5);
        QString out;
        QStringList stages;
        const EncodeResult r = convert(taskFor(src), OutputMode::Sticker, EncodePreset::Anime, &out, &stages);
        QVERIFY2(r.ok, qPrintable(r.statusText));
        QVERIFY(r.size > 0 && r.size <= 256 * 1024);
        QVERIFY(r.statusText.startsWith(QStringLiteral("转换完成，体积: ")));
        QVERIFY(stages.contains(QStringLiteral("第一遍分析...")));

        const ProbeResult p = MediaProbe::probe(out);
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.info.videoCodec, QStringLiteral("vp9"));
        QCOMPARE(p.info.width, 512);
        QCOMPARE(p.info.height, 288);
        QVERIFY(!p.info.hasAlpha);
        QVERIFY(p.info.duration < 3.0);
    }

    void longClipIsTrimmedAndDurationPatched()
    {
        // 15 秒素材截 1.5s ~ 6.5s（5 秒）：走运动探测 + 破限改写时长
        const QString src = testsrc(QStringLiteral("long.mp4"), QStringLiteral("640x480"), 15);
        StickerTask task = taskFor(src);
        task.startTime = 1.5;
        task.endTime = 6.5;
        QString out;
        QStringList stages;
        const EncodeResult r = convert(task, OutputMode::Sticker, EncodePreset::Fast, &out, &stages);
        QVERIFY2(r.ok, qPrintable(r.statusText));
        QVERIFY(r.size <= 256 * 1024);
        QVERIFY(stages.contains(QStringLiteral("分析运动量...")));
        QVERIFY(r.statusText.contains(QStringLiteral("运动")));

        const auto header = WebmDuration::read(out);
        QVERIFY(header.has_value());
        QVERIFY(qAbs(*header - 2.99) < 0.001);

        // 帧都在：解码出的帧数 ≈ 5 秒 × 帧率
        const ProcessResult frames = ProcessRunner::run(
            FFmpegTools::ffprobePath(),
            {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-count_frames"), QStringLiteral("-select_streams"),
             QStringLiteral("v:0"), QStringLiteral("-show_entries"), QStringLiteral("stream=nb_read_frames"),
             QStringLiteral("-of"), QStringLiteral("csv=p=0"), out});
        const int count = QString::fromUtf8(frames.stdOut).trimmed().toInt();
        QVERIFY2(count >= 5 * 12, qPrintable(QString::number(count)));
    }

    void roundedEmojiHasAlpha()
    {
        const QString src = testsrc(QStringLiteral("emoji.mp4"), QStringLiteral("640x360"), 2);
        StickerTask task = taskFor(src);
        task.crop = QRect(101, 21, 301, 301); // 奇数坐标会被向下取偶数
        task.cropRadius = 1.0;
        task.mirror = true;
        QString out;
        const EncodeResult r = convert(task, OutputMode::Emoji, EncodePreset::LiveAction, &out);
        QVERIFY2(r.ok, qPrintable(r.statusText));
        QVERIFY(r.size <= 64 * 1024);
        const ProbeResult p = MediaProbe::probe(out);
        QVERIFY2(p.ok, qPrintable(p.error));
        QCOMPARE(p.info.width, 100);
        QCOMPARE(p.info.height, 100);
        QVERIFY(p.info.hasAlpha);
    }

    void transparentWebmKeepsAlpha()
    {
        const QString src = generate(QStringLiteral("alpha_src.webm"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("testsrc2=size=320x320:rate=24:duration=2,format=yuva420p,colorchannelmixer=aa=0.5"),
                                      QStringLiteral("-c:v"), QStringLiteral("libvpx-vp9"), QStringLiteral("-pix_fmt"), QStringLiteral("yuva420p")});
        StickerTask task = taskFor(src);
        QVERIFY(task.info.hasAlpha);
        task.cropRadius = 0.3;
        QString out;
        const EncodeResult r = convert(task, OutputMode::Sticker, EncodePreset::Anime, &out);
        QVERIFY2(r.ok, qPrintable(r.statusText));
        const ProbeResult p = MediaProbe::probe(out);
        QVERIFY(p.info.hasAlpha);
        QCOMPARE(p.info.width, 512);
    }

    void staticImageLossless()
    {
        const QString src = generate(QStringLiteral("still.png"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("color=c=0x3080c0:size=800x600,format=rgba"), QStringLiteral("-frames:v"), QStringLiteral("1")});
        StickerTask task = taskFor(src);
        task.cropRadius = 0.5;
        QString out;
        const EncodeResult r = convert(task, OutputMode::Sticker, EncodePreset::Anime, &out);
        QVERIFY2(r.ok, qPrintable(r.statusText));
        QVERIFY(r.statusText.startsWith(QStringLiteral("转换完成（纯无损）")));
        const QImage img(out);
        QVERIFY(!img.isNull());
        QCOMPARE(img.size(), QSize(512, 384));
        QVERIFY(img.hasAlphaChannel());
        QCOMPARE(qAlpha(img.pixel(0, 0)), 0);         // 圆角外透明
        QCOMPARE(qAlpha(img.pixel(256, 192)), 255);   // 中心不透明
    }

    void staticNoisyImageFallsBackToLossy()
    {
        // 噪声图无损必然超过 64KB，应退到近无损 / 有损
        const QString src = generate(QStringLiteral("noise.png"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("nullsrc=size=1024x1024,geq=random(1)*255:128:128"), QStringLiteral("-frames:v"), QStringLiteral("1")});
        QString out;
        const EncodeResult r = convert(taskFor(src), OutputMode::Emoji, EncodePreset::Anime, &out);
        QVERIFY2(r.ok, qPrintable(r.statusText));
        QVERIFY(r.size <= 64 * 1024);
        QCOMPARE(QImage(out).size(), QSize(100, 100));
    }

    void missingSourceFails()
    {
        StickerTask task;
        task.inputPath = m_dir.filePath(QStringLiteral("gone.mp4"));
        task.info.isVideo = true;
        QString out;
        const EncodeResult r = convert(task, OutputMode::Sticker, EncodePreset::Anime, &out);
        QVERIFY(!r.ok);
        QCOMPARE(r.statusText, QStringLiteral("源文件不存在"));
    }

    void cancelStopsEncoding()
    {
        const QString src = testsrc(QStringLiteral("cancel.mp4"), QStringLiteral("1280x720"), 6);
        EncodeRequest req;
        req.task = taskFor(src);
        req.workDir = m_dir.filePath(QStringLiteral("work_cancel"));
        req.outputFile = m_dir.filePath(QStringLiteral("cancel.webm"));
        auto cancel = std::make_shared<std::atomic_bool>(false);
        StickerEncoder encoder(req, cancel, [cancel](int percent, const QString&) {
            if (percent >= 10)
                cancel->store(true);
        });
        const EncodeResult r = encoder.run();
        QVERIFY(r.cancelled);
        QCOMPARE(r.statusText, QStringLiteral("已取消"));
    }

    void packageZipAndFolders()
    {
        const QString staged1 = m_dir.filePath(QStringLiteral("p1.webm"));
        const QString staged2 = m_dir.filePath(QStringLiteral("p2.webm"));
        for (const QString& f : {staged1, staged2}) {
            QFile file(f);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("data");
        }
        const QString outDir = m_dir.filePath(QStringLiteral("输出 目录"));
        QList<OutputPackager::Item> items{
            {QStringLiteral("a"), staged1, QStringLiteral("001_😂.webm"), outDir, QStringLiteral("😂"), {QStringLiteral("哈哈")}, 0.3},
            {QStringLiteral("b"), staged2, QStringLiteral("001_😂.webm"), outDir, QStringLiteral("😂"), {}, 0.0},
        };
        const QDateTime now(QDate(2026, 10, 17), QTime(14, 30, 0));

        const OutputPackager::Result zip = OutputPackager::package(items, true, outDir, now);
        QVERIFY2(zip.ok, qPrintable(zip.error));
        QCOMPARE(QFileInfo(zip.locations.first()).fileName(), QStringLiteral("TG_Stickers_20261017_143000.zip"));
        QVERIFY(QFileInfo(zip.locations.first()).size() > 0);

        const OutputPackager::Result folders = OutputPackager::package(items, false, outDir, now);
        QVERIFY2(folders.ok, qPrintable(folders.error));
        const QDir folder(folders.locations.first());
        QCOMPARE(folder.dirName(), QStringLiteral("TG_Stickers_20261017_143000"));
        QVERIFY(folder.exists(QStringLiteral("001_😂.webm")));
        QVERIFY(folder.exists(QStringLiteral("001_😂_2.webm")));
        QFile json(folder.filePath(QStringLiteral("stickers.json")));
        QVERIFY(json.open(QIODevice::ReadOnly));
        const QJsonArray stickers = QJsonDocument::fromJson(json.readAll()).object().value(QStringLiteral("stickers")).toArray();
        QCOMPARE(stickers.size(), 2);
        QCOMPARE(stickers.at(0).toObject().value(QStringLiteral("shape")).toString(), QStringLiteral("圆角 30%"));
        QCOMPARE(stickers.at(1).toObject().value(QStringLiteral("file")).toString(), QStringLiteral("001_😂_2.webm"));
        QCOMPARE(stickers.at(0).toObject().value(QStringLiteral("keywords")).toArray().at(0).toString(), QStringLiteral("哈哈"));
    }
};

QTEST_GUILESS_MAIN(TestConvert)
#include "tst_convert.moc"
