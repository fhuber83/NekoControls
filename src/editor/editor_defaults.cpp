#include "editor/editor_defaults.hpp"

#include <QFontDatabase>

namespace nekogrid::editor {

EditorDefaults& EditorDefaults::instance() {
    static EditorDefaults defaults;
    return defaults;
}

QFont EditorDefaults::fallbackFont() {
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(11);
    return mono;
}

QFont EditorDefaults::font() const {
    return font_ ? *font_ : fallbackFont();
}

void EditorDefaults::setFont(const QFont& font) {
    font_ = font;
    emit fontChanged(font);
}

void EditorDefaults::resetFont() {
    font_.reset();
    emit fontChanged(font());
}

void EditorDefaults::setLineWrap(bool enabled) {
    lineWrap_ = enabled;
    emit lineWrapChanged(enabled);
}

} // namespace nekogrid::editor
