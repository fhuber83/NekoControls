#pragma once

#include <QString>
#include <QStringList>

class QTranslator;

namespace neko::markdown_editor {

/// This library's own translations (MarkdownDocumentEditor), compiled into it from
/// translations/neko_markdown_editor_<code>.qm. The editor library it builds on has its
/// own: an application installs both (neko::editor::loadEditorTranslation()).
///
/// Loads the translation for `languageCode` ("de", "it", "bar") into `translator` and
/// returns true, or returns false and leaves `translator` empty for a language the
/// library has none of. Install it with QCoreApplication::installTranslator.
///
/// UI thread only.
[[nodiscard]] bool loadMarkdownEditorTranslation(QTranslator& translator, const QString& languageCode);

/// The language codes loadMarkdownEditorTranslation() knows.
[[nodiscard]] QStringList markdownEditorTranslationLanguages();

} // namespace neko::markdown_editor
