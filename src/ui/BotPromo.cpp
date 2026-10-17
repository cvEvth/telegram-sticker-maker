#include "ui/BotPromo.h"

#include <QDesktopServices>
#include <QUrl>

namespace tsm::BotPromo {

void openBot()
{
    QDesktopServices::openUrl(QUrl(botUrl()));
}

void openGallery()
{
    QDesktopServices::openUrl(QUrl(galleryUrl()));
}

} // namespace tsm::BotPromo
