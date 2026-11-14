#pragma once

#include "editor/markdown_table.hpp"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace nekogrid::editor {

/// The table wizard of the Markdown editor: one list row per column (title, alignment,
/// width in characters -- 0 shows as "Auto" and is the default), Add / Remove buttons
/// (top left, with a green plus and a red cross), an optional number of empty body rows to
/// pre-create (a check box, off by default, enabling a spin box), and a live preview of
/// the Markdown that will be inserted. Starts with one column; the user adds the rest,
/// and the last one cannot be removed. Enter in a title edits the next column's title.
/// The list's columns can be resized. Columns are reordered by dragging a title within
/// the list.
///
/// The result is buildMarkdownTable() of spec(); the dialog does no text work of
/// its own. UI thread only.
class MarkdownTableDialog final : public QDialog {
    Q_OBJECT

public:
    static constexpr int kMaxRows = 200;
    static constexpr int kMaxColumns = 50;
    static constexpr int kMaxWidth = 500;

    explicit MarkdownTableDialog(QWidget* parent = nullptr);

    /// What the user has entered so far.
    [[nodiscard]] MarkdownTableSpec spec() const;
    /// The Markdown for spec().
    [[nodiscard]] QString markdown() const;

    [[nodiscard]] int columnCount() const;
    /// Appends a column (what "Add Column" does) and returns its index. `width` 0 is
    /// automatic; it is clamped to 0..kMaxWidth.
    int addColumn(const QString& title = {}, TableAlignment alignment = TableAlignment::Default, int width = 0);
    /// Moves column `from` so that it ends up at index `to`, title and alignment together,
    /// and selects it (what dropping a dragged row does). Title, alignment and width move
    /// together. Out of range: nothing.
    void moveColumn(int from, int to);
    /// Whether empty rows are created at all; rowsSpin() says how many.
    [[nodiscard]] QCheckBox* rowsCheck() const noexcept { return rowsCheck_; }
    [[nodiscard]] QSpinBox* rowsSpin() const noexcept { return rowsSpin_; }
    [[nodiscard]] QTableWidget* columnsTable() const noexcept { return table_; }
    [[nodiscard]] QPushButton* okButton() const noexcept { return okButton_; }
    [[nodiscard]] QPlainTextEdit* previewEdit() const noexcept { return preview_; }

private:
    void removeSelectedColumn();
    void updateState();
    QComboBox* alignmentCombo(int row) const;
    QSpinBox* widthSpin(int row) const;

    QTableWidget* table_ = nullptr;
    QCheckBox* rowsCheck_ = nullptr;
    QSpinBox* rowsSpin_ = nullptr;
    QPlainTextEdit* preview_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* okButton_ = nullptr;
};

} // namespace nekogrid::editor
