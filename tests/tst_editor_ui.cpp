// 截取 / 裁切窗口的交互测试：真实打开窗口，模拟按键与点击，检查片段数据。
// 设置环境变量 TSM_SNAPSHOT_DIR 时会把窗口截图保存到该目录，便于人工检查界面。
#include "app/AppSettings.h"
#include "media/FFmpegTools.h"
#include "media/MediaProbe.h"
#include "media/ProcessRunner.h"
#include "media/ProxyCache.h"
#include "media/ThumbnailService.h"
#include "ui/Theme.h"
#include "ui/editor/ClipEditorDialog.h"
#include "ui/editor/ClipListPanel.h"
#include "ui/editor/TimelineWidgets.h"
#include "ui/editor/PreviewCanvas.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace tsm;

class TestEditorUi : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<AppSettings> m_settings;
    ThumbnailService* m_thumbs = nullptr;
    ProxyCache* m_proxies = nullptr;

    QString generate(const QString& name, const QStringList& args)
    {
        const QString out = m_dir.filePath(name);
        QStringList full{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y")};
        full << args << out;
        const ProcessResult r = ProcessRunner::run(FFmpegTools::ffmpegPath(), full, 60000);
        if (!r.ok())
            qWarning("ffmpeg failed: %s", qPrintable(r.errorSummary()));
        return out;
    }

    StickerTask taskFor(const QString& path)
    {
        const ProbeResult p = MediaProbe::probe(path);
        return StickerTask::fromMedia(path, p.info);
    }

    void snapshot(QWidget* w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("TSM_SNAPSHOT_DIR");
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            w->grab().save(QDir(dir).filePath(name));
        }
    }

    QPushButton* buttonWithText(QWidget* root, const QString& prefix)
    {
        for (QPushButton* b : root->findChildren<QPushButton*>())
            if (b->text().startsWith(prefix))
                return b;
        return nullptr;
    }

    void key(QWidget* w, Qt::Key k, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::keyClick(w, k, mods);
        QTest::qWait(30);
    }

private slots:
    void initTestCase()
    {
        if (FFmpegTools::ffmpegPath().isEmpty())
            QSKIP("ffmpeg not found");
        QVERIFY(m_dir.isValid());
        m_settings = std::make_unique<AppSettings>(m_dir.filePath(QStringLiteral("settings.ini")));
        m_thumbs = new ThumbnailService(this);
        m_thumbs->setCacheDir(m_dir.filePath(QStringLiteral("thumbs")));
        m_proxies = new ProxyCache(this);
        m_proxies->setCacheDir(m_dir.filePath(QStringLiteral("proxies")));
        applyTheme(*qApp, themeModeFromString(qEnvironmentVariable("TSM_THEME", QStringLiteral("dark")))); // TSM_THEME=light 截浅色界面
    }

    void videoClipEditing()
    {
        const QString src = generate(QStringLiteral("clip.mp4"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("testsrc2=size=1280x720:rate=30:duration=8"), QStringLiteral("-f"),
                                      QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=8"),
                                      QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
                                      QStringLiteral("-q:v"), QStringLiteral("3"), QStringLiteral("-c:a"), QStringLiteral("aac")});
        StickerTask task = taskFor(src);
        QVERIFY(task.info.isVideo);

        ClipEditorDialog dialog(*m_settings, m_thumbs, m_proxies, {task}, 0);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.activateWindow();
        QTest::qWait(1500); // 等第一帧与缩略图
        snapshot(&dialog, QStringLiteral("editor_video_initial.png"));

        // → 4 次（小步长 0.5s）到 2.0s，[ 设起点
        for (int i = 0; i < 4; ++i)
            key(&dialog, Qt::Key_Right);
        key(&dialog, Qt::Key_BracketLeft);
        QVERIFY(qAbs(dialog.clips().first().startTime.value_or(-1) - 2.0) < 0.01);
        QVERIFY(dialog.clips().first().endTime.has_value()); // 原终点 8s 晚于新起点，保留

        // Shift+→ 大步长 5s 到 7.0s，] 设终点
        key(&dialog, Qt::Key_Right, Qt::ShiftModifier);
        key(&dialog, Qt::Key_BracketRight);
        QVERIFY(qAbs(*dialog.clips().first().endTime - 7.0) < 0.01);

        // 开启裁切：默认 1:1 居中最大；M 镜像时框也镜像
        QPushButton* crop = buttonWithText(&dialog, QStringLiteral("✂️ 画面裁切"));
        QVERIFY(crop);
        crop->click();
        QCOMPARE(*dialog.clips().first().crop, QRect(280, 0, 720, 720));
        dialog.clips(); // keep
        m_settings->setDefaultCropRadius(0);
        key(&dialog, Qt::Key_M);
        QVERIFY(dialog.clips().first().mirror);
        QCOMPARE(*dialog.clips().first().crop, QRect(280, 0, 720, 720)); // 居中框镜像后位置不变

        // 添加片段：从当前指针开始、终点留空、继承裁切 / 镜像
        key(&dialog, Qt::Key_Left, Qt::ShiftModifier); // 回到 2.0s
        buttonWithText(&dialog, QStringLiteral("➕ 添加片段"))->click();
        QCOMPARE(dialog.clips().size(), 2);
        const StickerTask added = dialog.clips().at(1);
        QVERIFY(qAbs(added.startTime.value_or(-1) - 2.0) < 0.01);
        QVERIFY(!added.endTime.has_value());
        QVERIFY(added.mirror);
        QCOMPARE(*added.crop, QRect(280, 0, 720, 720));
        QVERIFY(added.clipId != dialog.clips().at(0).clipId);

        // ] 在起点之前 → 拒绝
        key(&dialog, Qt::Key_Left);
        key(&dialog, Qt::Key_BracketRight);
        QVERIFY(!dialog.clips().at(1).endTime.has_value());
        key(&dialog, Qt::Key_Right, Qt::ShiftModifier);
        key(&dialog, Qt::Key_BracketRight);
        QVERIFY(dialog.clips().at(1).endTime.has_value());

        // 缩放快捷键
        key(&dialog, Qt::Key_0);
        QTest::qWait(800);
        snapshot(&dialog, QStringLiteral("editor_video_two_clips.png"));

        // 确认：两个片段都有终点 → 接受
        QTimer::singleShot(0, &dialog, [&] { buttonWithText(&dialog, QStringLiteral("✔ 确认并添加"))->click(); });
        QTest::qWait(200);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void missingEndBlocksConfirm()
    {
        const QString src = m_dir.filePath(QStringLiteral("clip.mp4"));
        StickerTask task = taskFor(src);
        task.endTime.reset();
        ClipEditorDialog dialog(*m_settings, m_thumbs, m_proxies, {task}, 0);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        // 自动关闭校验弹窗
        QTimer::singleShot(300, [] {
            if (QWidget* box = QApplication::activeModalWidget())
                if (qobject_cast<QDialog*>(box) && box->windowTitle() == QStringLiteral("提示"))
                    box->close();
        });
        buttonWithText(&dialog, QStringLiteral("✔ 确认并添加"))->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(dialog.isVisible());
    }

    // 手机竖拍视频（旋转 90°）：解析出的宽高与预览画面都按摆正后的方向
    void rotatedVideoIsUpright()
    {
        const QString raw = generate(QStringLiteral("landscape.mp4"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("testsrc2=size=320x240:rate=25:duration=2"), QStringLiteral("-pix_fmt"),
                                      QStringLiteral("yuv420p"), QStringLiteral("-c:v"), QStringLiteral("mpeg4")});
        const QString src = generate(QStringLiteral("portrait.mp4"),
                                     {QStringLiteral("-display_rotation"), QStringLiteral("90"), QStringLiteral("-i"), raw,
                                      QStringLiteral("-c"), QStringLiteral("copy")});
        const StickerTask task = taskFor(src);
        QCOMPARE(task.info.width, 240);
        QCOMPARE(task.info.height, 320);

        ClipEditorDialog dialog(*m_settings, m_thumbs, m_proxies, {task}, 0);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto* canvas = dialog.findChild<PreviewCanvas*>();
        QVERIFY(canvas);
        QTRY_VERIFY_WITH_TIMEOUT(!canvas->image().isNull(), 5000);
        QCOMPARE(canvas->image().size(), QSize(240, 320));
        snapshot(&dialog, QStringLiteral("editor_rotated.png"));
        dialog.reject();
    }

    // 动态 WebP：播放器解不了，打开时自动生成预览副本并切换过去，不报错
    void animatedWebpPreviews()
    {
        const QString src = generate(QStringLiteral("anim.webp"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("testsrc2=size=160x120:rate=10:duration=1"), QStringLiteral("-c:v"),
                                      QStringLiteral("libwebp_anim"), QStringLiteral("-loop"), QStringLiteral("0")});
        const StickerTask task = taskFor(src);
        QVERIFY(task.info.isAnimatedImage);
        ClipEditorDialog dialog(*m_settings, m_thumbs, m_proxies, {task}, 0);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto* canvas = dialog.findChild<PreviewCanvas*>();
        QTRY_VERIFY_WITH_TIMEOUT(!canvas->image().isNull(), 15000);
        QCOMPARE(canvas->image().size(), QSize(160, 120));
        for (QLabel* label : dialog.findChildren<QLabel*>())
            QVERIFY2(!label->text().contains(QStringLiteral("无法播放")), qPrintable(label->text()));
        snapshot(&dialog, QStringLiteral("editor_webp.png"));
        dialog.reject();
    }

    // 用鼠标真正点击片段卡片：选中并保持选中；点卡片里的缩略图也一样；点列表空白取消选中
    void clickingCardSelectsClip()
    {
        const QString src = generate(QStringLiteral("cards.mp4"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("testsrc2=size=320x240:rate=25:duration=6"), QStringLiteral("-c:v"),
                                      QStringLiteral("mpeg4")});
        StickerTask a = taskFor(src);
        a.startTime = 0.5;
        a.endTime = 1.5;
        a.clipGroupId = QStringLiteral("clipgrp-t");
        StickerTask b = a;
        b.id = StickerTask::newId();
        b.startTime = 3.0;
        b.endTime = 4.0;
        b.mirror = true;

        ClipEditorDialog dialog(*m_settings, m_thumbs, m_proxies, {a, b}, 0);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTest::qWait(300);
        QCOMPARE(dialog.selectedIndex(), 0);

        QList<ClipCard*> cards = dialog.findChildren<ClipCard*>();
        QCOMPARE(cards.size(), 2);
        std::sort(cards.begin(), cards.end(), [](ClipCard* x, ClipCard* y) { return x->y() < y->y(); });

        // 点第二张卡片的空白处（右下角附近，避开输入框）
        QTest::mouseClick(cards.at(1), Qt::LeftButton, {}, QPoint(cards.at(1)->width() - 6, cards.at(1)->height() - 4));
        QTest::qWait(100);
        QCOMPARE(dialog.selectedIndex(), 1);
        QVERIFY(cards.at(1)->property("selected").toBool());
        QVERIFY(!cards.at(0)->property("selected").toBool());

        // 点第一张卡片的缩略图
        QLabel* thumb = nullptr;
        for (QLabel* l : cards.at(0)->findChildren<QLabel*>())
            if (!thumb || l->width() * l->height() > thumb->width() * thumb->height())
                thumb = l;
        QVERIFY(thumb);
        QTest::mouseClick(thumb, Qt::LeftButton, {}, thumb->rect().center());
        QTest::qWait(100);
        QCOMPARE(dialog.selectedIndex(), 0);

        // 点列表空白（最后一张卡片下方）
        auto* panel = dialog.findChild<ClipListPanel*>();
        QWidget* host = cards.at(1)->parentWidget();
        const QPoint below(host->width() / 2, cards.at(1)->geometry().bottom() + 20);
        if (host->rect().contains(below)) {
            QTest::mouseClick(host, Qt::LeftButton, {}, below);
            QTest::qWait(100);
            QCOMPARE(dialog.selectedIndex(), -1);
        }
        Q_UNUSED(panel);
        snapshot(&dialog, QStringLiteral("editor_card_click.png"));
        dialog.reject();
    }

    // 播放时指针连续移动：25fps 视频每帧 40ms，指针不应按帧跳，也不往回走
    void playheadMovesSmoothly()
    {
        const QString src = generate(QStringLiteral("smooth.mp4"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("testsrc2=size=320x240:rate=25:duration=6"), QStringLiteral("-c:v"),
                                      QStringLiteral("mpeg4")});
        const StickerTask task = taskFor(src);
        m_settings->setPlaybackRate(1.0);
        ClipEditorDialog dialog(*m_settings, m_thumbs, m_proxies, {task}, 0);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTest::qWait(500);
        QPushButton* play = buttonWithText(&dialog, QStringLiteral("▶️ 播放"));
        QVERIFY(play);
        play->click();
        QTRY_VERIFY_WITH_TIMEOUT(dialog.playhead() > 0.3, 5000);

        QList<double> samples;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 1500) {
            QTest::qWait(16);
            samples << dialog.playhead();
        }
        buttonWithText(&dialog, QStringLiteral("⏸️ 暂停"))->click();

        double maxStep = 0;
        int moved = 0;
        for (qsizetype i = 1; i < samples.size(); ++i) {
            const double step = samples.at(i) - samples.at(i - 1);
            QVERIFY2(step >= -1e-9, qPrintable(QStringLiteral("往回走了 %1").arg(step)));
            maxStep = std::max(maxStep, step);
            moved += step > 1e-6;
        }
        // 按帧跳时只有约 1/2~1/3 的采样会变化、单步约 40ms；平滑后几乎每次采样都前进
        QVERIFY2(moved > samples.size() * 0.7, qPrintable(QStringLiteral("%1 / %2").arg(moved).arg(samples.size())));
        QVERIFY2(maxStep < 0.12, qPrintable(QString::number(maxStep)));
        const double total = samples.last() - samples.first();
        QVERIFY2(total > 1.0 && total < 2.0, qPrintable(QString::number(total)));
        dialog.reject();
    }

    // 点击片段卡片 → 选中并在起止之间循环；点时间轴上的片段 → 在该片段内循环；
    // 点时间轴上片段以外 → 退出循环，继续正常播放
    void clickingClipAutoLoops()
    {
        const QString src = generate(QStringLiteral("loop.mp4"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("testsrc2=size=320x240:rate=25:duration=8"), QStringLiteral("-c:v"),
                                      QStringLiteral("mpeg4")});
        StickerTask a = taskFor(src);
        a.startTime = 0.5;
        a.endTime = 1.5;
        a.clipGroupId = QStringLiteral("clipgrp-l");
        StickerTask b = a;
        b.id = StickerTask::newId();
        b.startTime = 3.0;
        b.endTime = 3.8;
        m_settings->setPlaybackRate(1.0);

        ClipEditorDialog dialog(*m_settings, m_thumbs, m_proxies, {a, b}, 0);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTest::qWait(500);
        QVERIFY(!dialog.isPlaying());

        QList<ClipCard*> cards = dialog.findChildren<ClipCard*>();
        std::sort(cards.begin(), cards.end(), [](ClipCard* x, ClipCard* y) { return x->y() < y->y(); });
        QTest::mouseClick(cards.at(1), Qt::LeftButton, {}, QPoint(cards.at(1)->width() - 6, cards.at(1)->height() - 4));
        QTRY_VERIFY_WITH_TIMEOUT(dialog.isPlaying(), 3000);
        QCOMPARE(dialog.selectedIndex(), 1);
        QVERIFY(dialog.isLooping());
        // 片段 0.8 秒，等 1.5 秒后仍在片段内（已经绕回过）
        double maxSeen = 0;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 1500) {
            QTest::qWait(20);
            const double t = dialog.playhead();
            QVERIFY2(t >= 3.0 - 0.05 && t <= 3.8 + 0.1, qPrintable(QString::number(t)));
            maxSeen = std::max(maxSeen, t);
            // 实际显示的画面不能是片段外的（终点 3.8s 及之后）
            const double shown = dialog.lastFrameTime();
            if (timer.elapsed() > 200 && shown >= 0)
                QVERIFY2(shown >= 3.0 - 0.05 && shown < 3.8, qPrintable(QStringLiteral("显示了 %1s 的画面").arg(shown)));
        }
        QVERIFY(maxSeen > 3.5);

        // 时间轴上点片段 1 的色块：改为在片段 1 内循环
        auto* track = dialog.findChild<TimelineTrack*>();
        QVERIFY(track);
        const int y = int(track->trackCenterY());
        double x = track->xAt(1.0);
        if (x < 0 || x > track->width()) {
            // 当前视窗没覆盖到 1.0s 时先恢复全长
            for (QPushButton* p : dialog.findChildren<QPushButton*>())
                if (p->text().startsWith(QStringLiteral("全长")))
                    p->click();
            QTest::qWait(50);
            x = track->xAt(1.0);
        }
        QTest::mouseClick(track, Qt::LeftButton, {}, QPoint(int(x), y));
        QTest::qWait(100);
        QCOMPARE(dialog.selectedIndex(), 0);
        QVERIFY(dialog.isLooping());
        QVERIFY(dialog.isPlaying());
        QTest::qWait(900);
        QVERIFY2(dialog.playhead() >= 0.45 && dialog.playhead() <= 1.6, qPrintable(QString::number(dialog.playhead())));

        // 点片段以外（6.0s）：退出循环，继续正常播放
        for (QPushButton* p : dialog.findChildren<QPushButton*>())
            if (p->text().startsWith(QStringLiteral("全长")))
                p->click();
        QTest::qWait(50);
        QTest::mouseClick(track, Qt::LeftButton, {}, QPoint(int(track->xAt(6.0)), y));
        QTest::qWait(100);
        QVERIFY(!dialog.isLooping());
        QVERIFY(dialog.isPlaying());
        QTest::qWait(600);
        QVERIFY2(dialog.playhead() > 6.2, qPrintable(QString::number(dialog.playhead())));
        dialog.reject();
    }

    void imageCropMode()
    {
        const QString src = generate(QStringLiteral("still.png"),
                                     {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                      QStringLiteral("testsrc2=size=800x600"), QStringLiteral("-frames:v"), QStringLiteral("1")});
        const StickerTask task = taskFor(src);
        QVERIFY(!task.info.isVideo);
        m_settings->setDefaultCropRadius(30);
        ClipEditorDialog dialog(*m_settings, m_thumbs, m_proxies, {task}, 0);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.activateWindow();
        // 图片模式默认开启裁切，使用默认圆角
        QCOMPARE(*dialog.clips().first().crop, QRect(100, 0, 600, 600));
        QCOMPARE(dialog.clips().first().cropRadius, 0.3);
        QTest::qWait(1200);
        snapshot(&dialog, QStringLiteral("editor_image.png"));
        key(&dialog, Qt::Key_M); // 图片模式 M 也有效
        QVERIFY(dialog.clips().first().mirror);
        QVERIFY(!buttonWithText(&dialog, QStringLiteral("➕ 添加片段")));
        QVERIFY(buttonWithText(&dialog, QStringLiteral("✔ 确认裁切")));
    }
};

QTEST_MAIN(TestEditorUi)
#include "tst_editor_ui.moc"
