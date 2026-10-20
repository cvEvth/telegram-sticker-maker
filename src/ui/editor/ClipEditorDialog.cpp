#include "ui/editor/ClipEditorDialog.h"

#include "app/AppSettings.h"
#include "core/CropMath.h"
#include "core/Format.h"
#include "media/ProxyCache.h"
#include "media/ThumbnailService.h"
#include "ui/editor/EditorSettingsDialog.h"
#include "ui/editor/PreviewCanvas.h"
#include "ui/editor/TimelineWidgets.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QAudioOutput>
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtConcurrent>
#include <array>
#include <cmath>

namespace tsm {

namespace {
constexpr std::array<double, 9> kSpeeds{0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0};
constexpr QSize kCardThumbBox(208, 116);

QPushButton* toolButton(const QString& text, const QString& tip, bool checkable = false)
{
    auto* b = new QPushButton(text);
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setFocusPolicy(Qt::NoFocus); // 让方向键 / 空格留给快捷键
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QString stepText(double seconds)
{
    return formatSecondsShort(seconds) + QStringLiteral("s");
}

bool isTextInput(QWidget* w)
{
    return qobject_cast<QLineEdit*>(w) || qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QComboBox*>(w);
}

void repolish(QWidget* w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}
} // namespace

ClipEditorDialog::ClipEditorDialog(AppSettings& settings, ThumbnailService* thumbnails, ProxyCache* proxies,
                                   const QList<StickerTask>& clips, int focusIndex, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_thumbs(thumbnails)
    , m_proxies(proxies)
    , m_clips(clips)
{
    Q_ASSERT(!m_clips.isEmpty());
    m_base = m_clips.first();
    m_isVideo = m_base.info.isVideo;
    m_frame = QSize(m_base.info.width, m_base.info.height);

    setWindowTitle(m_isVideo ? tr("截取画面与视频片段") : tr("裁切画面样式"));
    setObjectName(QStringLiteral("ClipEditor"));
    resize(1380, 860);
    setMinimumSize(1100, 700);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);

    m_canvas = new PreviewCanvas;
    m_canvas->setSourceSize(m_frame);

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(m_canvas, 1);
    if (m_isVideo)
        leftLayout->addWidget(buildTimeline());
    leftLayout->addWidget(buildControls());
    leftLayout->addWidget(buildCropBar());

    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    body->addWidget(left, 1);
    if (m_isVideo) {
        m_panel = new ClipListPanel;
        body->addWidget(m_panel);
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);
    root->addWidget(buildTopBar());
    root->addLayout(body, 1);
    root->addWidget(buildFooter());

    // 片段
    if (m_panel) {
        connect(m_panel, &ClipListPanel::clipClicked, this, [this](int i) {
            if (i != m_selected)
                selectClip(i, true);
        });
        connect(m_panel, &ClipListPanel::clipActivated, this, [this](int i) { loopClip(i); });
        connect(m_panel, &ClipListPanel::backgroundClicked, this, &ClipEditorDialog::deselectClip);
        connect(m_panel, &ClipListPanel::startEdited, this, &ClipEditorDialog::editStart);
        connect(m_panel, &ClipListPanel::endEdited, this, &ClipEditorDialog::editEnd);
        connect(m_panel, &ClipListPanel::emojiEdited, this, &ClipEditorDialog::editEmoji);
        connect(m_panel, &ClipListPanel::menuRequested, this, &ClipEditorDialog::showClipMenu);
        connect(m_panel, &ClipListPanel::addClicked, this, &ClipEditorDialog::addClip);
    }

    // 预览区
    connect(m_canvas, &PreviewCanvas::cropEdited, this, [this](const QRect& crop) {
        if (StickerTask* c = current()) {
            c->crop = crop;
            refreshControls();
            refreshClipPanel();
            scheduleThumbnails();
        }
    });
    connect(m_canvas, &PreviewCanvas::clicked, this, [this] {
        if (m_isVideo)
            togglePlay();
    });
    connect(m_canvas, &PreviewCanvas::wheelZoom, this, [this](double steps) {
        if (m_isVideo)
            m_state.zoomAround(m_state.playhead(), std::pow(0.8, steps));
    });

    // 缩略图
    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(150);
    connect(m_thumbTimer, &QTimer::timeout, this, &ClipEditorDialog::requestThumbnails);
    if (m_thumbs) {
        connect(m_thumbs, &ThumbnailService::ready, this, [this](const QString& key) {
            for (const int i : m_thumbKeys.value(key))
                if (m_panel && i < m_clips.size())
                    m_panel->setThumbnail(i, m_thumbs->thumbnail(ThumbnailService::requestFor(m_clips.at(i), kCardThumbBox)));
        });
    }

    qApp->installEventFilter(this);

    // 给每个片段一个稳定的 ID，用于写回主列表时对应原来的行
    for (StickerTask& clip : m_clips) {
        if (clip.clipId.isEmpty())
            clip.clipId = QStringLiteral("clip-%1-%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(++m_clipCounter);
    }

    if (m_isVideo) {
        m_state.setDuration(m_base.info.duration);
        setupPlayer();
        selectClip(std::clamp(focusIndex, 0, int(m_clips.size()) - 1), true);
    } else {
        m_selected = 0;
        loadStillImage();
        setCropMode(true); // 图片模式默认开启裁切
    }
    refreshAll();
    scheduleThumbnails();
    // 打开时焦点放在预览区，快捷键立即可用（不落在输入框里）
    m_canvas->setFocus();
}

ClipEditorDialog::~ClipEditorDialog()
{
    qApp->removeEventFilter(this);
    if (m_player) {
        m_player->stop();
        m_standby->stop();
    }
}

// ---------------------------------------------------------------- 界面搭建

QWidget* ClipEditorDialog::buildTopBar()
{
    auto* bar = new QFrame;
    bar->setObjectName(QStringLiteral("EditorTopBar"));
    auto* name = new QLabel(m_base.info.fileName);
    name->setObjectName(QStringLiteral("SectionTitle"));
    name->setToolTip(QDir::toNativeSeparators(m_base.inputPath));

    const MediaInfo& info = m_base.info;
    const QString size = QStringLiteral("%1×%2").arg(info.width).arg(info.height);
    m_infoLabel = new QLabel(m_isVideo ? tr("时长: %1 | %2 | %3 fps | %4")
                                             .arg(formatTimecode(info.duration), size, formatFps(info.fps), info.videoCodec)
                                       : size);
    m_infoLabel->setObjectName(QStringLiteral("Hint"));

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(name);
    layout->addSpacing(12);
    layout->addWidget(m_infoLabel);
    layout->addStretch();

    if (m_isVideo && m_proxies && ProxyCache::needsProxy(m_base.inputPath)) {
        m_proxyButton = toolButton(tr("缓存代理"), tr("在后台生成 480p 流畅预览副本，拖动更顺滑"));
        m_proxyButton->setObjectName(QStringLiteral("ProxyButton"));
        connect(m_proxyButton, &QPushButton::clicked, this, [this] {
            m_proxyError.clear();
            m_proxies->generate(m_base.inputPath, m_base.info.duration);
            refreshProxyButton();
        });
        connect(m_proxies, &ProxyCache::progress, this, [this](const QString& source, int percent) {
            if (source != m_base.inputPath)
                return;
            m_proxyButton->setText(tr("缓存中 %1%").arg(percent));
            if (!m_usingProxy && ProxyCache::playerCannotDecode(source))
                m_canvas->setPlaceholder(tr("正在生成预览副本 %1%").arg(percent));
        });
        connect(m_proxies, &ProxyCache::finished, this, [this](const QString& source, bool ok, const QString& path, const QString& error) {
            if (source != m_base.inputPath)
                return;
            m_proxyError = ok ? QString() : error;
            if (ok)
                switchToProxy(path);
            else if (!m_usingProxy && ProxyCache::playerCannotDecode(source))
                m_canvas->setPlaceholder(tr("无法生成预览：%1").arg(error));
            refreshProxyButton();
        });
        layout->addWidget(m_proxyButton);
    }
    return bar;
}

QWidget* ClipEditorDialog::buildTimeline()
{
    auto* box = new QFrame;
    box->setObjectName(QStringLiteral("TimelineBox"));

    m_timeLabel = new QLabel;
    m_timeLabel->setObjectName(QStringLiteral("TimeLabel"));
    m_contextLabel = new QLabel;
    m_contextLabel->setObjectName(QStringLiteral("Hint"));
    m_totalLabel = new QLabel(formatTimecode(m_base.info.duration));
    m_totalLabel->setObjectName(QStringLiteral("Hint"));

    m_overviewButton = toolButton(tr("总览"), tr("显示 / 隐藏总览条"), true);
    m_overviewButton->setObjectName(QStringLiteral("SmallButton"));
    m_fitButton = toolButton(tr("适应片段"), tr("放大到当前片段（F）"));
    m_fitButton->setObjectName(QStringLiteral("SmallButton"));
    m_fullButton = toolButton(QString(), tr("恢复全长（0）"));
    m_fullButton->setObjectName(QStringLiteral("SmallButton"));

    auto* info = new QHBoxLayout;
    info->setSpacing(8);
    info->addWidget(m_timeLabel);
    info->addWidget(m_contextLabel, 1);
    info->addWidget(m_overviewButton);
    info->addWidget(m_fitButton);
    info->addWidget(m_fullButton);
    info->addWidget(m_totalLabel);

    m_overview = new TimelineOverview(&m_state);
    m_track = new TimelineTrack(&m_state);

    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(10, 8, 10, 6);
    layout->setSpacing(4);
    layout->addLayout(info);
    layout->addWidget(m_overview);
    layout->addWidget(m_track);

    const bool showOverview = m_settings.showTimelineOverview();
    m_overviewButton->setChecked(showOverview);
    m_overview->setVisible(showOverview);
    connect(m_overviewButton, &QPushButton::toggled, this, [this](bool on) {
        m_overview->setVisible(on);
        m_settings.setShowTimelineOverview(on);
    });
    connect(m_fitButton, &QPushButton::clicked, this, [this] {
        if (const StickerTask* c = current())
            m_state.fitRange(c->startTime.value_or(0), c->endTime.value_or(c->startTime.value_or(0) + 1));
    });
    connect(m_fullButton, &QPushButton::clicked, &m_state, &TimelineState::showAll);
    connect(m_track, &TimelineTrack::scrubbed, this, [this](double t) {
        // 点到或拖到正在循环的片段外：改为正常播放（原来在播就继续播）
        if (m_playMode != PlayMode::Free && m_playIndex >= 0 && m_playIndex < m_clips.size()) {
            const StickerTask& c = m_clips.at(m_playIndex);
            const double start = c.startTime.value_or(0);
            const double end = c.endTime.value_or(m_base.info.duration);
            if (t < start || t >= end) {
                m_playMode = PlayMode::Free;
                m_playIndex = -1;
                refreshClipPanel();
            }
        }
        seek(t);
    });
    connect(m_track, &TimelineTrack::clipPressed, this, [this](int i) {
        if (i != m_selected)
            selectClip(i, false);
        // 播放中点在片段上：从点击位置开始在片段内循环（随后的 scrubbed 会定位到点击处）；
        // 暂停时只选中并定位，保持暂停
        if (isPlaying())
            loopClip(i, false);
    });
    connect(&m_state, &TimelineState::changed, this, &ClipEditorDialog::refreshInfoRow);
    return box;
}

QWidget* ClipEditorDialog::buildControls()
{
    // 两行：播放控制（仅视频） / 画面编辑
    auto* bar = new QWidget;
    auto* rows = new QVBoxLayout(bar);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(6);
    auto* transport = new QHBoxLayout;
    transport->setSpacing(6);
    auto* edit = new QHBoxLayout;
    edit->setSpacing(6);
    QHBoxLayout* layout = transport;

    if (m_isVideo) {
        m_backBig = toolButton(QString(), tr("后退大步长（Shift+←）"));
        m_backSmall = toolButton(QString(), tr("后退小步长（←）"));
        m_playButton = toolButton(tr("▶️ 播放"), tr("播放 / 暂停（空格）"));
        m_playButton->setMinimumWidth(84);
        m_fwdSmall = toolButton(QString(), tr("前进小步长（→）"));
        m_fwdBig = toolButton(QString(), tr("前进大步长（Shift+→）"));
        m_startButton = toolButton(tr("🚩 起点"), tr("设当前时刻为片段起点（[）"));
        m_endButton = toolButton(tr("🏁 终点"), tr("设当前时刻为片段终点（]）"));
        for (QPushButton* b : {m_backBig, m_backSmall, m_playButton, m_fwdSmall, m_fwdBig})
            layout->addWidget(b);
        layout->addSpacing(8);
        layout->addWidget(m_startButton);
        layout->addWidget(m_endButton);
        layout->addSpacing(8);

        connect(m_backBig, &QPushButton::clicked, this, [this] { step(-m_settings.bigStep()); });
        connect(m_backSmall, &QPushButton::clicked, this, [this] { step(-m_settings.smallStep()); });
        connect(m_fwdSmall, &QPushButton::clicked, this, [this] { step(m_settings.smallStep()); });
        connect(m_fwdBig, &QPushButton::clicked, this, [this] { step(m_settings.bigStep()); });
        connect(m_playButton, &QPushButton::clicked, this, &ClipEditorDialog::togglePlay);
        connect(m_startButton, &QPushButton::clicked, this, &ClipEditorDialog::setStartHere);
        connect(m_endButton, &QPushButton::clicked, this, &ClipEditorDialog::setEndHere);
    }
    layout = edit;

    m_cropButton = toolButton(tr("✂️ 画面裁切"), tr("显示 / 隐藏裁切框"), true);
    m_mirrorButton = toolButton(tr("🪞 镜像"), tr("水平镜像（M）"), true);
    layout->addWidget(m_cropButton);
    layout->addWidget(m_mirrorButton);
    connect(m_cropButton, &QPushButton::toggled, this, &ClipEditorDialog::setCropMode);
    connect(m_mirrorButton, &QPushButton::clicked, this, &ClipEditorDialog::toggleMirror);

    layout->addSpacing(8);
    layout->addWidget(new QLabel(tr("关联 Emoji:")));
    m_emojiEdit = new QLineEdit;
    m_emojiEdit->setPlaceholderText(tr("可留空"));
    m_emojiEdit->setMaximumWidth(90);
    layout->addWidget(m_emojiEdit);
    connect(m_emojiEdit, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (StickerTask* c = current()) {
            c->emoji = text.trimmed();
            refreshClipPanel();
            refreshTimeline();
        }
    });

    layout->addStretch();

    if (m_isVideo) {
        layout = transport;
        layout->addStretch();
        m_speed = new QComboBox;
        for (const double s : kSpeeds)
            m_speed->addItem(formatSecondsShort(s) + QStringLiteral("×"), s);
        m_speed->setToolTip(tr("播放倍速（, 减速  . 加速）"));
        m_speed->setFocusPolicy(Qt::NoFocus);
        int index = 3;
        for (int i = 0; i < int(kSpeeds.size()); ++i)
            if (std::abs(kSpeeds[i] - m_settings.playbackRate()) < 1e-6)
                index = i;
        m_speed->setCurrentIndex(index);
        connect(m_speed, &QComboBox::currentIndexChanged, this, [this] {
            const double rate = m_speed->currentData().toDouble();
            m_settings.setPlaybackRate(rate);
            if (m_player) {
                resyncClock(m_state.playhead()); // 倍速变了，从当前位置重新计时
                m_player->setPlaybackRate(rate);
                m_standby->setPlaybackRate(rate);
            }
        });
        m_muteButton = toolButton(QStringLiteral("🔊"), tr("静音"));
        connect(m_muteButton, &QPushButton::clicked, this, [this] {
            m_audio->setMuted(!m_audio->isMuted());
            m_standbyAudio->setMuted(m_audio->isMuted());
            m_muteButton->setText(m_audio->isMuted() ? QStringLiteral("🔇") : QStringLiteral("🔊"));
        });
        layout->addWidget(new QLabel(tr("倍速")));
        layout->addWidget(m_speed);
        layout->addWidget(m_muteButton);
        rows->addLayout(transport);
    }

    auto* settingsButton = toolButton(tr("⚙️ 窗口设置"), tr("步长、默认裁切比例与圆角、总览条"));
    connect(settingsButton, &QPushButton::clicked, this, &ClipEditorDialog::openSettings);
    edit->addWidget(settingsButton);
    rows->addLayout(edit);
    if (!m_isVideo)
        delete transport;
    refreshStepLabels();
    return bar;
}

QWidget* ClipEditorDialog::buildCropBar()
{
    m_cropBar = new QFrame;
    m_cropBar->setObjectName(QStringLiteral("CropBar"));
    auto* layout = new QHBoxLayout(m_cropBar);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(8);

    m_aspect = new QComboBox;
    for (int i = 0; i <= int(CropAspect::Portrait9x16); ++i)
        m_aspect->addItem(CropMath::aspectLabel(CropAspect(i)), i);
    m_aspect->setCurrentIndex(qMax(0, m_aspect->findData(int(m_settings.defaultCropAspect()))));
    m_aspect->setFocusPolicy(Qt::NoFocus);
    connect(m_aspect, &QComboBox::activated, this, &ClipEditorDialog::onAspectChanged);

    auto* reset = toolButton(tr("🔄 重置居中"), tr("按当前比例重置为居中的最大尺寸"));
    connect(reset, &QPushButton::clicked, this, &ClipEditorDialog::resetCropCenter);
    layout->addWidget(m_aspect);
    layout->addWidget(reset);
    if (m_isVideo) {
        m_syncButton = toolButton(tr("📐 同步形状"), tr("把当前片段的裁切宽高和圆角应用到所有片段（各片段保留自己的位置）"));
        connect(m_syncButton, &QPushButton::clicked, this, &ClipEditorDialog::syncShapeToAll);
        layout->addWidget(m_syncButton);
    }
    auto* clear = toolButton(tr("🗑️ 清除裁切"), tr("清除当前片段的裁切框和圆角"));
    connect(clear, &QPushButton::clicked, this, &ClipEditorDialog::clearCrop);
    layout->addWidget(clear);

    m_cropSize = new QLabel;
    m_cropSize->setObjectName(QStringLiteral("Hint"));
    m_cropSize->setMinimumWidth(110);
    layout->addWidget(m_cropSize);
    layout->addStretch();

    auto* left = new QLabel(tr("直角"));
    left->setObjectName(QStringLiteral("Hint"));
    m_radius = new QSlider(Qt::Horizontal);
    m_radius->setRange(0, 100);
    m_radius->setFixedWidth(180);
    m_radius->setFocusPolicy(Qt::NoFocus);
    auto* right = new QLabel(tr("圆形"));
    right->setObjectName(QStringLiteral("Hint"));
    m_radiusLabel = new QLabel;
    m_radiusLabel->setMinimumWidth(64);
    layout->addWidget(left);
    layout->addWidget(m_radius);
    layout->addWidget(right);
    layout->addWidget(m_radiusLabel);
    connect(m_radius, &QSlider::valueChanged, this, [this](int v) {
        m_radiusLabel->setText(shapeLabel(v / 100.0));
        if (StickerTask* c = current()) {
            if (std::abs(c->cropRadius - v / 100.0) < 1e-9)
                return;
            ensureCrop(*c);
            c->cropRadius = v / 100.0;
            refreshCanvas();
            refreshClipPanel();
            scheduleThumbnails();
        }
    });
    m_cropBar->hide();
    return m_cropBar;
}

QWidget* ClipEditorDialog::buildFooter()
{
    auto* bar = new QWidget;
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    m_hint = new QLabel;
    m_hint->setObjectName(QStringLiteral("Hint"));
    auto* cancel = new QPushButton(tr("取消"));
    cancel->setFocusPolicy(Qt::NoFocus);
    m_confirm = new QPushButton;
    m_confirm->setObjectName(QStringLiteral("PrimaryButton"));
    m_confirm->setMinimumHeight(38);
    m_confirm->setMinimumWidth(220);
    m_confirm->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_hint, 1);
    layout->addWidget(cancel);
    layout->addWidget(m_confirm);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_confirm, &QPushButton::clicked, this, &ClipEditorDialog::confirm);
    return bar;
}

void ClipEditorDialog::setupPlayer()
{
    const auto createPlayer = [this](QMediaPlayer*& player, QAudioOutput*& audio, QVideoSink*& sink) {
        player = new QMediaPlayer(this);
        audio = new QAudioOutput(this);
        audio->setVolume(0.5f); // 默认音量 50%
        player->setAudioOutput(audio);
        sink = new QVideoSink(this);
        player->setVideoSink(sink);
        player->setPlaybackRate(m_settings.playbackRate());
        connectPlayer(player, sink);
    };
    createPlayer(m_player, m_audio, m_sink);
    createPlayer(m_standby, m_standbyAudio, m_standbySink);

    m_tick = new QTimer(this);
    m_tick->setInterval(16);
    connect(m_tick, &QTimer::timeout, this, &ClipEditorDialog::onTick);

    // 已有缓存直接用
    QString source = m_base.inputPath;
    if (m_proxies && ProxyCache::needsProxy(source)) {
        if (const QString cached = m_proxies->cached(source); !cached.isEmpty()) {
            source = cached;
            m_usingProxy = true;
        }
    }
    // 动态 WebP 播放器解不了：不直接打开原文件，自动生成预览副本，完成后切过去
    if (!m_usingProxy && m_proxies && ProxyCache::playerCannotDecode(source)) {
        m_canvas->setPlaceholder(tr("正在生成预览副本..."));
        if (!m_proxies->isGenerating(source))
            m_proxies->generate(source, m_base.info.duration);
        refreshProxyButton();
        return;
    }
    m_player->setSource(QUrl::fromLocalFile(source));
    refreshProxyButton();
}

void ClipEditorDialog::connectPlayer(QMediaPlayer* player, QVideoSink* sink)
{
    // 两个播放器共用这些处理；只有当前播放器（m_player）的事件生效，备用的只用来备好起点画面
    connect(sink, &QVideoSink::videoFrameChanged, this, [this, sink](const QVideoFrame& frame) {
        if (!frame.isValid())
            return;
        const double frameTime = frame.startTime() >= 0 ? frame.startTime() / 1e6 : -1.0;
        if (sink != m_sink) {
            // 备用播放器：停到起点附近的画面才算就绪（跳转前可能还会送来旧位置的画面）
            if (sink == m_standbySink && m_standbyTargetMs >= 0 && frameTime >= 0
                && std::abs(frameTime - m_standbyTargetMs / 1000.0) <= 0.1) {
                m_standbyFrame = frame.toImage();
                m_standbyFrameTime = frameTime;
            }
            return;
        }
        // 片段播放 / 循环时按画面时间戳把关：终点之后的画面不显示（跳回起点是异步的，
        // 跳转生效前播放器还会送来几帧片段外的画面）
        if (m_playMode != PlayMode::Free && m_playIndex >= 0 && m_playIndex < m_clips.size() && frameTime >= 0) {
            const StickerTask& c = m_clips.at(m_playIndex);
            const double start = c.startTime.value_or(0);
            const double end = c.endTime.value_or(m_base.info.duration);
            if (frameTime >= end - 1e-3) {
                if (!m_loopRestartPending)
                    onClipEnd();
                return;
            }
            if (m_loopRestartPending) {
                if (frameTime < start - 0.05)
                    return;
                m_loopRestartPending = false;
            }
        }
        m_lastFrameTime = frameTime;
        m_canvas->setImage(frame.toImage());
    });
    connect(player, &QMediaPlayer::positionChanged, this, [this, player](qint64 ms) {
        if (player == m_player && player->playbackState() != QMediaPlayer::PlayingState)
            m_state.setPlayhead(ms / 1000.0);
    });
    connect(player, &QMediaPlayer::playbackStateChanged, this, [this, player](QMediaPlayer::PlaybackState s) {
        if (player != m_player)
            return;
        const bool playing = s == QMediaPlayer::PlayingState;
        m_playButton->setText(playing ? tr("⏸️ 暂停") : tr("▶️ 播放"));
        if (playing) {
            resyncClock(m_player->position() / 1000.0);
            m_tick->start();
        } else {
            m_tick->stop();
        }
        if (!playing && m_playMode != PlayMode::Free) {
            m_playMode = PlayMode::Free;
            m_playIndex = -1;
            refreshClipPanel();
        }
    });
    connect(player, &QMediaPlayer::mediaStatusChanged, this, [this, player](QMediaPlayer::MediaStatus status) {
        if (status != QMediaPlayer::LoadedMedia)
            return;
        if (player != m_player) {
            // 备用播放器打开了文件：停到要备好的位置
            if (player == m_standby && m_standbyTargetMs >= 0) {
                player->setPosition(m_standbyTargetMs);
                player->pause();
            }
            return;
        }
        if (m_restorePositionMs >= 0) {
            // 切换到缓存副本：保持当前播放位置，原来在播放就继续播放
            m_player->setPosition(m_restorePositionMs);
            if (m_restorePlaying)
                m_player->play();
            else
                m_player->pause();
            m_restorePositionMs = -1;
        } else if (m_player->playbackState() == QMediaPlayer::StoppedState) {
            // 停止状态不出画面：进入暂停状态并定位到播放头，显示当前帧
            m_player->setPosition(qint64(std::llround(m_state.playhead() * 1000)));
            m_player->pause();
        }
    });
    connect(player, &QMediaPlayer::errorOccurred, this, [this, player](QMediaPlayer::Error, const QString& message) {
        if (player != m_player)
            return;
        m_canvas->setPlaceholder(tr("无法播放：%1").arg(message));
        showHint(tr("无法播放：%1%2").arg(message, m_proxyButton ? tr("（可尝试“缓存代理”）") : QString()), true);
    });
}

void ClipEditorDialog::loadStillImage()
{
    m_canvas->setPlaceholder(tr("加载中..."));
    ThumbnailService::Request request;
    request.path = m_base.inputPath;
    request.box = QSize(2048, 2048);
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher] {
        const QImage image = watcher->result();
        if (image.isNull())
            m_canvas->setPlaceholder(tr("无法加载图片"));
        else
            m_canvas->setImage(image);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([request] { return ThumbnailService::render(request); }));
}

// ---------------------------------------------------------------- 片段

StickerTask* ClipEditorDialog::current()
{
    return m_selected >= 0 && m_selected < m_clips.size() ? &m_clips[m_selected] : nullptr;
}

void ClipEditorDialog::selectClip(int index, bool seekToStart)
{
    if (index < 0 || index >= m_clips.size())
        return;
    m_selected = index;
    StickerTask& clip = m_clips[index];
    if (m_cropMode)
        ensureCrop(clip);
    if (clip.crop) {
        const CropAspect aspect = inferAspect(*clip.crop);
        m_aspect->setCurrentIndex(qMax(0, m_aspect->findData(int(aspect))));
    }
    if (m_isVideo && seekToStart) {
        stopClipPlayback();
        seek(clip.startTime.value_or(0));
        const double start = clip.startTime.value_or(0);
        m_state.fitRange(start, clip.endTime.value_or(start + 1));
    }
    refreshAll();
    if (m_panel)
        m_panel->scrollToClip(index);
}

void ClipEditorDialog::deselectClip()
{
    if (!m_isVideo || m_selected < 0)
        return;
    m_selected = -1;
    refreshAll();
}

void ClipEditorDialog::addClip()
{
    StickerTask clip = m_base;
    clip.id.clear();
    clip.keywords.clear();
    clip.emoji.clear();
    clip.clipId = QStringLiteral("clip-%1-%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(++m_clipCounter);
    clip.startTime = m_state.playhead();
    clip.endTime.reset(); // 终点留空，用 ] 打点
    // 继承当前的裁切框、圆角、镜像
    if (const StickerTask* c = current()) {
        clip.crop = c->crop;
        clip.cropRadius = c->cropRadius;
        clip.mirror = c->mirror;
    } else {
        clip.crop.reset();
        clip.cropRadius = 0;
        clip.mirror = false;
    }
    clip.resetRuntimeState();
    m_clips << clip;
    selectClip(int(m_clips.size()) - 1, false);
    showHint(tr("已从 %1 添加片段，用 ] 设置终点").arg(formatTimecode(*clip.startTime)));
    scheduleThumbnails();
}

void ClipEditorDialog::deleteClip(int index)
{
    if (index < 0 || index >= m_clips.size())
        return;
    if (m_clips.size() <= 1) {
        QMessageBox::warning(this, tr("提示"), tr("请至少保留一个片段！"));
        return;
    }
    if (m_playIndex == index)
        stopClipPlayback();
    m_clips.removeAt(index);
    if (m_selected == index)
        m_selected = -1;
    else if (m_selected > index)
        --m_selected;
    refreshAll();
    scheduleThumbnails();
}

void ClipEditorDialog::setStartHere()
{
    StickerTask* c = current();
    if (!c)
        return showHint(tr("请先选择一个片段"), true);
    c->startTime = m_state.playhead();
    if (c->endTime && *c->endTime <= *c->startTime)
        c->endTime.reset(); // 终点不晚于新起点时清空
    refreshAll();
    scheduleThumbnails();
}

void ClipEditorDialog::setEndHere()
{
    StickerTask* c = current();
    if (!c)
        return showHint(tr("请先选择一个片段"), true);
    const double t = m_state.playhead();
    if (t <= c->startTime.value_or(0))
        return showHint(tr("终点必须晚于起点"), true);
    c->endTime = t;
    refreshAll();
}

void ClipEditorDialog::editStart(int index, const QString& text)
{
    if (index < 0 || index >= m_clips.size())
        return;
    StickerTask& c = m_clips[index];
    const auto t = parseTimecode(text);
    if (!t || *t > m_base.info.duration) {
        showHint(tr("起点格式无效：%1").arg(text), true);
    } else if (std::abs(*t - c.startTime.value_or(0)) > 1e-6) {
        c.startTime = *t;
        if (c.endTime && *c.endTime <= *t)
            c.endTime.reset();
        if (index == m_selected)
            seek(*t);
        scheduleThumbnails();
    }
    refreshAll();
}

void ClipEditorDialog::editEnd(int index, const QString& text)
{
    if (index < 0 || index >= m_clips.size())
        return;
    StickerTask& c = m_clips[index];
    if (text.trimmed().isEmpty()) {
        c.endTime.reset(); // 终点清空 = 未设终点
    } else {
        const auto t = parseTimecode(text);
        // 非法或不晚于起点：恢复原值
        if (!t || *t <= c.startTime.value_or(0) || *t > m_base.info.duration + 1e-3)
            showHint(tr("终点无效，已恢复原值"), true);
        else
            c.endTime = std::min(*t, m_base.info.duration);
    }
    refreshAll();
}

void ClipEditorDialog::editEmoji(int index, const QString& text)
{
    if (index < 0 || index >= m_clips.size())
        return;
    m_clips[index].emoji = text.trimmed();
    refreshAll();
}

void ClipEditorDialog::showClipMenu(int index, const QPoint& globalPos)
{
    if (index != m_selected)
        selectClip(index, true);
    QMenu menu(this);
    const bool playing = m_playIndex == index && m_playMode == PlayMode::Clip;
    const bool looping = m_playIndex == index && m_playMode == PlayMode::Loop;
    QAction* play = menu.addAction(playing ? tr("⏹️ 停止播放") : tr("▶️ 播放"));
    QAction* loop = menu.addAction(looping ? tr("⏹️ 停止循环") : tr("🔁 循环播放"));
    const bool hasEnd = m_clips.at(index).endTime.has_value();
    play->setEnabled(hasEnd || playing);
    loop->setEnabled(hasEnd || looping);
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("🗑️ 删除"));
    QAction* chosen = menu.exec(globalPos);
    if (chosen == play)
        playing ? stopClipPlayback() : playClip(index, PlayMode::Clip);
    else if (chosen == loop)
        looping ? stopClipPlayback() : playClip(index, PlayMode::Loop);
    else if (chosen == remove)
        deleteClip(index);
}

// ---------------------------------------------------------------- 裁切 / 镜像

void ClipEditorDialog::setCropMode(bool on)
{
    m_cropMode = on;
    if (m_cropButton->isChecked() != on) {
        const QSignalBlocker block(m_cropButton);
        m_cropButton->setChecked(on);
    }
    if (on) {
        if (StickerTask* c = current()) {
            if (!c->crop) {
                // 新开启裁切：使用默认比例与默认圆角
                m_aspect->setCurrentIndex(qMax(0, m_aspect->findData(int(m_settings.defaultCropAspect()))));
                ensureCrop(*c);
                scheduleThumbnails();
            }
        }
    }
    m_cropBar->setVisible(on);
    refreshAll();
}

void ClipEditorDialog::ensureCrop(StickerTask& clip)
{
    if (clip.crop)
        return;
    clip.crop = CropMath::centeredMax(m_frame, lockedAspect());
    if (clip.cropRadius <= 0)
        clip.cropRadius = m_settings.defaultCropRadius() / 100.0;
}

std::optional<double> ClipEditorDialog::lockedAspect() const
{
    return CropMath::aspectRatio(CropAspect(m_aspect->currentData().toInt()), m_frame);
}

CropAspect ClipEditorDialog::inferAspect(const QRect& crop) const
{
    if (crop.height() <= 0)
        return CropAspect::Free;
    const double ratio = double(crop.width()) / crop.height();
    for (const CropAspect a : {CropAspect::Square, CropAspect::Landscape16x9, CropAspect::Classic4x3, CropAspect::Portrait9x16,
                               CropAspect::Original}) {
        const auto r = CropMath::aspectRatio(a, m_frame);
        if (r && std::abs(ratio - *r) / *r < 0.02)
            return a;
    }
    return CropAspect::Free;
}

void ClipEditorDialog::onAspectChanged()
{
    // 切换比例时，框按新比例重置为居中的最大尺寸
    if (StickerTask* c = current()) {
        c->crop = CropMath::centeredMax(m_frame, lockedAspect());
        if (c->cropRadius <= 0)
            c->cropRadius = m_settings.defaultCropRadius() / 100.0;
        scheduleThumbnails();
    }
    refreshAll();
}

void ClipEditorDialog::resetCropCenter()
{
    if (StickerTask* c = current()) {
        c->crop = CropMath::centeredMax(m_frame, lockedAspect());
        scheduleThumbnails();
        refreshAll();
    }
}

void ClipEditorDialog::syncShapeToAll()
{
    const StickerTask* src = current();
    if (!src || !src->crop)
        return showHint(tr("当前片段没有裁切框"), true);
    const QSize size = src->crop->size();
    const double radius = src->cropRadius;
    for (int i = 0; i < m_clips.size(); ++i) {
        if (i == m_selected)
            continue;
        StickerTask& c = m_clips[i];
        // 各片段保留自己的位置（没有裁切框的居中），超出画面时自动收回
        const QRect base = c.crop ? *c.crop
                                  : QRect((m_frame.width() - size.width()) / 2, (m_frame.height() - size.height()) / 2, 0, 0);
        c.crop = CropMath::withSize(base, size, m_frame);
        c.cropRadius = radius;
    }
    showHint(tr("已将 %1×%2 %3 应用到全部 %n 个片段", nullptr, int(m_clips.size())).arg(size.width()).arg(size.height()).arg(shapeLabel(radius)));
    refreshAll();
    scheduleThumbnails();
}

void ClipEditorDialog::clearCrop()
{
    if (StickerTask* c = current()) {
        c->crop.reset();
        c->cropRadius = 0;
        refreshAll();
        scheduleThumbnails();
    }
}

void ClipEditorDialog::toggleMirror()
{
    StickerTask* c = current();
    if (!c) {
        refreshControls();
        return showHint(tr("请先选择一个片段"), true);
    }
    c->mirror = !c->mirror;
    // 镜像与裁切联动：框的 x 也水平镜像，框住的还是同一块内容
    if (c->crop)
        c->crop = CropMath::mirrored(*c->crop, m_frame);
    refreshAll();
    scheduleThumbnails();
}

// ---------------------------------------------------------------- 播放

void ClipEditorDialog::togglePlay()
{
    if (!m_player)
        return;
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else {
        m_playMode = PlayMode::Free;
        m_playIndex = -1;
        m_player->play();
    }
}

void ClipEditorDialog::playClip(int index, PlayMode mode)
{
    m_loopRestartPending = false;
    const StickerTask& c = m_clips.at(index);
    if (!c.endTime)
        return showHint(tr("请先为片段设置终点"), true);
    m_playMode = mode;
    m_playIndex = index;
    seek(c.startTime.value_or(0));
    m_player->play();
    prepareStandby();
    refreshClipPanel();
}

bool ClipEditorDialog::isPlaying() const
{
    return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
}

void ClipEditorDialog::loopClip(int index, bool fromStart)
{
    m_loopRestartPending = false;
    if (!m_player || index < 0 || index >= m_clips.size())
        return;
    const StickerTask& c = m_clips.at(index);
    if (!c.endTime)
        return; // 未设终点：只选中
    const bool alreadyLooping = m_playMode == PlayMode::Loop && m_playIndex == index
        && m_player->playbackState() == QMediaPlayer::PlayingState;
    if (alreadyLooping && fromStart)
        return;
    m_playMode = PlayMode::Loop;
    m_playIndex = index;
    if (fromStart)
        seek(c.startTime.value_or(0));
    if (m_player->playbackState() != QMediaPlayer::PlayingState)
        m_player->play();
    prepareStandby();
    refreshClipPanel();
}

void ClipEditorDialog::stopClipPlayback()
{
    if (m_playMode == PlayMode::Free)
        return;
    m_playMode = PlayMode::Free;
    m_playIndex = -1;
    if (m_player)
        m_player->pause();
    refreshClipPanel();
}

void ClipEditorDialog::seek(double seconds)
{
    const double t = std::clamp(seconds, 0.0, m_base.info.duration);
    if (m_player)
        m_player->setPosition(qint64(std::llround(t * 1000)));
    resyncClock(t);
    m_state.setPlayhead(t);
    refreshControls();
}

void ClipEditorDialog::step(double seconds)
{
    seek(m_state.playhead() + seconds);
}

void ClipEditorDialog::changeSpeed(int delta)
{
    if (!m_speed)
        return;
    m_speed->setCurrentIndex(std::clamp(m_speed->currentIndex() + delta, 0, m_speed->count() - 1));
    showHint(tr("播放倍速 %1").arg(m_speed->currentText()));
}

void ClipEditorDialog::resyncClock(double seconds)
{
    m_clockAnchor = seconds;
    m_lastShown = seconds;
    m_lastReportedMs = m_player ? m_player->position() : -1;
    m_clock.restart();
}

double ClipEditorDialog::smoothPosition()
{
    constexpr double kMaxDrift = 0.25; // 偏差超过这个值（卡顿、跳转）直接对齐
    constexpr double kMaxLead = 0.15;  // 推算值最多领先真实位置这么多，卡住时不会越跑越远
    const double rate = m_player->playbackRate() > 0 ? m_player->playbackRate() : 1.0;
    const qint64 reportedMs = m_player->position();
    const double reported = reportedMs / 1000.0;
    if (!m_clock.isValid()) {
        resyncClock(reported);
        return reported;
    }
    double estimated = m_clockAnchor + m_clock.elapsed() / 1000.0 * rate;
    if (reportedMs != m_lastReportedMs) {
        // 新的一帧：用真实位置校正推算值
        m_lastReportedMs = reportedMs;
        const double drift = reported - estimated;
        if (std::abs(drift) > kMaxDrift) {
            resyncClock(reported);
            return reported;
        }
        m_clockAnchor += drift * 0.2;
        estimated += drift * 0.2;
    }
    estimated = std::min(estimated, reported + kMaxLead);
    estimated = std::max(estimated, m_lastShown); // 不往回走
    m_lastShown = estimated;
    return std::min(estimated, m_base.info.duration);
}

void ClipEditorDialog::onClipEnd()
{
    if (m_playIndex < 0 || m_playIndex >= m_clips.size())
        return;
    const StickerTask& c = m_clips.at(m_playIndex);
    const double start = c.startTime.value_or(0);
    const double end = c.endTime.value_or(m_base.info.duration);
    if (m_playMode == PlayMode::Loop) {
        if (swapToStandby(start))
            return;
        m_loopRestartPending = true;
        m_player->setPosition(qint64(std::llround(start * 1000)));
        resyncClock(start);
        m_state.setPlayhead(start);
        prepareStandby();
        return;
    }
    // 播到终点自动停，停在片段最后一帧（终点那一帧已不属于片段）
    const double frame = m_base.info.fps > 0 ? 1.0 / m_base.info.fps : 0.04;
    m_player->pause();
    seek(std::max(start, end - frame));
}

void ClipEditorDialog::prepareStandby()
{
    m_standbyFrame = QImage();
    m_standbyFrameTime = -1;
    m_standbyTargetMs = -1;
    if (!m_standby || m_playMode != PlayMode::Loop || m_playIndex < 0 || m_playIndex >= m_clips.size())
        return;
    m_standbyTargetMs = qint64(std::llround(m_clips.at(m_playIndex).startTime.value_or(0) * 1000));
    m_standby->setPlaybackRate(m_player->playbackRate());
    if (m_standby->source() != m_player->source()) {
        m_standby->setSource(m_player->source()); // 打开后在 mediaStatusChanged 里定位
        return;
    }
    m_standby->setPosition(m_standbyTargetMs);
    m_standby->pause();
}

bool ClipEditorDialog::swapToStandby(double start)
{
    if (!m_standby || m_standbyFrame.isNull() || m_standbyTargetMs != qint64(std::llround(start * 1000))
        || m_standby->source() != m_player->source())
        return false;
    QMediaPlayer* old = m_player;
    std::swap(m_player, m_standby);
    std::swap(m_audio, m_standbyAudio);
    std::swap(m_sink, m_standbySink);
    m_audio->setVolume(m_standbyAudio->volume());
    m_audio->setMuted(m_standbyAudio->isMuted());
    m_player->play();
    old->pause();
    m_loopRestartPending = false;
    m_lastFrameTime = m_standbyFrameTime;
    m_canvas->setImage(m_standbyFrame);
    resyncClock(start);
    m_state.setPlayhead(start);
    prepareStandby(); // 换下来的播放器在后台跳回起点，备好下一圈
    return true;
}

void ClipEditorDialog::onTick()
{
    double t = smoothPosition();
    if (m_playMode != PlayMode::Free && m_playIndex >= 0 && m_playIndex < m_clips.size()) {
        const StickerTask& c = m_clips.at(m_playIndex);
        const double end = c.endTime.value_or(m_base.info.duration);
        // 平滑时间可能略慢于真实位置，判断终点时取两者较大者
        const double reported = m_player->position() / 1000.0;
        if (!m_loopRestartPending && std::max(t, reported) >= end) {
            onClipEnd();
            return;
        }
        if (m_loopRestartPending)
            t = std::min(t, end); // 跳回起点生效前指针不越过终点
    }
    m_state.setPlayhead(t);
    m_state.followPlayhead();
    refreshControls();
}

void ClipEditorDialog::switchToProxy(const QString& proxyPath)
{
    if (!m_player || m_usingProxy)
        return;
    m_restorePositionMs = m_player->position();
    m_restorePlaying = m_player->playbackState() == QMediaPlayer::PlayingState;
    m_usingProxy = true;
    m_player->setSource(QUrl::fromLocalFile(proxyPath));
}

void ClipEditorDialog::refreshProxyButton()
{
    if (!m_proxyButton)
        return;
    bool ok = false;
    if (m_usingProxy) {
        m_proxyButton->setText(tr("已缓存"));
        m_proxyButton->setToolTip(tr("正在使用 480p 预览副本"));
        ok = true;
    } else if (m_proxies->isGenerating(m_base.inputPath)) {
        m_proxyButton->setText(tr("缓存中 0%"));
    } else if (!m_proxyError.isEmpty()) {
        m_proxyButton->setText(tr("缓存失败 · 重试"));
        m_proxyButton->setToolTip(m_proxyError);
    } else {
        m_proxyButton->setText(tr("缓存代理"));
    }
    m_proxyButton->setEnabled(!ok && !m_proxies->isGenerating(m_base.inputPath));
    m_proxyButton->setProperty("state", ok ? QStringLiteral("ok") : m_proxyError.isEmpty() ? QString() : QStringLiteral("error"));
    repolish(m_proxyButton);
}

// ---------------------------------------------------------------- 刷新

void ClipEditorDialog::refreshAll()
{
    refreshTimeline();
    refreshClipPanel();
    refreshCanvas();
    refreshControls();
    refreshInfoRow();
}

void ClipEditorDialog::refreshTimeline()
{
    if (!m_isVideo)
        return;
    QList<TimelineState::Span> spans;
    for (const StickerTask& c : m_clips)
        spans << TimelineState::Span{c.startTime.value_or(0), c.endTime, c.emoji};
    m_state.setSpans(spans);
    m_state.setSelected(m_selected);
}

void ClipEditorDialog::refreshClipPanel()
{
    if (!m_panel)
        return;
    const ClipCard::PlayState play = m_playMode == PlayMode::Clip   ? ClipCard::PlayState::Playing
                                     : m_playMode == PlayMode::Loop ? ClipCard::PlayState::Looping
                                                                    : ClipCard::PlayState::None;
    m_panel->setClips(m_clips, m_selected, m_playIndex, play);
}

void ClipEditorDialog::refreshCanvas()
{
    const StickerTask* c = m_selected >= 0 && m_selected < m_clips.size() ? &m_clips.at(m_selected) : nullptr;
    m_canvas->setMirror(c && c->mirror);
    m_canvas->setCropVisible(m_cropMode && c);
    m_canvas->setCrop(c ? c->crop : std::nullopt);
    m_canvas->setRadius(c ? c->cropRadius : 0);
    m_canvas->setAspect(lockedAspect());
}

void ClipEditorDialog::refreshControls()
{
    const StickerTask* c = m_selected >= 0 && m_selected < m_clips.size() ? &m_clips.at(m_selected) : nullptr;
    const bool has = c != nullptr;
    const QString needClip = tr("请先选择一个片段");

    if (m_isVideo) {
        m_startButton->setEnabled(has);
        const bool canEnd = has && m_state.playhead() > c->startTime.value_or(0);
        m_endButton->setEnabled(canEnd);
        m_endButton->setToolTip(!has ? needClip : canEnd ? tr("设当前时刻为片段终点（]）") : tr("终点必须晚于起点"));
        m_fitButton->setEnabled(has);
        m_syncButton->setEnabled(has && c->crop && m_clips.size() > 1);
        m_confirm->setText(tr("✔ 确认并添加到贴纸列表 (共 %n 个)", nullptr, int(m_clips.size())));
    } else {
        m_confirm->setText(tr("✔ 确认裁切"));
    }
    m_mirrorButton->setEnabled(has);
    m_emojiEdit->setEnabled(has);
    m_cropBar->setEnabled(has);
    for (QPushButton* b : {m_startButton, m_mirrorButton})
        if (b && !has)
            b->setToolTip(needClip);

    {
        const QSignalBlocker block(m_mirrorButton);
        m_mirrorButton->setChecked(has && c->mirror);
    }
    if (!m_emojiEdit->hasFocus())
        m_emojiEdit->setText(has ? c->emoji : QString());
    m_emojiEdit->setPlaceholderText(has ? tr("可留空") : needClip);
    m_cropSize->setText(has && c->crop ? QStringLiteral("📍 %1×%2").arg(c->crop->width()).arg(c->crop->height())
                                       : tr("📍 未裁切"));
    {
        const QSignalBlocker block(m_radius);
        m_radius->setValue(has ? qRound(c->cropRadius * 100) : 0);
    }
    m_radiusLabel->setText(shapeLabel(has ? c->cropRadius : 0));
    if (m_isVideo && !has)
        showHint(needClip);
}

void ClipEditorDialog::refreshInfoRow()
{
    if (!m_isVideo)
        return;
    m_timeLabel->setText(formatTimecode(m_state.playhead()));
    if (const auto hover = m_state.hover()) {
        m_contextLabel->setText(tr("悬停 %1").arg(formatTimecode(*hover)));
    } else if (m_selected >= 0 && m_selected < m_clips.size()) {
        const StickerTask& c = m_clips.at(m_selected);
        const double start = c.startTime.value_or(0);
        QString text = tr("片段 %1 – ").arg(formatTimecode(start));
        text += c.endTime ? QStringLiteral("%1 (%2s)").arg(formatTimecode(*c.endTime)).arg(*c.endTime - start, 0, 'f', 2)
                          : tr("未设终点");
        if (!c.emoji.isEmpty())
            text += QStringLiteral(" (%1)").arg(c.emoji);
        m_contextLabel->setText(text);
    } else {
        m_contextLabel->clear();
    }
    m_fullButton->setVisible(m_state.isZoomed());
    m_fullButton->setText(tr("全长 %1s").arg(formatSecondsShort(m_base.info.duration)));
}

void ClipEditorDialog::refreshStepLabels()
{
    if (!m_isVideo)
        return;
    const QString big = stepText(m_settings.bigStep());
    const QString small = stepText(m_settings.smallStep());
    m_backBig->setText(QStringLiteral("⏪ -") + big);
    m_backSmall->setText(QStringLiteral("⏮️ -") + small);
    m_fwdSmall->setText(QStringLiteral("+") + small + QStringLiteral(" ⏭️"));
    m_fwdBig->setText(QStringLiteral("+") + big + QStringLiteral(" ⏩"));
}

void ClipEditorDialog::scheduleThumbnails()
{
    if (m_panel && m_thumbs)
        m_thumbTimer->start();
}

void ClipEditorDialog::requestThumbnails()
{
    m_thumbKeys.clear();
    for (int i = 0; i < m_clips.size(); ++i) {
        const ThumbnailService::Request request = ThumbnailService::requestFor(m_clips.at(i), kCardThumbBox);
        m_thumbKeys[ThumbnailService::keyFor(request)] << i;
        m_panel->setThumbnail(i, m_thumbs->thumbnail(request));
    }
}

void ClipEditorDialog::showHint(const QString& text, bool warning)
{
    m_hint->setText(text);
    if (m_hint->property("warning").toBool() != warning) {
        m_hint->setProperty("warning", warning);
        repolish(m_hint);
    }
}

void ClipEditorDialog::openSettings()
{
    EditorSettingsDialog dialog(m_settings, this);
    if (dialog.exec() == QDialog::Accepted) {
        refreshStepLabels();
        if (m_overviewButton)
            m_overviewButton->setChecked(m_settings.showTimelineOverview());
    }
}

void ClipEditorDialog::confirm()
{
    if (m_isVideo) {
        // 每个片段都必须有终点
        for (int i = 0; i < m_clips.size(); ++i) {
            const StickerTask& c = m_clips.at(i);
            if (!c.endTime || *c.endTime <= c.startTime.value_or(0)) {
                selectClip(i, true);
                QMessageBox::warning(this, tr("提示"),
                                     tr("时间区间无效：请为片段设置结束时间（起点 %1）").arg(formatTimecode(c.startTime.value_or(0))));
                return;
            }
        }
    }
    if (m_player) {
        m_player->stop();
        m_standby->stop();
    }
    accept();
}

// ---------------------------------------------------------------- 快捷键

bool ClipEditorDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress || QApplication::activePopupWidget())
        return QDialog::eventFilter(watched, event);
    // 只处理发往本窗口（或其子控件）的按键
    auto* target = qobject_cast<QWidget*>(watched);
    if (!target || (target != this && !isAncestorOf(target)))
        return QDialog::eventFilter(watched, event);
    // 焦点在输入框、下拉框里时不响应
    if (isTextInput(QApplication::focusWidget()))
        return QDialog::eventFilter(watched, event);

    auto* key = static_cast<QKeyEvent*>(event);
    const bool shift = key->modifiers() & Qt::ShiftModifier;
    if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return QDialog::eventFilter(watched, event);

    switch (key->key()) {
    case Qt::Key_M: toggleMirror(); return true; // 图片模式也有效
    case Qt::Key_Escape: return true;            // 菜单自己会关闭；这里只防止误关窗口
    default: break;
    }
    if (!m_isVideo)
        return QDialog::eventFilter(watched, event);

    switch (key->key()) {
    case Qt::Key_Space: togglePlay(); return true;
    case Qt::Key_Left: step(-(shift ? m_settings.bigStep() : m_settings.smallStep())); return true;
    case Qt::Key_Right: step(shift ? m_settings.bigStep() : m_settings.smallStep()); return true;
    case Qt::Key_BracketLeft: setStartHere(); return true;
    case Qt::Key_BracketRight: setEndHere(); return true;
    case Qt::Key_Comma: changeSpeed(-1); return true;
    case Qt::Key_Period: changeSpeed(1); return true;
    case Qt::Key_Equal:
    case Qt::Key_Plus: m_state.zoomAround(m_state.playhead(), 0.8); return true;
    case Qt::Key_Minus: m_state.zoomAround(m_state.playhead(), 1.25); return true;
    case Qt::Key_0: m_state.showAll(); return true;
    case Qt::Key_F: m_fitButton->click(); return true;
    default: break;
    }
    return QDialog::eventFilter(watched, event);
}

void ClipEditorDialog::keyPressEvent(QKeyEvent* event)
{
    // Esc 不关闭窗口（避免丢失编辑），Enter 不触发默认按钮
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void ClipEditorDialog::closeEvent(QCloseEvent* event)
{
    if (m_player) {
        m_player->stop();
        m_standby->stop();
    }
    QDialog::closeEvent(event);
}

} // namespace tsm
