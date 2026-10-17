#pragma once

#include "core/Spec.h"
#include "core/StickerTask.h"
#include "core/TimelineState.h"
#include "ui/editor/ClipListPanel.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QHash>

class QAudioOutput;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMediaPlayer;
class QPushButton;
class QSlider;
class QTimer;
class QVideoSink;
class QWidget;

namespace tsm {

class AppSettings;
class PreviewCanvas;
class ProxyCache;
class ThumbnailService;
class TimelineOverview;
class TimelineTrack;

// 截取 / 裁切编辑窗口（需求第 6 节）。
// 视频模式编辑同一视频的一组片段；图片模式只编辑一个素材的裁切样式。
class ClipEditorDialog : public QDialog {
    Q_OBJECT

public:
    ClipEditorDialog(AppSettings& settings, ThumbnailService* thumbnails, ProxyCache* proxies,
                     const QList<StickerTask>& clips, int focusIndex, QWidget* parent = nullptr);
    ~ClipEditorDialog() override;

    const QList<StickerTask>& clips() const { return m_clips; }
    int selectedIndex() const { return m_selected; }
    double playhead() const { return m_state.playhead(); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    enum class PlayMode { Free, Clip, Loop };

    // 界面搭建
    QWidget* buildTopBar();
    QWidget* buildTimeline();
    QWidget* buildControls();
    QWidget* buildCropBar();
    QWidget* buildFooter();
    void setupPlayer();
    void loadStillImage();

    // 片段
    StickerTask* current();
    void selectClip(int index, bool seek);
    void deselectClip();
    void addClip();
    void deleteClip(int index);
    void setStartHere();
    void setEndHere();
    void editStart(int index, const QString& text);
    void editEnd(int index, const QString& text);
    void editEmoji(int index, const QString& text);
    void showClipMenu(int index, const QPoint& globalPos);

    // 裁切 / 镜像
    void setCropMode(bool on);
    void ensureCrop(StickerTask& clip);
    void onAspectChanged();
    void resetCropCenter();
    void syncShapeToAll();
    void clearCrop();
    void toggleMirror();
    std::optional<double> lockedAspect() const;
    CropAspect inferAspect(const QRect& crop) const;

    // 播放
    void togglePlay();
    void playClip(int index, PlayMode mode);
    void stopClipPlayback();
    void seek(double seconds);
    void step(double seconds);
    void changeSpeed(int delta);
    void onTick();
    void switchToProxy(const QString& proxyPath);
    void refreshProxyButton();

    // 刷新
    void refreshAll();
    void refreshTimeline();
    void refreshClipPanel();
    void refreshControls();
    void refreshInfoRow();
    void refreshCanvas();
    void refreshStepLabels();
    void scheduleThumbnails();
    void requestThumbnails();
    void showHint(const QString& text, bool warning = false);
    void openSettings();
    void confirm();

    AppSettings& m_settings;
    ThumbnailService* m_thumbs;
    ProxyCache* m_proxies;
    QList<StickerTask> m_clips;
    StickerTask m_base;
    bool m_isVideo = false;
    QSize m_frame;
    int m_selected = -1;
    bool m_cropMode = false;
    int m_clipCounter = 0;

    TimelineState m_state;
    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audio = nullptr;
    QVideoSink* m_sink = nullptr;
    QTimer* m_tick = nullptr;
    // 播放时的平滑时钟：播放器的位置只在每解出一帧时更新（25fps 即 40ms 跳一次），
    // 放大时间轴后指针会明显跳动；这里用计时器连续推算，再用真实位置慢慢校正
    QElapsedTimer m_clock;
    double m_clockAnchor = 0;    // 计时开始时的位置（秒）
    qint64 m_lastReportedMs = -1;
    double m_lastShown = 0;
    void resyncClock(double seconds);
    double smoothPosition();
    QTimer* m_thumbTimer = nullptr;
    PlayMode m_playMode = PlayMode::Free;
    int m_playIndex = -1;
    qint64 m_restorePositionMs = -1;
    bool m_restorePlaying = false;
    bool m_usingProxy = false;
    QString m_proxyError;
    QHash<QString, QList<int>> m_thumbKeys; // 缩略图 key → 片段下标

    PreviewCanvas* m_canvas = nullptr;
    TimelineTrack* m_track = nullptr;
    TimelineOverview* m_overview = nullptr;
    ClipListPanel* m_panel = nullptr;
    QLabel* m_infoLabel = nullptr;
    QPushButton* m_proxyButton = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLabel* m_contextLabel = nullptr;
    QLabel* m_totalLabel = nullptr;
    QPushButton* m_overviewButton = nullptr;
    QPushButton* m_fitButton = nullptr;
    QPushButton* m_fullButton = nullptr;
    QPushButton* m_backBig = nullptr;
    QPushButton* m_backSmall = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_fwdSmall = nullptr;
    QPushButton* m_fwdBig = nullptr;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_endButton = nullptr;
    QPushButton* m_cropButton = nullptr;
    QPushButton* m_mirrorButton = nullptr;
    QLineEdit* m_emojiEdit = nullptr;
    QComboBox* m_speed = nullptr;
    QPushButton* m_muteButton = nullptr;
    QWidget* m_cropBar = nullptr;
    QComboBox* m_aspect = nullptr;
    QPushButton* m_syncButton = nullptr;
    QLabel* m_cropSize = nullptr;
    QSlider* m_radius = nullptr;
    QLabel* m_radiusLabel = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_confirm = nullptr;
};

} // namespace tsm
