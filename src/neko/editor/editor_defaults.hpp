#pragma once

#include <QFont>
#include <QObject>

#include <optional>

namespace neko::editor {

/// What every CodeEditor starts with and follows while it lives: the font, and whether
/// long lines wrap at the editor's edge. One instance per process, held in memory only
/// -- this library stores nothing and reads no settings.
///
/// An application that lets its user choose these loads them into here at start, before
/// it builds its first editor, and sets them here when the user changes them; every
/// open editor follows at once. An application without such a setting leaves this
/// alone and gets fallbackFont() and wrapping on.
///
/// The setters always notify, also when the value is the one already held: an editor
/// that was given its own font (QWidget::setFont) or had wrapping toggled for itself
/// (CodeEditor::setLineWrapEnabled, Alt+Z) goes back to the shared value.
///
/// UI thread only. font() and fallbackFont() need the QGuiApplication.
class EditorDefaults final : public QObject {
    Q_OBJECT

public:
    [[nodiscard]] static EditorDefaults& instance();

    /// The font new editors start with: the one set, else fallbackFont().
    [[nodiscard]] QFont font() const;
    void setFont(const QFont& font);
    /// Forgets the font set, so font() is fallbackFont() again, and notifies.
    void resetFont();
    /// The system's fixed-width font at 11 pt.
    [[nodiscard]] static QFont fallbackFont();

    /// Whether new editors wrap long lines. True until set otherwise.
    [[nodiscard]] bool lineWrap() const noexcept { return lineWrap_; }
    void setLineWrap(bool enabled);

signals:
    void fontChanged(const QFont& font);
    void lineWrapChanged(bool enabled);

private:
    EditorDefaults() = default;

    std::optional<QFont> font_;
    bool lineWrap_ = true;
};

} // namespace neko::editor
