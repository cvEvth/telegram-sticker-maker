#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;

namespace tsm {

// 批量设置 Emoji（右键菜单）：覆盖所有选中贴纸的 Emoji，文件名随之更新。
// 只保留 Emoji 字符（最多 12 个字符）；留空 = 清除。
class EmojiDialog : public QDialog {
    Q_OBJECT

public:
    EmojiDialog(const QString& subtitle, const QString& initial, QWidget* parent = nullptr);

    // 过滤后的 Emoji（可能为空）
    QString emoji() const;

private:
    void refreshPreview();
    void append(const QString& emoji);

    QLineEdit* m_input = nullptr;
    QLabel* m_preview = nullptr;
};

} // namespace tsm
