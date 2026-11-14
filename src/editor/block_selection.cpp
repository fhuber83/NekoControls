#include "editor/block_selection.hpp"

#include <algorithm>

namespace nekogrid::editor::block {

namespace {

int advance(QChar c, int column, int tabWidth) {
    return c == u'\t' ? (column / tabWidth + 1) * tabWidth : column + 1;
}

} // namespace

int columnOf(QStringView line, qsizetype index, int tabWidth) {
    int column = 0;
    const qsizetype end = std::min(index, line.size());
    for (qsizetype i = 0; i < end; ++i) {
        column = advance(line[i], column, tabWidth);
    }
    return column;
}

int lineWidth(QStringView line, int tabWidth) {
    return columnOf(line, line.size(), tabWidth);
}

qsizetype indexAtColumn(QStringView line, int column, int tabWidth) {
    int at = 0;
    for (qsizetype i = 0; i < line.size(); ++i) {
        if (at >= column) {
            return i;
        }
        at = advance(line[i], at, tabWidth);
    }
    return line.size();
}

QString rowText(QStringView row, int left, int right, int tabWidth) {
    const qsizetype start = indexAtColumn(row, left, tabWidth);
    const qsizetype end = std::max(start, indexAtColumn(row, right, tabWidth));
    return row.mid(start, end - start).toString();
}

QString copyText(const QStringList& rows, int left, int right, int tabWidth) {
    QStringList out;
    out.reserve(rows.size());
    for (const QString& row : rows) {
        out << rowText(row, left, right, tabWidth);
    }
    return out.join(u'\n');
}

std::vector<RowEdit>
replaceEdits(const QStringList& rows, int left, int right, const QStringList& insert, int tabWidth) {
    std::vector<RowEdit> edits;
    for (qsizetype r = 0; r < rows.size(); ++r) {
        const QString& row = rows[r];
        QString text = insert.size() == 1 ? insert.front() : (r < insert.size() ? insert[r] : QString());
        const int width = lineWidth(row, tabWidth);
        if (width < left) {
            if (text.isEmpty()) {
                continue; // nothing to delete, nothing to pad for
            }
            edits.push_back(
                {.row = static_cast<int>(r),
                 .start = row.size(),
                 .end = row.size(),
                 .text = QString(left - width, u' ') + text});
            continue;
        }
        const qsizetype start = indexAtColumn(row, left, tabWidth);
        const qsizetype end = std::max(start, indexAtColumn(row, right, tabWidth));
        if (start == end && text.isEmpty()) {
            continue;
        }
        edits.push_back({.row = static_cast<int>(r), .start = start, .end = end, .text = std::move(text)});
    }
    return edits;
}

std::vector<RowEdit> deleteEdits(const QStringList& rows, int left, int right, DeleteKey key, int tabWidth) {
    if (right > left) {
        return replaceEdits(rows, left, right, {QString()}, tabWidth);
    }
    std::vector<RowEdit> edits;
    for (qsizetype r = 0; r < rows.size(); ++r) {
        const QString& row = rows[r];
        if (lineWidth(row, tabWidth) < left) {
            continue; // the caret is past the end of this line
        }
        const qsizetype at = indexAtColumn(row, left, tabWidth);
        if (key == DeleteKey::Backspace && at > 0) {
            edits.push_back({.row = static_cast<int>(r), .start = at - 1, .end = at, .text = {}});
        } else if (key == DeleteKey::Delete && at < row.size()) {
            edits.push_back({.row = static_cast<int>(r), .start = at, .end = at + 1, .text = {}});
        }
    }
    return edits;
}

int columnAfterDelete(const QStringList& rows, int left, int right, DeleteKey key, int tabWidth) {
    if (right > left || key == DeleteKey::Delete) {
        return left;
    }
    for (const QString& row : rows) {
        if (lineWidth(row, tabWidth) < left) {
            continue;
        }
        const qsizetype at = indexAtColumn(row, left, tabWidth);
        if (at > 0) {
            return columnOf(row, at - 1, tabWidth);
        }
    }
    return std::max(0, left - 1);
}

} // namespace nekogrid::editor::block
