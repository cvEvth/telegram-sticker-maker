#pragma once

#include <QObject>

namespace tsm {

// 运行日志。可在任意线程调用 log()，界面通过 lineAdded 接收（跨线程自动排队）。
class Logger : public QObject {
    Q_OBJECT

public:
    static Logger& instance();
    void log(const QString& message);

signals:
    void lineAdded(const QString& line); // "[14:23:05] ..."

private:
    Logger() = default;
};

inline void logInfo(const QString& message)
{
    Logger::instance().log(message);
}

} // namespace tsm
