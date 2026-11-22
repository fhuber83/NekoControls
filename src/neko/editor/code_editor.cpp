#include "neko/editor/code_editor.hpp"

#include "neko/editor/editor_defaults.hpp"
#include "neko/editor/editor_icons.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextDocumentLayout>
#include <QSignalBlocker>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include <algorithm>
#include <cmath>

namespace neko::editor {

namespace {

class LineNumberGutter final : public QWidget {
public:
    explicit LineNumberGutter(CodeEditor* editor)
        : QWidget(editor),
          editor_(editor) {}

    [[nodiscard]] QSize sizeHint() const override { return QSize{editor_->lineNumberAreaWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent* event) override { editor_->lineNumberAreaPaintEvent(event); }

private:
    CodeEditor* editor_;
};

bool isModifierKey(int key) {
    return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta ||
           key == Qt::Key_AltGr;
}

/// Marks the format range of an inline suggestion among a layout's formats.
constexpr int kSuggestionFormatProperty = QTextFormat::UserProperty + 0x4e4b;

bool isSuggestionFormat(const QTextLayout::FormatRange& range) {
    return range.format.hasProperty(kSuggestionFormatProperty);
}

bool isWordChar(QChar c) {
    return c.isLetterOrNumber() || c == u'_';
}

} // namespace

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent) {
    lineNumberArea_ = new LineNumberGutter(this);
    const EditorDefaults& defaults = EditorDefaults::instance();
    setFont(defaults.font());
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * kTabWidth);
    setLineWrapMode(defaults.lineWrap() ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    lineWrapAct_ = new QAction(editorIcon(QStringLiteral("line-wrap")), tr("Wrap Lines"), this);
    lineWrapAct_->setObjectName(QStringLiteral("EditorLineWrapAction"));
    lineWrapAct_->setCheckable(true);
    lineWrapAct_->setChecked(isLineWrapEnabled());
    lineWrapAct_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Z));
    lineWrapAct_->setShortcutContext(Qt::WidgetShortcut);
    lineWrapAct_->setToolTip(tr("Wrap long lines at the edge of the editor (Alt+Z)"));
    connect(lineWrapAct_, &QAction::toggled, this, &CodeEditor::setLineWrapEnabled);
    addAction(lineWrapAct_);

    connect(&defaults, &EditorDefaults::fontChanged, this, [this](const QFont& f) { setFont(f); });
    connect(&defaults, &EditorDefaults::lineWrapChanged, this, &CodeEditor::setLineWrapEnabled);

    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        highlightCurrentLine();
        if (block_ && !applyingBlockEdit_ && !blockDragging_) {
            clearBlockSelection(); // moved by something else: Find, a click, the API
        }
        if (suggestion_ && !editingSuggestion_ &&
            (textCursor().position() != suggestion_->anchor.position() || textCursor().hasSelection())) {
            dismissSuggestion();
        }
    });
    connectDocument();
    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

void CodeEditor::connectDocument() {
    disconnect(documentConnection_);
    documentConnection_ =
        connect(document(), &QTextDocument::contentsChange, this, [this](int position, int removed, int added) {
            if (block_ && !applyingBlockEdit_) {
                clearBlockSelection(); // edited elsewhere, undo included: the box no longer fits
            }
            if (suggestion_ && !editingSuggestion_) {
                suggestionContentsChanged(position, removed, added);
            }
        });
}

bool CodeEditor::replaceDocument(QTextDocument* document) {
    if (!document || document == this->document() || document->thread() != thread() ||
        !qobject_cast<QPlainTextDocumentLayout*>(document->documentLayout())) {
        return false;
    }
    // What points into the old text goes with it.
    clearInlineSuggestion();
    clearBlockSelection();
    searchHighlights_.clear();
    disconnect(documentConnection_);
    // The extra selections (current line, search highlights) hold cursors into the old
    // document, which setDocument() may delete -- and then emits cursorPositionChanged,
    // whose highlightCurrentLine() would make QWidgetTextControl measure those stale
    // cursors. Drop them first.
    setExtraSelections({});

    // QPlainTextEdit deletes a replaced document only if it is its internal control's (the
    // editor's first one); one parented to the editor itself would be left behind. The
    // editor's children go too: that is how an application hands documents over.
    QTextDocument* const old = this->document();
    const bool oldIsOurs = old->parent() == this;
    // The gutter first, sized for the new line count: a wider gutter narrows the viewport,
    // and a narrower viewport lays the document out again -- the old, small one now rather
    // than the new one after the swap (~200 ms for a million lines).
    setViewportMargins(lineNumberAreaWidthFor(document->blockCount()), 0, 0, 0);
    setDocument(document);
    if (oldIsOurs) {
        delete old; // with its children: a highlighter, say
    }
    // QPlainTextEdit::setDocument carries the wrap mode over, not the font or the tab stops:
    // those are the document's own options. Each change lays the whole document out again
    // (~90 ms for 10 MiB), so only what differs -- a document built with the editor's
    // options needs neither.
    if (document->defaultFont() != font()) {
        document->setDefaultFont(font());
    }
    const qreal tabStop = fontMetrics().horizontalAdvance(QLatin1Char(' ')) * kTabWidth;
    if (document->defaultTextOption().tabStopDistance() != tabStop) {
        setTabStopDistance(tabStop);
    }

    connectDocument();
    updateLineNumberAreaWidth();
    highlightCurrentLine();
    emit documentReplaced();
    return true;
}

void CodeEditor::setTextUndoable(const QString& text) {
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void CodeEditor::setSearchHighlights(const QList<QTextEdit::ExtraSelection>& highlights) {
    searchHighlights_ = highlights;
    highlightCurrentLine();
}

void CodeEditor::setLineWrapEnabled(bool enabled) {
    const QPlainTextEdit::LineWrapMode target = enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap;
    const bool changed = (lineWrapMode() != target);
    if (changed) {
        setLineWrapMode(target);
    }
    if (lineWrapAct_ && lineWrapAct_->isChecked() != enabled) {
        const QSignalBlocker blocker(lineWrapAct_);
        lineWrapAct_->setChecked(enabled);
    }
    if (changed) {
        emit lineWrapChanged(enabled);
    }
}

void CodeEditor::setOverwriteMode(bool overwrite) {
    if (overwriteMode() == overwrite) {
        return;
    }
    QPlainTextEdit::setOverwriteMode(overwrite);
    if (block_) {
        setCursorWidth(0);
    }
    viewport()->update();
    emit overwriteModeChanged(overwrite);
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(palette().color(QPalette::AlternateBase));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }
    extraSelections.append(searchHighlights_);
    setExtraSelections(extraSelections);
}

// ---- Gutter -----------------------------------------------------------------------------

int CodeEditor::lineNumberAreaWidth() const {
    return lineNumberAreaWidthFor(blockCount());
}

int CodeEditor::lineNumberAreaWidthFor(int lines) const {
    int digits = 1;
    int maxBlocks = qMax(1, lines);
    while (maxBlocks >= 10) {
        maxBlocks /= 10;
        ++digits;
    }
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void CodeEditor::updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy != 0) {
        lineNumberArea_->scroll(0, dy);
    } else {
        lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth();
    }
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), palette().color(QPalette::Window));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    painter.setPen(palette().color(QPalette::PlaceholderText));
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.drawText(
                0,
                top,
                lineNumberArea_->width() - 6,
                fontMetrics().height(),
                Qt::AlignRight,
                QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect{cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()});
}

void CodeEditor::changeEvent(QEvent* event) {
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * kTabWidth);
        updateLineNumberAreaWidth();
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        highlightCurrentLine();
        lineNumberArea_->update();
        if (suggestion_) {
            showSuggestion(); // in the new placeholder colour
        }
    }
}

// ---- Column selection: geometry ---------------------------------------------------------

QString CodeEditor::lineText(int line) const {
    return document()->findBlockByNumber(line).text();
}

QStringList CodeEditor::lineTexts(int first, int count) const {
    QStringList out;
    QTextBlock b = document()->findBlockByNumber(first);
    for (int i = 0; i < count && b.isValid(); ++i, b = b.next()) {
        out << b.text();
    }
    return out;
}

std::pair<int, int> CodeEditor::lineColumnAt(const QPoint& viewportPos) const {
    const QTextCursor c = cursorForPosition(viewportPos);
    const QString text = c.block().text();
    const int index = c.positionInBlock();
    int column = block::columnOf(text, index, kTabWidth);
    if (index == text.size()) {
        const int endX = cursorRect(c).left();
        const qreal space = QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '));
        if (viewportPos.x() > endX && space > 0) {
            column += static_cast<int>(std::lround((viewportPos.x() - endX) / space));
        }
    }
    return {c.blockNumber(), column};
}

int CodeEditor::xOfColumn(int line, int column) const {
    const QTextBlock b = document()->findBlockByNumber(line);
    const QString text = b.text();
    const int width = block::lineWidth(text, kTabWidth);
    QTextCursor c(b);
    if (column <= width) {
        c.setPosition(b.position() + static_cast<int>(block::indexAtColumn(text, column, kTabWidth)));
        return cursorRect(c).left();
    }
    c.setPosition(b.position() + static_cast<int>(text.size()));
    const qreal space = QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '));
    return cursorRect(c).left() + static_cast<int>(std::lround((column - width) * space));
}

// ---- Column selection: state ------------------------------------------------------------

void CodeEditor::setBlockSelection(const block::BlockSelection& selection) {
    if (suggestion_) {
        dismissSuggestion();
    }
    const int last = std::max(0, blockCount() - 1);
    block::BlockSelection s = selection;
    s.anchorLine = std::clamp(s.anchorLine, 0, last);
    s.cursorLine = std::clamp(s.cursorLine, 0, last);
    s.anchorColumn = std::max(0, s.anchorColumn);
    s.cursorColumn = std::max(0, s.cursorColumn);
    block_ = s;
    setCursorWidth(0); // the box draws its own carets
    syncCursorToBlock();
    viewport()->update();
}

void CodeEditor::clearBlockSelection() {
    if (!block_) {
        return;
    }
    block_.reset();
    blockDragging_ = false;
    setCursorWidth(1);
    viewport()->update();
}

void CodeEditor::syncCursorToBlock() {
    if (!block_) {
        return;
    }
    const QTextBlock b = document()->findBlockByNumber(block_->cursorLine);
    QTextCursor c(b);
    c.setPosition(b.position() + static_cast<int>(block::indexAtColumn(b.text(), block_->cursorColumn, kTabWidth)));
    applyingBlockEdit_ = true;
    setTextCursor(c);
    ensureCursorVisible();
    applyingBlockEdit_ = false;
}

QString CodeEditor::blockSelectedText() const {
    if (!block_) {
        return {};
    }
    return block::copyText(lineTexts(block_->top(), block_->lineCount()), block_->left(), block_->right(), kTabWidth);
}

// ---- Column selection: editing ----------------------------------------------------------

void CodeEditor::applyRowEdits(int firstLine, const std::vector<block::RowEdit>& edits) {
    QTextCursor c(document());
    applyingBlockEdit_ = true;
    c.beginEditBlock();
    // Bottom-up: a row's positions do not move when the rows below it change.
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        const QTextBlock b = document()->findBlockByNumber(firstLine + it->row);
        if (!b.isValid()) {
            continue;
        }
        c.setPosition(b.position() + static_cast<int>(it->start));
        c.setPosition(b.position() + static_cast<int>(it->end), QTextCursor::KeepAnchor);
        c.insertText(it->text);
    }
    c.endEditBlock();
    applyingBlockEdit_ = false;
}

void CodeEditor::insertIntoBlock(const QString& text) {
    if (!block_ || isReadOnly()) {
        return;
    }
    QString normalized = text;
    normalized.remove(u'\r');
    const QStringList insert = normalized.split(u'\n');
    const block::BlockSelection box = *block_;
    const int rows = insert.size() == 1 ? box.lineCount() : std::max<int>(box.lineCount(), int(insert.size()));

    // One undo step for all of it, the lines appended for a tall paste included.
    QTextCursor group(document());
    applyingBlockEdit_ = true;
    group.beginEditBlock();
    if (const int missing = box.top() + rows - blockCount(); missing > 0) {
        group.movePosition(QTextCursor::End);
        group.insertText(QString(missing, u'\n'));
    }
    const QStringList before = lineTexts(box.top(), rows);
    const int rightCol = (overwriteMode() && box.isZeroWidth() && insert.size() == 1)
                             ? box.left() + block::lineWidth(insert.front(), kTabWidth)
                             : box.right();
    const auto edits = block::replaceEdits(before, box.left(), rightCol, insert, kTabWidth);
    applyRowEdits(box.top(), edits);
    applyingBlockEdit_ = true;
    group.endEditBlock();
    applyingBlockEdit_ = false;

    if (insert.size() == 1) {
        // A column of carets right after what was typed, on the same lines.
        int column = box.left();
        if (!edits.empty()) {
            const auto& e = edits.front();
            const QString after = lineText(box.top() + e.row);
            column = block::columnOf(after, e.start + e.text.size(), kTabWidth);
        }
        setBlockSelection(
            {.anchorLine = box.anchorLine,
             .anchorColumn = column,
             .cursorLine = box.cursorLine,
             .cursorColumn = column});
    } else {
        // A pasted block: the caret goes after its last row, as in Visual Studio.
        const int lastRow = static_cast<int>(insert.size()) - 1;
        clearBlockSelection();
        const auto lastEdit = std::ranges::find_if(edits, [lastRow](const auto& e) { return e.row == lastRow; });
        const QTextBlock b = document()->findBlockByNumber(box.top() + lastRow);
        QTextCursor c(b);
        if (lastEdit != edits.end()) {
            c.setPosition(b.position() + static_cast<int>(lastEdit->start + lastEdit->text.size()));
        }
        setTextCursor(c);
    }
}

void CodeEditor::deleteBlock(block::DeleteKey key) {
    if (!block_ || isReadOnly()) {
        return;
    }
    const block::BlockSelection box = *block_;
    const QStringList rows = lineTexts(box.top(), box.lineCount());
    const int column = block::columnAfterDelete(rows, box.left(), box.right(), key, kTabWidth);
    applyRowEdits(box.top(), block::deleteEdits(rows, box.left(), box.right(), key, kTabWidth));
    setBlockSelection(
        {.anchorLine = box.anchorLine, .anchorColumn = column, .cursorLine = box.cursorLine, .cursorColumn = column});
}

void CodeEditor::copyBlock() const {
    if (!block_) {
        return;
    }
    auto* mime = new QMimeData();
    mime->setText(blockSelectedText());
    mime->setData(QString::fromLatin1(kBlockMimeType), QByteArrayLiteral("1"));
    QApplication::clipboard()->setMimeData(mime);
}

void CodeEditor::cutBlock() {
    if (!block_ || block_->isZeroWidth()) {
        return;
    }
    copyBlock();
    deleteBlock(block::DeleteKey::Delete);
}

bool CodeEditor::canInsertFromMimeData(const QMimeData* source) const {
    return source->hasText() || QPlainTextEdit::canInsertFromMimeData(source);
}

void CodeEditor::insertFromMimeData(const QMimeData* source) {
    if (block_) {
        insertIntoBlock(source->text());
        return;
    }
    // A block copied here pastes as a block at the caret, one row per line.
    if (source->hasFormat(QString::fromLatin1(kBlockMimeType)) && !textCursor().hasSelection() &&
        source->text().contains(u'\n') && !isReadOnly()) {
        const QTextCursor c = textCursor();
        const int column = block::columnOf(c.block().text(), c.positionInBlock(), kTabWidth);
        block_ = block::BlockSelection{
            .anchorLine = c.blockNumber(),
            .anchorColumn = column,
            .cursorLine = c.blockNumber(),
            .cursorColumn = column};
        insertIntoBlock(source->text());
        return;
    }
    QPlainTextEdit::insertFromMimeData(source);
}

// ---- Column selection: input ------------------------------------------------------------

bool CodeEditor::handleBlockKey(QKeyEvent* event) {
    const Qt::KeyboardModifiers mods = event->modifiers();
    const int key = event->key();
    const bool altShift = (mods & Qt::AltModifier) && (mods & Qt::ShiftModifier) && !(mods & Qt::ControlModifier);
    const bool arrow = key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right;
    if (altShift && arrow) {
        block::BlockSelection box;
        if (block_) {
            box = *block_;
        } else {
            const QTextCursor c = textCursor();
            const int column = block::columnOf(c.block().text(), c.positionInBlock(), kTabWidth);
            box = {
                .anchorLine = c.blockNumber(),
                .anchorColumn = column,
                .cursorLine = c.blockNumber(),
                .cursorColumn = column};
        }
        box.cursorLine += key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : 0;
        box.cursorColumn += key == Qt::Key_Left ? -1 : key == Qt::Key_Right ? 1 : 0;
        setBlockSelection(box);
        return true;
    }
    if (!block_) {
        return false;
    }
    if (isModifierKey(key)) {
        return false; // pressing Alt or Shift on its own keeps the box
    }
    if (key == Qt::Key_Insert && (mods & ~Qt::KeypadModifier) == Qt::NoModifier) {
        return false; // toggling insert/overwrite mode keeps the box
    }
    if (key == Qt::Key_Escape) {
        clearBlockSelection();
        return true;
    }
    if (event->matches(QKeySequence::Copy)) {
        copyBlock();
        return true;
    }
    if (event->matches(QKeySequence::Cut)) {
        cutBlock();
        return true;
    }
    if (event->matches(QKeySequence::Paste)) {
        if (const QMimeData* mime = QApplication::clipboard()->mimeData()) {
            insertIntoBlock(mime->text());
        }
        return true;
    }
    if (!(mods & Qt::ControlModifier) && (key == Qt::Key_Backspace || key == Qt::Key_Delete)) {
        deleteBlock(key == Qt::Key_Backspace ? block::DeleteKey::Backspace : block::DeleteKey::Delete);
        return true;
    }
    const QString typed = event->text();
    if (!typed.isEmpty() && !(mods & Qt::ControlModifier) && (typed.front().isPrint() || typed.front() == u'\t')) {
        insertIntoBlock(typed);
        return true;
    }
    clearBlockSelection(); // navigation, Enter, Select All, Undo, ...: back to normal editing
    return false;
}

bool CodeEditor::event(QEvent* event) {
    // Esc ends the box or dismisses the suggestion here, ahead of the window's Cancel shortcut.
    if (event->type() == QEvent::ShortcutOverride && (block_ || suggestion_) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        event->accept();
        return true;
    }
    return QPlainTextEdit::event(event);
}

void CodeEditor::keyPressEvent(QKeyEvent* event) {
    if (suggestion_) {
        const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
        const bool handled =
            (event->key() == Qt::Key_Tab && mods == Qt::NoModifier && acceptInlineSuggestion()) ||
            (event->key() == Qt::Key_Right && mods == Qt::ControlModifier && acceptInlineSuggestionWord());
        if (handled) {
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape && mods == Qt::NoModifier) {
            dismissSuggestion();
            event->accept();
            return;
        }
    }
    if (handleBlockKey(event)) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Insert && (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier &&
        !isReadOnly()) {
        setOverwriteMode(!overwriteMode());
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void CodeEditor::mousePressEvent(QMouseEvent* event) {
    if (suggestion_) {
        dismissSuggestion(); // before QPlainTextEdit commits it as an input method's pre-edit
    }
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier)) {
        const auto [line, column] = lineColumnAt(event->position().toPoint());
        blockDragging_ = true;
        setFocus(Qt::MouseFocusReason);
        setBlockSelection({.anchorLine = line, .anchorColumn = column, .cursorLine = line, .cursorColumn = column});
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        clearBlockSelection();
    }
    QPlainTextEdit::mousePressEvent(event);
}

void CodeEditor::mouseMoveEvent(QMouseEvent* event) {
    if (blockDragging_ && block_) {
        const auto [line, column] = lineColumnAt(event->position().toPoint());
        block::BlockSelection box = *block_;
        box.cursorLine = line;
        box.cursorColumn = column;
        setBlockSelection(box);
        event->accept();
        return;
    }
    QPlainTextEdit::mouseMoveEvent(event);
}

void CodeEditor::mouseReleaseEvent(QMouseEvent* event) {
    if (blockDragging_) {
        blockDragging_ = false;
        event->accept();
        return;
    }
    QPlainTextEdit::mouseReleaseEvent(event);
}

void CodeEditor::mouseDoubleClickEvent(QMouseEvent* event) {
    clearBlockSelection();
    QPlainTextEdit::mouseDoubleClickEvent(event);
}

QMenu* CodeEditor::createEditorContextMenu() {
    if (!block_) {
        QMenu* menu = createStandardContextMenu();
        if (lineWrapAct_) {
            menu->addSeparator();
            menu->addAction(lineWrapAct_);
        }
        return menu;
    }
    auto* menu = new QMenu(this);
    const bool editable = !isReadOnly();
    const bool wide = !block_->isZeroWidth();
    menu->addAction(tr("Cu&t"), QKeySequence::Cut, this, &CodeEditor::cutBlock)->setEnabled(editable && wide);
    menu->addAction(tr("&Copy"), QKeySequence::Copy, this, &CodeEditor::copyBlock)->setEnabled(wide);
    const QMimeData* clip = QApplication::clipboard()->mimeData();
    menu->addAction(
            tr("&Paste"),
            QKeySequence::Paste,
            this,
            [this] {
                if (const QMimeData* mime = QApplication::clipboard()->mimeData()) {
                    insertIntoBlock(mime->text());
                }
            })
        ->setEnabled(editable && clip != nullptr && clip->hasText());
    menu->addAction(tr("Delete"), this, [this] { deleteBlock(block::DeleteKey::Delete); })
        ->setEnabled(editable && wide);
    return menu;
}

void CodeEditor::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = createEditorContextMenu();
    menu->exec(event->globalPos());
    delete menu;
}

// ---- Inline suggestion ------------------------------------------------------------------

void CodeEditor::setInlineSuggestion(const QString& text) {
    clearInlineSuggestion();
    QString normalized = text;
    normalized.remove(u'\r');
    if (normalized.isEmpty() || isReadOnly() || block_ || textCursor().hasSelection()) {
        return;
    }
    suggestion_ = Suggestion{.anchor = textCursor(), .text = normalized, .consumed = 0};
    showSuggestion();
}

void CodeEditor::clearInlineSuggestion() {
    if (!suggestion_) {
        return;
    }
    hideSuggestion();
    suggestion_.reset();
}

QString CodeEditor::inlineSuggestion() const {
    return suggestion_ ? suggestion_->text : QString();
}

bool CodeEditor::acceptInlineSuggestion() {
    return suggestion_ && acceptSuggestionPart(suggestion_->text.size());
}

bool CodeEditor::acceptInlineSuggestionWord() {
    return suggestion_ && acceptSuggestionPart(nextSuggestionWordLength(suggestion_->text));
}

qsizetype CodeEditor::nextSuggestionWordLength(const QString& text) {
    const auto isBlank = [](QChar c) {
        return c == u' ' || c == u'\t';
    };
    qsizetype i = 0;
    if (text.startsWith(u'\n')) {
        for (i = 1; i < text.size() && isBlank(text[i]); ++i) {
        }
        return i;
    }
    while (i < text.size() && isBlank(text[i])) {
        ++i;
    }
    if (i < text.size() && isWordChar(text[i])) {
        while (i < text.size() && isWordChar(text[i])) {
            ++i;
        }
    } else {
        while (i < text.size() && !isWordChar(text[i]) && !isBlank(text[i]) && text[i] != u'\n') {
            ++i;
        }
    }
    return i;
}

QTextCharFormat CodeEditor::suggestionFormat() const {
    QTextCharFormat format;
    format.setForeground(palette().color(QPalette::PlaceholderText));
    format.setProperty(kSuggestionFormatProperty, true);
    return format;
}

void CodeEditor::showSuggestion() {
    const QTextBlock block = suggestion_->anchor.block();
    QTextLayout* layout = block.layout();
    QString shown = suggestion_->text;
    shown.replace(u'\n', QChar::LineSeparator);
    const int at = suggestion_->anchor.positionInBlock();
    layout->setPreeditArea(at, shown);
    QList<QTextLayout::FormatRange> formats = layout->formats();
    formats.removeIf(isSuggestionFormat);
    formats.append({at, static_cast<int>(shown.size()), suggestionFormat()});
    layout->setFormats(formats);
    document()->markContentsDirty(block.position(), block.length());
    viewport()->update();
}

void CodeEditor::hideSuggestion(const QTextBlock& other) {
    const auto hideIn = [this](const QTextBlock& block) {
        if (!block.isValid()) {
            return;
        }
        QTextLayout* layout = block.layout();
        QList<QTextLayout::FormatRange> formats = layout->formats();
        const bool hadFormat = formats.removeIf(isSuggestionFormat) > 0;
        const bool hadPreedit = layout->preeditAreaPosition() >= 0;
        if (!hadFormat && !hadPreedit) {
            return;
        }
        layout->setPreeditArea(-1, QString());
        layout->setFormats(formats);
        document()->markContentsDirty(block.position(), block.length());
    };
    hideIn(suggestion_->anchor.block());
    if (other.isValid() && other != suggestion_->anchor.block()) {
        hideIn(other);
    }
    viewport()->update();
}

void CodeEditor::ensureSuggestionShown() {
    const QTextBlock block = suggestion_->anchor.block();
    const QTextLayout* layout = block.layout();
    const bool hasPreedit = layout->preeditAreaPosition() == suggestion_->anchor.positionInBlock();
    if (hasPreedit && std::ranges::any_of(layout->formats(), isSuggestionFormat)) {
        return;
    }
    if (!hasPreedit) {
        // Something else ended the pre-edit (QPlainTextEdit commits an input method's on
        // some cursor changes) and with it dropped the block's formats: highlight it again.
        for (QSyntaxHighlighter* highlighter : document()->findChildren<QSyntaxHighlighter*>()) {
            highlighter->rehighlightBlock(block);
        }
    }
    showSuggestion();
}

void CodeEditor::dismissSuggestion() {
    clearInlineSuggestion();
    emit inlineSuggestionDismissed();
}

bool CodeEditor::acceptSuggestionPart(qsizetype length) {
    if (!suggestion_ || length <= 0 || isReadOnly()) {
        return false;
    }
    const QString part = suggestion_->text.left(length);
    hideSuggestion();
    editingSuggestion_ = true;
    QTextCursor cursor = textCursor();
    cursor.setPosition(suggestion_->anchor.position());
    cursor.beginEditBlock(); // its own undo step, not merged with typing before it
    cursor.insertText(part); // moves the anchor past it
    cursor.endEditBlock();
    setTextCursor(cursor);
    ensureCursorVisible();
    editingSuggestion_ = false;

    suggestion_->text.remove(0, part.size());
    suggestion_->consumed += part.size();
    const qsizetype consumed = suggestion_->consumed;
    const bool complete = suggestion_->text.isEmpty();
    if (complete) {
        suggestion_.reset();
    } else {
        showSuggestion();
    }
    emit inlineSuggestionAccepted(consumed, complete);
    return true;
}

void CodeEditor::suggestionContentsChanged(int position, int removed, int added) {
    hideSuggestion(document()->findBlock(position));
    // Typed right before the anchor (which moved past it): eat into the suggestion if it
    // starts with what was typed.
    if (removed == 0 && added > 0 && position + added == suggestion_->anchor.position()) {
        QTextCursor typed(document());
        typed.setPosition(position);
        typed.setPosition(position + added, QTextCursor::KeepAnchor);
        const QString text = typed.selectedText().replace(QChar::ParagraphSeparator, u'\n');
        if (suggestion_->text.startsWith(text)) {
            suggestion_->text.remove(0, text.size());
            suggestion_->consumed += text.size();
            if (suggestion_->text.isEmpty()) {
                suggestion_.reset(); // typed to its end
            } else {
                showSuggestion();
            }
            return;
        }
    }
    suggestion_.reset();
    emit inlineSuggestionDismissed();
}

void CodeEditor::focusOutEvent(QFocusEvent* event) {
    if (suggestion_) {
        dismissSuggestion();
    }
    QPlainTextEdit::focusOutEvent(event);
}

void CodeEditor::inputMethodEvent(QInputMethodEvent* event) {
    // The input method's own pre-edit text takes the place the suggestion is drawn in.
    if (suggestion_) {
        dismissSuggestion();
    }
    QPlainTextEdit::inputMethodEvent(event);
}

// ---- Column selection: painting ---------------------------------------------------------

void CodeEditor::paintEvent(QPaintEvent* event) {
    if (suggestion_) {
        ensureSuggestionShown();
    }
    QPlainTextEdit::paintEvent(event);
    if (!block_) {
        return;
    }
    QPainter painter(viewport());
    QColor fill = palette().color(QPalette::Highlight);
    fill.setAlpha(110);
    const QColor caret = palette().color(QPalette::Text);
    const QRect visible = event->rect();
    for (int line = block_->top(); line <= block_->bottom(); ++line) {
        const QTextBlock b = document()->findBlockByNumber(line);
        if (!b.isValid() || !b.isVisible()) {
            continue;
        }
        QTextCursor at(b);
        at.setPosition(b.position() + static_cast<int>(block::indexAtColumn(b.text(), block_->left(), kTabWidth)));
        const QRect r = cursorRect(at);
        if (r.bottom() < visible.top() || r.top() > visible.bottom()) {
            continue;
        }
        const int xLeft = xOfColumn(line, block_->left());
        if (block_->isZeroWidth()) {
            painter.fillRect(QRect(xLeft, r.top(), 2, r.height()), caret);
        } else {
            const int xRight = xOfColumn(line, block_->right());
            painter.fillRect(QRect(xLeft, r.top(), std::max(1, xRight - xLeft), r.height()), fill);
        }
    }
}

} // namespace neko::editor
