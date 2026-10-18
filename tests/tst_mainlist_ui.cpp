// 主列表交互与关键词弹窗的界面测试。
// 设置环境变量 TSM_SNAPSHOT_DIR 时保存截图。
#include "app/AppPaths.h"
#include "app/AppSettings.h"
#include "app/Logger.h"
#include "core/StickerList.h"
#include "core/TaskListModel.h"
#include "io/ZipWriter.h"
#include "media/FFmpegTools.h"
#include "media/ProcessRunner.h"
#include "media/ProxyCache.h"
#include "ui/AiSettingsDialog.h"
#include "ui/ConversionDoneDialog.h"
#include "ui/EmojiDialog.h"
#include "ui/KeywordDialog.h"
#include "ui/MainWindow.h"
#include "ui/SettingsPanel.h"
#include "ui/TaskTableView.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace tsm;

// 记录 QDesktopServices::openUrl 打开的网址，测试时不真的打开浏览器
class UrlRecorder : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void open(const QUrl& url) { urls << url; }
};

class TestMainListUi : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    void snapshot(QWidget* w, const QString& name)
    {
        const QString dir = qEnvironmentVariable("TSM_SNAPSHOT_DIR");
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            w->grab().save(QDir(dir).filePath(name));
        }
    }

    static StickerTask fakeTask(int i)
    {
        StickerTask t;
        t.id = StickerTask::newId();
        t.inputPath = QStringLiteral("C:/nope/%1.png").arg(i);
        t.info.fileName = QStringLiteral("%1.png").arg(i);
        t.info.width = 100;
        t.info.height = 100;
        t.resetRuntimeState();
        return t;
    }

    static void click(TaskTableView& view, int row, int column, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        const QRect r = view.visualRect(view.model()->index(row, column));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, mods, r.center());
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true); // 不碰真实配置目录
        QVERIFY(m_dir.isValid());
        applyDarkTheme(*qApp);
    }

    // 窗口图标：多尺寸 .ico 能从资源读出全部尺寸
    void appIconLoads()
    {
        const QIcon icon(QStringLiteral(":/icons/app.ico"));
        QVERIFY(!icon.isNull());
        const QList<QSize> sizes = icon.availableSizes();
        for (const int s : {16, 24, 32, 48, 64, 128, 256})
            QVERIFY2(sizes.contains(QSize(s, s)), qPrintable(QStringLiteral("缺少 %1px").arg(s)));
    }

    // 网站推广：标题栏链接、空列表提示、转换成功弹窗（可勾选不再弹出）
    void sitePromotion()
    {
        if (FFmpegTools::ffmpegPath().isEmpty())
            QSKIP("ffmpeg not found");
        QDir(AppPaths::configDir()).removeRecursively();
        UrlRecorder recorder;
        QDesktopServices::setUrlHandler(QStringLiteral("https"), &recorder, "open");
        const auto clickText = [](QWidget* root, const QString& prefix) {
            for (QPushButton* b : root->findChildren<QPushButton*>())
                if (b->isVisible() && b->text().startsWith(prefix)) {
                    b->click();
                    return true;
                }
            return false;
        };
        const QUrl site(QStringLiteral("https://www.cvevth.top/sticker"));

        const QString outDir = m_dir.filePath(QStringLiteral("promo_out"));
        {
            AppSettings settings;
            settings.setOutputDir(outDir);
            settings.setOutputAsZip(true);
        }
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        // 空列表提示：网站为主，机器人其次
        auto* promo = window.findChild<QLabel*>(QStringLiteral("PromoLine"));
        QVERIFY(promo && promo->isVisible());
        QVERIFY(promo->text().indexOf(site.toString()) < promo->text().indexOf(QStringLiteral("https://t.me/cvEvthBot")));
        snapshot(&window, QStringLiteral("promo_empty.png"));

        // 标题栏链接打开网站
        QVERIFY(clickText(&window, QStringLiteral("🌐 cvevth.top")));
        QCOMPARE(recorder.urls.value(0), site);

        const QString video = m_dir.filePath(QStringLiteral("promo.mp4"));
        ProcessRunner::run(FFmpegTools::ffmpegPath(),
                           {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                            QStringLiteral("testsrc2=size=320x240:rate=25:duration=1"), QStringLiteral("-c:v"),
                            QStringLiteral("mpeg4"), video});
        QStringList logs;
        auto conn = connect(&Logger::instance(), &Logger::lineAdded, this, [&logs](const QString& line) { logs << line; });
        window.openPaths({video});
        auto* table = window.findChild<TaskTableView*>();
        auto* model = qobject_cast<TaskListModel*>(table->model());
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(logs.join(u'\n').contains(QStringLiteral("已添加 1 项")), 5000);

        // 转换成功弹窗
        emit window.findChild<SettingsPanel*>()->startRequested();
        ConversionDoneDialog* dialog = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((dialog = window.findChild<ConversionDoneDialog*>()) && dialog->isVisible(), 120000);
        QVERIFY(logs.join(u'\n').contains(QStringLiteral("前往 https://www.cvevth.top/sticker 创建")));
        bool mentionsSite = false;
        for (QLabel* l : dialog->findChildren<QLabel*>())
            mentionsSite |= l->text().contains(QStringLiteral("www.cvevth.top/sticker"));
        QVERIFY(mentionsSite);
        QTest::qWait(200);
        snapshot(dialog, QStringLiteral("promo_done_dialog.png"));
        dialog->findChild<QCheckBox*>()->setChecked(true); // 以后不再弹出
        QVERIFY(clickText(dialog, QStringLiteral("🌐 去网站创建")));
        QCOMPARE(recorder.urls.last(), site);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<ConversionDoneDialog*>() == nullptr, 3000);
        QVERIFY(!AppSettings().showDoneDialog());

        // 勾选后再次转换：不再弹窗，只在日志里提示
        logs.clear();
        emit window.findChild<SettingsPanel*>()->startRequested();
        QTRY_VERIFY_WITH_TIMEOUT(logs.join(u'\n').contains(QStringLiteral("批量转换已全部完成")), 120000);
        QTest::qWait(300);
        QVERIFY(window.findChild<ConversionDoneDialog*>() == nullptr);
        QVERIFY(logs.join(u'\n').contains(QStringLiteral("cvevth.top/sticker")));

        disconnect(conn);
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
        window.close();
    }

    // 右键菜单批量设置 Emoji：点选 + 输入（忽略非 Emoji 文字）→ 覆盖全部选中项；再次打开预填，清空即清除
    void batchEmojiFromMenu()
    {
        if (FFmpegTools::ffmpegPath().isEmpty())
            QSKIP("ffmpeg not found");
        QDir(AppPaths::configDir()).removeRecursively();
        QStringList files;
        for (int i = 1; i <= 3; ++i) {
            const QString out = m_dir.filePath(QStringLiteral("emoji%1.png").arg(i));
            ProcessRunner::run(FFmpegTools::ffmpegPath(),
                               {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc2=size=200x200"),
                                QStringLiteral("-frames:v"), QStringLiteral("1"), out});
            files << out;
        }
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.openPaths(files);
        auto* table = window.findChild<TaskTableView*>();
        auto* model = qobject_cast<TaskListModel*>(table->model());
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 15000);
        model->updateTask(1, [](StickerTask& t) { t.emoji = QStringLiteral("😡"); });
        table->setFocus();
        QTest::keySequence(table, QKeySequence::SelectAll);
        QCOMPARE(table->selectedRowList().size(), 3);

        // 打开右键菜单并点"设置选中贴纸的 Emoji"
        const auto openFromMenu = [&] {
            QTimer::singleShot(200, [] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                QVERIFY(menu);
                for (QAction* a : menu->actions()) {
                    if (a->text().startsWith(QStringLiteral("😀 设置选中贴纸的 Emoji"))) {
                        QVERIFY(a->text().endsWith(QStringLiteral("(3)")));
                        QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(a).center());
                        return;
                    }
                }
                QFAIL("menu entry not found");
            });
            const QRect r = table->visualRect(model->index(0, TaskListModel::ColName));
            QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, r.center(), table->viewport()->mapToGlobal(r.center()));
            QApplication::sendEvent(table->viewport(), &menuEvent);
        };

        bool dialogChecked = false;
        QTimer::singleShot(600, [&] {
            auto* dialog = qobject_cast<EmojiDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto* input = dialog->findChild<QLineEdit*>();
            QVERIFY(input->text().isEmpty()); // 选中项 Emoji 不一致：不预填
            for (QPushButton* b : dialog->findChildren<QPushButton*>())
                if (b->text() == QStringLiteral("😂"))
                    b->click();
            input->insert(QStringLiteral(" 哈哈 🤣"));
            QCOMPARE(dialog->emoji(), QStringLiteral("😂🤣"));
            bool warned = false;
            for (QLabel* l : dialog->findChildren<QLabel*>())
                warned |= l->text().contains(QStringLiteral("已忽略非 Emoji 文字"));
            QVERIFY(warned);
            snapshot(dialog, QStringLiteral("emoji_dialog.png"));
            dialogChecked = true;
            for (QPushButton* b : dialog->findChildren<QPushButton*>())
                if (b->text() == QStringLiteral("保存"))
                    b->click();
        });
        openFromMenu();
        QTRY_VERIFY_WITH_TIMEOUT(dialogChecked, 5000);
        for (int row = 0; row < 3; ++row)
            QCOMPARE(model->taskAt(row).emoji, QStringLiteral("😂🤣"));
        QCOMPARE(model->targetFileName(2), QStringLiteral("003_😂🤣.webp"));

        // 再次打开：Emoji 相同时预填；清空保存 = 清除
        dialogChecked = false;
        QTimer::singleShot(600, [&] {
            auto* dialog = qobject_cast<EmojiDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto* input = dialog->findChild<QLineEdit*>();
            QCOMPARE(input->text(), QStringLiteral("😂🤣"));
            input->clear();
            dialogChecked = true;
            for (QPushButton* b : dialog->findChildren<QPushButton*>())
                if (b->text() == QStringLiteral("保存"))
                    b->click();
        });
        openFromMenu();
        QTRY_VERIFY_WITH_TIMEOUT(dialogChecked, 5000);
        for (int row = 0; row < 3; ++row)
            QVERIFY(model->taskAt(row).emoji.isEmpty());
        QCOMPARE(model->targetFileName(0), QStringLiteral("001.webp"));
        window.close();
    }

    // 每个主窗口测试从空会话开始
    void init()
    {
        QDir(AppPaths::configDir()).removeRecursively();
    }

    void selectionRules()
    {
        TaskListModel model;
        QList<StickerTask> tasks;
        for (int i = 0; i < 5; ++i)
            tasks << fakeTask(i);
        model.appendTasks(tasks);
        TaskTableView view;
        view.setModel(&model);
        view.setButtonColumns({TaskListModel::ColKeywords, TaskListModel::ColActions});
        view.resize(1000, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const int name = TaskListModel::ColName;

        click(view, 1, name);
        QCOMPARE(view.selectedRowList(), QList<int>{1});
        click(view, 1, name); // 再次单击已选中的行取消选中
        QVERIFY(view.selectedRowList().isEmpty());

        click(view, 1, name);
        click(view, 3, name, Qt::ControlModifier); // Ctrl 切换
        QCOMPARE(view.selectedRowList(), (QList<int>{1, 3}));
        click(view, 1, name, Qt::ControlModifier);
        QCOMPARE(view.selectedRowList(), QList<int>{3});

        click(view, 4, name, Qt::ShiftModifier); // Shift 从上次选中行做范围选择
        QCOMPARE(view.selectedRowList(), (QList<int>{3, 4}));
        click(view, 0, name, Qt::ShiftModifier);
        QCOMPARE(view.selectedRowList(), (QList<int>{0, 1, 2, 3}));

        click(view, 2, name); // 普通单击已选中的一行：取消它
        QCOMPARE(view.selectedRowList(), (QList<int>{0, 1, 3}));
        click(view, 4, name); // 普通单击未选中行：只选中它
        QCOMPARE(view.selectedRowList(), QList<int>{4});

        // 按钮列点击不改变选择
        click(view, 2, TaskListModel::ColKeywords);
        QCOMPARE(view.selectedRowList(), QList<int>{4});

        // 空白处取消选择
        QTest::mouseClick(view.viewport(), Qt::LeftButton, {}, QPoint(200, view.viewport()->height() - 5));
        QVERIFY(view.selectedRowList().isEmpty());

        view.selectAllRows();
        QCOMPARE(view.selectedRowList().size(), 5);
        view.selectRows({0, 2});
        view.invertSelection();
        QCOMPARE(view.selectedRowList(), (QList<int>{1, 3, 4}));

        // 右键未选中的行：先只选中它；右键已选中的行：保持多选
        QSignalSpy menu(&view, &TaskTableView::rowContextMenuRequested);
        const QRect r0 = view.visualRect(model.index(0, name));
        QContextMenuEvent e0(QContextMenuEvent::Mouse, r0.center(), view.viewport()->mapToGlobal(r0.center()));
        QApplication::sendEvent(view.viewport(), &e0);
        QCOMPARE(view.selectedRowList(), QList<int>{0});
        view.selectRows({1, 2});
        const QRect r2 = view.visualRect(model.index(2, name));
        QContextMenuEvent e2(QContextMenuEvent::Mouse, r2.center(), view.viewport()->mapToGlobal(r2.center()));
        QApplication::sendEvent(view.viewport(), &e2);
        QCOMPARE(view.selectedRowList(), (QList<int>{1, 2}));
        QCOMPARE(menu.size(), 2);
    }

    void keywordDialog()
    {
        KeywordDialog dialog(QStringLiteral("😂 test.mp4"), {QStringLiteral("初始")});
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto* input = dialog.findChild<QLineEdit*>();
        QVERIFY(input);
        QLabel* counter = nullptr;
        for (QLabel* l : dialog.findChildren<QLabel*>())
            if (l->objectName() == QStringLiteral("Counter"))
                counter = l;
        QVERIFY(counter);
        QCOMPARE(counter->text(), QStringLiteral("1/20 · 2/64"));

        input->insert(QStringLiteral("哈哈，笑死 哈哈")); // QTest 只能模拟 ASCII 按键
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(dialog.keywords(), (QStringList{QStringLiteral("初始"), QStringLiteral("哈哈"), QStringLiteral("笑死")}));
        QVERIFY(input->text().isEmpty());
        bool duplicateShown = false;
        for (QLabel* l : dialog.findChildren<QLabel*>())
            duplicateShown |= l->text() == QStringLiteral("「哈哈」已存在");
        QVERIFY(duplicateShown);
        QCOMPARE(counter->text(), QStringLiteral("3/20 · 6/64"));
        QTest::qWait(100);
        snapshot(&dialog, QStringLiteral("keyword_dialog.png"));

        QTest::keyClick(input, Qt::Key_Backspace); // 空输入框 Backspace 删除最后一个
        QCOMPARE(dialog.keywords().size(), 2);

        // 填满 64 字符：输入框禁用
        QTest::keyClicks(input, QString(60, u'x'));
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(counter->text(), QStringLiteral("3/20 · 64/64"));
        QVERIFY(!input->isEnabled());
        QVERIFY(counter->property("full").toBool());
    }

    void mainWindowShortcutsAndKeywords()
    {
        if (FFmpegTools::ffmpegPath().isEmpty())
            QSKIP("ffmpeg not found");
        QStringList files;
        for (int i = 1; i <= 3; ++i) {
            const QString out = m_dir.filePath(QStringLiteral("素材%1.png").arg(i));
            ProcessRunner::run(FFmpegTools::ffmpegPath(),
                               {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                QStringLiteral("testsrc2=size=%1x300").arg(200 + i * 100), QStringLiteral("-frames:v"),
                                QStringLiteral("1"), out});
            files << out;
        }

        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        window.openPaths(files);
        auto* table = window.findChild<TaskTableView*>();
        QVERIFY(table);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 3, 15000);

        table->setFocus();
        QTest::keySequence(table, QKeySequence::SelectAll);
        QCOMPARE(table->selectedRowList().size(), 3);
        QTest::keyClick(table, Qt::Key_Escape);
        QVERIFY(table->selectedRowList().isEmpty());

        // 点关键词按钮 → 弹窗输入 → 保存
        QTimer::singleShot(300, [] {
            auto* dialog = qobject_cast<KeywordDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->findChild<QLineEdit*>()->insert(QStringLiteral("开心、跳舞，好耶"));
            QTest::keyClick(dialog->findChild<QLineEdit*>(), Qt::Key_Return);
            for (QPushButton* b : dialog->findChildren<QPushButton*>())
                if (b->text() == QStringLiteral("保存"))
                    b->click();
        });
        click(*table, 1, TaskListModel::ColKeywords);
        auto* model = qobject_cast<TaskListModel*>(table->model());
        QCOMPARE(model->taskAt(1).keywords,
                 (QStringList{QStringLiteral("开心"), QStringLiteral("跳舞"), QStringLiteral("好耶")}));

        // Delete 删除选中项
        click(*table, 0, TaskListModel::ColName);
        QCOMPARE(table->selectedRowList(), QList<int>{0});
        QTest::qWait(1500); // 等缩略图
        snapshot(&window, QStringLiteral("main_list_keywords.png"));

        // 右键菜单截图（菜单弹出后抓图再关闭）
        QTimer::singleShot(300, [this] {
            if (QWidget* popup = QApplication::activePopupWidget()) {
                snapshot(popup, QStringLiteral("row_menu_single.png"));
                popup->close();
            }
        });
        const QRect r = table->visualRect(model->index(0, TaskListModel::ColName));
        QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, r.center(), table->viewport()->mapToGlobal(r.center()));
        QApplication::sendEvent(table->viewport(), &menuEvent);

        table->setFocus();
        QTest::keyClick(table, Qt::Key_Delete);
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->targetFileName(0), QStringLiteral("001.webp")); // 删除后重新编号
    }

    void sessionSaveAndRestore()
    {
        if (FFmpegTools::ffmpegPath().isEmpty())
            QSKIP("ffmpeg not found");
        const QString dir = m_dir.filePath(QStringLiteral("session_src"));
        QDir().mkpath(dir);
        QStringList files;
        for (int i = 1; i <= 3; ++i) {
            const QString out = QDir(dir).filePath(QStringLiteral("片段%1.mp4").arg(i));
            ProcessRunner::run(FFmpegTools::ffmpegPath(),
                               {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                QStringLiteral("testsrc2=size=320x240:rate=25:duration=%1").arg(i + 1), QStringLiteral("-c:v"),
                                QStringLiteral("mpeg4"), out});
            files << out;
        }
        const QString sessionFile = AppPaths::sessionFile(AppPaths::configDir());

        // 第一次运行：添加、编辑，等自动保存
        {
            MainWindow window;
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            window.openPaths(files);
            auto* table = window.findChild<TaskTableView*>();
            auto* model = qobject_cast<TaskListModel*>(table->model());
            QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 15000);
            model->updateTask(1, [](StickerTask& t) {
                t.emoji = QStringLiteral("😂");
                t.keywords = {QStringLiteral("哈哈")};
                t.startTime = 0.5;
                t.endTime = 2.0;
                t.crop = QRect(40, 0, 240, 240);
                t.cropRadius = 0.5;
                t.mirror = true;
                t.clipGroupId = QStringLiteral("clipgrp-1");
                t.clipId = QStringLiteral("clip-1");
            });
            // 250ms 防抖后写入
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(sessionFile), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(sessionFile).size() > 0, 3000);
            QFile f(sessionFile);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const StickerList::ParseResult saved = StickerList::parse(f.readAll());
            QVERIFY(saved.ok);
            QTRY_COMPARE_WITH_TIMEOUT(saved.entries.size(), 3, 3000);
            QCOMPARE(saved.entries.at(1).task.emoji, QStringLiteral("😂"));
            QVERIFY(saved.entries.at(0).hasMedia); // 附带解析结果缓存
            window.close();
        }

        // 第二次运行前删掉一个源文件
        QVERIFY(QFile::remove(files.at(2)));
        QStringList logs;
        auto conn = connect(&Logger::instance(), &Logger::lineAdded, this, [&logs](const QString& line) { logs << line; });
        {
            MainWindow window;
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto* table = window.findChild<TaskTableView*>();
            auto* model = qobject_cast<TaskListModel*>(table->model());
            QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 15000);
            QTRY_VERIFY_WITH_TIMEOUT(logs.join(u'\n').contains(QStringLiteral("已恢复上次进度：2 项")), 5000);
            QVERIFY(logs.join(u'\n').contains(QStringLiteral("有 1 个源文件找不到，已跳过")));

            const StickerTask& t = model->taskAt(1);
            QCOMPARE(t.emoji, QStringLiteral("😂"));
            QCOMPARE(t.keywords, QStringList{QStringLiteral("哈哈")});
            QCOMPARE(*t.startTime, 0.5);
            QCOMPARE(*t.endTime, 2.0);
            QCOMPARE(*t.crop, QRect(40, 0, 240, 240));
            QCOMPARE(t.cropRadius, 0.5);
            QVERIFY(t.mirror);
            QCOMPARE(t.clipGroupId, QStringLiteral("clipgrp-1"));
            QCOMPARE(t.status, TaskStatus::Pending); // 不保存转换状态
            QVERIFY(t.info.isVideo);
            QCOMPARE(t.info.width, 320);
            QTest::qWait(500);
            snapshot(&window, QStringLiteral("session_restored.png"));
            window.close();
        }
        disconnect(conn);

        // 恢复结束后保存一次：缺失的条目已从会话中移除
        QFile f(sessionFile);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(StickerList::parse(f.readAll()).entries.size(), 2);
    }

    // 导入 zip（附带预览缓存）→ 同一个 zip 再导入直接复用 → 格式错误的 json 报错 → 从面板导出
    void importZipAndExportThroughWindow()
    {
        if (FFmpegTools::ffmpegPath().isEmpty())
            QSKIP("ffmpeg not found");
        const QString dir = m_dir.filePath(QStringLiteral("import_src"));
        QDir().mkpath(dir);
        const QString video = QDir(dir).filePath(QStringLiteral("导入.mp4"));
        ProcessRunner::run(FFmpegTools::ffmpegPath(),
                           {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                            QStringLiteral("testsrc2=size=320x240:rate=25:duration=2"), QStringLiteral("-c:v"),
                            QStringLiteral("mpeg4"), video});
        StickerTask t;
        t.inputPath = QStringLiteral("sources/导入.mp4");
        t.info.fileName = QStringLiteral("导入.mp4");
        t.info.isVideo = true;
        t.emoji = QStringLiteral("🎉");
        t.startTime = 0.5;
        t.endTime = 1.5;
        StickerList::WriteOptions options;
        options.exportMode = QStringLiteral("sources");
        options.pathFor = [](const StickerTask& task) { return task.inputPath; };
        options.proxyFor = [](const StickerTask&) { return QStringLiteral("proxies/导入.mp4.mp4"); };
        const QString zip = QDir(dir).filePath(QStringLiteral("bundle.zip"));
        QVERIFY(ZipWriter::writeStored(zip, {{QStringLiteral("sticker_list.json"), {}, StickerList::write({t}, options)},
                                             {QStringLiteral("sources/导入.mp4"), video, {}},
                                             {QStringLiteral("proxies/导入.mp4.mp4"), {}, QByteArray("proxy")}}));
        const QString bad = QDir(dir).filePath(QStringLiteral("bad.json"));
        {
            QFile f(bad);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{}");
        }
        const QString outDir = QDir(dir).filePath(QStringLiteral("out"));
        {
            AppSettings settings;
            settings.setOutputDir(outDir);
            settings.setOutputAsZip(true);
        }

        QStringList logs;
        auto conn = connect(&Logger::instance(), &Logger::lineAdded, this, [&logs](const QString& line) { logs << line; });
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.openPaths({zip, bad}); // 会话恢复结束后再按顺序处理
        auto* table = window.findChild<TaskTableView*>();
        auto* model = qobject_cast<TaskListModel*>(table->model());
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(logs.join(u'\n').contains(QStringLiteral("列表文件格式无效")), 5000);
        const QString all = logs.join(u'\n');
        QVERIFY2(all.contains(QStringLiteral("已导入 1 项")), qPrintable(all));
        QVERIFY(all.contains(QStringLiteral("已装回 1 个预览缓存")));
        QCOMPARE(model->taskAt(0).emoji, QStringLiteral("🎉"));
        QCOMPARE(*model->taskAt(0).startTime, 0.5);
        QVERIFY(model->taskAt(0).inputPath.contains(QStringLiteral("import_cache")));
        QVERIFY(!window.findChild<ProxyCache*>()->cached(model->taskAt(0).inputPath).isEmpty());

        window.openPaths({zip});
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 15000);
        QVERIFY(logs.join(u'\n').contains(QStringLiteral("直接复用导入缓存")));

        emit window.findChild<SettingsPanel*>()->exportRequested(SettingsPanel::ExportTrimmed);
        QTRY_VERIFY_WITH_TIMEOUT(logs.join(u'\n').contains(QStringLiteral("导出完成：2 项")), 60000);
        QCOMPARE(QDir(outDir).entryList({QStringLiteral("TG_Trimmed_*.zip")}, QDir::Files).size(), 1);
        QCOMPARE(model->taskAt(1).status, TaskStatus::Succeeded);
        QTest::qWait(300);
        snapshot(&window, QStringLiteral("export_done.png"));
        disconnect(conn);
        window.close();
    }

    // AI 设置保存后自动关闭；AI 匹配覆盖 Emoji 并同步文件名（本机假接口，不填 Key）
    void aiSettingsAndMatching()
    {
        if (FFmpegTools::ffmpegPath().isEmpty())
            QSKIP("ffmpeg not found");
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int requests = 0;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket* socket = server.nextPendingConnection();
            auto received = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, received, &requests] {
                *received += socket->readAll();
                const qsizetype headEnd = received->indexOf("\r\n\r\n");
                if (headEnd < 0)
                    return;
                const qsizetype at = received->toLower().indexOf("content-length:");
                const qsizetype length = received->mid(at + 15, received->indexOf('\r', at) - at - 15).trimmed().toLongLong();
                if (received->size() < headEnd + 4 + length)
                    return;
                ++requests;
                const QByteArray body = R"({"choices":[{"message":{"content":"😂🤣"}}]})";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });

        // 设置弹窗
        {
            AppSettings settings;
            AiSettingsDialog dialog(settings);
            dialog.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            dialog.findChild<QLineEdit*>(QStringLiteral("AiBaseUrl"))->setText(QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()));
            dialog.findChild<QLineEdit*>(QStringLiteral("AiModel"))->setText(QStringLiteral("local-vision"));
            dialog.findChild<QLineEdit*>(QStringLiteral("AiApiKey"))->clear();
            snapshot(&dialog, QStringLiteral("ai_settings.png"));
            for (QPushButton* b : dialog.findChildren<QPushButton*>())
                if (b->text() == QStringLiteral("保存"))
                    b->click();
            QTRY_VERIFY_WITH_TIMEOUT(!dialog.isVisible(), 3000); // ✓ 后自动关闭
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
            QCOMPARE(settings.aiModel(), QStringLiteral("local-vision"));
        }

        const QString video = m_dir.filePath(QStringLiteral("ai_src.mp4"));
        ProcessRunner::run(FFmpegTools::ffmpegPath(),
                           {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                            QStringLiteral("testsrc2=size=320x240:rate=25:duration=2"), QStringLiteral("-c:v"),
                            QStringLiteral("mpeg4"), video});
        QStringList logs;
        auto conn = connect(&Logger::instance(), &Logger::lineAdded, this, [&logs](const QString& line) { logs << line; });
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.openPaths({video});
        auto* table = window.findChild<TaskTableView*>();
        auto* model = qobject_cast<TaskListModel*>(table->model());
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(logs.join(u'\n').contains(QStringLiteral("已添加 1 项")), 5000);
        for (QPushButton* b : window.findChildren<QPushButton*>())
            if (b->text().startsWith(QStringLiteral("✨ AI 匹配")))
                b->click();
        QTRY_VERIFY_WITH_TIMEOUT(logs.join(u'\n').contains(QStringLiteral("AI Emoji 智能匹配已完成")), 30000);
        QVERIFY2(logs.join(u'\n').contains(QStringLiteral("[AI Emoji] ai_src.mp4 -> 😂🤣")), qPrintable(logs.join(u'\n')));
        QCOMPARE(requests, 1);
        QCOMPARE(model->taskAt(0).emoji, QStringLiteral("😂🤣"));
        QCOMPARE(model->targetFileName(0), QStringLiteral("001_😂🤣.webm"));
        snapshot(&window, QStringLiteral("ai_done.png"));
        disconnect(conn);
        window.close();
    }
};

QTEST_MAIN(TestMainListUi)
#include "tst_mainlist_ui.moc"
