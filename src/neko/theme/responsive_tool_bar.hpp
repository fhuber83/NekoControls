#pragma once

#include <QIcon>
#include <QString>
#include <QToolBar>
#include <vector>

class QAction;
class QEvent;
class QResizeEvent;
class QShowEvent;
class QToolButton;

namespace neko::theme {

/// Tool bar for dockable panels that keeps every button reachable while the
/// dock is narrowed.
///
/// Each registered button shows an icon (neko::theme::themedIcon) beside its caption. A
/// plain QToolBar reacts to a shrinking dock by pushing whole buttons into its
/// overflow extension, which makes them look as if they had vanished. This bar
/// instead drops the captions first -- from right to left, one button at a time
/// -- by switching the button to icon-only, leaving the icon and the tool tip
/// visible, and restores them as soon as the dock is wide enough again. The
/// overflow extension stays as the last resort for a bar that cannot even fit
/// icon-only.
///
/// Only the button's style changes, never the action's text, so a registered
/// action may also sit in a menu. Call sites may re-label a button at run time
/// with QAction::setText() / setIcon(); the bar re-measures.
///
/// Only entries registered through addLabeledAction() or
/// registerLabeledButton() take part. Separators, spacers and status labels are
/// left untouched and merely counted as fixed cost. A hidden action costs
/// nothing: its caption is not budgeted, and showing or hiding it re-measures
/// the bar.
///
/// Like every widget, this class is UI-thread only.
class ResponsiveToolBar final : public QToolBar {
    Q_OBJECT

public:
    explicit ResponsiveToolBar(QWidget* parent = nullptr);

    /// Adds an action with `icon` and the caption `caption`, which may be dropped
    /// when space gets tight. The caption is also the tool tip unless one is set.
    QAction* addLabeledAction(const QIcon& icon, const QString& caption);

    /// addLabeledAction() plus a `triggered()` connection, mirroring
    /// QToolBar::addAction(icon, text, receiver, slot).
    template <typename Receiver, typename Slot>
    QAction* addLabeledAction(const QIcon& icon, const QString& caption, Receiver receiver, Slot slot) {
        QAction* action = addLabeledAction(icon, caption);
        connect(action, &QAction::triggered, receiver, slot);
        return action;
    }

    /// Adds an action that exists already -- one a widget of a library owns, say -- with
    /// its own icon, text and tool tip, and registers its caption like the others. The
    /// action stays its owner's.
    QAction* addLabeledAction(QAction* action);

    /// Registers a button added through addWidget() -- a menu button, say -- so
    /// that its caption is dropped together with the action captions. Sets its
    /// icon and text; the button keeps its position in the bar.
    void registerLabeledButton(QToolButton* button, const QIcon& icon, const QString& caption);

    /// Number of registered entries, and how many of them currently show their
    /// caption rather than the icon alone. Exposed for tests.
    [[nodiscard]] int labeledEntryCount() const noexcept { return static_cast<int>(entries_.size()); }
    [[nodiscard]] int shownCaptionCount() const noexcept;

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    struct Entry {
        QAction* action = nullptr;     ///< null when the entry is a plain button
        QToolButton* button = nullptr; ///< null when the entry is a plain action
        QString text;                  ///< the action's text when last measured
        qint64 iconKey = 0;            ///< the action's icon (cacheKey) when last measured
        int captionWidth = 0;          ///< pixels the caption costs this button
        bool captionShown = true;
        bool visible = true; ///< the action's visibility when last measured
    };

    void registerEntry(Entry entry);
    void entryChanged(std::size_t index);
    void applyEntry(Entry& entry, bool showCaption) const;
    [[nodiscard]] QToolButton* entryButton(const Entry& entry) const;
    void measure();
    void updateCaptions();

    std::vector<Entry> entries_;
    int iconOnlyWidth_ = -1; ///< bar width with every caption dropped; < 0 until measured
    bool applying_ = false;  ///< guards against reacting to our own text changes
};

} // namespace neko::theme
