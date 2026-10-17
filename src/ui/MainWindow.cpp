#include "ui/MainWindow.h"

#include "app/AiMatcher.h"
#include "app/AppPaths.h"
#include "app/BatchConverter.h"
#include "app/Exporter.h"
#include "app/Logger.h"
#include "app/SessionStore.h"
#include "core/Format.h"
#include "core/MediaKinds.h"
#include "core/TaskListModel.h"
#include "media/FFmpegTools.h"
#include "media/ProxyCache.h"
#include "media/ThumbnailService.h"
#include "ui/AiSettingsDialog.h"
#include "ui/DropOverlay.h"
#include "ui/DropZone.h"
#include "ui/HeaderBar.h"
#include "ui/LogPanel.h"
#include "ui/SettingsPanel.h"
#include "ui/KeywordDialog.h"
#include "ui/TaskTableDelegates.h"
#include "ui/TaskTableView.h"
#include "ui/editor/ClipEditorDialog.h"

#include <QCloseEvent>
#include <QCollator>
#include <QDateTime>
#include <QTimer>
#include <QDesktopServices>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QItemSelectionModel>
#include <QtConcurrent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
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
    , m_exporter(new Exporter(this))
    , m_aiMatcher(new AiMatcher(this))
    , m_thumbs(new ThumbnailService(this))
    , m_proxies(new ProxyCache(this))
    , m_session(new SessionStore(m_model, this))
{
    setWindowTitle(QStringLiteral("Telegram Sticker Maker"));
    resize(1500, 820);
    setMinimumSize(1180, 640);
    setAcceptDrops(true);
    m_workers.setMaxThreadCount(1);

    applyDataDir();
    buildUi();
    connectSignals();
    m_model->setOutputLocation(m_settings.outputDir(), m_settings.saveBesideSource());
    refreshUi();
    refreshCacheInfo();

    logInfo(QStringLiteral("✅ 启动就绪"));
    if (FFmpegTools::ffmpegPath().isEmpty() || FFmpegTools::ffprobePath().isEmpty())
        logInfo(QStringLiteral("⚠️ 未找到 ffmpeg / ffprobe，请确认它们与程序位于同一目录"));

    // 启动时恢复上次进度；恢复结束前不触发保存
    m_session->setSuspended(true);
    m_session->setPath(AppPaths::sessionFile(m_settings.resolvedDataDir()));
    connect(m_session, &SessionStore::saveFailed, this,
            [](const QString& error) { logInfo(QStringLiteral("⚠️ 会话保存失败: %1").arg(error)); });
    QTimer::singleShot(0, this, &MainWindow::restoreSession);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
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
    m_table = new TaskTableView;
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
    m_table->setButtonColumns({TaskListModel::ColKeywords, TaskListModel::ColActions});
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

    m_table->setItemDelegateForColumn(TaskListModel::ColPreview, new PreviewDelegate(m_model, m_thumbs, m_table));
    auto* actions = new ActionsDelegate(m_model, m_table);
    m_table->setItemDelegateForColumn(TaskListModel::ColActions, actions);
    connect(actions, &ActionsDelegate::triggered, this, &MainWindow::onRowAction);
    auto* keywords = new KeywordsDelegate(m_model, m_table);
    m_table->setItemDelegateForColumn(TaskListModel::ColKeywords, keywords);
    connect(keywords, &KeywordsDelegate::clicked, this, [this](int row) { openKeywordDialog({row}); });
    m_table->setMouseTracking(true);
    // TODO(主列表): 再次单击已选中行取消选中、点击空白取消选择、右键菜单（需求 3.3 / 3.4）
}

void MainWindow::connectSignals()
{
    connect(&Logger::instance(), &Logger::lineAdded, m_log, &LogPanel::appendLine);

    // 工具栏
    connect(m_btnAddFiles, &QPushButton::clicked, this, &MainWindow::addFilesDialog);
    connect(m_btnAddFolder, &QPushButton::clicked, this, &MainWindow::addFolderDialog);
    connect(m_btnImport, &QPushButton::clicked, this, &MainWindow::importDialog);
    connect(m_btnClip, &QPushButton::clicked, this, &MainWindow::openClipTool);
    connect(m_thumbs, &ThumbnailService::ready, m_table->viewport(), qOverload<>(&QWidget::update));
    // 有选中项时只识别选中项
    connect(m_btnAi, &QPushButton::clicked, this, [this] { startAiMatch(selectedRows()); });
    connect(m_btnAiSettings, &QPushButton::clicked, this, &MainWindow::openAiSettings);
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

    // 右侧面板
    connect(m_panel, &SettingsPanel::startRequested, this, [this] { startConversion({}); });
    connect(m_panel, &SettingsPanel::exportRequested, this, &MainWindow::startExport);
    connect(m_panel, &SettingsPanel::stopRequested, this, &MainWindow::requestStop);
    connect(m_panel, &SettingsPanel::openOutputRequested, this, &MainWindow::openOutputDir);
    connect(m_panel, &SettingsPanel::openDataDirRequested, this, &MainWindow::openDataDir);
    connect(m_panel, &SettingsPanel::clearCacheRequested, this, &MainWindow::clearPreviewCacheClicked);
    connect(m_panel, &SettingsPanel::outputLocationChanged, this,
            [this] { m_model->setOutputLocation(m_settings.outputDir(), m_settings.saveBesideSource()); });
    connect(m_panel, &SettingsPanel::dataDirChanged, this, [this](const QString&, const QString& newDir) {
        // 新目录还没有会话文件时，把旧目录的会话复制过去
        m_session->moveTo(AppPaths::sessionFile(newDir));
        m_session->saveNow();
        applyDataDir();
        refreshCacheInfo();
        logInfo(QStringLiteral("💾 数据目录已切换为 %1").arg(QDir::toNativeSeparators(newDir)));
    });

    connectConverter();
    connectExporter();
    connectAiMatcher();

    // 列表快捷键与"点击空白取消选择"在 eventFilter 里处理
    qApp->installEventFilter(this);
    connect(m_table, &TaskTableView::rowContextMenuRequested, this, &MainWindow::showRowMenu);
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
    // 关闭前强制保存一次（恢复尚未完成时不保存，以免丢掉还没恢复的条目）
    if (m_busy != Busy::Restoring)
        m_session->saveNow();
    if (m_cancel)
        m_cancel->store(true);
    m_converter->cancel();
    m_exporter->cancel();
    m_aiMatcher->cancel();
    event->accept();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    auto* target = qobject_cast<QWidget*>(watched);
    if (!target || target->window() != this || QApplication::activePopupWidget() || QApplication::activeModalWidget())
        return QMainWindow::eventFilter(watched, event);

    if (event->type() == QEvent::KeyPress) {
        // 正在输入框里打字（含行内编辑 Emoji）时快捷键不生效
        QWidget* focus = QApplication::focusWidget();
        const bool typing = qobject_cast<QLineEdit*>(focus) || qobject_cast<QAbstractSpinBox*>(focus)
            || qobject_cast<QComboBox*>(focus) || qobject_cast<QPlainTextEdit*>(focus);
        if (typing)
            return QMainWindow::eventFilter(watched, event);
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->matches(QKeySequence::SelectAll)) {
            m_table->selectAllRows();
            return true;
        }
        if (key->key() == Qt::Key_Delete && key->modifiers() == Qt::NoModifier) {
            removeSelected(); // 忙碌时无效
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            m_table->clearRowSelection();
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonPress) {
        // 点击窗口其他非交互区域：取消选择（列表内部的点击由 TaskTableView 处理）
        if (target == m_table || m_table->isAncestorOf(target))
            return QMainWindow::eventFilter(watched, event);
        for (QWidget* w = target; w && w != this; w = w->parentWidget()) {
            if (qobject_cast<QAbstractButton*>(w) || qobject_cast<QLineEdit*>(w) || qobject_cast<QComboBox*>(w)
                || qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QAbstractSlider*>(w) || qobject_cast<QPlainTextEdit*>(w)) {
                return QMainWindow::eventFilter(watched, event);
            }
        }
        m_table->clearRowSelection();
    }
    return QMainWindow::eventFilter(watched, event);
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
    // 会话恢复或导入进行中：排队，结束后再添加
    if (m_restorePending || m_busy == Busy::Restoring || m_busy == Busy::Importing) {
        m_pendingPaths << paths;
        return;
    }
    QStringList media;
    QStringList manifests;
    int unsupported = 0;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            // 文件夹里有本工具导出的清单时改为走导入
            if (!ImportSource::findManifestIn(info.absoluteFilePath()).isEmpty())
                manifests << info.absoluteFilePath();
            else
                media << scanFolder(info.absoluteFilePath());
        } else if (MediaKinds::isImportFile(path)) {
            manifests << info.absoluteFilePath();
        } else if (MediaKinds::isSupportedMedia(path)) {
            media << info.absoluteFilePath();
        } else {
            ++unsupported;
        }
    }
    if (unsupported > 0)
        logInfo(QStringLiteral("⚠️ 跳过 %1 个不支持的文件").arg(unsupported));
    if (!manifests.isEmpty()) {
        // 一次导入一个，其余（含普通素材）排在后面按顺序处理
        if (m_busy == Busy::None) {
            m_pendingPaths << manifests.mid(1) << media;
            importManifest(manifests.first());
        } else {
            logInfo(QStringLiteral("⚠️ 任务正在进行中，请先停止或等待完成"));
        }
        return;
    }
    if (!media.isEmpty())
        startProbing(media);
    else if (unsupported == 0 && paths.size() == 1 && QFileInfo(paths.first()).isDir())
        logInfo(QStringLiteral("⚠️ 文件夹中没有找到支持的媒体文件"));
}

void MainWindow::importManifest(const QString& path)
{
    if (m_restorePending || m_busy == Busy::Restoring || m_busy == Busy::Importing) {
        m_pendingPaths << path;
        return;
    }
    if (m_busy != Busy::None) {
        logInfo(QStringLiteral("⚠️ 任务正在进行中，请先停止或等待完成"));
        return;
    }
    const QFileInfo fi(path);
    const bool isZip = fi.isFile() && fi.suffix().compare(u"zip", Qt::CaseInsensitive) == 0;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    setBusy(Busy::Importing, 0);
    m_busyHint = (isZip ? QStringLiteral("正在解压 ") : QStringLiteral("正在读取 ")) + fi.fileName();
    refreshUi();

    const QString cacheDir = AppPaths::importCacheDir(m_settings.resolvedDataDir());
    const auto cancel = m_cancel;
    m_workers.start([this, path, cacheDir, cancel] {
        const ImportSource::Prepared prepared = ImportSource::prepare(path, cacheDir, cancel);
        const bool cancelled = cancel->load();
        QMetaObject::invokeMethod(this, [this, path, prepared, cancelled] { onImportPrepared(path, prepared, cancelled); },
                                  Qt::QueuedConnection);
    });
}

void MainWindow::onImportPrepared(const QString& path, const ImportSource::Prepared& prepared, bool cancelled)
{
    setBusy(Busy::None, 0);
    const QString name = QFileInfo(path).fileName();
    if (cancelled) {
        logInfo(QStringLiteral("🛑 已取消导入，已加入 0 项"));
        m_pendingPaths.clear();
        return;
    }
    if (!prepared.ok) {
        logInfo(QStringLiteral("❌ 导入 %1 失败: %2").arg(name, prepared.error));
        processPendingPaths();
        return;
    }
    if (prepared.reusedExtraction)
        logInfo(QStringLiteral("♻️ %1 之前解压过，直接复用导入缓存").arg(name));
    const QList<StickerList::Entry>& entries = prepared.list.entries;
    if (entries.isEmpty()) {
        logInfo(QStringLiteral("⚠️ %1 中没有贴纸条目").arg(QFileInfo(prepared.manifestPath).fileName()));
        processPendingPaths();
        return;
    }
    const QString kind = prepared.list.kind == StickerList::Kind::StickerList ? QStringLiteral("列表") : QStringLiteral("转换输出");
    logInfo(QStringLiteral("📥 正在导入%1 %2（%3 项）").arg(kind, name).arg(entries.size()));
    startLoading(entries, prepared.baseDir, LoadPurpose::Import);
}

void MainWindow::processPendingPaths()
{
    if (!m_pendingPaths.isEmpty() && m_busy == Busy::None)
        addPaths(std::exchange(m_pendingPaths, {}));
}

void MainWindow::startProbing(const QStringList& files)
{
    QList<StickerList::Entry> entries;
    for (const QString& file : files) {
        StickerList::Entry e;
        e.task.inputPath = file;
        e.task.info.fileName = QFileInfo(file).fileName();
        entries << e;
    }
    startLoading(entries, {}, LoadPurpose::AddFiles);
}

void MainWindow::startLoading(const QList<StickerList::Entry>& entries, const QString& baseDir, LoadPurpose purpose)
{
    if (m_busy != Busy::None) {
        logInfo(QStringLiteral("⚠️ 任务正在进行中，请先停止或等待完成"));
        return;
    }
    m_loadPurpose = purpose;
    m_loadAdded = m_loadFailed = m_loadMissing = m_loadProxies = 0;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    setBusy(purpose == LoadPurpose::Restore ? Busy::Restoring : Busy::Importing, int(entries.size()));

    const auto cancel = m_cancel;
    const QString proxyDir = m_proxies->cacheDir();
    m_workers.start([this, entries, baseDir, cancel, proxyDir] {
        for (const StickerList::Entry& entry : entries) {
            if (cancel->load())
                break;
            const QString name = entry.task.info.fileName.isEmpty() ? QFileInfo(entry.task.inputPath).fileName()
                                                                    : entry.task.info.fileName;
            QMetaObject::invokeMethod(this, [this, name] {
                m_busyHint = QStringLiteral("正在解析 ") + name;
                refreshUi();
            }, Qt::QueuedConnection);
            const ListLoader::Result result = ListLoader::load(entry, baseDir);
            // 附带的预览缓存装回缓存目录
            bool proxyInstalled = false;
            if (result.outcome == ListLoader::Outcome::Loaded && !entry.proxy.isEmpty()) {
                const QString proxyFile = ListLoader::resolvePath(entry.proxy, baseDir, {});
                proxyInstalled = ImportSource::installProxy(proxyFile, result.task.inputPath, proxyDir);
            }
            QMetaObject::invokeMethod(this, [this, result, name, proxyInstalled] {
                m_loadProxies += proxyInstalled;
                onEntryLoaded(result, name);
            }, Qt::QueuedConnection);
        }
        const bool cancelled = cancel->load();
        QMetaObject::invokeMethod(this, [this, cancelled] { onLoadingFinished(cancelled); }, Qt::QueuedConnection);
    });
}

void MainWindow::onEntryLoaded(const ListLoader::Result& result, const QString& name)
{
    ++m_busyDone;
    switch (result.outcome) {
    case ListLoader::Outcome::Loaded:
        m_model->appendTasks({result.task});
        ++m_loadAdded;
        break;
    case ListLoader::Outcome::Missing:
        ++m_loadMissing;
        break;
    case ListLoader::Outcome::Failed:
        ++m_loadFailed;
        logInfo(QStringLiteral("❌ [%1] 解析失败: %2").arg(name, result.error));
        break;
    }
    refreshUi();
}

void MainWindow::onLoadingFinished(bool cancelled)
{
    const LoadPurpose purpose = m_loadPurpose;
    setBusy(Busy::None, 0);
    const QString failed = m_loadFailed > 0 ? QStringLiteral("，%1 项解析失败").arg(m_loadFailed) : QString();

    switch (purpose) {
    case LoadPurpose::Restore:
        if (cancelled)
            logInfo(QStringLiteral("🛑 已停止恢复，已恢复 %1 项").arg(m_loadAdded));
        else
            logInfo(QStringLiteral("📂 已恢复上次进度：%1 项%2").arg(m_loadAdded).arg(failed));
        if (m_loadMissing > 0)
            logInfo(QStringLiteral("⚠️ 有 %1 个源文件找不到，已跳过（可能被移动或删除）").arg(m_loadMissing));
        // 恢复期间不保存，结束后保存一次
        m_session->setSuspended(false);
        m_session->saveNow();
        break;
    case LoadPurpose::Import:
        if (cancelled)
            logInfo(QStringLiteral("🛑 已取消导入，已加入 %1 项").arg(m_loadAdded));
        else
            logInfo(QStringLiteral("📥 已导入 %1 项%2").arg(m_loadAdded).arg(failed));
        if (m_loadMissing > 0)
            logInfo(QStringLiteral("⚠️ 有 %1 个源文件缺失，已跳过").arg(m_loadMissing));
        if (m_loadProxies > 0) {
            logInfo(QStringLiteral("🎞️ 已装回 %1 个预览缓存").arg(m_loadProxies));
            refreshCacheInfo();
        }
        break;
    case LoadPurpose::AddFiles:
        if (cancelled)
            logInfo(QStringLiteral("🛑 已取消导入，已加入 %1 项").arg(m_loadAdded));
        else
            logInfo(QStringLiteral("➕ 已添加 %1 项%2").arg(m_loadAdded).arg(failed));
        break;
    }
    // 用户停止导入时，排队的其余导入一并取消；恢复结束后照常添加启动参数里的文件
    if (cancelled && purpose != LoadPurpose::Restore)
        m_pendingPaths.clear();
    processPendingPaths();
}

// ---------------------------------------------------------------- 会话

void MainWindow::restoreSession()
{
    m_restorePending = false;
    // 没有可恢复的内容：直接开始自动保存，并处理启动参数里的文件
    const auto nothingToRestore = [this] {
        m_session->setSuspended(false);
        processPendingPaths();
    };
    const QString path = m_session->path();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        nothingToRestore();
        return;
    }
    const StickerList::ParseResult parsed = StickerList::parse(file.readAll());
    file.close();
    if (!parsed.ok || parsed.kind != StickerList::Kind::StickerList) {
        // 损坏的会话改名备份，不覆盖
        const QString backup = path + QStringLiteral(".broken");
        QFile::remove(backup);
        QFile::rename(path, backup);
        logInfo(QStringLiteral("⚠️ 会话文件无法读取，已备份为 %1").arg(QDir::toNativeSeparators(backup)));
        m_session->setPath(path);
        nothingToRestore();
        return;
    }
    if (parsed.entries.isEmpty()) {
        nothingToRestore();
        return;
    }
    startLoading(parsed.entries, QFileInfo(path).absolutePath(), LoadPurpose::Restore);
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

// ---------------------------------------------------------------- 导出

void MainWindow::startExport(int mode)
{
    if (m_busy != Busy::None)
        return;
    if (m_model->rowCount() == 0) {
        logInfo(QStringLiteral("⚠️ 列表为空，没有可导出的内容"));
        return;
    }

    ExportSettings settings;
    settings.kind = mode == SettingsPanel::ExportTrimmed ? ExportKind::Trimmed : ExportKind::Sources;
    settings.asZip = m_settings.outputAsZip();
    settings.destDir = m_settings.outputDir();
    settings.workRoot = QDir(m_settings.resolvedDataDir()).filePath(QStringLiteral("work"));
    settings.proxyCacheDir = m_proxies->cacheDir();
    if (settings.kind == ExportKind::Sources && !settings.asZip) {
        // 不压缩：先选父文件夹，在里面建 sticker_bundle_YYYYMMDD_HHMMSS
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择导出位置"), settings.destDir);
        if (dir.isEmpty())
            return;
        settings.destDir = dir;
    }

    QList<ExportJob> jobs;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        m_model->updateTask(row, [](StickerTask& t) {
            t.resetRuntimeState();
            t.statusText = QStringLiteral("等待导出...");
        }, false);
        jobs << ExportJob{m_model->taskAt(row), m_model->targetFileName(row), m_model->targetDirectory(row)};
    }

    setBusy(Busy::Exporting, int(jobs.size()));
    const QString what = settings.kind == ExportKind::Sources ? QStringLiteral("原片 + 参数清单") : QStringLiteral("已裁切源文件");
    logInfo(QStringLiteral("📤 开始导出 %1 项（%2）").arg(jobs.size()).arg(what));
    m_exporter->start(jobs, settings);
}

void MainWindow::connectExporter()
{
    const auto nameOf = [this](const QString& id) {
        const int row = m_model->rowOfId(id);
        return row >= 0 ? m_model->taskAt(row).info.fileName : QString();
    };

    connect(m_exporter, &Exporter::taskStarted, this, [this, nameOf](const QString& id) {
        m_model->updateTask(m_model->rowOfId(id), [](StickerTask& t) {
            t.status = TaskStatus::Running;
            t.statusText = QStringLiteral("正在导出...");
            t.progress = 0;
        }, false);
        m_busyHint = QStringLiteral("正在导出 ") + nameOf(id);
        refreshUi();
    });

    connect(m_exporter, &Exporter::taskProgress, this, [this, nameOf](const QString& id, int percent, const QString& text) {
        m_model->updateTask(m_model->rowOfId(id), [&](StickerTask& t) {
            t.progress = percent;
            t.statusText = text;
        }, false);
        m_busyHint = QStringLiteral("[%1] %2").arg(nameOf(id), text);
        refreshUi();
    });

    connect(m_exporter, &Exporter::taskFinished, this, [this](const QString& id, TaskStatus status, const QString& text, qint64 size) {
        const int row = m_model->rowOfId(id);
        // 打包失败时同一行会再收到一次结果，只在第一次计数
        const bool first = row >= 0 && (m_model->taskAt(row).status == TaskStatus::Running
                                         || m_model->taskAt(row).status == TaskStatus::Pending);
        m_model->updateTask(row, [&](StickerTask& t) {
            t.status = status;
            t.statusText = text;
            t.progress = status == TaskStatus::Succeeded ? 100 : t.progress;
            t.outputSize = size;
        }, false);
        if (first)
            ++m_busyDone;
        refreshUi();
    });

    connect(m_exporter, &Exporter::packaging, this, [this](const QString& text) {
        m_busyHint = text;
        refreshUi();
    });

    connect(m_exporter, &Exporter::finished, this, [this](const ExportSummary& summary) {
        for (const QString& line : summary.logLines)
            logInfo(line);
        if (!summary.locations.isEmpty())
            m_lastOutput = summary.locations.first();
        setBusy(Busy::None, 0);
        const QString failed = summary.failed > 0 ? QStringLiteral("，%1 项失败").arg(summary.failed) : QString();
        if (summary.cancelled > 0)
            logInfo(QStringLiteral("🛑 已停止导出：%1 项已导出，%2 项已取消%3").arg(summary.exported).arg(summary.cancelled).arg(failed));
        else
            logInfo(QStringLiteral("📤 导出完成：%1 项%2").arg(summary.exported).arg(failed));
        if (summary.missing > 0)
            logInfo(QStringLiteral("⚠️ 有 %1 个源文件缺失，已跳过").arg(summary.missing));
    });
}

// ---------------------------------------------------------------- AI 匹配 Emoji

void MainWindow::openAiSettings()
{
    AiSettingsDialog dialog(m_settings, this);
    dialog.exec();
}

void MainWindow::startAiMatch(QList<int> rows)
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
    AiConfig config;
    config.baseUrl = m_settings.aiBaseUrl();
    config.model = m_settings.aiModel();
    config.apiKey = m_settings.aiApiKey();
    if (const QString problem = AiMatcher::validate(config); !problem.isEmpty()) {
        logInfo(QStringLiteral("⚠️ ") + problem);
        openAiSettings();
        return;
    }

    QList<StickerTask> tasks;
    for (const int row : rows)
        tasks << m_model->taskAt(row);
    setBusy(Busy::AiMatching, int(tasks.size()));
    logInfo(QStringLiteral("✨ 开始 AI 匹配 Emoji：%1 项（%2）").arg(tasks.size()).arg(config.model));
    m_aiMatcher->start(tasks, config, QDir(m_settings.resolvedDataDir()).filePath(QStringLiteral("work")));
}

void MainWindow::connectAiMatcher()
{
    const auto nameOf = [this](const QString& id) {
        const int row = m_model->rowOfId(id);
        return row >= 0 ? m_model->taskAt(row).info.fileName : QString();
    };

    connect(m_aiMatcher, &AiMatcher::itemStarted, this, [this, nameOf](const QString& id) {
        m_busyHint = QStringLiteral("正在识别 ") + nameOf(id);
        refreshUi();
    });

    connect(m_aiMatcher, &AiMatcher::itemFinished, this,
            [this, nameOf](const QString& id, const QString& emoji, const QString& error) {
                ++m_busyDone;
                const QString name = nameOf(id);
                if (!error.isEmpty()) {
                    logInfo(QStringLiteral("❌ [AI Emoji] %1: %2").arg(name, error));
                } else if (emoji.isEmpty()) {
                    logInfo(QStringLiteral("[AI Emoji] %1 -> 无匹配").arg(name));
                } else {
                    // 覆盖该行的 Emoji，文件名随之更新
                    m_model->updateTask(m_model->rowOfId(id), [&](StickerTask& t) { t.emoji = emoji; });
                    logInfo(QStringLiteral("[AI Emoji] %1 -> %2").arg(name, emoji));
                }
                refreshUi();
            });

    connect(m_aiMatcher, &AiMatcher::finished, this, [this](int matched, int empty, int failed, bool cancelled) {
        setBusy(Busy::None, 0);
        if (cancelled) {
            logInfo(QStringLiteral("🛑 已停止 AI 匹配：%1 项已匹配").arg(matched));
            return;
        }
        logInfo(QStringLiteral("✨ AI Emoji 智能匹配已完成！（%1 项匹配，%2 项无匹配，%3 项失败）").arg(matched).arg(empty).arg(failed));
    });
}

// ---------------------------------------------------------------- 截取 / 裁切

void MainWindow::openEditor(int row)
{
    if (row < 0 || row >= m_model->rowCount() || m_editorOpen)
        return;
    if (m_busy != Busy::None) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("任务正在进行中，请先停止！"));
        return;
    }
    const QString path = m_model->taskAt(row).inputPath;
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("源文件不存在：%1").arg(QDir::toNativeSeparators(path)));
        return;
    }

    // 属于片段组时一次加载整组，并聚焦到点击的那个
    const QList<int> rows = m_model->clipGroupRows(row);
    QList<StickerTask> clips;
    int focus = 0;
    for (int i = 0; i < rows.size(); ++i) {
        clips << m_model->taskAt(rows.at(i));
        if (rows.at(i) == row)
            focus = i;
    }

    m_editorOpen = true;
    ClipEditorDialog dialog(m_settings, m_thumbs, m_proxies, clips, focus, this);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    m_editorOpen = false;
    refreshCacheInfo();
    if (!accepted)
        return;

    const QList<StickerTask> edited = dialog.clips();
    const StickerTask& base = clips.first();
    if (!base.info.isVideo) {
        const StickerTask e = edited.first();
        m_model->updateTask(row, [&](StickerTask& t) {
            t.crop = e.crop;
            t.cropRadius = e.cropRadius;
            t.mirror = e.mirror;
            t.emoji = e.emoji;
            t.resetRuntimeState();
        });
        logInfo(QStringLiteral("[%1] 已保存裁切样式").arg(base.info.fileName));
        return;
    }

    // 同一视频的所有片段组成片段组，占据原来的位置；已有片段按 ID 更新原行，新增的插入，删除的移除
    QHash<QString, StickerTask> existing;
    for (const StickerTask& t : clips)
        existing.insert(t.clipId, t);
    QString groupId = base.clipGroupId;
    if (groupId.isEmpty())
        groupId = QStringLiteral("clipgrp-%1").arg(QDateTime::currentMSecsSinceEpoch());

    QList<StickerTask> tasks;
    for (const StickerTask& e : edited) {
        StickerTask t;
        if (existing.contains(e.clipId)) {
            t = existing.value(e.clipId);
        } else {
            t = base;
            t.id = StickerTask::newId();
            t.keywords.clear();
        }
        t.clipId = e.clipId;
        t.clipGroupId = groupId;
        t.startTime = e.startTime;
        t.endTime = e.endTime;
        t.crop = e.crop;
        t.cropRadius = e.cropRadius;
        t.mirror = e.mirror;
        t.emoji = e.emoji;
        t.resetRuntimeState();
        tasks << t;
    }
    const int first = m_model->replaceRows(rows, tasks);

    // 确认后自动选中这一组所有行
    QList<int> groupRows;
    for (int i = 0; i < tasks.size(); ++i)
        groupRows << first + i;
    m_table->selectRows(groupRows);
    m_table->scrollTo(m_model->index(first, 0));
    logInfo(QStringLiteral("[%1] 已保存截取列表，共 %2 个贴纸").arg(base.info.fileName).arg(tasks.size()));
}

void MainWindow::openClipTool()
{
    // 当前选中的是视频就直接打开
    const QList<int> rows = selectedRows();
    if (!rows.isEmpty() && m_model->taskAt(rows.first()).info.isVideo) {
        openEditor(rows.first());
        return;
    }
    if (m_busy != Busy::None) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("任务正在进行中，请先停止！"));
        return;
    }
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择要截取的视频"), m_lastDir, MediaKinds::openFileFilter());
    if (file.isEmpty())
        return;
    m_lastDir = QFileInfo(file).absolutePath();
    const MediaKind kind = MediaKinds::classify(file);
    if (kind != MediaKind::Video && kind != MediaKind::AnimatedImage) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("所选文件不是视频格式！"));
        return;
    }

    // 先在后台解析，加入列表后再打开
    auto* watcher = new QFutureWatcher<ProbeResult>(this);
    connect(watcher, &QFutureWatcher<ProbeResult>::finished, this, [this, watcher, file] {
        const ProbeResult result = watcher->result();
        watcher->deleteLater();
        if (!result.ok) {
            logInfo(QStringLiteral("❌ [%1] 解析失败: %2").arg(QFileInfo(file).fileName(), result.error));
            return;
        }
        m_model->appendTasks({StickerTask::fromMedia(file, result.info)});
        logInfo(QStringLiteral("➕ 已添加 1 项"));
        openEditor(m_model->rowCount() - 1);
    });
    watcher->setFuture(QtConcurrent::run([file] { return MediaProbe::probe(file); }));
}

void MainWindow::onRowAction(int row, int action)
{
    if (row < 0 || row >= m_model->rowCount())
        return;
    switch (action) {
    case ActionsDelegate::Edit:
        openEditor(row);
        break;
    case ActionsDelegate::OpenFolder: {
        const QString path = m_model->taskAt(row).outputPath;
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
        break;
    }
    case ActionsDelegate::Remove:
        if (m_busy != Busy::None)
            return;
        logInfo(QStringLiteral("🗑️ 已移除 %1").arg(m_model->taskAt(row).info.fileName));
        m_model->removeTaskRows({row});
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- 数据目录与预览缓存

void MainWindow::applyDataDir()
{
    const QString preview = AppPaths::previewCacheDir(m_settings.resolvedDataDir());
    m_thumbs->setCacheDir(QDir(preview).filePath(QStringLiteral("thumbs")));
    m_proxies->setCacheDir(QDir(preview).filePath(QStringLiteral("proxies")));
}

void MainWindow::refreshCacheInfo()
{
    if (m_panel)
        m_panel->setCacheInfo(measurePreviewCache(AppPaths::previewCacheDir(m_settings.resolvedDataDir())).text());
}

void MainWindow::clearPreviewCacheClicked()
{
    if (m_busy != Busy::None || m_editorOpen) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("任务正在进行中，请先停止！"));
        return;
    }
    m_proxies->cancelAll();
    const QString dir = AppPaths::previewCacheDir(m_settings.resolvedDataDir());
    const CacheUsage before = measurePreviewCache(dir);
    if (clearPreviewCache(dir))
        logInfo(QStringLiteral("🧹 已清除预览缓存（%1）").arg(before.text()));
    else
        logInfo(QStringLiteral("⚠️ 部分预览缓存无法删除（可能正在使用）"));
    m_thumbs->clearMemory();
    m_table->viewport()->update();
    refreshCacheInfo();
}

// ---------------------------------------------------------------- 右键菜单与关键词

void MainWindow::showRowMenu(const QPoint& globalPos)
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty())
        return;
    const int n = int(rows.size());
    const bool single = n == 1;
    const bool busy = m_busy != Busy::None;
    const QString suffix = single ? QString() : QStringLiteral(" (%1)").arg(n);

    QMenu menu(this);
    QAction* convert = menu.addAction(QStringLiteral("🚀 转换选中贴纸") + suffix);
    QAction* keywords = menu.addAction(QStringLiteral("🏷️ 设置选中贴纸的关键词") + suffix);
    QAction* ai = menu.addAction(single ? QStringLiteral("✨ AI 识别此项 Emoji") : QStringLiteral("✨ AI 识别选中项") + suffix);
    QAction* open = nullptr;
    if (single && m_model->taskAt(rows.first()).status == TaskStatus::Succeeded && !m_model->taskAt(rows.first()).outputPath.isEmpty())
        open = menu.addAction(QStringLiteral("📂 打开所在目录"));
    menu.addSeparator();
    QAction* all = menu.addAction(single ? QStringLiteral("全选（Ctrl+A）") : QStringLiteral("全选"));
    QAction* invert = menu.addAction(QStringLiteral("反选"));
    QAction* none = single ? nullptr : menu.addAction(QStringLiteral("取消选择"));
    menu.addSeparator();
    QAction* remove = menu.addAction(single ? QStringLiteral("🗑️ 移除此项（Del）") : QStringLiteral("🗑️ 移除选中项") + suffix);
    // 忙碌时"转换""设置关键词"置灰；移除也不可用
    for (QAction* a : {convert, keywords, ai, remove})
        a->setEnabled(!busy);

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    if (chosen == convert)
        startConversion(rows);
    else if (chosen == keywords)
        openKeywordDialog(rows);
    else if (chosen == ai)
        startAiMatch(rows);
    else if (open && chosen == open)
        onRowAction(rows.first(), ActionsDelegate::OpenFolder);
    else if (chosen == all)
        m_table->selectAllRows();
    else if (chosen == invert)
        m_table->invertSelection();
    else if (none && chosen == none)
        m_table->clearRowSelection();
    else if (chosen == remove)
        removeSelected();
}

void MainWindow::openKeywordDialog(const QList<int>& rows)
{
    if (rows.isEmpty())
        return;
    if (m_busy != Busy::None) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("任务正在进行中，请先停止！"));
        return;
    }
    QString subtitle;
    if (rows.size() == 1) {
        const StickerTask& t = m_model->taskAt(rows.first());
        subtitle = (t.emoji.isEmpty() ? QString() : t.emoji + QStringLiteral(" ")) + t.info.fileName;
    } else {
        subtitle = QStringLiteral("已选 %1 项贴纸").arg(rows.size());
    }
    // 批量：所有选中项的关键词完全相同时预填，否则为空
    QStringList initial = m_model->taskAt(rows.first()).keywords;
    for (const int row : rows) {
        if (m_model->taskAt(row).keywords != initial) {
            initial.clear();
            break;
        }
    }

    KeywordDialog dialog(subtitle, initial, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QStringList keywords = dialog.keywords();
    for (const int row : rows)
        m_model->updateTask(row, [&](StickerTask& t) { t.keywords = keywords; });
    if (keywords.isEmpty())
        logInfo(QStringLiteral("🏷️ 已清空 %1 项的关键词").arg(rows.size()));
    else
        logInfo(QStringLiteral("🏷️ 已为 %1 项设置关键词：%2").arg(rows.size()).arg(keywords.join(QStringLiteral("、"))));
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
    else if (m_busy == Busy::Exporting)
        m_exporter->cancel();
    else if (m_busy == Busy::AiMatching)
        m_aiMatcher->cancel();
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
    case Busy::AiMatching: return QStringLiteral("AI 识别中");
    case Busy::None: break;
    }
    return {};
}

QList<int> MainWindow::selectedRows() const
{
    return m_table->selectedRowList();
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

} // namespace tsm
