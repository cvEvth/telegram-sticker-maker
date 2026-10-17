#include "app/Logger.h"

#include <QTime>

namespace tsm {

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::log(const QString& message)
{
    emit lineAdded(QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message));
}

} // namespace tsm
