// 生成 README 截图（docs/screenshots）：设置 TSM_README_MEDIA（素材目录，取其中的 .mp4）与
// TSM_SNAPSHOT_DIR（截图输出目录）后运行，否则跳过。会真实转换素材，耗时约半分钟。
// 输出目录与数据目录放在 TSM_README_ROOT（默认 D:/StickerDemo）下，截图里不出现个人目录；结束后删除。
#include "app/AppPaths.h"
#include "app/AppSettings.h"
#include "core/TaskListModel.h"
#include "media/ProxyCache.h"
#include "media/ThumbnailService.h"
#include "ui/ConversionDoneDialog.h"
#include "ui/MainWindow.h"
#include "ui/SettingsPanel.h"
#include "ui/TaskTableView.h"
#include "ui/Theme.h"
#include "ui/editor/ClipEditorDialog.h"

#include <QApplication>
#include <QCollator>
#include <QDir>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <tuple>

using namespace tsm;

class TestReadmeShots : public QObject {
    Q_OBJECT

private:
    QString m_media;
    QString m_out;
    QString m_root;
    QTemporaryDir m_dir;

    void save(QWidget* w, const QString& name)
    {
        QDir().mkpath(m_out);
        QVERIFY(w->grab().save(QDir(m_out).filePath(name)));
    }

private slots:
    void initTestCase()
    {
        m_media = qEnvironmentVariable("TSM_README_MEDIA");
        m_out = qEnvironmentVariable("TSM_SNAPSHOT_DIR");
        if (m_media.isEmpty() || m_out.isEmpty())
            QSKIP("set TSM_README_MEDIA and TSM_SNAPSHOT_DIR to generate README screenshots");
        m_root = qEnvironmentVariable("TSM_README_ROOT", QStringLiteral("D:/StickerDemo"));
        QVERIFY2(!QDir(m_root).exists(), qPrintable(m_root + QStringLiteral(" already exists; pick another TSM_README_ROOT")));
        QStandardPaths::setTestModeEnabled(true);
        QDir(AppPaths::configDir()).removeRecursively();
        applyDarkTheme(*qApp);
    }

    void screenshots()
    {
        QStringList files = QDir(m_media).entryList({QStringLiteral("*.mp4")}, QDir::Files);
        QCollator collator;
        collator.setNumericMode(true);
        std::sort(files.begin(), files.end(), [&](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });
        QVERIFY2(files.size() >= 4, "need at least four .mp4 files");
        for (QString& f : files)
            f = QDir(m_media).filePath(f);
        files = files.mid(0, 4);

        {
            AppSettings settings;
            settings.setOutputDir(QDir(m_root).filePath(QStringLiteral("TG_Stickers")));
            settings.setDataDirChoice(m_root);
            settings.setOutputAsZip(true);
        }

        MainWindow window;
        window.resize(1500, 820);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.openPaths(files);
        auto* table = window.findChild<TaskTableView*>();
        auto* model = qobject_cast<TaskListModel*>(table->model());
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 4, 20000);

        // 按文件名配 Emoji / 关键词；第一个文件名不认识的按顺序给默认值
        const QList<std::tuple<QString, QString, QStringList>> meta{
            {QStringLiteral("化物语"), QStringLiteral("👉"), {QStringLiteral("指"), QStringLiteral("就是你")}},
            {QStringLiteral("八九寺"), QStringLiteral("😤"), {QStringLiteral("冲"), QStringLiteral("出发")}},
            {QStringLiteral("物语"), QStringLiteral("😆"), {QStringLiteral("开心"), QStringLiteral("害羞")}},
            {QStringLiteral("斧乃木"), QStringLiteral("✌️"), {QStringLiteral("早安")}},
        };
        int focusRow = -1;
        for (int row = 0; row < 4; ++row) {
            const QString name = model->taskAt(row).info.fileName;
            int pick = row;
            for (int i = 0; i < meta.size(); ++i) {
                if (name.contains(std::get<0>(meta.at(i)))) {
                    pick = i;
                    break;
                }
            }
            if (pick == 2)
                focusRow = row;
            model->updateTask(row, [&](StickerTask& t) {
                t.emoji = std::get<1>(meta.at(pick));
                t.keywords = std::get<2>(meta.at(pick));
                if (pick == 1) { // 圆形裁切
                    const int side = std::min(t.info.width, t.info.height);
                    t.crop = QRect((t.info.width - side) / 2, (t.info.height - side) / 2, side, side);
                    t.cropRadius = 1.0;
                }
            });
        }

        // 转换全部 → 转换成功弹窗
        emit window.findChild<SettingsPanel*>()->startRequested();
        ConversionDoneDialog* done = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((done = window.findChild<ConversionDoneDialog*>()) && done->isVisible(), 240000);
        QTest::qWait(300);
        save(done, QStringLiteral("done-dialog.png"));
        for (QPushButton* b : done->findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("关闭"))
                b->click();
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<ConversionDoneDialog*>() == nullptr, 3000);

        table->clearRowSelection();
        QTest::qWait(1500); // 等缩略图
        save(&window, QStringLiteral("main-window.png"));

        // 截取 / 裁切窗口：同一段视频的两个片段，第二个片段圆角裁切
        AppSettings editorSettings(m_dir.filePath(QStringLiteral("editor.ini")));
        ThumbnailService thumbs;
        thumbs.setCacheDir(m_dir.filePath(QStringLiteral("thumbs")));
        ProxyCache proxies;
        proxies.setCacheDir(m_dir.filePath(QStringLiteral("proxies")));
        StickerTask base = model->taskAt(focusRow >= 0 ? focusRow : 2);
        base.resetRuntimeState();
        base.clipGroupId = QStringLiteral("clipgrp-shot");
        StickerTask a = base;
        a.clipId = QStringLiteral("clip-a");
        a.startTime = 0.0;
        a.endTime = 0.9;
        a.crop.reset();
        a.cropRadius = 0;
        StickerTask b = base;
        b.id = StickerTask::newId();
        b.clipId = QStringLiteral("clip-b");
        b.emoji = QStringLiteral("🥰");
        b.startTime = 1.0;
        b.endTime = 1.9;
        const int side = std::min(base.info.width, base.info.height) * 3 / 4;
        b.crop = QRect((base.info.width - side) / 2, (base.info.height - side) / 2, side, side);
        b.cropRadius = 0.3;
        ClipEditorDialog editor(editorSettings, &thumbs, &proxies, {a, b}, 1);
        editor.resize(1380, 860);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QTest::qWait(1800);
        save(&editor, QStringLiteral("editor.png"));
        editor.reject();
        window.close();
    }

    void cleanupTestCase()
    {
        if (!m_root.isEmpty())
            QDir(m_root).removeRecursively();
    }
};

QTEST_MAIN(TestReadmeShots)
#include "tst_readme_shots.moc"
