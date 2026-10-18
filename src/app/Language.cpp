#include "app/Language.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace tsm::Language {

namespace {
const QString kSource = QStringLiteral("zh_CN");

bool isAvailable(const QString& code)
{
    for (const Option& option : available()) {
        if (option.code == code)
            return true;
    }
    return false;
}

// 翻译器装上后要活到程序退出
void installTranslator(const QString& file, const QStringList& dirs)
{
    auto* translator = new QTranslator(QCoreApplication::instance());
    for (const QString& dir : dirs) {
        if (translator->load(file, dir)) {
            QCoreApplication::installTranslator(translator);
            return;
        }
    }
    delete translator;
}
} // namespace

QList<Option> available()
{
    return {{kSource, QStringLiteral("简体中文")},
            {QStringLiteral("en"), QStringLiteral("English")},
            {QStringLiteral("ru"), QStringLiteral("Русский")}};
}

QString resolve(const QString& setting)
{
    if (isAvailable(setting))
        return setting;
    for (const QString& name : QLocale::system().uiLanguages()) {
        if (name.startsWith(u"zh"))
            return kSource;
        if (name.startsWith(u"ru"))
            return QStringLiteral("ru");
        if (name.startsWith(u"en"))
            return QStringLiteral("en");
    }
    return QStringLiteral("en");
}

void install(const QString& code)
{
    QLocale::setDefault(QLocale(code));

    // 界面文字直接用中文写在代码里，中文不需要本程序的翻译文件
    if (code != kSource)
        installTranslator(QStringLiteral("tsm_") + code, {QStringLiteral(":/i18n")});

    // Qt 自带对话框 / 按钮：优先程序目录下的 translations（部署后），其次 Qt 安装目录。英语是 Qt 的源语言
    if (code == u"en")
        return;
    const QStringList dirs{QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
    installTranslator(QStringLiteral("qtbase_") + code, dirs);
    installTranslator(QStringLiteral("qtmultimedia_") + code, dirs);
}

} // namespace tsm::Language
