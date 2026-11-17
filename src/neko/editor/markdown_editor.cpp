#include "neko/editor/markdown_editor.hpp"

#include "neko/editor/emoji_shortcodes.hpp"

#include <QAbstractItemView>
#include <QAbstractListModel>
#include <QCompleter>
#include <QKeyEvent>
#include <QListView>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTextBlock>
#include <QTextCursor>

#include <algorithm>
#include <utility>
#include <vector>

namespace neko::editor {

namespace {

constexpr int kRoleShortcodeName = Qt::UserRole + 1;
constexpr int kRoleLinkTarget = Qt::UserRole + 2;
/// Rows measured for the popup's width; the rest are no wider in practice.
constexpr int kMeasuredRows = 200;

QString displayText(const EmojiShortcode& entry) {
    return QStringLiteral("%1  :%2:")
        .arg(
            QString::fromUtf8(entry.emoji.data(), static_cast<qsizetype>(entry.emoji.size())),
            QLatin1StringView(entry.name.data(), static_cast<qsizetype>(entry.name.size())));
}

bool isModifierKey(int key) noexcept {
    return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta ||
           key == Qt::Key_AltGr;
}

} // namespace

/// The open list's rows: pointers into the static emojiShortcodes() table.
class EmojiShortcodeModel final : public QAbstractListModel {
public:
    using QAbstractListModel::QAbstractListModel;

    void setEntries(std::vector<const EmojiShortcode*> entries) {
        beginResetModel();
        entries_ = std::move(entries);
        endResetModel();
    }
    [[nodiscard]] const std::vector<const EmojiShortcode*>& entries() const noexcept { return entries_; }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(entries_.size());
    }

    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
            return {};
        }
        const auto& entry = *entries_[static_cast<std::size_t>(index.row())];
        switch (role) {
        case Qt::DisplayRole:
            return displayText(entry);
        case kRoleShortcodeName:
            return QString::fromLatin1(entry.name.data(), static_cast<qsizetype>(entry.name.size()));
        default:
            return {};
        }
    }

private:
    std::vector<const EmojiShortcode*> entries_;
};

MarkdownEditor::MarkdownEditor(QWidget* parent)
    : CodeEditor(parent) {
    model_ = new EmojiShortcodeModel(this);
    completer_ = new QCompleter(model_, this);
    completer_->setWidget(this);
    // The rows are already the matches (matchEmojiShortcodes).
    completer_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    completer_->setCompletionRole(kRoleShortcodeName);
    completer_->setMaxVisibleItems(12);
    completer_->setWrapAround(false);
    if (auto* list = qobject_cast<QListView*>(completer_->popup())) {
        list->setUniformItemSizes(true); // ~1900 rows
    }
    completer_->popup()->setObjectName(QStringLiteral("EmojiPopup"));
    completer_->popup()->setFont(font());
    connect(completer_, qOverload<const QString&>(&QCompleter::activated), this, &MarkdownEditor::insertEmojiShortcode);

    linkModel_ = new QStandardItemModel(this);
    linkCompleter_ = new QCompleter(linkModel_, this);
    linkCompleter_->setWidget(this);
    linkCompleter_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    linkCompleter_->setCompletionRole(kRoleLinkTarget);
    linkCompleter_->setMaxVisibleItems(12);
    linkCompleter_->setWrapAround(false);
    linkCompleter_->popup()->setObjectName(QStringLiteral("LinkTargetPopup"));
    linkCompleter_->popup()->setFont(font());
    connect(linkCompleter_, qOverload<const QString&>(&QCompleter::activated), this, &MarkdownEditor::insertLinkTarget);
}

void MarkdownEditor::setLinkTargetProvider(MarkdownLinkTargetProvider provider) {
    linkProvider_ = std::move(provider);
    hideLinkPopup();
}

bool MarkdownEditor::isLinkPopupVisible() const {
    return linkCompleter_->popup()->isVisible();
}

QStringList MarkdownEditor::linkPopupTargets() const {
    QStringList targets;
    if (!isLinkPopupVisible()) {
        return targets;
    }
    for (int row = 0; row < linkModel_->rowCount(); ++row) {
        targets.append(linkModel_->item(row)->data(kRoleLinkTarget).toString());
    }
    return targets;
}

void MarkdownEditor::hideLinkPopup() {
    if (isLinkPopupVisible()) {
        linkCompleter_->popup()->hide();
    }
    linkCandidates_.clear();
    linkCandidatesKey_.clear();
}

bool MarkdownEditor::updateLinkPopup(bool mayOpen) {
    const QTextCursor cursor = textCursor();
    const bool visible = isLinkPopupVisible();
    std::optional<MarkdownLinkTargetQuery> query;
    if (linkProvider_ && (visible || mayOpen) && !cursor.hasSelection() && !hasBlockSelection() &&
        markdownLinkTargetQueryAt(cursor.block().text(), cursor.positionInBlock())) {
        query = markdownLinkTargetQueryAt(toPlainText(), cursor.position()); // fences need the whole note
    }
    if (!query) {
        hideLinkPopup();
        return false;
    }
    const QString key = query->fragment ? QStringLiteral("#") + query->path : QStringLiteral("files");
    if (!visible || key != linkCandidatesKey_) {
        linkCandidates_ = linkProvider_(*query);
        linkCandidatesKey_ = key;
    }
    const auto matches = matchMarkdownLinkTargets(linkCandidates_, query->prefix);
    if (matches.empty()) {
        hideLinkPopup();
        return false;
    }
    linkModel_->clear();
    QAbstractItemView* popup = linkCompleter_->popup();
    const QFontMetrics metrics(popup->font());
    int textWidth = 0;
    for (const MarkdownLinkTarget& t : matches) {
        auto* item = new QStandardItem(t.label.isEmpty() ? t.target : t.label);
        item->setData(t.target, kRoleLinkTarget);
        item->setToolTip(t.target);
        item->setEditable(false);
        linkModel_->appendRow(item);
        if (linkModel_->rowCount() <= kMeasuredRows) {
            textWidth = std::max(textWidth, metrics.horizontalAdvance(item->text()));
        }
    }
    linkCompleter_->setCompletionPrefix(QString());
    const int scrollBar = popup->verticalScrollBar() ? popup->verticalScrollBar()->sizeHint().width() : 16;
    QRect rect = cursorRect();
    rect.setWidth(std::clamp(textWidth + scrollBar + 28, 220, 640));
    linkCompleter_->complete(rect);
    popup->setCurrentIndex(linkCompleter_->completionModel()->index(0, 0));
    popup->scrollToTop();
    return true;
}

void MarkdownEditor::insertLinkTarget(const QString& target) {
    const auto query = markdownLinkTargetQueryAt(toPlainText(), textCursor().position());
    hideLinkPopup();
    if (!query || target.isEmpty()) {
        return;
    }
    QTextCursor cursor = textCursor();
    cursor.setPosition(static_cast<int>(query->start));
    cursor.setPosition(static_cast<int>(query->end), QTextCursor::KeepAnchor);
    cursor.insertText(target);
    setTextCursor(cursor);
}

bool MarkdownEditor::isEmojiPopupVisible() const {
    return completer_->popup()->isVisible();
}

QStringList MarkdownEditor::emojiPopupNames() const {
    QStringList names;
    if (!isEmojiPopupVisible()) {
        return names;
    }
    names.reserve(static_cast<qsizetype>(model_->entries().size()));
    for (const auto* entry : model_->entries()) {
        names.append(QString::fromLatin1(entry->name.data(), static_cast<qsizetype>(entry->name.size())));
    }
    return names;
}

void MarkdownEditor::hideEmojiPopup() {
    if (isEmojiPopupVisible()) {
        completer_->popup()->hide();
    }
}

void MarkdownEditor::updateEmojiPopup() {
    const QTextCursor cursor = textCursor();
    // The line alone settles most keystrokes; only a hit there needs the whole note
    // (fenced code blocks).
    const bool candidate = !cursor.hasSelection() && !hasBlockSelection() &&
                           emojiShortcodeQueryAt(cursor.block().text(), cursor.positionInBlock());
    const auto query = candidate ? emojiShortcodeQueryAt(toPlainText(), cursor.position()) : std::nullopt;
    auto matches = query ? matchEmojiShortcodes(query->prefix) : std::vector<const EmojiShortcode*>{};
    if (matches.empty()) {
        hideEmojiPopup();
        return;
    }
    model_->setEntries(std::move(matches));
    completer_->setCompletionPrefix(QString());

    QAbstractItemView* popup = completer_->popup();
    const QFontMetrics metrics(popup->font());
    int textWidth = 0;
    const int measured = std::min<int>(model_->rowCount(), kMeasuredRows);
    for (int row = 0; row < measured; ++row) {
        textWidth = std::max(textWidth, metrics.horizontalAdvance(displayText(*model_->entries()[row])));
    }
    const int scrollBar = popup->verticalScrollBar() ? popup->verticalScrollBar()->sizeHint().width() : 16;
    QRect rect = cursorRect();
    rect.setWidth(std::clamp(textWidth + scrollBar + 28, 220, 480));
    completer_->complete(rect);

    // complete() makes the first row current; after a bare ':' nothing is chosen yet.
    if (query->prefix.isEmpty()) {
        popup->selectionModel()->clear();
        popup->setCurrentIndex(QModelIndex());
    } else {
        popup->setCurrentIndex(completer_->completionModel()->index(0, 0));
    }
    popup->scrollToTop();
}

void MarkdownEditor::insertEmojiShortcode(const QString& name) {
    const auto query = emojiShortcodeQueryAt(toPlainText(), textCursor().position());
    hideEmojiPopup();
    if (!query || name.isEmpty()) {
        return;
    }
    QTextCursor cursor = textCursor();
    cursor.setPosition(static_cast<int>(query->start));
    cursor.setPosition(static_cast<int>(query->end), QTextCursor::KeepAnchor);
    cursor.insertText(QLatin1Char(':') + name + QLatin1Char(':'));
    setTextCursor(cursor);
}

void MarkdownEditor::keyPressEvent(QKeyEvent* event) {
    // While a list is open, QCompleter hands us the keys first; Up / Down never get here.
    if (isLinkPopupVisible()) {
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Tab: {
            const QModelIndex current = linkCompleter_->popup()->currentIndex();
            if (current.isValid()) {
                insertLinkTarget(current.data(kRoleLinkTarget).toString());
                event->accept();
                return;
            }
            hideLinkPopup();
            break;
        }
        case Qt::Key_Escape:
            hideLinkPopup();
            event->accept();
            return;
        default:
            break;
        }
    }
    if (isEmojiPopupVisible()) {
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Tab: {
            const QModelIndex current = completer_->popup()->currentIndex();
            if (current.isValid()) {
                insertEmojiShortcode(current.data(kRoleShortcodeName).toString());
                event->accept();
                return;
            }
            hideEmojiPopup(); // nothing chosen: the key does what it always does
            break;
        }
        case Qt::Key_Escape:
            hideEmojiPopup();
            event->accept();
            return;
        default:
            break;
        }
    }

    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::ControlModifier) {
        if (!updateLinkPopup(true)) {
            updateEmojiPopup();
        }
        event->accept();
        return;
    }
    if (isModifierKey(event->key())) {
        CodeEditor::keyPressEvent(event);
        return;
    }

    const bool wasVisible = isEmojiPopupVisible();
    const bool linkWasVisible = isLinkPopupVisible();
    CodeEditor::keyPressEvent(event);

    if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
        if (linkWasVisible) {
            (void)updateLinkPopup(false);
        }
        if (wasVisible) {
            updateEmojiPopup(); // narrows back out; never opens on its own
        }
        return;
    }
    const QString typed = event->text();
    if (typed.isEmpty() || !typed.front().isPrint()) {
        hideLinkPopup();
        hideEmojiPopup(); // a cursor move, a shortcut
        return;
    }
    if (updateLinkPopup(typed == QStringLiteral("#"))) {
        hideEmojiPopup();
        return;
    }
    updateEmojiPopup();
}

void MarkdownEditor::changeEvent(QEvent* event) {
    CodeEditor::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        completer_->popup()->setFont(font());
        linkCompleter_->popup()->setFont(font());
    }
}

} // namespace neko::editor
