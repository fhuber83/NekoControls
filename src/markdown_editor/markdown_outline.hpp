#pragma once

#include "markdown_editor/markdown_structure.hpp"

#include <QWidget>

#include <vector>

class QTreeWidget;

namespace nekogrid::markdown_editor {

/// The heading outline of one Markdown document, as a tree: a heading's deeper headings
/// are its children. MarkdownDocumentEditor shows one beside the source and preview.
///
/// A pure view: setHeadings() replaces everything, and the owner scans the document
/// again after edits (extractMarkdownHeadings()). Branches the user collapsed stay
/// collapsed and the scroll position is kept, as far as heading texts still match.
///
/// UI thread only.
class MarkdownOutline final : public QWidget {
    Q_OBJECT

public:
    explicit MarkdownOutline(QWidget* parent = nullptr);

    void setHeadings(const std::vector<MarkdownHeading>& headings);

signals:
    /// An entry was clicked or activated: move the editor cursor to `offset` and
    /// scroll the preview to `line` (0-based in the full note).
    void jumpRequested(qsizetype offset, int line);

private:
    QTreeWidget* tree_ = nullptr;
};

} // namespace nekogrid::markdown_editor
