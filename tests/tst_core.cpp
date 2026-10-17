#include "app/AppPaths.h"
#include "core/Format.h"
#include "core/OutputNaming.h"
#include "core/StickerTask.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace tsm;

class TestCore : public QObject {
    Q_OBJECT

private slots:
    void stickerFileName()
    {
        QCOMPARE(OutputNaming::stickerFileName(3, QStringLiteral("😂"), QStringLiteral("webm")), QStringLiteral("003_😂.webm"));
        QCOMPARE(OutputNaming::stickerFileName(1, QString(), QStringLiteral("webp")), QStringLiteral("001.webp"));
        QCOMPARE(OutputNaming::stickerFileName(12, QStringLiteral("a/b"), QStringLiteral("webm")), QStringLiteral("012_ab.webm"));
        QCOMPARE(OutputNaming::stickerFileName(1000, QStringLiteral("👍"), QStringLiteral("webm")), QStringLiteral("1000_👍.webm"));
    }

    void sanitizeEmoji()
    {
        QCOMPARE(OutputNaming::sanitizeEmoji(QStringLiteral("😂<>:\"/\\|?*")), QStringLiteral("😂"));
        QCOMPARE(OutputNaming::sanitizeEmoji(QStringLiteral("  🙂\t ")), QStringLiteral("🙂"));
        QCOMPARE(OutputNaming::sanitizeEmoji(QStringLiteral("🙂.")), QStringLiteral("🙂"));
    }

    void uniqueName()
    {
        const QSet<QString> taken{QStringLiteral("001.webm"), QStringLiteral("001_2.webm")};
        QCOMPARE(OutputNaming::uniqueName(QStringLiteral("001.webm"), taken), QStringLiteral("001_3.webm"));
        QCOMPARE(OutputNaming::uniqueName(QStringLiteral("002.webm"), taken), QStringLiteral("002.webm"));
        QCOMPARE(OutputNaming::uniqueName(QStringLiteral("001.WEBM"), taken), QStringLiteral("001_3.WEBM"));
        // 无扩展名（时间戳文件夹）
        const QSet<QString> folders{QStringLiteral("tg_stickers_20261017_143000")};
        QCOMPARE(OutputNaming::uniqueName(QStringLiteral("TG_Stickers_20261017_143000"), folders),
                 QStringLiteral("TG_Stickers_20261017_143000_2"));
    }

    void timestampedName()
    {
        const QDateTime t(QDate(2026, 10, 17), QTime(14, 30, 5));
        QCOMPARE(OutputNaming::timestampedName(QStringLiteral("TG_Stickers"), t), QStringLiteral("TG_Stickers_20261017_143005"));
    }

    void formatBytes()
    {
        QCOMPARE(tsm::formatBytes(512), QStringLiteral("512 B"));
        QCOMPARE(tsm::formatBytes(187801), QStringLiteral("183.4 KB"));
        QCOMPARE(tsm::formatBytes(12897484), QStringLiteral("12.3 MB"));
    }

    void formatTimecode()
    {
        QCOMPARE(tsm::formatTimecode(1.2), QStringLiteral("00:01.200"));
        QCOMPARE(tsm::formatTimecode(83.456), QStringLiteral("01:23.456"));
        QCOMPARE(tsm::formatTimecode(3723.5), QStringLiteral("01:02:03.500"));
        QCOMPARE(tsm::formatTimecode(-1), QStringLiteral("00:00.000"));
    }

    void parseTimecode_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<double>("seconds");
        QTest::newRow("hh:mm:ss.ms") << QStringLiteral("01:23:45.678") << true << 5025.678;
        QTest::newRow("mm:ss.s") << QStringLiteral("14:23.5") << true << 863.5;
        QTest::newRow("seconds") << QStringLiteral("23.5") << true << 23.5;
        QTest::newRow("seconds suffix") << QStringLiteral("23.5s") << true << 23.5;
        QTest::newRow("integer") << QStringLiteral("863") << true << 863.0;
        QTest::newRow("padded") << QStringLiteral("  00:01.200 ") << true << 1.2;
        QTest::newRow("empty") << QString() << false << 0.0;
        QTest::newRow("text") << QStringLiteral("abc") << false << 0.0;
        QTest::newRow("too many parts") << QStringLiteral("1:2:3:4") << false << 0.0;
        QTest::newRow("negative") << QStringLiteral("-1") << false << 0.0;
        QTest::newRow("seconds >= 60") << QStringLiteral("1:75") << false << 0.0;
        QTest::newRow("fractional minutes") << QStringLiteral("1.5:10") << false << 0.0;
    }

    void parseTimecode()
    {
        QFETCH(QString, input);
        QFETCH(bool, valid);
        QFETCH(double, seconds);
        const std::optional<double> result = tsm::parseTimecode(input);
        QCOMPARE(result.has_value(), valid);
        if (valid)
            QVERIFY(qAbs(*result - seconds) < 1e-9);
    }

    void normalizeFrameRate()
    {
        QCOMPARE(tsm::normalizeFrameRate(29.97), 30.0);
        QCOMPARE(tsm::normalizeFrameRate(23.976), 24.0);
        QCOMPARE(tsm::normalizeFrameRate(25.0), 25.0);
        QCOMPARE(tsm::normalizeFrameRate(12.5), 12.5);
        QCOMPARE(tsm::normalizeFrameRate(0.0), 0.0);
        QCOMPARE(tsm::formatFps(30.0), QStringLiteral("30"));
        QCOMPARE(tsm::formatFps(12.5), QStringLiteral("12.5"));
    }

    void shapeAndCropLabels()
    {
        QCOMPARE(shapeLabel(0.0), QStringLiteral("直角"));
        QCOMPARE(shapeLabel(0.3), QStringLiteral("圆角 30%"));
        QCOMPARE(shapeLabel(1.0), QStringLiteral("圆形"));

        StickerTask task;
        QVERIFY(task.cropTag().isEmpty());
        task.crop = QRect(100, 0, 512, 512);
        task.cropRadius = 0.3;
        QCOMPARE(task.cropTag(), QStringLiteral("[✂️ 圆角 30% 512×512]"));
        task.crop.reset();
        task.cropRadius = 1.0;
        QCOMPARE(task.cropTag(), QStringLiteral("[✂️ 圆形]"));
    }

    void clipDurations()
    {
        MediaInfo info;
        info.isVideo = true;
        info.duration = 60.0;
        StickerTask whole = StickerTask::fromMedia(QStringLiteral("clip.mp4"), info);
        QVERIFY(!whole.isTrimmed());
        QCOMPARE(whole.effectiveDuration(), 60.0);
        QCOMPARE(whole.status, TaskStatus::Pending);
        QCOMPARE(whole.statusText, QStringLiteral("等待转换"));

        StickerTask clip = whole;
        clip.startTime = 1.2;
        clip.endTime = 3.7;
        QVERIFY(clip.isTrimmed());
        QVERIFY(qAbs(clip.effectiveDuration() - 2.5) < 1e-9);
        QCOMPARE(clip.clipLabel(), QStringLiteral("[1.200s - 3.700s]"));

        StickerTask unknown;
        QCOMPARE(unknown.effectiveDuration(), 3.0); // 缺失按 3 秒
    }

    void resolveDataDir()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QCOMPARE(AppPaths::resolveDataDir(tmp.path()), QDir(tmp.path()).filePath(QStringLiteral("telegram_sticker_maker")));

        // 所选路径本身已是数据目录时直接使用
        QFile session(QDir(tmp.path()).filePath(QStringLiteral("session.json")));
        QVERIFY(session.open(QIODevice::WriteOnly));
        session.close();
        QCOMPARE(AppPaths::resolveDataDir(tmp.path()), QDir(tmp.path()).absolutePath());

        QVERIFY(!AppPaths::resolveDataDir(QString()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
