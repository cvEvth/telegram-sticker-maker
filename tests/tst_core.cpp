#include "app/AppPaths.h"
#include "app/AppSettings.h"
#include "app/ListLoader.h"
#include "core/StickerList.h"
#include "core/Format.h"
#include "core/Keywords.h"
#include "core/OutputNaming.h"
#include "core/StickerTask.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

    void keywordSplit()
    {
        QCOMPARE(Keywords::split(QStringLiteral("哈哈, 笑死，绝了、好耶;ok；  yes\tno")),
                 (QStringList{QStringLiteral("哈哈"), QStringLiteral("笑死"), QStringLiteral("绝了"), QStringLiteral("好耶"),
                              QStringLiteral("ok"), QStringLiteral("yes"), QStringLiteral("no")}));
        QVERIFY(Keywords::split(QStringLiteral(" ,， ")).isEmpty());
        QCOMPARE(Keywords::charCount(QStringLiteral("😂a")), 2); // Emoji 算一个字符
    }

    void keywordAddRules()
    {
        QStringList list;
        Keywords::AddResult r = Keywords::add(list, QStringLiteral("哈哈 笑死"));
        QCOMPARE(r.added.size(), 2);
        QVERIFY(r.error.isEmpty());

        r = Keywords::add(list, QStringLiteral("哈哈，新词"));
        QCOMPARE(r.added, QStringList{QStringLiteral("新词")});
        QCOMPARE(r.error, QStringLiteral("「哈哈」已存在"));

        // 字符上限：已用 6，剩 58
        QStringList longList{QString(60, u'a')};
        r = Keywords::add(longList, QStringLiteral("bbbbb cc"));
        QCOMPARE(r.error, QStringLiteral("「bbbbb」需要 5 字符，还剩 4"));
        QCOMPARE(r.added, QStringList{QStringLiteral("cc")}); // 更短的还能放下
        r = Keywords::add(longList, QStringLiteral("dd ee"));
        QCOMPARE(r.added, QStringList{QStringLiteral("dd")});
        QCOMPARE(r.error, QStringLiteral("总长度已满 64 字符"));
        QVERIFY(Keywords::isFull(longList));

        // 数量上限
        QStringList many;
        for (int i = 0; i < 20; ++i)
            many << QString::number(i);
        r = Keywords::add(many, QStringLiteral("x"));
        QVERIFY(r.added.isEmpty());
        QCOMPARE(r.error, QStringLiteral("最多 20 个关键词"));
        QVERIFY(Keywords::isFull(many));
    }

    void stickerListRoundTrip()
    {
        StickerTask video;
        video.inputPath = QStringLiteral("C:/media/clip.mp4");
        video.info.fileName = QStringLiteral("clip.mp4");
        video.info.isVideo = true;
        video.info.width = 1920;
        video.info.height = 1080;
        video.emoji = QStringLiteral("😂");
        video.keywords = {QStringLiteral("哈哈"), QStringLiteral("笑死")};
        video.startTime = 1.2;
        video.endTime = 3.7;
        video.crop = QRect(100, 0, 512, 512);
        video.cropRadius = 0.3;
        video.mirror = true;
        video.clipGroupId = QStringLiteral("clipgrp-3");
        video.clipId = QStringLiteral("clip-1700000000000");

        StickerTask still;
        still.inputPath = QStringLiteral("C:/media/a.png");
        still.info.fileName = QStringLiteral("a.png");

        StickerList::WriteOptions options;
        options.exportMode = QStringLiteral("sources");
        options.exportedAt = QDateTime(QDate(2026, 10, 17), QTime(14, 30));
        options.pathFor = [](const StickerTask& t) { return QStringLiteral("sources/") + t.info.fileName; };
        const QByteArray json = StickerList::write({video, still}, options);

        // 格式（需求 9.3）
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        QCOMPARE(root.value(u"kind").toString(), QStringLiteral("sticker_maker_list"));
        QCOMPARE(root.value(u"version").toInt(), 1);
        QCOMPARE(root.value(u"export_mode").toString(), QStringLiteral("sources"));
        QCOMPARE(root.value(u"exported_at").toString(), QStringLiteral("2026-10-17T14:30:00"));
        const QJsonObject first = root.value(u"stickers").toArray().at(0).toObject();
        QCOMPARE(first.value(u"input_path").toString(), QStringLiteral("sources/clip.mp4"));
        QCOMPARE(first.value(u"keywords").toString(), QStringLiteral("哈哈, 笑死"));
        QCOMPARE(first.value(u"crop").toArray(), (QJsonArray{100, 0, 512, 512}));
        QCOMPARE(first.value(u"clip_label").toString(), QStringLiteral("[1.200s - 3.700s]"));
        // 空值 / 默认值的可选字段省略
        const QJsonObject second = root.value(u"stickers").toArray().at(1).toObject();
        for (const char* key : {"emoji", "keywords", "start_time", "crop", "crop_radius", "mirror", "clip_group_id", "media"})
            QVERIFY2(!second.contains(QLatin1StringView(key)), key);

        const StickerList::ParseResult parsed = StickerList::parse(json);
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.kind, StickerList::Kind::StickerList);
        QCOMPARE(parsed.entries.size(), 2);
        const StickerTask& t = parsed.entries.at(0).task;
        QCOMPARE(t.inputPath, QStringLiteral("sources/clip.mp4"));
        QCOMPARE(t.emoji, video.emoji);
        QCOMPARE(t.keywords, video.keywords);
        QCOMPARE(*t.startTime, 1.2);
        QCOMPARE(*t.endTime, 3.7);
        QCOMPARE(*t.crop, *video.crop);
        QCOMPARE(t.cropRadius, 0.3);
        QVERIFY(t.mirror);
        QCOMPARE(t.clipGroupId, video.clipGroupId);
        QCOMPARE(t.clipId, video.clipId);
        QVERIFY(!parsed.entries.at(1).task.startTime.has_value());
    }

    void stickerListParsesOutputJsonAndRejectsGarbage()
    {
        const QByteArray out = R"({"stickers":[{"file":"001_😂.webm","emoji":"😂","keywords":["哈哈","笑死"],"crop_radius":0.3,"shape":"圆角 30%"}]})";
        const StickerList::ParseResult parsed = StickerList::parse(out);
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.kind, StickerList::Kind::StickersJson);
        QCOMPARE(parsed.entries.first().task.inputPath, QStringLiteral("001_😂.webm"));
        QCOMPARE(parsed.entries.first().task.keywords, (QStringList{QStringLiteral("哈哈"), QStringLiteral("笑死")}));
        QCOMPARE(parsed.entries.first().task.cropRadius, 0.3);

        for (const QByteArray& bad : {QByteArray("not json"), QByteArray("[]"), QByteArray(R"({"kind":"x"})")}) {
            const StickerList::ParseResult r = StickerList::parse(bad);
            QVERIFY(!r.ok);
            QCOMPARE(r.error, QStringLiteral("列表文件格式无效"));
        }
    }

    void resolvePathOrder()
    {
        QTemporaryDir tmp;
        QDir dir(tmp.path());
        QVERIFY(dir.mkpath(QStringLiteral("sources")));
        const auto touch = [](const QString& p) {
            QFile f(p);
            return f.open(QIODevice::WriteOnly);
        };
        const QString abs = dir.filePath(QStringLiteral("abs.mp4"));
        const QString rel = dir.filePath(QStringLiteral("sources/rel.mp4"));
        const QString flat = dir.filePath(QStringLiteral("flat.mp4"));
        QVERIFY(touch(abs) && touch(rel) && touch(flat));

        // 1. 绝对路径
        QCOMPARE(ListLoader::resolvePath(abs, QStringLiteral("D:/elsewhere"), QString()), abs);
        // 2. 清单所在目录的相对路径
        QCOMPARE(ListLoader::resolvePath(QStringLiteral("sources/rel.mp4"), tmp.path(), QString()), rel);
        // 3. 清单同目录按文件名
        QCOMPARE(ListLoader::resolvePath(QStringLiteral("D:/old/machine/flat.mp4"), tmp.path(), QStringLiteral("flat.mp4")), flat);
        // 都找不到
        QVERIFY(ListLoader::resolvePath(QStringLiteral("D:/gone/x.mp4"), tmp.path(), QStringLiteral("x.mp4")).isEmpty());
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

    void legacyEncodePresetMigrates()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QList<std::pair<int, EncodePreset>> cases{
            {0, EncodePreset::Quality}, // 动漫
            {1, EncodePreset::Quality}, // 实拍
            {2, EncodePreset::Fast},
        };
        for (const auto& [legacy, expected] : cases) {
            const QString ini = QDir(tmp.path()).filePath(QStringLiteral("legacy_%1.ini").arg(legacy));
            {
                QSettings old(ini, QSettings::IniFormat);
                old.setValue(QStringLiteral("output/preset"), legacy);
            }
            {
                AppSettings settings(ini);
                QCOMPARE(settings.encodePreset(), expected);
            }
            QSettings after(ini, QSettings::IniFormat);
            QVERIFY(!after.contains(QStringLiteral("output/preset")));
            QCOMPARE(AppSettings(ini).encodePreset(), expected);
        }

        AppSettings fresh(QDir(tmp.path()).filePath(QStringLiteral("fresh.ini")));
        QCOMPARE(fresh.encodePreset(), EncodePreset::Quality);
        fresh.setEncodePreset(EncodePreset::Fast);
        QCOMPARE(fresh.encodePreset(), EncodePreset::Fast);
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
