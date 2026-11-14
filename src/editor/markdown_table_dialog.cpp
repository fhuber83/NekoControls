#include "editor/markdown_table_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMetaObject>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace nekogrid::editor {

namespace {

constexpr int kTitleColumn = 0;
constexpr int kAlignColumn = 1;
constexpr int kWidthColumn = 2;

enum class IconGlyph {
    Plus,
    Cross
};

/// A green plus or a red cross, drawn rather than shipped: crisp at every size and
/// device pixel ratio, the same on every platform and theme. Qt derives the greyed
/// Disabled look from it.
QIcon drawnIcon(IconGlyph glyph) {
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 48}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor color = glyph == IconGlyph::Plus ? QColor(0x2e, 0x9d, 0x3f) : QColor(0xd0, 0x34, 0x3a);
        painter.setPen(QPen(color, size / 6.0, Qt::SolidLine, Qt::RoundCap));
        const qreal m = size * 0.2; // margin
        const qreal e = size - m;   // far edge
        if (glyph == IconGlyph::Plus) {
            painter.drawLine(QPointF(size / 2.0, m), QPointF(size / 2.0, e));
            painter.drawLine(QPointF(m, size / 2.0), QPointF(e, size / 2.0));
        } else {
            const qreal d = size * 0.25; // a cross is drawn a bit smaller than the plus
            painter.drawLine(QPointF(d, d), QPointF(size - d, size - d));
            painter.drawLine(QPointF(size - d, d), QPointF(d, size - d));
        }
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

/// The column list. Rows are reordered by dragging them. The drop is handled here, by
/// value, through `moveRow`: Qt's own row move would leave the alignment combo boxes
/// (cell widgets) behind.
class ColumnList final : public QTableWidget {
public:
    using QTableWidget::QTableWidget;

    /// Called on a drop of a row of this list: move row `from` so it ends up at `to`.
    std::function<void(int from, int to)> moveRow;

protected:
    void dropEvent(QDropEvent* event) override {
        if (event->source() != this || !moveRow) {
            QTableWidget::dropEvent(event);
            return;
        }
        const int from = currentRow(); // pressing a row to drag it makes it current
        const QPoint pos = event->position().toPoint();
        const QModelIndex at = indexAt(pos);
        int to = rowCount();
        if (at.isValid()) {
            to = at.row() + (pos.y() >= visualRect(at).center().y() ? 1 : 0);
        }
        if (to > from) {
            --to; // `to` was a gap between rows; the row itself leaves one above it
        }
        // Done by hand: with MoveAction reported back, the view would remove the source row.
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        viewport()->update();
        if (from >= 0 && to != from) {
            moveRow(from, to);
        }
    }
};

} // namespace

MarkdownTableDialog::MarkdownTableDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Insert Table"));
    resize(560, 560);

    auto* layout = new QVBoxLayout(this);

    // Add / Remove first, top left, where the eye starts.
    auto* buttons = new QHBoxLayout();
    addButton_ = new QPushButton(drawnIcon(IconGlyph::Plus), tr("Add Column"), this);
    addButton_->setToolTip(tr("Add a column at the end"));
    removeButton_ = new QPushButton(drawnIcon(IconGlyph::Cross), tr("Remove"), this);
    removeButton_->setToolTip(tr("Remove the selected column (a table keeps at least one)"));
    buttons->addWidget(addButton_);
    buttons->addWidget(removeButton_);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto* list = new ColumnList(0, 3, this);
    list->moveRow = [this](int from, int to) {
        moveColumn(from, to);
    };
    table_ = list;
    table_->setHorizontalHeaderLabels({tr("Title"), tr("Alignment"), tr("Width")});
    table_->horizontalHeaderItem(kWidthColumn)
        ->setToolTip(tr("Width in characters of the Markdown source; Auto fits the title"));
    // Every column can be resized by dragging; Width takes whatever room is left.
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(kTitleColumn, 220);
    table_->setColumnWidth(kAlignColumn, 110);
    table_->verticalHeader()->hide(); // the order is the list order; numbers add nothing
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setDragEnabled(true);
    table_->setDragDropMode(QAbstractItemView::InternalMove);
    table_->setDefaultDropAction(Qt::MoveAction);
    table_->setDragDropOverwriteMode(false);
    table_->setDropIndicatorShown(true);
    table_->setToolTip(tr("Drag a column's title to move it"));
    layout->addWidget(table_, 1);

    auto* rowsRow = new QHBoxLayout();
    rowsCheck_ = new QCheckBox(tr("Generate empty rows:"), this);
    rowsCheck_->setToolTip(tr("Create empty rows below the header"));
    rowsSpin_ = new QSpinBox(this);
    rowsSpin_->setRange(1, kMaxRows);
    rowsSpin_->setValue(2);
    rowsSpin_->setToolTip(rowsCheck_->toolTip());
    rowsSpin_->setEnabled(false);
    rowsRow->addWidget(rowsCheck_);
    rowsRow->addWidget(rowsSpin_);
    rowsRow->addStretch(1);
    layout->addLayout(rowsRow);

    layout->addWidget(new QLabel(tr("Preview:"), this));
    preview_ = new QPlainTextEdit(this);
    preview_->setReadOnly(true);
    preview_->setLineWrapMode(QPlainTextEdit::NoWrap);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    preview_->setMaximumHeight(160);
    layout->addWidget(preview_);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    okButton_ = box->button(QDialogButtonBox::Ok);
    okButton_->setText(tr("Insert"));
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);

    connect(addButton_, &QPushButton::clicked, this, [this] {
        const int row = addColumn(tr("Column %1").arg(columnCount() + 1));
        table_->setCurrentCell(row, kTitleColumn);
        table_->editItem(table_->item(row, kTitleColumn));
    });
    // Enter in a title commits it and goes on to the next column's title. Only Enter
    // closes the editor with SubmitModelCache; leaving it by focus or Tab does not.
    connect(
        table_->itemDelegate(),
        &QAbstractItemDelegate::closeEditor,
        this,
        [this](QWidget*, QAbstractItemDelegate::EndEditHint hint) {
            if (hint != QAbstractItemDelegate::SubmitModelCache || table_->currentColumn() != kTitleColumn) {
                return;
            }
            const int next = table_->currentRow() + 1;
            if (next <= 0 || next >= columnCount()) {
                return;
            }
            // Queued: the view is still closing the old editor.
            QMetaObject::invokeMethod(
                this,
                [this, next] {
                    if (next < columnCount()) {
                        table_->setCurrentCell(next, kTitleColumn);
                        table_->editItem(table_->item(next, kTitleColumn));
                    }
                },
                Qt::QueuedConnection);
        });
    connect(removeButton_, &QPushButton::clicked, this, &MarkdownTableDialog::removeSelectedColumn);
    connect(rowsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        rowsSpin_->setEnabled(on);
        updateState();
    });
    connect(rowsSpin_, &QSpinBox::valueChanged, this, &MarkdownTableDialog::updateState);
    connect(table_, &QTableWidget::itemChanged, this, &MarkdownTableDialog::updateState);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &MarkdownTableDialog::updateState);

    addColumn(tr("Column %1").arg(1));
    table_->selectRow(0);
    updateState();
}

int MarkdownTableDialog::columnCount() const {
    return table_->rowCount();
}

int MarkdownTableDialog::addColumn(const QString& title, TableAlignment alignment, int width) {
    if (columnCount() >= kMaxColumns) {
        return columnCount() - 1;
    }
    const int row = table_->rowCount();
    {
        // No signal may reach updateState() while the row is half built.
        const QSignalBlocker blocker(table_);
        table_->insertRow(row);
        auto* item = new QTableWidgetItem(title);
        // Not a drop target itself: a drop lands between rows (the indicator is a line).
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        table_->setItem(row, kTitleColumn, item);
    }
    auto* combo = new QComboBox(table_);
    combo->addItem(tr("Default"), static_cast<int>(TableAlignment::Default));
    combo->addItem(tr("Left"), static_cast<int>(TableAlignment::Left));
    combo->addItem(tr("Center"), static_cast<int>(TableAlignment::Center));
    combo->addItem(tr("Right"), static_cast<int>(TableAlignment::Right));
    combo->setCurrentIndex(combo->findData(static_cast<int>(alignment)));
    connect(combo, &QComboBox::currentIndexChanged, this, &MarkdownTableDialog::updateState);
    table_->setCellWidget(row, kAlignColumn, combo);
    auto* spin = new QSpinBox(table_);
    spin->setRange(0, kMaxWidth);
    spin->setSpecialValueText(tr("Auto")); // shown for 0
    spin->setSuffix(tr(" chars"));
    spin->setValue(std::clamp(width, 0, kMaxWidth));
    connect(spin, &QSpinBox::valueChanged, this, &MarkdownTableDialog::updateState);
    table_->setCellWidget(row, kWidthColumn, spin);
    updateState();
    return row;
}

QComboBox* MarkdownTableDialog::alignmentCombo(int row) const {
    return qobject_cast<QComboBox*>(table_->cellWidget(row, kAlignColumn));
}

QSpinBox* MarkdownTableDialog::widthSpin(int row) const {
    return qobject_cast<QSpinBox*>(table_->cellWidget(row, kWidthColumn));
}

void MarkdownTableDialog::removeSelectedColumn() {
    const int row = table_->currentRow();
    if (row < 0 || columnCount() <= 1) {
        return; // a table needs at least one column
    }
    table_->removeRow(row);
    if (columnCount() > 0) {
        table_->selectRow(std::min(row, columnCount() - 1));
    }
    updateState();
}

void MarkdownTableDialog::moveColumn(int from, int to) {
    const int count = columnCount();
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
        return;
    }
    // By value: cell widgets do not travel with their row.
    QStringList titles;
    QList<int> aligns;
    QList<int> widths;
    for (int row = 0; row < count; ++row) {
        titles.append(table_->item(row, kTitleColumn)->text());
        aligns.append(alignmentCombo(row)->currentIndex());
        widths.append(widthSpin(row)->value());
    }
    titles.move(from, to);
    aligns.move(from, to);
    widths.move(from, to);
    {
        const QSignalBlocker blocker(table_);
        for (int row = 0; row < count; ++row) {
            table_->item(row, kTitleColumn)->setText(titles[row]);
        }
    }
    for (int row = 0; row < count; ++row) {
        const QSignalBlocker alignBlocker(alignmentCombo(row));
        const QSignalBlocker widthBlocker(widthSpin(row));
        alignmentCombo(row)->setCurrentIndex(aligns[row]);
        widthSpin(row)->setValue(widths[row]);
    }
    table_->selectRow(to);
    updateState();
}

MarkdownTableSpec MarkdownTableDialog::spec() const {
    MarkdownTableSpec s;
    for (int row = 0; row < columnCount(); ++row) {
        const auto* item = table_->item(row, kTitleColumn);
        const auto* combo = alignmentCombo(row);
        s.headers.append(item != nullptr ? item->text() : QString());
        s.alignments.push_back(
            combo != nullptr ? static_cast<TableAlignment>(combo->currentData().toInt()) : TableAlignment::Default);
        const auto* spin = widthSpin(row);
        s.widths.push_back(spin != nullptr ? spin->value() : 0);
    }
    s.rows = rowsCheck_->isChecked() ? rowsSpin_->value() : 0;
    return s;
}

QString MarkdownTableDialog::markdown() const {
    return buildMarkdownTable(spec());
}

void MarkdownTableDialog::updateState() {
    const int count = columnCount();
    const int row = table_->currentRow();
    removeButton_->setEnabled(row >= 0 && count > 1);
    addButton_->setEnabled(count < kMaxColumns);
    okButton_->setEnabled(count > 0);
    preview_->setPlainText(markdown());
}

} // namespace nekogrid::editor
