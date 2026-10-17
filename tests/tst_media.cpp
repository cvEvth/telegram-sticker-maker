// 用 ffmpeg 现场生成小素材，验证 ffprobe 解析链路（需要 ffmpeg / ffprobe 与测试程序同目录或在 PATH）
#include "core/MediaKinds.h"
#include "media/FFmpegTools.h"
#include "media/MediaProbe.h"
#include "media/ProcessRunner.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace tsm;

class TestMedia : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString generate(const QString& name, const QStringList& args)
    {
        const QString out = m_dir.filePath(name);
        QStringList full{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y")};
        full << args << out;
        const ProcessResult r = ProcessRunner::run(FFmpegTools::ffmpegPath(), full, 60000);
        if (!r.ok())
            qWarning("ffmpeg failed for %s: %s", qPrintable(name), qPrintable(r.errorSummary()));
        return out;
    }

private slots:
    void initTestCase()
    {
        if (FFmpegTools::ffmpegPath().isEmpty() || FFmpegTools::ffprobePath().isEmpty())
            QSKIP("ffmpeg / ffprobe not found");
        QVERIFY(m_dir.isValid());
    }

    void probeMp4()
    {
        const QString path = generate(QStringLiteral("clip.mp4"),
                                      {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                       QStringLiteral("testsrc2=size=320x180:rate=30000/1001:duration=2"),
                                       QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p")});
        const ProbeResult r = MediaProbe::probe(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.info.isVideo);
        QCOMPARE(r.info.width, 320);
        QCOMPARE(r.info.height, 180);
        QCOMPARE(r.info.fps, 30.0); // 29.97 → 30
        QVERIFY(qAbs(r.info.duration - 2.0) < 0.1);
        QVERIFY(!r.info.hasAlpha);
        QCOMPARE(r.info.fileName, QStringLiteral("clip.mp4"));
    }

    void probeTransparentWebm()
    {
        // libvpx-vp9 + yuva420p：透明信息写在 ALPHA_MODE 标签里
        const QString path = generate(QStringLiteral("alpha.webm"),
                                      {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                       QStringLiteral("color=c=red@0.5:size=64x64:rate=24:duration=1,format=yuva420p"),
                                       QStringLiteral("-c:v"), QStringLiteral("libvpx-vp9"), QStringLiteral("-pix_fmt"),
                                       QStringLiteral("yuva420p")});
        const ProbeResult r = MediaProbe::probe(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.info.isVideo);
        QVERIFY(r.info.hasAlpha);
        QCOMPARE(r.info.fps, 24.0);
        QCOMPARE(r.info.videoCodec, QStringLiteral("vp9"));
    }

    void probePng()
    {
        const QString path = generate(QStringLiteral("still.png"),
                                      {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                       QStringLiteral("color=c=blue@0.3:size=800x600,format=rgba"),
                                       QStringLiteral("-frames:v"), QStringLiteral("1")});
        QCOMPARE(MediaKinds::classify(path), MediaKind::StaticImage);
        const ProbeResult r = MediaProbe::probe(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!r.info.isVideo);
        QCOMPARE(r.info.width, 800);
        QCOMPARE(r.info.height, 600);
        QVERIFY(r.info.hasAlpha);
    }

    void probeApngIsAnimated()
    {
        const QString path = generate(QStringLiteral("anim.png"),
                                      {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                       QStringLiteral("testsrc2=size=64x64:rate=10:duration=1"),
                                       QStringLiteral("-f"), QStringLiteral("apng")});
        QVERIFY(MediaKinds::isAnimatedPng(path));
        QCOMPARE(MediaKinds::classify(path), MediaKind::AnimatedImage);
        const ProbeResult r = MediaProbe::probe(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.info.isVideo);
        QVERIFY(r.info.isAnimatedImage);
    }

    void missingFile()
    {
        const ProbeResult r = MediaProbe::probe(m_dir.filePath(QStringLiteral("nope.mp4")));
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("文件不存在"));
    }
};

QTEST_GUILESS_MAIN(TestMedia)
#include "tst_media.moc"
