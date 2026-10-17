#include "ui/editor/EditorSettingsDialog.h"

#include "app/AppSettings.h"
#include "core/CropMath.h"
#include "core/StickerTask.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace tsm {

EditorSettingsDialog::EditorSettingsDialog(AppSettings& settings, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(QStringLiteral("窗口设置"));
    setMinimumWidth(380);

    m_small = new QDoubleSpinBox;
    m_small->setRange(0.1, 60);
    m_small->setDecimals(2);
    m_small->setSingleStep(0.1);
    m_small->setSuffix(QStringLiteral(" 秒"));
    m_big = new QDoubleSpinBox;
    m_big->setRange(0.5, 300);
    m_big->setDecimals(1);
    m_big->setSingleStep(0.5);
    m_big->setSuffix(QStringLiteral(" 秒"));

    m_aspect = new QComboBox;
    for (int i = 0; i <= int(CropAspect::Portrait9x16); ++i)
        m_aspect->addItem(CropMath::aspectLabel(CropAspect(i)), i);

    m_overview = new QCheckBox(QStringLiteral("显示时间轴总览条"));

    m_radius = new QSlider(Qt::Horizontal);
    m_radius->setRange(0, 100);
    m_radiusLabel = new QLabel;
    m_radiusLabel->setMinimumWidth(60);
    connect(m_radius, &QSlider::valueChanged, this, [this](int v) { m_radiusLabel->setText(shapeLabel(v / 100.0)); });
    auto* radiusRow = new QHBoxLayout;
    radiusRow->addWidget(m_radius, 1);
    radiusRow->addWidget(m_radiusLabel);

    auto* form = new QFormLayout;
    form->setSpacing(10);
    form->addRow(QStringLiteral("小快退 / 快进步长"), m_small);
    form->addRow(QStringLiteral("大快退 / 快进步长"), m_big);
    form->addRow(QStringLiteral("默认裁切比例"), m_aspect);
    form->addRow(QStringLiteral("默认圆角"), radiusRow);
    form->addRow(QString(), m_overview);

    auto* reset = new QPushButton(QStringLiteral("重置默认"));
    auto* ok = new QPushButton(QStringLiteral("确定"));
    ok->setObjectName(QStringLiteral("PrimaryButton"));
    ok->setMinimumHeight(32);
    ok->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(reset);
    buttons->addStretch();
    buttons->addWidget(ok);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(16);
    layout->addLayout(form);
    layout->addLayout(buttons);

    connect(reset, &QPushButton::clicked, this, [this] { load(0.5, 5.0, int(CropAspect::Square), true, 0); });
    connect(ok, &QPushButton::clicked, this, [this] {
        save();
        accept();
    });

    load(m_settings.smallStep(), m_settings.bigStep(), int(m_settings.defaultCropAspect()), m_settings.showTimelineOverview(),
         m_settings.defaultCropRadius());
}

void EditorSettingsDialog::load(double small, double big, int aspect, bool overview, int radius)
{
    m_small->setValue(small);
    m_big->setValue(big);
    m_aspect->setCurrentIndex(qMax(0, m_aspect->findData(aspect)));
    m_overview->setChecked(overview);
    m_radius->setValue(radius);
    m_radiusLabel->setText(shapeLabel(radius / 100.0));
}

void EditorSettingsDialog::save()
{
    m_settings.setSmallStep(m_small->value());
    m_settings.setBigStep(m_big->value());
    m_settings.setDefaultCropAspect(CropAspect(m_aspect->currentData().toInt()));
    m_settings.setShowTimelineOverview(m_overview->isChecked());
    m_settings.setDefaultCropRadius(m_radius->value());
}

} // namespace tsm
