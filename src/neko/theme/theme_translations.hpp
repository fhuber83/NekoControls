#pragma once

#include <QString>
#include <QStringList>

class QTranslator;

namespace neko::theme {

/// This library's own translations (the restart notice of ThemeManager),
/// compiled into it from translations/neko_theme_<code>.qm. The source text is English,
/// which needs none.
///
/// Loads the translation for `languageCode` ("de", "it", "bar") into `translator` and
/// returns true, or returns false and leaves `translator` empty for a language the
/// library has none of. Install the translator with QCoreApplication::installTranslator;
/// the application decides the language, the library never reads the locale itself.
///
/// UI thread only, like any QTranslator an application installs.
[[nodiscard]] bool loadThemeTranslation(QTranslator& translator, const QString& languageCode);

/// The language codes loadThemeTranslation() knows.
[[nodiscard]] QStringList themeTranslationLanguages();

} // namespace neko::theme
