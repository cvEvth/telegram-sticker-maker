#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace tsm {

class AppSettings;

// ⚙️ AI 设置（需求 7.1）：API Key / Base URL / 模型名称；保存后显示 ✓ 并自动关闭
class AiSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit AiSettingsDialog(AppSettings& settings, QWidget* parent = nullptr);

private:
    void save();

    AppSettings& m_settings;
    QLineEdit* m_key = nullptr;
    QLineEdit* m_baseUrl = nullptr;
    QLineEdit* m_model = nullptr;
    QLabel* m_message = nullptr;
    QPushButton* m_save = nullptr;
};

} // namespace tsm
