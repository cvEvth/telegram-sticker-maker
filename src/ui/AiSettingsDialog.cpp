#include "ui/AiSettingsDialog.h"

#include "app/AiMatcher.h"
#include "app/AppSettings.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace tsm {

AiSettingsDialog::AiSettingsDialog(AppSettings& settings, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(QStringLiteral("AI 设置"));
    setObjectName(QStringLiteral("AiSettingsDialog"));
    setMinimumWidth(480);

    auto* title = new QLabel(QStringLiteral("⚙️ AI 设置"));
    title->setObjectName(QStringLiteral("DialogTitle"));
    auto* desc = new QLabel(QStringLiteral("支持任何兼容 OpenAI 格式的接口 (如 DeepSeek, Moonshot, OneAPI 等)。"));
    desc->setObjectName(QStringLiteral("Hint"));
    desc->setWordWrap(true);

    m_key = new QLineEdit(settings.aiApiKey());
    m_key->setObjectName(QStringLiteral("AiApiKey"));
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(QStringLiteral("sk-..."));
    m_baseUrl = new QLineEdit(settings.aiBaseUrl());
    m_baseUrl->setObjectName(QStringLiteral("AiBaseUrl"));
    m_baseUrl->setPlaceholderText(QStringLiteral("https://api.openai.com/v1"));
    m_model = new QLineEdit(settings.aiModel());
    m_model->setObjectName(QStringLiteral("AiModel"));
    m_model->setPlaceholderText(QStringLiteral("gemini-3.5-flash-lite"));

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    form->addRow(QStringLiteral("API Key"), m_key);
    form->addRow(QStringLiteral("Base URL"), m_baseUrl);
    form->addRow(QStringLiteral("模型名称"), m_model);

    auto* localHint = new QLabel(QStringLiteral("Base URL 指向本机（localhost / 127.0.0.1）时可以不填 Key。"));
    localHint->setObjectName(QStringLiteral("Hint"));
    localHint->setWordWrap(true);

    m_message = new QLabel;
    m_message->setObjectName(QStringLiteral("Hint"));
    auto* cancel = new QPushButton(QStringLiteral("取消"));
    m_save = new QPushButton(QStringLiteral("保存"));
    m_save->setObjectName(QStringLiteral("PrimaryButton"));
    m_save->setMinimumSize(96, 34);
    m_save->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_message, 1);
    buttons->addWidget(cancel);
    buttons->addWidget(m_save);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    layout->addWidget(title);
    layout->addWidget(desc);
    layout->addLayout(form);
    layout->addWidget(localHint);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_save, &QPushButton::clicked, this, &AiSettingsDialog::save);
}

void AiSettingsDialog::save()
{
    AiConfig config;
    config.baseUrl = m_baseUrl->text().trimmed();
    if (config.baseUrl.isEmpty())
        config.baseUrl = QStringLiteral("https://api.openai.com/v1");
    config.model = m_model->text().trimmed();
    if (config.model.isEmpty())
        config.model = QStringLiteral("gemini-3.5-flash-lite");
    config.apiKey = m_key->text().trimmed();
    // Key 留空也允许保存（之后识别时再提示），这里只拦明显无效的地址
    const QString problem = AiMatcher::validate(config);
    if (!problem.isEmpty() && !problem.contains(QStringLiteral("API Key"))) {
        m_message->setProperty("warning", true);
        m_message->setText(problem);
        m_message->style()->polish(m_message);
        return;
    }
    m_settings.setAiBaseUrl(config.baseUrl);
    m_settings.setAiModel(config.model);
    m_settings.setAiApiKey(config.apiKey);
    m_message->setProperty("warning", false);
    m_message->setText(QStringLiteral("✓ 已保存"));
    m_message->style()->polish(m_message);
    m_save->setEnabled(false);
    QTimer::singleShot(700, this, &QDialog::accept);
}

} // namespace tsm
