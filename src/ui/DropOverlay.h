#pragma once

#include <QWidget>

namespace tsm {

// 拖入文件时覆盖整个窗口的提示遮罩
class DropOverlay : public QWidget {
    Q_OBJECT

public:
    explicit DropOverlay(QWidget* parent);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace tsm
