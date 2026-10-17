#include "ui/MainWindow.h"

#include "app/AppPaths.h"
#include "app/BatchConverter.h"
#include "app/Logger.h"
#include "core/Format.h"
#include "core/MediaKinds.h"
#include "core/TaskListModel.h"
#include "media/FFmpegTools.h"
#include "ui/DropOverlay.h"
#include "ui/DropZone.h"
#include "ui/HeaderBar.h"
#include "ui/LogPanel.h"
#include "ui/SettingsPanel.h"

#include <QCloseEvent>
#include <QCollator>
#include <QDesktopServices>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace tsm {

namespace {
QStringList localPathsFrom(const QMimeData* mime)
{
    QStringList paths;
    if (!mime || !mime->hasUrls())
        return paths;
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    return paths;
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_model(new TaskListModel(this))
    , m_converter(new BatchConverter(this))
{
    setWindowTitle(QStringLiteral("Telegram Sticker Maker"));
    resize(1500, 820);
    setMinimumSize(1180, 640);
    setAcceptDrops(true);
    m_workers.setMaxThreadCount(1);

    buildUi();
    connectSignals();
    m_model->setOutputLocation(m_settings.outputDir(), m_settings.saveBesideSource());
    refreshUi();

    logInfo(QStringLiteral("✅ 启动就绪"));
    if (FFmpegTools::ffmpegPath().isEmpty() || FFmpegTools::ffprobePath().isEmpty())
        logInfo(QStringLiteral("⚠️ 未找到 ffmpeg / ffprobe，请确认它们与程序位于同一目录"));
    // TODO(会话): 启动时从数据目录恢复上次进度（需求第 10 节）
}

MainWindow::~MainWindow()
{
    if (m_cancel)
        m_cancel->store(true);
    m_workers.waitForDone();
}

// ---------------------------------------------------------------- 界面搭建

void MainWindow::buildUi()
{
    auto* central = new QWidget;
    central->setObjectName(QStringLiteral("Central"));
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_header = new HeaderBar;
    root->addWidget(m_header);

    auto* body = new QWidget;
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(16, 12, 16, 12);
    bodyLayout->setSpacing(14);

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);
    leftLayout->addWidget(buildToolbar());

    m_dropZone = new DropZone;
    m_table = new QTableView;
    configureTable();
    m_listStack = new QStackedWidget;
    m_listStack->addWidget(m_dropZone);
    m_listStack->addWidget(m_table);
    leftLayout->addWidget(m_listStack, 1);

    m_panel = new SettingsPanel(m_settings);
    bodyLayout->addWidget(left, 1);
    bodyLayout->addWidget(m_panel);
    root->addWidget(body, 1);

    m_log = new LogPanel;
    root->addWidget(m_log);

    setCentralWidget(central);
    m_overlay = new DropOverlay(this);
}

QWidget* MainWindow::buildToolbar()
{
    auto* bar = new QWidget;
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    const auto add = [&](const QString& text, const QString& tip) {
        auto* button = new QPushButton(text);
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        layout->addWidget(button);
        return button;
    };
    m_btnAddFiles = add(QStringLiteral("➕ 添加文件"), QStringLiteral("选择一个或多个视频 / 动图 / 图片"));
    m_btnClip = add(QStringLiteral("✂️ 截取视频片段"), QStringLiteral("从一个视频截取一段或多段贴纸"));
    m_btnAddFolder = add(QStringLiteral("📁 添加文件夹"), QStringLiteral("递归扫描文件夹内所有支持的媒体文件"));
    m_btnImport = add(QStringLiteral("📥 导入"), QStringLiteral("导入本工具导出的 zip / JSON 列表"));
    m_btnAi = add(QStringLiteral("✨ AI 匹配 Emoji"), QStringLiteral("用 AI 识别画面情绪并填写 Emoji"));
    m_btnAiSettings = add(QStringLiteral("⚙️ AI 设置"), QStringLiteral("配置兼容 OpenAI 的接口"));
    layout->addStretch();
    m_btnClear = add(QStringLiteral("🗑️ 清空列表"), QStringLiteral("移除列表中的全部项目"));
    m_btnClear->setObjectName(QStringLiteral("DangerGhostButton"));
    return bar;
}

void MainWindow::configureTable()
{
    m_table->setObjectName(QStringLiteral("TaskTable"));
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_table->setShowGrid(false);
    m_table->setWordWrap(true);
    m_table->setTextElideMode(Qt::ElideMiddle);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(64);

    QHeaderView* header = m_table->horizontalHeader();
    header->setHighlightSections(false);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setSectionResizeMode(TaskListModel::ColName, QHeaderView::Stretch);
    const std::pair<int, int> widths[] = {
        {TaskListModel::ColPreview, 80},  {TaskListModel::ColSpec, 230},   {TaskListModel::ColTarget, 150},
        {TaskListModel::ColKeywords, 120}, {TaskListModel::ColStatus, 210}, {TaskListModel::ColSize, 90},
        {TaskListModel::ColActions, 96},
    };
    for (const auto& [column, width] : widths)
        m_table->setColumnWidth(column, width);
    // TODO(主列表): 再次单击已选中行取消选中、点击空白取消选择、右键菜单（需求 3.3 / 3.4）
}

void MainWindow::connectSignals()
{
    connect(&Logger::instance(), &Logger::lineAdded, m_log, &LogPanel::appendLine);

    // 工具栏
    connect(m_btnAddFiles, &QPushButton::clicked, this, &MainWindow::addFilesDialog);
    connect(m_btnAddFolder, &QPushButton::clicked, this, &MainWindow::addFolderDialog);
    connect(m_btnImport, &QPushButton::clicked, this, &MainWindow::importDialog);
    connect(m_btnClip, &QPushButton::clicked, this, [this] { notImplemented(QStringLiteral("截取视频片段")); });
    connect(m_btnAi, &QPushButton::clicked, this, [this] { notImplemented(QStringLiteral("AI 匹配 Emoji")); });
    connect(m_btnAiSettings, &QPushButton::clicked, this, [this] { notImplemented(QStringLiteral("AI 设置")); });
    connect(m_btnClear, &QPushButton::clicked, this, &MainWindow::clearList);

    // 空列表拖放区
    connect(m_dropZone, &DropZone::addFilesClicked, this, &MainWindow::addFilesDialog);
    connect(m_dropZone, &DropZone::addFolderClicked, this, &MainWindow::addFolderDialog);
    connect(m_dropZone, &DropZone::importClicked, this, &MainWindow::importDialog);

    // 列表变化 → 刷新徽章 / 空状态 / 日志摘要
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &MainWindow::refreshUi);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &MainWindow::refreshUi);
    connect(m_model, &QAbstractItemModel::modelReset, this, &MainWindow::refreshUi);
    connect(m_model, &QAbstractItemModel::dataChanged, this, &MainWindow::updateLogSummary);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        updateBadge();
        updateAiButton();
    });
    // TODO(会话): contentChanged → 250ms 防抖后原子写入会话文件

    // 右侧面板
    connect(m_panel, &SettingsPanel::startRequested, this, [this] { startConversion({}); });
    connect(m_panel, &SettingsPanel::exportRequested, this, [this] { notImplemented(QStringLiteral("导出")); });
    connect(m_panel, &SettingsPanel::stopRequested, this, &MainWindow::requestStop);
    connect(m_panel, &SettingsPanel::openOutputRequested, this, &MainWindow::openOutputDir);
    connect(m_panel, &SettingsPanel::openDataDirRequested, this, &MainWindow::openDataDir);
    connect(m_panel, &SettingsPanel::clearCacheRequested, this, [this] { notImplemented(QStringLiteral("清除预览缓存")); });
    connect(m_panel, &SettingsPanel::outputLocationChanged, this,
            [this] { m_model->setOutputLocation(m_settings.outputDir(), m_settings.saveBesideSource()); });
    connect(m_panel, &SettingsPanel::dataDirChanged, this, [](const QString&, const QString& newDir) {
        // TODO(会话): 新目录没有会话文件时，把旧目录的会话复制过去
        logInfo(QStringLiteral("💾 数据目录已切换为 %1").arg(QDir::toNativeSeparators(newDir)));
    });

    connectConverter();

    // 列表快捷键（仅表格自身有焦点时生效，行内编辑时不触发）
    auto* deleteKey = new QShortcut(QKeySequence::Delete, m_table, nullptr, nullptr, Qt::WidgetShortcut);
    connect(deleteKey, &QShortcut::activated, this, &MainWindow::removeSelected);
    auto* escapeKey = new QShortcut(QKeySequence(Qt::Key_Escape), m_table, nullptr, nullptr, Qt::WidgetShortcut);
    connect(escapeKey, &QShortcut::activated, m_table, &QTableView::clearSelection);
}

// ---------------------------------------------------------------- 拖放

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (localPathsFrom(event->mimeData()).isEmpty())
        return;
    event->acceptProposedAction();
    m_overlay->setGeometry(rect());
    m_overlay->show();
    m_overlay->raise();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    event->acceptProposedAction();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent* event)
{
    m_overlay->hide();
    QMainWindow::dragLeaveEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event)
{
    m_overlay->hide();
    const QStringList paths = localPathsFrom(event->mimeData());
    if (paths.isEmpty())
        return;
    event->acceptProposedAction();
    addPaths(paths);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_cancel)
        m_cancel->store(true);
    // TODO(会话): 关闭前强制保存一次
    event->accept();
}

// ---------------------------------------------------------------- 添加素材

void MainWindow::addFilesDialog()
{
    const QStringList files =
        QFileDialog::getOpenFileNames(this, QStringLiteral("添加文件"), m_lastDir, MediaKinds::openFileFilter());
    if (files.isEmpty())
        return;
    m_lastDir = QFileInfo(files.first()).absolutePath();
    addPaths(files);
}

void MainWindow::addFolderDialog()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("添加文件夹"), m_lastDir);
    if (dir.isEmpty())
        return;
    m_lastDir = dir;
    addPaths({dir});
}

void MainWindow::importDialog()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("导入列表"), m_lastDir,
                                                      QStringLiteral("贴纸列表 (*.zip *.json)"));
    if (file.isEmpty())
        return;
    m_lastDir = QFileInfo(file).absolutePath();
    importManifest(file);
}

void MainWindow::addPaths(const QStringList& paths)
{
    QStringList media;
    int unsupported = 0;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            // 文件夹里有本工具导出的清单时改为走导入
            if (const QString manifest = findManifestIn(info.absoluteFilePath()); !manifest.isEmpty())
                importManifest(manifest);
            else
                media << scanFolder(info.absoluteFilePath());
        } else if (MediaKinds::isImportFile(path)) {
            importManifest(path);
        } else if (MediaKinds::isSupportedMedia(path)) {
            media << info.absoluteFilePath();
        } else {
            ++unsupported;
        }
    }
    if (unsupported > 0)
        logInfo(QStringLiteral("⚠️ 跳过 %1 个不支持的文件").arg(unsupported));
    if (!media.isEmpty())
        startProbing(media);
    else if (unsupported == 0 && paths.size() == 1 && QFileInfo(paths.first()).isDir())
        logInfo(QStringLiteral("⚠️ 文件夹中没有找到支持的媒体文件"));
}

void MainWindow::importManifest(const QString& path)
{
    // TODO(导入): 解析 sticker_list.json / stickers.json / zip（需求 9.2）
    notImplemented(QStringLiteral("导入 %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::startProbing(const QStringList& files)
{
    if (m_busy != Busy::None) {
        logInfo(QStringLiteral("⚠️ 任务正在进行中，请先停止或等待完成"));
        return;
    }
    m_probeAdded = 0;
    m_probeFailed = 0;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    setBusy(Busy::Importing, int(files.size()));

    const auto cancel = m_cancel;
    m_workers.start([this, files, cancel] {
        for (const QString& path : files) {
            if (cancel->load())
                break;
            QMetaObject::invokeMethod(this, [this, path] {
                m_busyHint = QStringLiteral("正在解析 ") + QFileInfo(path).fileName();
                refreshUi();
            }, Qt::QueuedConnection);
            const ProbeResult result = MediaProbe::probe(path);
            QMetaObject::invokeMethod(this, [this, path, result] { onProbed(path, result); }, Qt::QueuedConnection);
        }
        const bool cancelled = cancel->load();
        QMetaObject::invokeMethod(this, [this, cancelled] { onProbingFinished(cancelled); }, Qt::QueuedConnection);
    });
}

void MainWindow::onProbed(const QString& path, const ProbeResult& result)
{
    ++m_busyDone;
    if (result.ok) {
        m_model->appendTasks({StickerTask::fromMedia(path, result.info)});
        ++m_probeAdded;
    } else {
        ++m_probeFailed;
        logInfo(QStringLiteral("❌ [%1] 解析失败: %2").arg(QFileInfo(path).fileName(), result.error));
    }
    refreshUi();
}

void MainWindow::onProbingFinished(bool cancelled)
{
    setBusy(Busy::None, 0);
    if (cancelled) {
        logInfo(QStringLiteral("🛑 已取消导入，已加入 %1 项").arg(m_probeAdded));
    } else if (m_probeFailed > 0) {
        logInfo(QStringLiteral("➕ 已添加 %1 项，%2 项解析失败").arg(m_probeAdded).arg(m_probeFailed));
    } else {
        logInfo(QStringLiteral("➕ 已添加 %1 项").arg(m_probeAdded));
    }
}

// ---------------------------------------------------------------- 转换

void MainWindow::startConversion(QList<int> rows)
{
    if (m_busy != Busy::None)
        return;
    if (rows.isEmpty()) {
        for (int i = 0; i < m_model->rowCount(); ++i)
            rows << i;
    }
    if (rows.isEmpty()) {
        logInfo(QStringLiteral("⚠️ 列表为空，请先添加素材"));
        return;
    }

    QList<ConvertJob> jobs;
    for (const int row : rows) {
        m_model->updateTask(row, [](StickerTask& t) { t.resetRuntimeState(); }, false);
        jobs << ConvertJob{m_model->taskAt(row), m_model->targetFileName(row), m_model->targetDirectory(row)};
    }

    ConvertSettings settings;
    settings.mode = m_settings.outputMode();
    settings.preset = m_settings.encodePreset();
    settings.asZip = m_settings.outputAsZip();
    settings.zipDir = m_settings.outputDir();
    settings.workRoot = QDir(m_settings.resolvedDataDir()).filePath(QStringLiteral("work"));

    m_stageText.clear();
    setBusy(Busy::Converting, int(jobs.size()));
    const QString mode = settings.mode == OutputMode::Emoji ? QStringLiteral("自定义表情 100px") : QStringLiteral("标准贴纸 512px");
    logInfo(QStringLiteral("🚀 开始转换 %1 项（%2）").arg(jobs.size()).arg(mode));
    m_converter->start(jobs, settings);
}

void MainWindow::connectConverter()
{
    const auto nameOf = [this](const QString& id) {
        const int row = m_model->rowOfId(id);
        return row >= 0 ? m_model->taskAt(row).info.fileName : QString();
    };

    connect(m_converter, &BatchConverter::taskStarted, this, [this, nameOf](const QString& id) {
        m_model->updateTask(m_model->rowOfId(id), [](StickerTask& t) {
            t.status = TaskStatus::Running;
            t.statusText = defaultStatusText(TaskStatus::Running);
            t.progress = 0;
        }, false);
        logInfo(QStringLiteral("▶️ [%1] 开始转换").arg(nameOf(id)));
        refreshUi();
    });

    connect(m_converter, &BatchConverter::taskProgress, this, [this, nameOf](const QString& id, int percent, const QString& text) {
        m_model->updateTask(m_model->rowOfId(id), [&](StickerTask& t) {
            t.progress = percent;
            t.statusText = text;
        }, false);
        // 阶段说明变化时写日志（进度百分比不写）
        if (m_stageText.value(id) != text) {
            m_stageText.insert(id, text);
            logInfo(QStringLiteral("[%1] %2").arg(nameOf(id), text));
        }
        m_busyHint = QStringLiteral("[%1] %2").arg(nameOf(id), text);
        refreshUi();
    });

    connect(m_converter, &BatchConverter::taskFinished, this,
            [this, nameOf](const QString& id, TaskStatus status, const QString& text, qint64 size) {
                m_model->updateTask(m_model->rowOfId(id), [&](StickerTask& t) {
                    t.status = status;
                    t.statusText = text;
                    t.progress = status == TaskStatus::Succeeded ? 100 : t.progress;
                    t.outputSize = size;
                }, false);
                ++m_busyDone;
                if (status == TaskStatus::Succeeded)
                    logInfo(QStringLiteral("✅ [%1] 转换成功 (%2)").arg(nameOf(id), formatBytes(size)));
                else if (status == TaskStatus::Failed)
                    logInfo(QStringLiteral("❌ [%1] %2").arg(nameOf(id), text));
                refreshUi();
            });

    connect(m_converter, &BatchConverter::packaging, this, [this] {
        m_busyHint = QStringLiteral("正在写出贴纸文件...");
        refreshUi();
    });

    connect(m_converter, &BatchConverter::finished, this,
            [this](const OutputPackager::Result& result, int succeeded, int failed, int cancelled) {
                for (auto it = result.finalPaths.cbegin(); it != result.finalPaths.cend(); ++it) {
                    const QString path = it.value();
                    m_model->updateTask(m_model->rowOfId(it.key()), [&](StickerTask& t) { t.outputPath = path; }, false);
                }
                for (const QString& line : result.logLines)
                    logInfo(line);
                if (!result.locations.isEmpty())
                    m_lastOutput = result.locations.first();
                setBusy(Busy::None, 0);
                if (cancelled > 0)
                    logInfo(QStringLiteral("🛑 已停止：%1 成功，%2 失败，%3 已取消").arg(succeeded).arg(failed).arg(cancelled));
                else
                    logInfo(QStringLiteral("🎉 批量转换已全部完成！（%1 成功，%2 失败）").arg(succeeded).arg(failed));
            });
}

// ---------------------------------------------------------------- 列表操作

void MainWindow::removeSelected()
{
    if (m_busy != Busy::None)
        return;
    const QList<int> rows = selectedRows();
    if (rows.isEmpty())
        return;
    m_model->removeTaskRows(rows);
    logInfo(QStringLiteral("🗑️ 已移除 %1 项").arg(rows.size()));
}

void MainWindow::clearList()
{
    if (m_busy != Busy::None) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("任务正在进行中，请先停止！"));
        return;
    }
    if (m_model->rowCount() == 0)
        return;
    m_model->clearTasks();
    logInfo(QStringLiteral("🧹 已清空列表"));
}

void MainWindow::requestStop()
{
    if (m_busy == Busy::None)
        return;
    if (m_busy == Busy::Converting)
        m_converter->cancel();
    else if (m_cancel)
        m_cancel->store(true);
    logInfo(QStringLiteral("🛑 正在停止..."));
}

void MainWindow::openOutputDir()
{
    // 优先打开最近一次生成的压缩包或文件夹所在位置
    if (!m_lastOutput.isEmpty() && QFileInfo::exists(m_lastOutput)) {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(m_lastOutput)});
        return;
#endif
    }
    const QString dir = m_settings.outputDir();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::openDataDir()
{
    const QString dir = m_settings.resolvedDataDir();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::notImplemented(const QString& feature)
{
    logInfo(QStringLiteral("🚧 %1：功能尚未实现").arg(feature));
}

// ---------------------------------------------------------------- 状态刷新

void MainWindow::setBusy(Busy kind, int total)
{
    m_busy = kind;
    m_busyDone = 0;
    m_busyTotal = total;
    m_busyHint.clear();
    refreshUi();
}

void MainWindow::refreshUi()
{
    const bool busy = m_busy != Busy::None;
    SettingsPanel::BusyState state;
    state.active = busy;
    state.converting = m_busy == Busy::Converting;
    state.title = busyTitle();
    state.done = m_busyDone;
    state.total = m_busyTotal;
    state.hint = m_busyHint;
    m_panel->setBusyState(state);

    m_btnImport->setEnabled(!busy);
    m_btnAi->setEnabled(!busy);
    m_btnClip->setEnabled(!busy);

    const bool empty = m_model->rowCount() == 0;
    m_listStack->setCurrentWidget(empty ? static_cast<QWidget*>(m_dropZone) : m_table);
    if (empty && (m_busy == Busy::Importing || m_busy == Busy::Restoring)) {
        const QString title = m_busy == Busy::Restoring ? QStringLiteral("正在恢复上次进度") : QStringLiteral("正在导入");
        m_dropZone->showProgress(title, m_busyHint, m_busyDone, m_busyTotal);
    } else {
        m_dropZone->showIdle();
    }

    updateBadge();
    updateLogSummary();
    updateAiButton();
}

void MainWindow::updateBadge()
{
    const int total = m_model->rowCount();
    const int selected = int(selectedRows().size());
    if (m_busy != Busy::None)
        m_header->setBadge(QStringLiteral("%1 %2 / %3").arg(busyTitle()).arg(m_busyDone).arg(m_busyTotal), false);
    else if (selected > 0)
        m_header->setBadge(QStringLiteral("已选 %1 / %2 项").arg(selected).arg(total), false);
    else
        m_header->setBadge(QStringLiteral("已添加 %1 项").arg(total), total == 0);
}

void MainWindow::updateLogSummary()
{
    int succeeded = 0;
    int failed = 0;
    int running = 0;
    for (const StickerTask& task : m_model->tasks()) {
        succeeded += task.status == TaskStatus::Succeeded;
        failed += task.status == TaskStatus::Failed;
        running += task.status == TaskStatus::Running;
    }
    const int total = m_model->rowCount();
    QString summary;
    switch (m_busy) {
    case Busy::None:
        summary = QStringLiteral("就绪: 共 %1 项 (%2 完成, %3 失败)").arg(total).arg(succeeded).arg(failed);
        break;
    case Busy::Converting:
        summary = QStringLiteral("正在转码: %1 进行中, %2 完成 / 共 %3 项").arg(running).arg(succeeded).arg(total);
        break;
    default:
        summary = QStringLiteral("%1: %2 / %3").arg(busyTitle()).arg(m_busyDone).arg(m_busyTotal);
        break;
    }
    m_log->setSummary(summary);
}

void MainWindow::updateAiButton()
{
    const int selected = int(selectedRows().size());
    m_btnAi->setText(selected > 0 ? QStringLiteral("✨ AI 匹配 (%1项)").arg(selected) : QStringLiteral("✨ AI 匹配 Emoji"));
}

QString MainWindow::busyTitle() const
{
    switch (m_busy) {
    case Busy::Converting: return QStringLiteral("正在转码");
    case Busy::Exporting: return QStringLiteral("正在导出");
    case Busy::Importing: return QStringLiteral("正在导入");
    case Busy::Restoring: return QStringLiteral("正在恢复");
    case Busy::None: break;
    }
    return {};
}

QList<int> MainWindow::selectedRows() const
{
    QList<int> rows;
    for (const QModelIndex& index : m_table->selectionModel()->selectedRows())
        rows << index.row();
    std::sort(rows.begin(), rows.end());
    return rows;
}

QStringList MainWindow::scanFolder(const QString& dir)
{
    QStringList files;
    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (MediaKinds::isSupportedMedia(path))
            files << path;
    }
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(files.begin(), files.end(), [&](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });
    return files;
}

QString MainWindow::findManifestIn(const QString& dir)
{
    const QStringList names{QStringLiteral("sticker_list.json"), QStringLiteral("stickers.json")};
    const QDir root(dir);
    for (const QString& name : names) {
        if (root.exists(name))
            return root.filePath(name);
    }
    for (const QString& sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QDir child(root.filePath(sub));
        for (const QString& name : names) {
            if (child.exists(name))
                return child.filePath(name);
        }
    }
    return {};
}

} // namespace tsm
