#include "app/AppSettings.h"

#include "app/AppPaths.h"

#include <QDir>

namespace tsm {

namespace key {
constexpr auto OutputMode = "output/mode";
constexpr auto EncodePreset = "output/encode_preset";
constexpr auto LegacyEncodePreset = "output/preset"; // 0.1.x：0 动漫 / 1 实拍 / 2 快速
constexpr auto SaveBesideSource = "output/beside_source";
constexpr auto OutputDir = "output/dir";
constexpr auto OutputAsZip = "output/as_zip";
constexpr auto DataDir = "data/dir";
constexpr auto SmallStep = "editor/small_step";
constexpr auto BigStep = "editor/big_step";
constexpr auto CropAspect = "editor/default_crop_aspect";
constexpr auto CropRadius = "editor/default_crop_radius";
constexpr auto ShowOverview = "editor/show_overview";
constexpr auto PlaybackRate = "editor/playback_rate";
constexpr auto AiBaseUrl = "ai/base_url";
constexpr auto AiModel = "ai/model";
constexpr auto AiApiKey = "ai/api_key";
constexpr auto ShowDoneDialog = "ui/show_done_dialog";
constexpr auto Language = "ui/language";
} // namespace key

AppSettings::AppSettings()
    : AppSettings(QDir(AppPaths::configDir()).filePath(QStringLiteral("settings.ini")))
{
}

AppSettings::AppSettings(const QString& iniPath)
    : m_settings(iniPath, QSettings::IniFormat)
{
    migrate();
}

void AppSettings::migrate()
{
    // 0.1.x 的三个预设合并为两个：动漫 / 实拍 → 高画质，快速保持快速
    const QLatin1StringView legacy(key::LegacyEncodePreset);
    if (m_settings.contains(legacy)) {
        if (!m_settings.contains(QLatin1StringView(key::EncodePreset)))
            setEncodePreset(m_settings.value(legacy).toInt() == 2 ? EncodePreset::Fast : EncodePreset::Quality);
        m_settings.remove(legacy);
        m_settings.sync();
    }
}

QVariant AppSettings::value(const char* name, const QVariant& fallback) const
{
    return m_settings.value(QLatin1StringView(name), fallback);
}

void AppSettings::setValue(const char* name, const QVariant& v)
{
    m_settings.setValue(QLatin1StringView(name), v);
    m_settings.sync();
}

OutputMode AppSettings::outputMode() const
{
    return value(key::OutputMode, 0).toInt() == int(OutputMode::Emoji) ? OutputMode::Emoji : OutputMode::Sticker;
}

void AppSettings::setOutputMode(OutputMode mode) { setValue(key::OutputMode, int(mode)); }

EncodePreset AppSettings::encodePreset() const
{
    return value(key::EncodePreset, QString()).toString() == QLatin1StringView("fast") ? EncodePreset::Fast : EncodePreset::Quality;
}

void AppSettings::setEncodePreset(EncodePreset preset)
{
    setValue(key::EncodePreset, preset == EncodePreset::Fast ? QStringLiteral("fast") : QStringLiteral("quality"));
}

bool AppSettings::saveBesideSource() const { return value(key::SaveBesideSource, false).toBool(); }
void AppSettings::setSaveBesideSource(bool on) { setValue(key::SaveBesideSource, on); }

QString AppSettings::outputDir() const
{
    const QString dir = value(key::OutputDir, QString()).toString();
    return dir.isEmpty() ? AppPaths::defaultOutputDir() : dir;
}

void AppSettings::setOutputDir(const QString& dir) { setValue(key::OutputDir, dir); }

bool AppSettings::outputAsZip() const { return value(key::OutputAsZip, true).toBool(); }
void AppSettings::setOutputAsZip(bool on) { setValue(key::OutputAsZip, on); }

QString AppSettings::dataDirChoice() const { return value(key::DataDir, QString()).toString(); }
void AppSettings::setDataDirChoice(const QString& dir) { setValue(key::DataDir, dir); }
QString AppSettings::resolvedDataDir() const { return AppPaths::resolveDataDir(dataDirChoice()); }

double AppSettings::smallStep() const { return qBound(0.1, value(key::SmallStep, 0.5).toDouble(), 60.0); }
void AppSettings::setSmallStep(double seconds) { setValue(key::SmallStep, qBound(0.1, seconds, 60.0)); }
double AppSettings::bigStep() const { return qBound(0.5, value(key::BigStep, 5.0).toDouble(), 300.0); }
void AppSettings::setBigStep(double seconds) { setValue(key::BigStep, qBound(0.5, seconds, 300.0)); }

CropAspect AppSettings::defaultCropAspect() const
{
    const int v = value(key::CropAspect, 0).toInt();
    return v >= 0 && v <= int(CropAspect::Portrait9x16) ? CropAspect(v) : CropAspect::Square;
}

void AppSettings::setDefaultCropAspect(CropAspect aspect) { setValue(key::CropAspect, int(aspect)); }

int AppSettings::defaultCropRadius() const { return qBound(0, value(key::CropRadius, 0).toInt(), 100); }
void AppSettings::setDefaultCropRadius(int percent) { setValue(key::CropRadius, qBound(0, percent, 100)); }
bool AppSettings::showTimelineOverview() const { return value(key::ShowOverview, true).toBool(); }
void AppSettings::setShowTimelineOverview(bool on) { setValue(key::ShowOverview, on); }
double AppSettings::playbackRate() const { return qBound(0.25, value(key::PlaybackRate, 1.0).toDouble(), 4.0); }
void AppSettings::setPlaybackRate(double rate) { setValue(key::PlaybackRate, qBound(0.25, rate, 4.0)); }

QString AppSettings::aiBaseUrl() const
{
    return value(key::AiBaseUrl, QStringLiteral("https://api.openai.com/v1")).toString();
}

void AppSettings::setAiBaseUrl(const QString& url) { setValue(key::AiBaseUrl, url); }
QString AppSettings::aiModel() const { return value(key::AiModel, QStringLiteral("gemini-3.5-flash-lite")).toString(); }
void AppSettings::setAiModel(const QString& model) { setValue(key::AiModel, model); }
QString AppSettings::aiApiKey() const { return value(key::AiApiKey, QString()).toString(); }
void AppSettings::setAiApiKey(const QString& apiKey) { setValue(key::AiApiKey, apiKey); }
bool AppSettings::showDoneDialog() const { return value(key::ShowDoneDialog, true).toBool(); }
void AppSettings::setShowDoneDialog(bool on) { setValue(key::ShowDoneDialog, on); }
QString AppSettings::language() const { return value(key::Language, QString()).toString(); }
void AppSettings::setLanguage(const QString& language) { setValue(key::Language, language); }

} // namespace tsm
