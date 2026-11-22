#include "neko/editor/editor_find_bar.hpp"

#include "neko/editor/code_editor.hpp"
#include "neko/editor/editor_icons.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <ranges>

namespace neko::editor {

namespace {

QToolButton* makeButton(const QString& text, const QString& toolTip, QWidget* parent, bool checkable = false) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(toolTip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    return b;
}

/// The same with the application's icon called `icon` (editor/editor_icons) and no
/// text; `caption` instead where the application has no such icon.
QToolButton* makeIconButton(
    const char* icon, const QString& caption, const QString& toolTip, QWidget* parent, bool checkable = false) {
    const QIcon image = editorIcon(QString::fromLatin1(icon));
    QToolButton* b = makeButton(image.isNull() ? caption : QString(), toolTip, parent, checkable);
    b->setIcon(image);
    return b;
}

} // namespace

EditorFindBar::EditorFindBar(QPlainTextEdit* editor, Highlighter highlighter, QWidget* parent)
    : QWidget(parent),
      editor_(editor),
      highlighter_(std::move(highlighter)) {
    setObjectName(QStringLiteral("EditorFindBar"));
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(6, 3, 6, 3);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(3);

    findEdit_ = new QLineEdit(this);
    findEdit_->setObjectName(QStringLiteral("FindField"));
    findEdit_->setPlaceholderText(tr("Find"));
    findEdit_->setClearButtonEnabled(true);
    findEdit_->installEventFilter(this);

    auto* prevBtn =
        makeIconButton("find-previous", QStringLiteral("▲"), tr("Previous match (Shift+F3, Shift+Enter)"), this);
    prevBtn->setObjectName(QStringLiteral("FindPrevious"));
    auto* nextBtn = makeIconButton("find-next", QStringLiteral("▼"), tr("Next match (F3, Enter)"), this);
    nextBtn->setObjectName(QStringLiteral("FindNext"));
    caseBtn_ = makeIconButton("match-case", QStringLiteral("Aa"), tr("Match case"), this, true);
    wordBtn_ = makeIconButton("whole-word", QStringLiteral("ab|"), tr("Whole word"), this, true);
    regexBtn_ = makeIconButton(
        "regex",
        QStringLiteral(".*"),
        tr("Regular expression. In the replacement, $1 or \\1 inserts a group, \\n a line break."),
        this,
        true);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("FindStatus"));
    status_->setMinimumWidth(status_->fontMetrics().horizontalAdvance(QStringLiteral("10000+ matches")));
    auto* closeBtn = makeIconButton("close", QStringLiteral("✕"), tr("Close (Esc)"), this);
    closeBtn->setObjectName(QStringLiteral("FindClose"));

    auto* findRow = new QHBoxLayout();
    findRow->setSpacing(2);
    findRow->addWidget(findEdit_, 1);
    for (QToolButton* w : {prevBtn, nextBtn, caseBtn_, wordBtn_, regexBtn_}) {
        findRow->addWidget(w);
    }
    findRow->addWidget(status_);
    findRow->addWidget(closeBtn);
    grid->addLayout(findRow, 0, 0);

    replaceRow_ = new QWidget(this);
    auto* replaceLayout = new QHBoxLayout(replaceRow_);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replaceLayout->setSpacing(2);
    replaceEdit_ = new QLineEdit(replaceRow_);
    replaceEdit_->setObjectName(QStringLiteral("ReplaceField"));
    replaceEdit_->setPlaceholderText(tr("Replace"));
    replaceEdit_->installEventFilter(this);
    replaceBtn_ = makeButton(tr("Replace"), tr("Replace this match, go to the next"), replaceRow_);
    replaceAllBtn_ = makeButton(tr("Replace All"), tr("Replace every match (one Ctrl+Z undoes it)"), replaceRow_);
    replaceLayout->addWidget(replaceEdit_, 1);
    replaceLayout->addWidget(replaceBtn_);
    replaceLayout->addWidget(replaceAllBtn_);
    grid->addWidget(replaceRow_, 1, 0);

    researchTimer_ = new QTimer(this);
    researchTimer_->setSingleShot(true);
    researchTimer_->setInterval(150);
    connect(researchTimer_, &QTimer::timeout, this, [this] { research(false); });

    connect(findEdit_, &QLineEdit::textChanged, this, [this] { research(true); });
    for (QToolButton* b : {caseBtn_, wordBtn_, regexBtn_}) {
        connect(b, &QToolButton::toggled, this, [this] { research(true); });
    }
    connect(prevBtn, &QToolButton::clicked, this, [this] { findNext(false); });
    connect(nextBtn, &QToolButton::clicked, this, [this] { findNext(true); });
    connect(closeBtn, &QToolButton::clicked, this, &EditorFindBar::closeBar);
    connect(replaceBtn_, &QToolButton::clicked, this, &EditorFindBar::replaceCurrent);
    connect(replaceAllBtn_, &QToolButton::clicked, this, [this] { (void)replaceAll(); });
    connectDocument();
    if (auto* code = qobject_cast<CodeEditor*>(editor_)) {
        connect(code, &CodeEditor::documentReplaced, this, &EditorFindBar::editorDocumentChanged);
    }

    setVisible(false);
}

void EditorFindBar::connectDocument() {
    disconnect(documentConnection_);
    // Edits while the bar is open: the matches follow, the selection stays.
    documentConnection_ = connect(editor_->document(), &QTextDocument::contentsChanged, this, [this] {
        if (isVisible()) {
            researchTimer_->start();
        }
    });
}

void EditorFindBar::editorDocumentChanged() {
    connectDocument();
    researchTimer_->stop();
    matches_.clear();
    note_.clear();
    anchor_ = 0;
    if (isVisible()) {
        research(false);
    } else if (highlighter_) {
        highlighter_({});
    }
}

void EditorFindBar::installShortcuts(QWidget* scope) {
    const auto add = [scope](const QKeySequence& keys, auto slot) {
        auto* sc = new QShortcut(keys, scope);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        QObject::connect(sc, &QShortcut::activated, scope, slot);
    };
    add(QKeySequence::Find, [this] { openFind(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_H), [this] { openReplace(); });
    add(QKeySequence(Qt::Key_F3), [this] { isVisible() ? void(findNext(true)) : openFind(); });
    add(QKeySequence(Qt::SHIFT | Qt::Key_F3), [this] { isVisible() ? void(findNext(false)) : openFind(); });
}

TextSearchOptions EditorFindBar::options() const {
    return {
        .caseSensitive = caseBtn_->isChecked(), .wholeWord = wordBtn_->isChecked(), .regex = regexBtn_->isChecked()};
}

void EditorFindBar::setOptions(const TextSearchOptions& options) {
    caseBtn_->setChecked(options.caseSensitive);
    wordBtn_->setChecked(options.wholeWord);
    regexBtn_->setChecked(options.regex);
}

void EditorFindBar::openFind() {
    open(false);
}

void EditorFindBar::openReplace() {
    open(true);
}

void EditorFindBar::open(bool replace) {
    replaceMode_ = replace && !editor_->isReadOnly();
    replaceRow_->setVisible(replaceMode_);
    const QTextCursor cursor = editor_->textCursor();
    anchor_ = cursor.selectionStart();
    const QString selected = cursor.selectedText();
    // QTextCursor marks line breaks in a selection with U+2029.
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator)) {
        findEdit_->blockSignals(true);
        findEdit_->setText(regexBtn_->isChecked() ? QRegularExpression::escape(selected) : selected);
        findEdit_->blockSignals(false);
    }
    setVisible(true);
    research(false);
    QLineEdit* focusField = replaceMode_ && !findEdit_->text().isEmpty() ? replaceEdit_ : findEdit_;
    focusField->setFocus();
    focusField->selectAll();
}

void EditorFindBar::closeBar() {
    setVisible(false);
    researchTimer_->stop();
    matches_.clear();
    if (highlighter_) {
        highlighter_({});
    }
    editor_->setFocus();
}

void EditorFindBar::research(bool selectFromAnchor) {
    note_.clear();
    const QString text = editor_->toPlainText();
    const TextSearchResult r = findAll(text, findEdit_->text(), options());
    matches_ = r.matches;
    truncated_ = r.truncated;
    error_ = r.valid ? QString() : r.error;

    QList<QTextEdit::ExtraSelection> highlights;
    const QColor color(255, 196, 0, 110); // readable on light and dark bases
    highlights.reserve(static_cast<qsizetype>(std::min<std::size_t>(matches_.size(), 2000)));
    for (const TextMatch& m : matches_) {
        if (highlights.size() >= 2000) {
            break; // painting more costs more than it tells
        }
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(color);
        sel.cursor = QTextCursor(editor_->document());
        sel.cursor.setPosition(static_cast<int>(m.start));
        sel.cursor.setPosition(static_cast<int>(m.start + m.length), QTextCursor::KeepAnchor);
        highlights.append(sel);
    }
    if (highlighter_) {
        highlighter_(highlights);
    }

    if (selectFromAnchor) {
        const auto it = std::ranges::find_if(matches_, [this](const TextMatch& m) { return m.start >= anchor_; });
        if (it != matches_.end()) {
            selectMatch(*it);
        } else if (!matches_.empty()) {
            selectMatch(matches_.front());
        } else {
            QTextCursor c = editor_->textCursor();
            c.setPosition(static_cast<int>(std::min<qsizetype>(anchor_, text.size())));
            editor_->setTextCursor(c);
        }
    }
    updateStatus();
}

int EditorFindBar::selectedMatchIndex() const {
    const QTextCursor c = editor_->textCursor();
    if (!c.hasSelection()) {
        return -1;
    }
    const TextMatch sel{.start = c.selectionStart(), .length = c.selectionEnd() - c.selectionStart()};
    const auto it = std::ranges::find(matches_, sel);
    return it == matches_.end() ? -1 : static_cast<int>(it - matches_.begin());
}

void EditorFindBar::selectMatch(const TextMatch& match) {
    QTextCursor c(editor_->document());
    c.setPosition(static_cast<int>(match.start));
    c.setPosition(static_cast<int>(match.start + match.length), QTextCursor::KeepAnchor);
    editor_->setTextCursor(c);
    editor_->ensureCursorVisible();
}

bool EditorFindBar::findNext(bool forward) {
    if (researchTimer_->isActive()) {
        researchTimer_->stop();
        research(false);
    }
    if (matches_.empty()) {
        updateStatus();
        return false;
    }
    const QTextCursor c = editor_->textCursor();
    const TextMatch* target = nullptr;
    if (forward) {
        const qsizetype from = c.selectionEnd();
        const auto it = std::ranges::find_if(matches_, [from](const TextMatch& m) { return m.start >= from; });
        target = it != matches_.end() ? &*it : &matches_.front();
    } else {
        const qsizetype before = c.selectionStart();
        const auto it = std::ranges::find_if(
            matches_ | std::views::reverse, [before](const TextMatch& m) { return m.start < before; });
        target = it != (matches_ | std::views::reverse).end() ? &*it : &matches_.back();
    }
    selectMatch(*target);
    note_.clear();
    updateStatus();
    return true;
}

void EditorFindBar::replaceCurrent() {
    if (editor_->isReadOnly()) {
        return;
    }
    if (researchTimer_->isActive()) {
        researchTimer_->stop();
        research(false);
    }
    const int index = selectedMatchIndex();
    if (index < 0) {
        (void)findNext(true);
        return;
    }
    const TextMatch match = matches_[static_cast<std::size_t>(index)];
    const QString replacement =
        replacementFor(editor_->toPlainText(), match, findEdit_->text(), replaceEdit_->text(), options());
    QTextCursor c = editor_->textCursor();
    c.insertText(replacement); // one undo step
    editor_->setTextCursor(c); // collapsed after the replacement: findNext continues from there
    researchTimer_->stop();
    research(false);
    (void)findNext(true);
}

int EditorFindBar::replaceAll() {
    if (editor_->isReadOnly()) {
        return 0;
    }
    researchTimer_->stop();
    research(false);
    if (matches_.empty()) {
        return 0;
    }
    const QString text = editor_->toPlainText();
    std::vector<QString> replacements;
    replacements.reserve(matches_.size());
    for (const TextMatch& m : matches_) {
        replacements.push_back(replacementFor(text, m, findEdit_->text(), replaceEdit_->text(), options()));
    }
    // Back to front, so earlier positions stay valid; one edit block = one undo.
    QTextCursor c(editor_->document());
    c.beginEditBlock();
    for (std::size_t i = matches_.size(); i-- > 0;) {
        c.setPosition(static_cast<int>(matches_[i].start));
        c.setPosition(static_cast<int>(matches_[i].start + matches_[i].length), QTextCursor::KeepAnchor);
        c.insertText(replacements[i]);
    }
    c.endEditBlock();
    const int replaced = static_cast<int>(replacements.size());
    research(false);
    note_ = QStringLiteral("Replaced %1").arg(replaced);
    updateStatus();
    return replaced;
}

QString EditorFindBar::statusText() const {
    return status_->text();
}

void EditorFindBar::updateStatus() {
    const bool bad = !error_.isEmpty();
    status_->setStyleSheet(bad ? QStringLiteral("color: #dc2626;") : QString());
    status_->setToolTip(bad ? error_ : QString());
    replaceBtn_->setEnabled(!bad && !matches_.empty());
    replaceAllBtn_->setEnabled(!bad && !matches_.empty());
    if (bad) {
        status_->setText(QStringLiteral("Invalid regex"));
    } else if (!note_.isEmpty()) {
        status_->setText(note_);
    } else if (findEdit_->text().isEmpty()) {
        status_->clear();
    } else if (matches_.empty()) {
        status_->setText(QStringLiteral("No results"));
    } else if (const int i = selectedMatchIndex(); i >= 0) {
        status_->setText(QStringLiteral("%1 of %2%3")
                             .arg(i + 1)
                             .arg(matches_.size())
                             .arg(truncated_ ? QStringLiteral("+") : QString()));
    } else {
        status_->setText(
            QStringLiteral("%1%2 matches").arg(matches_.size()).arg(truncated_ ? QStringLiteral("+") : QString()));
    }
}

bool EditorFindBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == findEdit_ || watched == replaceEdit_) {
        if (event->type() == QEvent::ShortcutOverride) {
            // Esc closes the bar here, not the window's Cancel shortcut.
            if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
                event->accept();
                return true;
            }
        } else if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                closeBar();
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                if (watched == replaceEdit_) {
                    replaceCurrent();
                } else {
                    (void)findNext(!(key->modifiers() & Qt::ShiftModifier));
                }
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace neko::editor
