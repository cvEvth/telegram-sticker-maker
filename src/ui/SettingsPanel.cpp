#include "ui/SettingsPanel.h"

#include "app/AppPaths.h"
#include "app/AppSettings.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace tsm {

namespace {
QWidget* makeSection(const QString& title, std::initializer_list<QWidget*> rows)
{
    auto* box = new QFrame;
    box->setObjectName(QStringLiteral("Section"));
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    auto* label = new QLabel(title);
    label->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(label);
    for (QWidget* row : rows)
        layout->addWidget(row);
    return box;
}

// 一行横排控件；stretchIndex 指定哪个控件占满剩余宽度
QWidget* makeRow(std::initializer_list<QWidget*> widgets, int stretchIndex = -1)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    int i = 0;
    for (QWidget* w : widgets)
        layout->addWidget(w, i++ == stretchIndex ? 1 : 0);
    return row;
}

QLabel* makeHint(const QString& text)
{
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("Hint"));
    label->setWordWrap(true);
    return label;
}

// 长路径没有空格，插入零宽空格让 QLabel 能在分隔符处换行
QString wrappablePath(const QString& path)
{
    QString native = QDir::toNativeSeparators(path);
    return native.replace(QDir::separator(), QString(QDir::separator()) + QChar(0x200B));
}
} // namespace

SettingsPanel::SettingsPanel(AppSettings& settings, QWidget* parent)
    : QFrame(parent)
    , m_s(settings)
{
    setObjectName(QStringLiteral("SettingsPanel"));
    setFixedWidth(360);

    // 1. 输出规格
    m_modeSticker = new QPushButton(QStringLiteral("🎨 标准贴纸 (512px)"));
    m_modeEmoji = new QPushButton(QStringLiteral("😀 自定义表情 (100px)"));
    auto* modeGroup = new QButtonGroup(this);
    for (QPushButton* button : {m_modeSticker, m_modeEmoji}) {
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("SegmentButton"));
        button->setCursor(Qt::PointingHandCursor);
    }
    modeGroup->addButton(m_modeSticker, int(OutputMode::Sticker));
    modeGroup->addButton(m_modeEmoji, int(OutputMode::Emoji));
    (m_s.outputMode() == OutputMode::Emoji ? m_modeEmoji : m_modeSticker)->setChecked(true);
    connect(modeGroup, &QButtonGroup::idClicked, this, [this](int id) { m_s.setOutputMode(OutputMode(id)); });

    // 2. 编码预设
    m_preset = new QComboBox;
    m_preset->addItem(QStringLiteral("🎨 动漫 / 插画（轻度锐化）"), int(EncodePreset::Anime));
    m_preset->addItem(QStringLiteral("🎬 真人实拍（平滑降噪）"), int(EncodePreset::LiveAction));
    m_preset->addItem(QStringLiteral("⚡ 快速导出（单遍编码）"), int(EncodePreset::Fast));
    m_preset->setCurrentIndex(qMax(0, m_preset->findData(int(m_s.encodePreset()))));
    connect(m_preset, &QComboBox::currentIndexChanged, this,
            [this] { m_s.setEncodePreset(EncodePreset(m_preset->currentData().toInt())); });

    // 3. 输出目录
    m_besideSource = new QCheckBox(QStringLiteral("保存至原文件所在目录"));
    m_besideSource->setChecked(m_s.saveBesideSource());
    m_outputDir = new QLineEdit(QDir::toNativeSeparators(m_s.outputDir()));
    m_browseOutput = new QPushButton(QStringLiteral("浏览..."));
    m_outputRow = makeRow({m_outputDir, m_browseOutput}, 0);
    connect(m_besideSource, &QCheckBox::toggled, this, [this](bool on) {
        m_s.setSaveBesideSource(on);
        refreshOutputDirRow();
        emit outputLocationChanged();
    });
    connect(m_outputDir, &QLineEdit::editingFinished, this, [this] { applyOutputDir(m_outputDir->text()); });
    connect(m_browseOutput, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择输出目录"), m_s.outputDir());
        if (!dir.isEmpty())
            applyOutputDir(dir);
    });

    // 4. 数据目录
    m_dataDir = new QLineEdit(QDir::toNativeSeparators(m_s.dataDirChoice()));
    m_dataDir->setPlaceholderText(QStringLiteral("留空则使用配置目录"));
    m_browseData = new QPushButton(QStringLiteral("浏览..."));
    m_dataResolved = new QLabel;
    m_dataResolved->setObjectName(QStringLiteral("PathLabel"));
    m_dataResolved->setWordWrap(true);
    m_dataResolved->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* openData = new QPushButton(QStringLiteral("打开文件夹"));
    m_clearCache = new QPushButton(QStringLiteral("清除预览缓存"));
    m_cacheInfo = makeHint(QStringLiteral("缓存为空"));
    connect(m_dataDir, &QLineEdit::editingFinished, this, [this] { applyDataDirChoice(m_dataDir->text()); });
    connect(m_browseData, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择数据目录"), m_s.resolvedDataDir());
        if (dir.isEmpty())
            return;
        m_dataDir->setText(QDir::toNativeSeparators(dir));
        applyDataDirChoice(dir);
    });
    connect(openData, &QPushButton::clicked, this, &SettingsPanel::openDataDirRequested);
    connect(m_clearCache, &QPushButton::clicked, this, &SettingsPanel::clearCacheRequested);

    auto* sections = new QWidget;
    auto* sectionsLayout = new QVBoxLayout(sections);
    sectionsLayout->setContentsMargins(0, 0, 4, 0);
    sectionsLayout->setSpacing(10);
    sectionsLayout->addWidget(makeSection(QStringLiteral("🎯 输出规格"), {makeRow({m_modeSticker, m_modeEmoji})}));
    sectionsLayout->addWidget(makeSection(QStringLiteral("🎨 编码预设"), {m_preset}));
    sectionsLayout->addWidget(makeSection(QStringLiteral("📁 输出目录"), {m_besideSource, m_outputRow}));
    sectionsLayout->addWidget(makeSection(
        QStringLiteral("💾 数据目录"),
        {makeHint(QStringLiteral("实际写入所选路径下的 telegram_sticker_maker 文件夹。留空则用配置目录。")),
         makeRow({m_dataDir, m_browseData}, 0), m_dataResolved, makeRow({openData, m_clearCache, m_cacheInfo}, 2)}));
    sectionsLayout->addStretch();

    auto* scroll = new QScrollArea;
    scroll->setWidget(sections);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 5. 底部操作区
    m_progressCard = new QFrame;
    m_progressCard->setObjectName(QStringLiteral("ProgressCard"));
    m_progressTitle = new QLabel;
    m_progressTitle->setObjectName(QStringLiteral("SectionTitle"));
    m_progressCount = makeHint(QString());
    m_progressBar = new QProgressBar;
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);
    m_progressHint = makeHint(QString());
    auto* cardLayout = new QVBoxLayout(m_progressCard);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(6);
    cardLayout->addWidget(makeRow({m_progressTitle, m_progressCount}, 0));
    cardLayout->addWidget(m_progressBar);
    cardLayout->addWidget(m_progressHint);
    m_progressCard->hide();


    m_start = new QPushButton(QStringLiteral("🚀 开始转换"));
    m_start->setObjectName(QStringLiteral("PrimaryButton"));
    m_start->setMinimumHeight(46);
    m_start->setCursor(Qt::PointingHandCursor);

    m_export = new QPushButton(QStringLiteral("📤 导出"));
    auto* exportMenu = new QMenu(m_export);
    m_exportSources = exportMenu->addAction(QString());
    m_exportTrimmed = exportMenu->addAction(QString());
    m_export->setMenu(exportMenu);

    m_zip = new QCheckBox(QStringLiteral("输出为压缩包"));
    m_zip->setChecked(m_s.outputAsZip());
    m_stop = new QPushButton(QStringLiteral("🛑 停止"));
    m_stop->setObjectName(QStringLiteral("DangerButton"));
    m_stop->setEnabled(false);
    m_openOutput = new QPushButton(QStringLiteral("📂 打开输出目录"));

    auto* actions = new QWidget;
    actions->setObjectName(QStringLiteral("ActionArea"));
    auto* actionsLayout = new QVBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 10, 4, 0);
    actionsLayout->setSpacing(8);
    actionsLayout->addWidget(m_progressCard);
    actionsLayout->addWidget(m_start);
    actionsLayout->addWidget(m_export);
    actionsLayout->addWidget(m_zip);
    actionsLayout->addWidget(makeHint(QStringLiteral("keywords 写入压缩包或输出文件夹内的 stickers.json")));
    actionsLayout->addWidget(makeRow({m_stop, m_openOutput}));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(scroll, 1);
    root->addWidget(actions);

    connect(m_start, &QPushButton::clicked, this, &SettingsPanel::startRequested);
    connect(m_exportSources, &QAction::triggered, this, [this] { emit exportRequested(ExportSources); });
    connect(m_exportTrimmed, &QAction::triggered, this, [this] { emit exportRequested(ExportTrimmed); });
    connect(m_zip, &QCheckBox::toggled, this, [this](bool on) {
        m_s.setOutputAsZip(on);
        refreshExportMenu();
    });
    connect(m_stop, &QPushButton::clicked, this, &SettingsPanel::stopRequested);
    connect(m_openOutput, &QPushButton::clicked, this, &SettingsPanel::openOutputRequested);

    refreshOutputDirRow();
    refreshDataDirLabel();
    refreshExportMenu();
}

void SettingsPanel::setBusyState(const BusyState& state)
{
    m_progressCard->setVisible(state.active);
    if (state.active) {
        const int percent = state.total > 0 ? qRound(state.done * 100.0 / state.total) : 0;
        m_progressTitle->setText(state.title);
        m_progressCount->setText(QStringLiteral("%1 / %2 · %3%").arg(state.done).arg(state.total).arg(percent));
        m_progressBar->setRange(0, qMax(1, state.total));
        m_progressBar->setValue(state.done);
        m_progressHint->setText(state.hint);
        m_progressHint->setVisible(!state.hint.isEmpty());
    }
    m_start->setText(state.converting ? QStringLiteral("转换中...") : QStringLiteral("🚀 开始转换"));

    // 忙碌锁：禁用会冲突的操作
    const bool idle = !state.active;
    for (QWidget* w : std::initializer_list<QWidget*>{m_start, m_export, m_modeSticker, m_modeEmoji, m_preset,
                                                      m_besideSource, m_outputDir, m_browseOutput, m_dataDir,
                                                      m_browseData, m_zip, m_clearCache}) {
        w->setEnabled(idle);
    }
    m_stop->setEnabled(state.active);
}

void SettingsPanel::setCacheInfo(const QString& text)
{
    m_cacheInfo->setText(text);
}

void SettingsPanel::applyOutputDir(const QString& dir)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(dir.trimmed()));
    const QString target = clean.isEmpty() || clean == u"." ? AppPaths::defaultOutputDir() : clean;
    m_outputDir->setText(QDir::toNativeSeparators(target));
    if (target == m_s.outputDir())
        return;
    m_s.setOutputDir(target);
    emit outputLocationChanged();
}

void SettingsPanel::applyDataDirChoice(const QString& choice)
{
    const QString trimmed = choice.trimmed();
    const QString clean = trimmed.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
    if (clean == m_s.dataDirChoice())
        return;
    const QString oldDir = m_s.resolvedDataDir();
    m_s.setDataDirChoice(clean);
    refreshDataDirLabel();
    const QString newDir = m_s.resolvedDataDir();
    if (oldDir != newDir)
        emit dataDirChanged(oldDir, newDir);
}

void SettingsPanel::refreshExportMenu()
{
    if (m_zip->isChecked()) {
        m_exportSources->setText(QStringLiteral("打包原片与参数清单为压缩包"));
        m_exportTrimmed->setText(QStringLiteral("按转换结果打包已裁切源文件，不压成贴纸码率"));
    } else {
        m_exportSources->setText(QStringLiteral("复制原片到文件夹，并附带参数清单"));
        m_exportTrimmed->setText(QStringLiteral("按转换结果输出已裁切源文件到文件夹，不压成贴纸码率"));
    }
}

void SettingsPanel::refreshOutputDirRow()
{
    m_outputRow->setVisible(!m_besideSource->isChecked());
}

void SettingsPanel::refreshDataDirLabel()
{
    m_dataResolved->setText(QStringLiteral("实际路径：") + wrappablePath(m_s.resolvedDataDir()));
}

} // namespace tsm
