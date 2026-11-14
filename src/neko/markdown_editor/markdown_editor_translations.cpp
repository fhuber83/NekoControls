#include "neko/markdown_editor/markdown_editor_translations.hpp"

#include <QTranslator>

// The .qm files (neko_markdown_editor_translations.qrc). Resources in a STATIC library
// register only when their initializer is referenced, and the macro must sit at global
// scope -- inside a namespace it declares the initializer there and fails to link.
static void ensureMarkdownEditorTranslationsInitialized() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(neko_markdown_editor_translations);
        return true;
    }();
    (void)initialized;
}

namespace neko::markdown_editor {

QStringList markdownEditorTranslationLanguages() {
    return {QStringLiteral("de"), QStringLiteral("bar"), QStringLiteral("it")};
}

bool loadMarkdownEditorTranslation(QTranslator& translator, const QString& languageCode) {
    ensureMarkdownEditorTranslationsInitialized();
    if (!markdownEditorTranslationLanguages().contains(languageCode)) {
        return false;
    }
    return translator.load(QStringLiteral(":/neko/i18n/neko_markdown_editor_%1.qm").arg(languageCode));
}

} // namespace neko::markdown_editor
