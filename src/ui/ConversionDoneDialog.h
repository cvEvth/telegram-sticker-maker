#pragma once

#include <QDialog>

class QCheckBox;

namespace tsm {

// 批量转换成功后的提示：转换结果 + 引导去 cvevth.top 创建贴纸包
class ConversionDoneDialog : public QDialog {
    Q_OBJECT

public:
    ConversionDoneDialog(int succeeded, int failed, const QString& location, bool zipOutput, QWidget* parent = nullptr);

    bool dontShowAgain() const;

signals:
    void openOutputRequested();

private:
    QCheckBox* m_dontShow = nullptr;
};

} // namespace tsm
