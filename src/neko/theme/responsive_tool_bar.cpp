#include "neko/theme/responsive_tool_bar.hpp"

#include <QAction>
#include <QEvent>
#include <QLayout>
#include <QResizeEvent>
#include <QShowEvent>
#include <QToolButton>

#include <algorithm>

namespace neko::theme {

ResponsiveToolBar::ResponsiveToolBar(QWidget* parent)
    : QToolBar(parent) {
    setMovable(false);
    setFloatable(false);
    setIconSize(QSize(16, 16));
    // Each button's own style is switched between this and icon-only; the bar's
    // style is what buttons added later start from.
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // Preferred (not Maximum) so the bar fills the panel width: the available
    // width is what the caption budget is computed from.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QAction* ResponsiveToolBar::addLabeledAction(const QIcon& icon, const QString& caption) {
    QAction* action = addAction(icon, caption);
    registerEntry(Entry{.action = action, .button = nullptr, .text = {}});
    return action;
}

QAction* ResponsiveToolBar::addLabeledAction(QAction* action) {
    if (!action) {
        return nullptr;
    }
    addAction(action);
    registerEntry(Entry{.action = action, .button = nullptr, .text = {}});
    return action;
}

void ResponsiveToolBar::registerLabeledButton(QToolButton* button, const QIcon& icon, const QString& caption) {
    if (!button) {
        return;
    }
    button->setIcon(icon);
    button->setText(caption);
    button->setIconSize(iconSize());
    registerEntry(Entry{.action = nullptr, .button = button, .text = {}});
}

void ResponsiveToolBar::registerEntry(Entry entry) {
    // Icon-only buttons need a tool tip of their own; Qt's fallback is the text,
    // which an icon-only button no longer shows -- keep it as the tip.
    if (entry.button && entry.button->toolTip().isEmpty()) {
        entry.button->setToolTip(entry.button->text());
    }

    entries_.push_back(std::move(entry));

    if (QAction* action = entries_.back().action) {
        const std::size_t index = entries_.size() - 1;
        connect(action, &QAction::changed, this, [this, index] { entryChanged(index); });
    }

    iconOnlyWidth_ = -1;
    updateCaptions();
}

void ResponsiveToolBar::entryChanged(std::size_t index) {
    if (applying_ || index >= entries_.size()) {
        return;
    }
    const Entry& entry = entries_[index];
    if (entry.action->isVisible() == entry.visible && entry.action->text() == entry.text &&
        entry.action->icon().cacheKey() == entry.iconKey) {
        return; // enabled / checked / tool tip: no size change
    }
    iconOnlyWidth_ = -1;
    updateCaptions();
}

QToolButton* ResponsiveToolBar::entryButton(const Entry& entry) const {
    return entry.action ? qobject_cast<QToolButton*>(widgetForAction(entry.action)) : entry.button;
}

void ResponsiveToolBar::applyEntry(Entry& entry, bool showCaption) const {
    if (QToolButton* button = entryButton(entry)) {
        button->setToolButtonStyle(showCaption ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    }
    entry.captionShown = showCaption;
}

void ResponsiveToolBar::measure() {
    ensurePolished();
    for (Entry& entry : entries_) {
        QToolButton* button = entryButton(entry);
        entry.visible = entry.action == nullptr || entry.action->isVisible();
        if (entry.action) {
            entry.text = entry.action->text();
            entry.iconKey = entry.action->icon().cacheKey();
        }
        if (!button || !entry.visible) {
            entry.captionWidth = 0;
            continue;
        }
        button->ensurePolished();
        applyEntry(entry, true);
        const int withCaption = button->sizeHint().width();
        applyEntry(entry, false);
        entry.captionWidth = std::max(0, withCaption - button->sizeHint().width());
    }
    // Every caption is dropped now, so the bar's size hint is its narrowest
    // layout -- the fixed cost that the caption budget is measured against.
    if (QLayout* bar = layout()) {
        bar->invalidate();
    }
    iconOnlyWidth_ = sizeHint().width();
}

void ResponsiveToolBar::updateCaptions() {
    if (applying_ || entries_.empty()) {
        return;
    }
    // Re-label if our own relayout changed the available width under us; the
    // decision only depends on that width, so this settles immediately.
    for (int pass = 0; pass < 3; ++pass) {
        const int available = width();
        applying_ = true;
        if (iconOnlyWidth_ < 0) {
            measure();
        }
        int budget = available - iconOnlyWidth_;
        bool showCaption = true;
        for (Entry& entry : entries_) {
            showCaption = showCaption && entry.captionWidth <= budget;
            if (showCaption) {
                budget -= entry.captionWidth;
            }
            applyEntry(entry, showCaption);
        }
        applying_ = false;
        if (width() == available) {
            break;
        }
    }
}

int ResponsiveToolBar::shownCaptionCount() const noexcept {
    return static_cast<int>(
        std::count_if(entries_.begin(), entries_.end(), [](const Entry& entry) { return entry.captionShown; }));
}

void ResponsiveToolBar::changeEvent(QEvent* event) {
    QToolBar::changeEvent(event);
    switch (event->type()) {
    case QEvent::FontChange:
    case QEvent::ApplicationFontChange:
    case QEvent::StyleChange:
    case QEvent::LayoutDirectionChange:
        iconOnlyWidth_ = -1;
        updateCaptions();
        break;
    default:
        break;
    }
}

void ResponsiveToolBar::resizeEvent(QResizeEvent* event) {
    QToolBar::resizeEvent(event);
    if (event->oldSize().width() != event->size().width()) {
        updateCaptions();
    }
}

void ResponsiveToolBar::showEvent(QShowEvent* event) {
    QToolBar::showEvent(event);
    // Widths measured before the bar was polished and laid out are unreliable.
    iconOnlyWidth_ = -1;
    updateCaptions();
}

} // namespace neko::theme
