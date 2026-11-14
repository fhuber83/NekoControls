#include "neko/theme/theme_translations.hpp"

#include <QTranslator>

// The .qm files (neko_theme_translations.qrc). Resources in a STATIC library register only
// when their initializer is referenced, and the macro must sit at global scope --
// inside a namespace it declares the initializer there and fails to link.
static void ensureThemeTranslationsInitialized() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(neko_theme_translations);
        return true;
    }();
    (void)initialized;
}

namespace neko::theme {

QStringList themeTranslationLanguages() {
    return {QStringLiteral("de"), QStringLiteral("bar"), QStringLiteral("it")};
}

bool loadThemeTranslation(QTranslator& translator, const QString& languageCode) {
    ensureThemeTranslationsInitialized();
    if (!themeTranslationLanguages().contains(languageCode)) {
        return false;
    }
    return translator.load(QStringLiteral(":/neko/i18n/neko_theme_%1.qm").arg(languageCode));
}

} // namespace neko::theme
