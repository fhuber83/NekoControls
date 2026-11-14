#include "neko/editor/markdown_table.hpp"

#include <algorithm>

namespace neko::editor {

namespace {

/// Automatic columns are at least this wide: room for any alignment's delimiter.
constexpr qsizetype kAutomaticMinimumWidth = 5;

QString escapeCell(QString title) {
    title.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    title.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    title.replace(QLatin1Char('\r'), QLatin1Char(' '));
    title.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return title.trimmed();
}

/// The narrowest delimiter cell valid everywhere: three dashes plus the colons.
qsizetype minimumWidth(TableAlignment alignment) {
    switch (alignment) {
    case TableAlignment::Left:
    case TableAlignment::Right:
        return 4;
    case TableAlignment::Center:
        return 5;
    case TableAlignment::Default:
        break;
    }
    return 3;
}

QString delimiterCell(TableAlignment alignment, qsizetype width) {
    const qsizetype dashes = std::max<qsizetype>(
        width - (alignment == TableAlignment::Center    ? 2
                 : alignment == TableAlignment::Default ? 0
                                                        : 1),
        3);
    const QString run(dashes, QLatin1Char('-'));
    switch (alignment) {
    case TableAlignment::Left:
        return QLatin1Char(':') + run;
    case TableAlignment::Center:
        return QLatin1Char(':') + run + QLatin1Char(':');
    case TableAlignment::Right:
        return run + QLatin1Char(':');
    case TableAlignment::Default:
        break;
    }
    return run;
}

QString row(const QStringList& cells, const QList<qsizetype>& widths, const std::vector<TableAlignment>& alignments) {
    QString line = QStringLiteral("|");
    for (qsizetype i = 0; i < cells.size(); ++i) {
        const QString& text = cells[i];
        const qsizetype pad = std::max<qsizetype>(widths[i] - text.size(), 0);
        const TableAlignment a = alignments[static_cast<size_t>(i)];
        QString padded;
        if (a == TableAlignment::Right) {
            padded = QString(pad, QLatin1Char(' ')) + text;
        } else if (a == TableAlignment::Center) {
            const qsizetype left = pad / 2;
            padded = QString(left, QLatin1Char(' ')) + text + QString(pad - left, QLatin1Char(' '));
        } else {
            padded = text + QString(pad, QLatin1Char(' '));
        }
        line += QLatin1Char(' ') + padded + QStringLiteral(" |");
    }
    return line + QLatin1Char('\n');
}

} // namespace

QString buildMarkdownTable(const MarkdownTableSpec& spec) {
    const qsizetype columns = spec.headers.size();
    if (columns == 0) {
        return {};
    }
    QStringList titles;
    std::vector<TableAlignment> alignments(static_cast<size_t>(columns), TableAlignment::Default);
    QList<qsizetype> widths;
    for (qsizetype i = 0; i < columns; ++i) {
        titles.append(escapeCell(spec.headers[i]));
        if (static_cast<size_t>(i) < spec.alignments.size()) {
            alignments[static_cast<size_t>(i)] = spec.alignments[static_cast<size_t>(i)];
        }
        const qsizetype title = titles[i].size();
        const int wanted = static_cast<size_t>(i) < spec.widths.size() ? spec.widths[static_cast<size_t>(i)] : 0;
        if (wanted > 0) {
            // Never narrower than the title or a valid delimiter.
            widths.append(std::max({qsizetype{wanted}, title, minimumWidth(alignments[static_cast<size_t>(i)])}));
        } else {
            widths.append(std::max<qsizetype>(title, kAutomaticMinimumWidth));
        }
    }

    QStringList delimiters;
    for (qsizetype i = 0; i < columns; ++i) {
        delimiters.append(delimiterCell(alignments[static_cast<size_t>(i)], widths[i]));
    }

    // Delimiter cells never exceed the column width, so the text rows stay aligned.
    QString out = row(titles, widths, alignments);
    out += row(delimiters, widths, std::vector<TableAlignment>(static_cast<size_t>(columns), TableAlignment::Default));
    const QStringList blank(columns, QString());
    for (int r = 0; r < spec.rows; ++r) {
        out += row(blank, widths, alignments);
    }
    return out;
}

} // namespace neko::editor
