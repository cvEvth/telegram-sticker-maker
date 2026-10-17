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

#include <QApplication>
#include <QDir>
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
        applyDarkTheme(*qApp);
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
