#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace tsm {

class FlowLayout;

// 关键词弹窗（需求第 8 节）：标签式输入，最多 20 个、合计不超过 64 字符。
class KeywordDialog : public QDialog {
    Q_OBJECT

public:
    KeywordDialog(const QString& subtitle, const QStringList& initial, QWidget* parent = nullptr);

    QStringList keywords() const { return m_keywords; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void addFromInput();
    void removeAt(int index);
    void rebuildChips();
    void refreshState();
    void showMessage(const QString& text);

    QStringList m_keywords;
    QWidget* m_chipHost = nullptr;
    FlowLayout* m_chips = nullptr;
    QLabel* m_empty = nullptr;
    QLineEdit* m_input = nullptr;
    QPushButton* m_add = nullptr;
    QLabel* m_counter = nullptr;
    QLabel* m_message = nullptr;
};

} // namespace tsm
