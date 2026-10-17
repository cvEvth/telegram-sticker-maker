#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSlider;
class QLabel;

namespace tsm {

class AppSettings;

// 截取窗口设置（需求 6.9），确定后写入 AppSettings
class EditorSettingsDialog : public QDialog {
    Q_OBJECT

public:
    EditorSettingsDialog(AppSettings& settings, QWidget* parent = nullptr);

private:
    void load(double small, double big, int aspect, bool overview, int radius);
    void save();

    AppSettings& m_settings;
    QDoubleSpinBox* m_small = nullptr;
    QDoubleSpinBox* m_big = nullptr;
    QComboBox* m_aspect = nullptr;
    QCheckBox* m_overview = nullptr;
    QSlider* m_radius = nullptr;
    QLabel* m_radiusLabel = nullptr;
};

} // namespace tsm
