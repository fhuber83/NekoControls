#pragma once

#include <QString>
#include <QStringList>

class QTranslator;

namespace neko::editor {

/// This library's own translations (CodeEditor, EditorFindBar, MarkdownTableDialog, ...),
/// compiled into it from translations/neko_editor_<code>.qm. The source text is English,
/// which needs none.
///
/// Loads the translation for `languageCode` ("de", "it", "bar") into `translator` and
/// returns true, or returns false and leaves `translator` empty for a language the
/// library has none of. Install the translator with QCoreApplication::installTranslator;
/// the application decides the language, the library never reads the locale itself.
///
/// UI thread only, like any QTranslator an application installs.
[[nodiscard]] bool loadEditorTranslation(QTranslator& translator, const QString& languageCode);

/// The language codes loadEditorTranslation() knows.
[[nodiscard]] QStringList editorTranslationLanguages();

} // namespace neko::editor
