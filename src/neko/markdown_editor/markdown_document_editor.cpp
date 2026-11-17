#include "neko/markdown_editor/markdown_document_editor.hpp"

#include "neko/editor/code_editor.hpp"
#include "neko/editor/editor_find_bar.hpp"
#include "neko/editor/editor_icons.hpp"
#include "neko/editor/markdown_highlighter.hpp"
#include "neko/editor/markdown_table_dialog.hpp"
#include "neko/markdown_editor/markdown_outline.hpp"
#include "neko/markdown_editor/markdown_structure.hpp"

#include <QAction>
#include <QActionGroup>
#include <QEvent>
#include <QKeySequence>
#include <QPalette>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <utility>

namespace neko::markdown_editor {

namespace {

bool paletteIsDark(const QWidget* widget) {
    return widget->palette().color(QPalette::Base).lightness() < 128;
}

} // namespace

markdown::MarkdownPreviewContent defaultMarkdownContent(const QString& text, const QString& baseDirectory) {
    const int bodyStartLine = frontMatterLineCount(text);
    qsizetype bodyStart = 0;
    for (int line = 0; line < bodyStartLine; ++line) {
        const qsizetype newline = text.indexOf(QLatin1Char('\n'), bodyStart);
        if (newline < 0) {
            bodyStart = text.size(); // the front matter's closing line is the last one
            break;
        }
        bodyStart = newline + 1;
    }
    return {
        .body = text.mid(bodyStart),
        .bodyStartLine = bodyStartLine,
        .baseDirectory = baseDirectory,
    };
}

QStringList markdownEditorIconNames() {
    return {
        QStringLiteral("note-source"),
        QStringLiteral("note-split"),
        QStringLiteral("preview"),
        QStringLiteral("insert-table"),
        QStringLiteral("line-wrap"),
        QStringLiteral("outline"),
    };
}

MarkdownDocumentEditor::MarkdownDocumentEditor(QWidget* parent)
    : MarkdownDocumentEditor(MarkdownToolBar::BuiltIn, parent) {}

MarkdownDocumentEditor::MarkdownDocumentEditor(MarkdownToolBar toolBar, QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    modeGroup_ = new QActionGroup(this);
    modeGroup_->setExclusive(true);
    sourceAct_ = new QAction(editor::editorIcon(QStringLiteral("note-source")), tr("Source"), this);
    sourceAct_->setToolTip(tr("Show the Markdown source only (Ctrl+Shift+M cycles the views)"));
    splitAct_ = new QAction(editor::editorIcon(QStringLiteral("note-split")), tr("Split"), this);
    splitAct_->setToolTip(tr("Show source and preview side by side (Ctrl+Shift+M cycles the views)"));
    previewAct_ = new QAction(editor::editorIcon(QStringLiteral("preview")), tr("Preview"), this);
    previewAct_->setToolTip(tr("Show the rendered document only (Ctrl+Shift+M cycles the views)"));
    for (QAction* act : {sourceAct_, splitAct_, previewAct_}) {
        act->setCheckable(true);
        modeGroup_->addAction(act);
    }
    connect(sourceAct_, &QAction::triggered, this, [this] { chooseViewMode(MarkdownViewMode::Source); });
    connect(splitAct_, &QAction::triggered, this, [this] { chooseViewMode(MarkdownViewMode::Split); });
    connect(previewAct_, &QAction::triggered, this, [this] { chooseViewMode(MarkdownViewMode::Preview); });

    tableAct_ = new QAction(editor::editorIcon(QStringLiteral("insert-table")), tr("Table..."), this);
    tableAct_->setToolTip(tr("Insert a table: columns, titles, alignment and empty rows"));
    connect(tableAct_, &QAction::triggered, this, &MarkdownDocumentEditor::insertTable);
    wrapAct_ = new QAction(editor::editorIcon(QStringLiteral("line-wrap")), tr("Wrap"), this);
    wrapAct_->setCheckable(true);
    wrapAct_->setToolTip(tr("Wrap long lines in the source editor (Alt+Z)"));
    outlineAct_ = new QAction(editor::editorIcon(QStringLiteral("outline")), tr("Outline"), this);
    outlineAct_->setCheckable(true);
    outlineAct_->setToolTip(tr("Show the document's outline (headings)"));

    // The shortcut is an action of the widget, not of a tool bar button: it must work
    // with MarkdownToolBar::None and in a bar that dropped the button.
    cycleAct_ = new QAction(this);
    cycleAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    cycleAct_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(cycleAct_, &QAction::triggered, this, [this] {
        chooseViewMode(static_cast<MarkdownViewMode>((static_cast<int>(mode_) + 1) % 3));
    });
    addAction(cycleAct_);

    if (toolBar == MarkdownToolBar::BuiltIn) {
        toolBar_ = new QToolBar(this);
        toolBar_->setMovable(false);
        toolBar_->setFloatable(false);
        toolBar_->setIconSize(QSize(16, 16));
        toolBar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toolBar_->addActions({sourceAct_, splitAct_, previewAct_});
        toolBar_->addSeparator();
        toolBar_->addActions({tableAct_, wrapAct_, outlineAct_});
        layout->addWidget(toolBar_);
    }

    auto* outlineSplit = new QSplitter(Qt::Horizontal, this);
    outlineSplit->setChildrenCollapsible(false);
    outline_ = new MarkdownOutline(outlineSplit);
    outline_->setVisible(false);
    splitter_ = new QSplitter(Qt::Horizontal, outlineSplit);
    splitter_->setChildrenCollapsible(false);
    editorPane_ = new QWidget(splitter_);
    auto* paneLayout = new QVBoxLayout(editorPane_);
    paneLayout->setContentsMargins(0, 0, 0, 0);
    paneLayout->setSpacing(0);
    editor_ = new editor::MarkdownEditor(editorPane_);
    wrapAct_->setChecked(editor_->isLineWrapEnabled());
    connect(wrapAct_, &QAction::toggled, editor_, &editor::CodeEditor::setLineWrapEnabled);
    connect(editor_, &editor::CodeEditor::lineWrapChanged, this, [this](bool enabled) {
        if (wrapAct_->isChecked() != enabled) {
            const QSignalBlocker blocker(wrapAct_);
            wrapAct_->setChecked(enabled);
        }
    });
    findBar_ = new editor::EditorFindBar(
        editor_, [e = editor_](const QList<QTextEdit::ExtraSelection>& h) { e->setSearchHighlights(h); }, editorPane_);
    paneLayout->addWidget(findBar_);
    paneLayout->addWidget(editor_, 1);
    findBar_->installShortcuts(editorPane_);
    preview_ = markdown::createMarkdownPreview(splitter_);
    splitter_->addWidget(editorPane_);
    splitter_->addWidget(preview_);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 1);
    outlineSplit->addWidget(outline_);
    outlineSplit->addWidget(splitter_);
    outlineSplit->setStretchFactor(0, 0);
    outlineSplit->setStretchFactor(1, 1);
    layout->addWidget(outlineSplit, 1);

    outlineTimer_ = new QTimer(this);
    outlineTimer_->setSingleShot(true);
    outlineTimer_->setInterval(kOutlineDelayMs);
    connect(outlineTimer_, &QTimer::timeout, this, &MarkdownDocumentEditor::refreshOutline);
    connect(outlineAct_, &QAction::toggled, this, [this](bool on) {
        outline_->setVisible(on);
        if (on) {
            refreshOutline();
        }
    });
    connect(outline_, &MarkdownOutline::jumpRequested, this, &MarkdownDocumentEditor::jumpTo);

    ownHighlighter_ = new editor::MarkdownHighlighter(editor_->document(), paletteIsDark(editor_));
    highlighter_ = ownHighlighter_;

    previewTimer_ = new QTimer(this);
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(kPreviewDelayMs);
    connect(previewTimer_, &QTimer::timeout, this, &MarkdownDocumentEditor::refreshPreview);
    connect(editor_, &QPlainTextEdit::textChanged, this, &MarkdownDocumentEditor::onTextChanged);
    connect(editor_->verticalScrollBar(), &QScrollBar::valueChanged, this, &MarkdownDocumentEditor::syncPreviewScroll);
    connect(
        preview_, &markdown::MarkdownPreview::customLinkActivated, this, &MarkdownDocumentEditor::customLinkActivated);
    connect(
        preview_,
        &markdown::MarkdownPreview::documentLinkActivated,
        this,
        &MarkdownDocumentEditor::documentLinkActivated);
    connect(
        preview_, &markdown::MarkdownPreview::localLinkActivated, this, &MarkdownDocumentEditor::localLinkActivated);

    // Nothing is rendered yet: an empty document has nothing to show, and whoever
    // builds the widget usually sets a provider, a view and a text next.
    applyViewMode();
}

void MarkdownDocumentEditor::setText(const QString& text) {
    editor_->setPlainText(text);
    editor_->document()->setModified(false);
    refreshPreview();
    if (outlineAct_->isChecked()) {
        refreshOutline();
    }
}

QString MarkdownDocumentEditor::text() const {
    return editor_->toPlainText();
}

bool MarkdownDocumentEditor::isModified() const {
    return editor_->document()->isModified();
}

void MarkdownDocumentEditor::setModified(bool modified) {
    editor_->document()->setModified(modified);
}

void MarkdownDocumentEditor::setBaseDirectory(const QString& directory) {
    if (baseDirectory_ == directory) {
        return;
    }
    baseDirectory_ = directory;
    invalidatePreview();
}

void MarkdownDocumentEditor::setViewMode(MarkdownViewMode mode) {
    const bool changed = mode_ != mode;
    mode_ = mode;
    applyViewMode();
    if (mode != MarkdownViewMode::Source && previewStale_) {
        refreshPreview();
    }
    if (mode == MarkdownViewMode::Preview) {
        preview_->setFocus();
    } else {
        editor_->setFocus();
    }
    if (changed) {
        emit viewModeChanged(mode);
    }
}

void MarkdownDocumentEditor::chooseViewMode(MarkdownViewMode mode) {
    setViewMode(mode);
    emit viewModeChosen(mode);
}

void MarkdownDocumentEditor::applyViewMode() {
    editorPane_->setVisible(mode_ != MarkdownViewMode::Preview);
    preview_->setVisible(mode_ != MarkdownViewMode::Source);
    sourceAct_->setChecked(mode_ == MarkdownViewMode::Source);
    splitAct_->setChecked(mode_ == MarkdownViewMode::Split);
    previewAct_->setChecked(mode_ == MarkdownViewMode::Preview);
}

void MarkdownDocumentEditor::setContentProvider(MarkdownContentProvider provider) {
    provider_ = std::move(provider);
    // A preview that never rendered has nothing to redo: the next view change or text
    // brings it up, with this provider.
    if (!previewStale_) {
        invalidatePreview();
    }
}

markdown::MarkdownPreviewContent MarkdownDocumentEditor::previewContent() const {
    const QString source = editor_->toPlainText();
    return provider_ ? provider_(source, baseDirectory_) : defaultMarkdownContent(source, baseDirectory_);
}

void MarkdownDocumentEditor::refreshPreview() {
    previewTimer_->stop();
    preview_->setContent(previewContent());
    previewStale_ = false;
    syncPreviewScroll();
}

void MarkdownDocumentEditor::invalidatePreview() {
    previewStale_ = true;
    if (mode_ != MarkdownViewMode::Source) {
        refreshPreview();
    }
}

void MarkdownDocumentEditor::insertTable() {
    editor::MarkdownTableDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    insertTableMarkdown(dialog.markdown());
}

void MarkdownDocumentEditor::insertTableMarkdown(const QString& table) {
    if (table.isEmpty()) {
        return;
    }
    if (mode_ == MarkdownViewMode::Preview) {
        setViewMode(MarkdownViewMode::Split); // the user wants to see where it landed
    }
    // A table needs a blank line before it (after text) and after it (before text), or
    // the lines around it become part of the table or of a paragraph.
    QTextCursor cursor = editor_->textCursor();
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    QString text = table;
    while (text.endsWith(QLatin1Char('\n'))) {
        text.chop(1); // the line break after the table comes from the text around it
    }
    if (!cursor.atBlockStart()) {
        text.prepend(QStringLiteral("\n\n"));
    } else if (cursor.block().previous().isValid() && !cursor.block().previous().text().trimmed().isEmpty()) {
        text.prepend(QLatin1Char('\n'));
    }
    if (!cursor.atBlockEnd()) {
        text.append(QStringLiteral("\n\n")); // the rest of the line becomes its own paragraph
    } else if (!cursor.block().next().isValid() || !cursor.block().next().text().trimmed().isEmpty()) {
        text.append(QLatin1Char('\n'));
    }
    cursor.insertText(text);
    cursor.endEditBlock();
    editor_->setTextCursor(cursor);
    editor_->setFocus();
}

void MarkdownDocumentEditor::setHighlighter(QSyntaxHighlighter* highlighter) {
    if (highlighter == highlighter_) {
        return;
    }
    delete highlighter_; // detaches itself from the document
    ownHighlighter_ = nullptr;
    highlighter_ = highlighter;
    if (highlighter_) {
        highlighter_->setParent(this);
        highlighter_->setDocument(editor_->document());
    }
}

void MarkdownDocumentEditor::onTextChanged() {
    previewStale_ = true;
    if (mode_ != MarkdownViewMode::Source) {
        previewTimer_->start();
    }
    if (outlineAct_->isChecked()) {
        outlineTimer_->start();
    }
}

void MarkdownDocumentEditor::refreshOutline() {
    outlineTimer_->stop();
    // From the line the preview's body starts on: what the provider treats as front
    // matter has no headings.
    outline_->setHeadings(extractMarkdownHeadings(editor_->toPlainText(), previewContent().bodyStartLine));
}

void MarkdownDocumentEditor::syncPreviewScroll() {
    if (mode_ != MarkdownViewMode::Split) {
        return;
    }
    preview_->scrollToSourceLine(editor_->cursorForPosition(QPoint(0, 0)).blockNumber());
}

bool MarkdownDocumentEditor::showHeading(const QString& anchor) {
    if (anchor.isEmpty()) {
        return false;
    }
    for (const MarkdownHeading& h : extractMarkdownHeadings(editor_->toPlainText(), previewContent().bodyStartLine)) {
        if (h.anchor == anchor) {
            jumpTo(h.offset, h.line);
            return true;
        }
    }
    return false;
}

void MarkdownDocumentEditor::jumpTo(qsizetype offset, int line) {
    if (mode_ != MarkdownViewMode::Preview) {
        QTextCursor cursor = editor_->textCursor();
        cursor.setPosition(
            static_cast<int>(qBound(qsizetype{0}, offset, qsizetype{editor_->document()->characterCount()} - 1)));
        editor_->setTextCursor(cursor);
        editor_->centerCursor();
        editor_->setFocus();
    }
    if (mode_ != MarkdownViewMode::Source) {
        preview_->scrollToSourceLine(line);
    }
}

void MarkdownDocumentEditor::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && ownHighlighter_) {
        ownHighlighter_->setDark(paletteIsDark(editor_));
    }
}

} // namespace neko::markdown_editor
