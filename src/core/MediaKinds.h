#pragma once

#include <QString>
#include <QStringList>

namespace tsm {

enum class MediaKind { Unsupported, Video, AnimatedImage, StaticImage };

namespace MediaKinds {

const QStringList& videoExtensions();
const QStringList& imageExtensions();

MediaKind classify(const QString& path);
bool isSupportedMedia(const QString& path);
bool isImportFile(const QString& path); // .zip / .json

// 读文件头判断 PNG / WebP 是否为动图
bool isAnimatedPng(const QString& path);
bool isAnimatedWebp(const QString& path);

QString openFileFilter();

} // namespace MediaKinds
} // namespace tsm
