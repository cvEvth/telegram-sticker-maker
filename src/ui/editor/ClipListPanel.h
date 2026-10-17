#pragma once

#include "core/StickerTask.h"

#include <QFrame>
#include <QImage>

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace tsm {

// 片段卡片（需求 6.7）
class ClipCard : public QFrame {
    Q_OBJECT

public:
    enum class PlayState { None, Playing, Looping };

    explicit ClipCard(QWidget* parent = nullptr);
    void setClip(int index, const StickerTask& clip, bool selected, PlayState play);
    void setThumbnail(const QImage& image);

signals:
    void clicked();
    void activated(); // 鼠标点在卡片上（不含输入框），用于自动循环播放
    void startEdited(const QString& text);
    void endEdited(const QString& text);
    void emojiEdited(const QString& text);
    void menuRequested(const QPoint& globalPos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    QLabel* m_index = nullptr;
    QLabel* m_thumb = nullptr;
    QLineEdit* m_start = nullptr;
    QLineEdit* m_end = nullptr;
    QLineEdit* m_emoji = nullptr;
    QLabel* m_duration = nullptr;
    QLabel* m_tags = nullptr;
    QToolButton* m_menu = nullptr;
};

class ClipListPanel : public QFrame {
    Q_OBJECT

public:
    explicit ClipListPanel(QWidget* parent = nullptr);

    void setClips(const QList<StickerTask>& clips, int selected, int playingIndex, ClipCard::PlayState play);
    void setThumbnail(int index, const QImage& image);
    void scrollToClip(int index);

signals:
    void clipClicked(int index);
    void clipActivated(int index);
    void backgroundClicked();
    void startEdited(int index, const QString& text);
    void endEdited(int index, const QString& text);
    void emojiEdited(int index, const QString& text);
    void menuRequested(int index, const QPoint& globalPos);
    void addClicked();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLabel* m_count = nullptr;
    QWidget* m_listHost = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QList<ClipCard*> m_cards;
    QScrollArea* m_scroll = nullptr;
};

} // namespace tsm
