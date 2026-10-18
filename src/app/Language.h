#pragma once

#include <QList>
#include <QString>

namespace tsm::Language {

struct Option {
    QString code;       // "zh_CN" / "en" / "ru"
    QString nativeName; // 用该语言自己的写法显示，如 "Русский"
};

// 支持的界面语言，第一个是源语言（简体中文）
QList<Option> available();

// 设置值为空时跟随系统：系统语言是中文 → 简体中文，俄语 → 俄语，其他 → 英语
QString resolve(const QString& setting);

// 安装本程序与 Qt 自带对话框的翻译；必须在创建任何界面之前、在主线程调用
void install(const QString& code);

} // namespace tsm::Language
