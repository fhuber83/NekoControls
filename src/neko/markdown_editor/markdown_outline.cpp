#include "neko/markdown_editor/markdown_outline.hpp"

#include <QScrollBar>
#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace neko::markdown_editor {

namespace {

constexpr int kOffsetRole = Qt::UserRole;
constexpr int kLineRole = Qt::UserRole + 1;

} // namespace

MarkdownOutline::MarkdownOutline(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("MarkdownOutline"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("MarkdownOutlineTree"));
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    layout->addWidget(tree_);
    setMinimumWidth(160);

    const auto emitJump = [this](QTreeWidgetItem* item) {
        if (!item) {
            return;
        }
        const QVariant offset = item->data(0, kOffsetRole);
        const QVariant line = item->data(0, kLineRole);
        if (offset.isValid() && line.isValid()) {
            emit jumpRequested(offset.toLongLong(), line.toInt());
        }
    };
    connect(tree_, &QTreeWidget::itemClicked, this, emitJump);
    connect(tree_, &QTreeWidget::itemActivated, this, emitJump);
}

void MarkdownOutline::setHeadings(const std::vector<MarkdownHeading>& headings) {
    QSet<QString> collapsed;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        if (!(*it)->isExpanded() && (*it)->childCount() > 0) {
            collapsed.insert((*it)->data(0, Qt::DisplayRole).toString());
        }
    }
    const int scroll = tree_->verticalScrollBar()->value();
    tree_->setUpdatesEnabled(false);
    tree_->clear();

    struct StackEntry {
        int level;
        QTreeWidgetItem* item;
    };
    std::vector<StackEntry> stack;

    for (const auto& h : headings) {
        while (!stack.empty() && stack.back().level >= h.level) {
            stack.pop_back();
        }
        auto* parent = stack.empty() ? nullptr : stack.back().item;
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        item->setText(0, h.text);
        item->setToolTip(0, QStringLiteral("H%1: %2").arg(h.level).arg(h.text));
        item->setData(0, kOffsetRole, static_cast<qlonglong>(h.offset));
        item->setData(0, kLineRole, h.line);
        stack.push_back({h.level, item});
    }

    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        if (!collapsed.contains((*it)->data(0, Qt::DisplayRole).toString())) {
            (*it)->setExpanded(true);
        }
    }
    tree_->setUpdatesEnabled(true);
    tree_->verticalScrollBar()->setValue(scroll);
}

} // namespace neko::markdown_editor
