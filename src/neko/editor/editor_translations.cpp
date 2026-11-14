#include "neko/editor/editor_translations.hpp"

#include <QTranslator>

// The .qm files (neko_editor_translations.qrc). Resources in a STATIC library register only
// when their initializer is referenced, and the macro must sit at global scope --
// inside a namespace it declares the initializer there and fails to link.
static void ensureEditorTranslationsInitialized() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(neko_editor_translations);
        return true;
    }();
    (void)initialized;
}

namespace neko::editor {

QStringList editorTranslationLanguages() {
    return {QStringLiteral("de"), QStringLiteral("bar"), QStringLiteral("it")};
}

bool loadEditorTranslation(QTranslator& translator, const QString& languageCode) {
    ensureEditorTranslationsInitialized();
    if (!editorTranslationLanguages().contains(languageCode)) {
        return false;
    }
    return translator.load(QStringLiteral(":/neko/i18n/neko_editor_%1.qm").arg(languageCode));
}

} // namespace neko::editor
