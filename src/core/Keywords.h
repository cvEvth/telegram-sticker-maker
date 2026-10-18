#pragma once

#include <QStringList>

// 贴纸关键词规则：每个贴纸最多 20 个，合计不超过 64 个字符。
namespace tsm::Keywords {

// 按逗号（中英文）、顿号、分号（中英文）、空白拆分，去掉空项
QStringList split(const QString& input);
// 字符数（按 Unicode 码点计，一个 Emoji 算一个）
int charCount(const QString& text);
int totalChars(const QStringList& keywords);

struct AddResult {
    QStringList added;
    QString error; // 第一条提示，如 "「哈哈」已存在"；全部成功时为空
};

// 把一次输入里的词逐个加入 keywords；放不下的跳过并给出提示
AddResult add(QStringList& keywords, const QString& input);

bool isFull(const QStringList& keywords);

} // namespace tsm::Keywords
