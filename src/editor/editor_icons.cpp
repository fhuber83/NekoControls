#include "editor/editor_icons.hpp"

#include <utility>

namespace nekogrid::editor {

namespace {

EditorIconProvider& provider() {
    static EditorIconProvider instance;
    return instance;
}

} // namespace

void setEditorIconProvider(EditorIconProvider newProvider) {
    provider() = std::move(newProvider);
}

QIcon editorIcon(const QString& name) {
    return provider() ? provider()(name) : QIcon();
}

QStringList editorIconNames() {
    return {
        QStringLiteral("line-wrap"),
        QStringLiteral("find-previous"),
        QStringLiteral("find-next"),
        QStringLiteral("match-case"),
        QStringLiteral("whole-word"),
        QStringLiteral("regex"),
        QStringLiteral("close"),
    };
}

} // namespace nekogrid::editor
