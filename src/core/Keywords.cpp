#include "core/Keywords.h"

#include "core/Spec.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace tsm::Keywords {

QStringList split(const QString& input)
{
    static const QRegularExpression separators(QStringLiteral("[,，、;；\\s]+"));
    QStringList words;
    for (const QString& part : input.split(separators, Qt::SkipEmptyParts)) {
        const QString word = part.trimmed();
        if (!word.isEmpty())
            words << word;
    }
    return words;
}

int charCount(const QString& text)
{
    return int(text.toUcs4().size());
}

int totalChars(const QStringList& keywords)
{
    int total = 0;
    for (const QString& k : keywords)
        total += charCount(k);
    return total;
}

AddResult add(QStringList& keywords, const QString& input)
{
    AddResult result;
    const auto fail = [&](const QString& message) {
        if (result.error.isEmpty())
            result.error = message;
    };
    for (const QString& word : split(input)) {
        if (keywords.contains(word)) {
            fail(QCoreApplication::translate("Keywords", "「%1」已存在").arg(word));
            continue;
        }
        if (keywords.size() >= spec::kMaxKeywords) {
            fail(QCoreApplication::translate("Keywords", "最多 %1 个关键词").arg(spec::kMaxKeywords));
            break;
        }
        const int remaining = spec::kMaxKeywordChars - totalChars(keywords);
        if (remaining <= 0) {
            fail(QCoreApplication::translate("Keywords", "总长度已满 %1 字符").arg(spec::kMaxKeywordChars));
            break;
        }
        const int need = charCount(word);
        if (need > remaining) {
            fail(QCoreApplication::translate("Keywords", "「%1」需要 %2 字符，还剩 %3").arg(word).arg(need).arg(remaining));
            continue; // 后面更短的词也许还放得下
        }
        keywords << word;
        result.added << word;
    }
    return result;
}

bool isFull(const QStringList& keywords)
{
    return keywords.size() >= spec::kMaxKeywords || totalChars(keywords) >= spec::kMaxKeywordChars;
}

} // namespace tsm::Keywords
