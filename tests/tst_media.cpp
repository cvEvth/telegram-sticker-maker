// 用 ffmpeg 现场生成小素材，验证 ffprobe 解析链路（需要 ffmpeg / ffprobe 与测试程序同目录或在 PATH）
#include "core/ImageHeaders.h"
#include "core/MediaKinds.h"
#include "media/FFmpegTools.h"
#include "media/MediaProbe.h"
#include "media/ProcessRunner.h"
#include "media/SourceInput.h"

#include <QDir>
#include <QFile>
#include <QtEndian>
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
        // 时长由逐帧时长累加：10 帧 × 100ms
        QVERIFY2(qAbs(r.info.duration - 1.0) < 0.01, qPrintable(QString::number(r.info.duration)));
        QCOMPARE(r.info.fps, 10.0);
    }

    // 手工拼的 GIF：第 1 帧延迟 0（按 100ms），第 2 帧 50cs 且带调色板透明
    void gifHeaderDelaysAndTransparency()
    {
        QByteArray gif = "GIF89a";
        gif += QByteArray::fromHex("0100010080"); // 1x1，全局调色板 2 色
        gif += QByteArray::fromHex("0000");       // 背景色、像素比
        gif += QByteArray::fromHex("000000ffffff");
        // 首帧透明 → 透明；后续帧透明但不恢复背景（只是"保留上一帧"）→ 不透明；恢复背景 → 透明
        const auto frame = [](quint16 delayCs, bool transparent, int disposal = 1) {
            QByteArray f = QByteArray::fromHex("21f904");
            f += char((transparent ? 0x01 : 0x00) | (disposal << 2));
            f += char(delayCs & 0xff);
            f += char(delayCs >> 8);
            f += QByteArray::fromHex("0000");                 // 透明色索引、结束
            f += QByteArray::fromHex("2c000000000100010000"); // 图像描述
            f += QByteArray::fromHex("0202440100");           // LZW 数据
            return f;
        };
        QByteArray opaque = gif + frame(0, false) + frame(50, false) + ";";
        ImageHeaders::Info info = ImageHeaders::scan(opaque);
        QVERIFY(info.parsed);
        QVERIFY(info.animated);
        QCOMPARE(info.frames, 2);
        QCOMPARE(info.durationMs, 600);
        QVERIFY(!info.hasAlpha);
        info = ImageHeaders::scan(gif + frame(0, false) + frame(50, true) + ";");
        QVERIFY(!info.hasAlpha);
        info = ImageHeaders::scan(gif + frame(0, true) + frame(50, false) + ";");
        QVERIFY(info.hasAlpha);
        info = ImageHeaders::scan(gif + frame(0, false, 2) + frame(50, true) + ";");
        QVERIFY(info.hasAlpha);
    }

    // 真实文件：FFmpeg 的 GIF 解码器总是报告 bgra，透明要看调色板
    void gifTransparencyFromFile()
    {
        const QString opaque = generate(QStringLiteral("opaque.gif"),
                                        {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                         QStringLiteral("testsrc2=size=64x64:rate=5:duration=1")});
        ProbeResult r = MediaProbe::probe(opaque);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!r.info.hasAlpha);
        QVERIFY(qAbs(r.info.duration - 1.0) < 0.01);
        QCOMPARE(r.info.fps, 5.0);

        const QString transparent = generate(
            QStringLiteral("transparent.gif"),
            {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
             QStringLiteral("color=c=black@0:size=64x64:rate=5:duration=1,format=rgba[bg];"
                            "testsrc2=size=32x32:rate=5:duration=1[fg];[bg][fg]overlay=16:16,split[a][b];"
                            "[a]palettegen=reserve_transparent=1[p];[b][p]paletteuse=alpha_threshold=128")});
        r = MediaProbe::probe(transparent);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.info.hasAlpha);
    }

    void pngPaletteTransparency()
    {
        const auto chunk = [](const QByteArray& type, const QByteArray& data) {
            QByteArray c(4, 0);
            qToBigEndian(quint32(data.size()), c.data());
            return c + type + data + QByteArray(4, 0);
        };
        QByteArray ihdr(13, 0);
        qToBigEndian(quint32(10), ihdr.data());
        qToBigEndian(quint32(20), ihdr.data() + 4);
        ihdr[8] = 8;
        ihdr[9] = 3; // 调色板
        const QByteArray head = QByteArray("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", ihdr) + chunk("PLTE", QByteArray(6, 0));
        ImageHeaders::Info info = ImageHeaders::scan(head + chunk("IEND", {}));
        QVERIFY(info.parsed);
        QVERIFY(!info.hasAlpha);
        QCOMPARE(info.width, 10);
        info = ImageHeaders::scan(head + chunk("tRNS", QByteArray(1, 0)) + chunk("IEND", {}));
        QVERIFY(info.hasAlpha);
        QVERIFY(!info.animated);

        // 真实文件：不透明的调色板 PNG 不算透明
        const QString pal = generate(QStringLiteral("pal.png"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("testsrc2=size=64x64"), QStringLiteral("-frames:v"), QStringLiteral("1"),
                                      QStringLiteral("-pix_fmt"), QStringLiteral("pal8")});
        const ProbeResult r = MediaProbe::probe(pal);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.info.hasAlpha, ImageHeaders::scanFile(pal).hasAlpha);
    }

    // FFmpeg 不能解码动态 WebP：解析读文件头，解码走 libwebp 中间文件
    void animatedWebp()
    {
        SourceInput::setCacheDir(m_dir.filePath(QStringLiteral("decoded")));
        const QString path = generate(QStringLiteral("anim.webp"),
                                      {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                       QStringLiteral("testsrc2=size=96x64:rate=10:duration=1,format=rgba,"
                                                      "geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='if(lt(X,48),255,0)'"),
                                       QStringLiteral("-c:v"), QStringLiteral("libwebp_anim"), QStringLiteral("-lossless"),
                                       QStringLiteral("1"), QStringLiteral("-loop"), QStringLiteral("0")});
        QCOMPARE(MediaKinds::classify(path), MediaKind::AnimatedImage);
        const ProbeResult r = MediaProbe::probe(path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.info.isAnimatedImage);
        QCOMPARE(r.info.width, 96);
        QCOMPARE(r.info.height, 64);
        QVERIFY(r.info.hasAlpha);
        QVERIFY2(qAbs(r.info.duration - 1.0) < 0.01, qPrintable(QString::number(r.info.duration)));
        QCOMPARE(r.info.fps, 10.0);

        const QString decoded = SourceInput::resolve(path);
        QVERIFY(decoded != path);
        QVERIFY(decoded.endsWith(QStringLiteral(".mkv")));
        const QDateTime made = QFileInfo(decoded).lastModified();
        QCOMPARE(SourceInput::resolve(path), decoded); // 复用
        QCOMPARE(QFileInfo(decoded).lastModified(), made);
        const ProbeResult d = MediaProbe::probe(decoded);
        QVERIFY2(d.ok, qPrintable(d.error));
        QCOMPARE(d.info.width, 96);
        QVERIFY(qAbs(d.info.duration - 1.0) < 0.15);
        QVERIFY(d.info.hasAlpha); // rgba PNG 帧
    }

    // 旋转：JPEG 的 EXIF 方向、视频的显示矩阵，宽高按摆正后的画面
    void rotationSwapsSize()
    {
        const QString jpg = generate(QStringLiteral("photo.jpg"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("testsrc2=size=320x240"), QStringLiteral("-frames:v"), QStringLiteral("1")});
        QFile in(jpg);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QByteArray data = in.readAll();
        in.close();
        // APP1 Exif：IFD0 只有 Orientation = 6（顺时针 90°）
        QByteArray tiff = QByteArray::fromHex("49492a0008000000" "0100" "120103000100000006000000" "00000000");
        const QByteArray app1 = QByteArray("Exif\0\0", 6) + tiff;
        QByteArray segment = QByteArray::fromHex("ffe1");
        segment += char((app1.size() + 2) >> 8);
        segment += char((app1.size() + 2) & 0xff);
        const QString rotated = m_dir.filePath(QStringLiteral("photo_exif.jpg"));
        QFile out(rotated);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(data.left(2) + segment + app1 + data.mid(2));
        out.close();
        ProbeResult r = MediaProbe::probe(rotated);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.info.width, 240);
        QCOMPARE(r.info.height, 320);
        QCOMPARE(MediaProbe::probe(jpg).info.width, 320);

        const QString raw = generate(QStringLiteral("land.mp4"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("testsrc2=size=320x240:rate=25:duration=1"), QStringLiteral("-c:v"),
                                      QStringLiteral("mpeg4")});
        const QString portrait = generate(QStringLiteral("portrait.mp4"),
                                          {QStringLiteral("-display_rotation"), QStringLiteral("-90"), QStringLiteral("-i"), raw,
                                           QStringLiteral("-c"), QStringLiteral("copy")});
        r = MediaProbe::probe(portrait);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.info.width, 240);
        QCOMPARE(r.info.height, 320);
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
