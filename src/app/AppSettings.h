#pragma once

#include "core/Spec.h"

#include <QSettings>
#include <QString>

namespace tsm {

// 持久化设置。每次修改立即写盘。
class AppSettings {
public:
    AppSettings();
    explicit AppSettings(const QString& iniPath);
    AppSettings(const AppSettings&) = delete;
    AppSettings& operator=(const AppSettings&) = delete;

    OutputMode outputMode() const;
    void setOutputMode(OutputMode mode);
    EncodePreset encodePreset() const;
    void setEncodePreset(EncodePreset preset);

    bool saveBesideSource() const;
    void setSaveBesideSource(bool on);
    QString outputDir() const; // 未设置时为 桌面/TG_Stickers
    void setOutputDir(const QString& dir);
    bool outputAsZip() const;
    void setOutputAsZip(bool on);

    QString dataDirChoice() const;
    void setDataDirChoice(const QString& dir);
    QString resolvedDataDir() const;

    // 截取窗口
    double smallStep() const;
    void setSmallStep(double seconds);
    double bigStep() const;
    void setBigStep(double seconds);
    CropAspect defaultCropAspect() const;
    void setDefaultCropAspect(CropAspect aspect);
    int defaultCropRadius() const; // 0~100
    void setDefaultCropRadius(int percent);
    bool showTimelineOverview() const;
    void setShowTimelineOverview(bool on);
    double playbackRate() const;
    void setPlaybackRate(double rate);

    // AI
    QString aiBaseUrl() const;
    void setAiBaseUrl(const QString& url);
    QString aiModel() const;
    void setAiModel(const QString& model);
    // TODO(AI): 改用 Windows DPAPI 加密存储
    QString aiApiKey() const;
    void setAiApiKey(const QString& key);

    // 转换成功后弹出提示（可在弹窗里勾选"以后不再弹出"）
    bool showDoneDialog() const;
    void setShowDoneDialog(bool on);

    QString filePath() const { return m_settings.fileName(); }

private:
    void migrate(); // 旧版设置项改名 / 合并
    QVariant value(const char* key, const QVariant& fallback) const;
    void setValue(const char* key, const QVariant& value);

    QSettings m_settings;
};

} // namespace tsm
