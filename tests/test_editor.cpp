// Editor library test suite: src/editor only.
//
// This target links neko_editor alone -- no engine, no UI, no settings, no icon
// files. That keeps the library usable by other projects: if src/editor ever includes
// src/core or src/ui, this binary stops linking. It is also the whole test of the
// library, so it can be taken along with it; it needs nothing from tests/.
//
// How Neko Grid uses the controls (the SQL tab's editor and bar, the note tab, the
// font from Settings, the icon set) is tested in tests/test_ui.cpp.

#include "neko/editor/block_selection.hpp"
#include "neko/editor/code_editor.hpp"
#include "neko/editor/editor_defaults.hpp"
#include "neko/editor/editor_find_bar.hpp"
#include "neko/editor/editor_icons.hpp"
#include "neko/editor/editor_translations.hpp"
#include "neko/editor/emoji_shortcodes.hpp"
#include "neko/editor/markdown_editor.hpp"
#include "neko/editor/markdown_highlighter.hpp"
#include "neko/editor/markdown_link_target.hpp"
#include "neko/editor/markdown_table.hpp"
#include "neko/editor/markdown_table_dialog.hpp"
#include "neko/editor/text_search.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QHeaderView>
#include <QLineEdit>
#include <QMimeData>
#include <QPixmap>
#include <QPlainTextDocumentLayout>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSyntaxHighlighter>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QThread>
#include <QToolButton>
#include <QTranslator>
#include <QVBoxLayout>
#include <QtTest>

#include <algorithm>
#include <string_view>
#include <vector>

// `ed`, not `editor`: the tests call their editor widgets `editor`.
namespace ed = neko::editor;

class TestEditor final : public QObject {
    Q_OBJECT

private slots:
    // ---- Widget-free logic ------------------------------------------------------------

    /// Column (box) selection arithmetic: display columns with tabs, copy, typing with
    /// padding, Delete / Backspace on a box and on a column of carets.
    void testBlockSelection() {
        namespace b = ed::block;
        // Tabs advance to the next multiple of 4.
        const QString tabbed = QStringLiteral("a\tbc\td");
        QCOMPARE(b::columnOf(tabbed, 0, 4), 0);
        QCOMPARE(b::columnOf(tabbed, 1, 4), 1);
        QCOMPARE(b::columnOf(tabbed, 2, 4), 4); // after the tab
        QCOMPARE(b::columnOf(tabbed, 5, 4), 8);
        QCOMPARE(b::lineWidth(tabbed, 4), 9);
        QCOMPARE(b::indexAtColumn(tabbed, 4, 4), qsizetype{2});
        QCOMPARE(b::indexAtColumn(tabbed, 2, 4), qsizetype{2}); // inside the tab: the next character
        QCOMPARE(b::indexAtColumn(tabbed, 1, 4), qsizetype{1}); // the tab itself starts at 1
        QCOMPARE(b::indexAtColumn(tabbed, 99, 4), tabbed.size());

        const b::BlockSelection box{.anchorLine = 5, .anchorColumn = 7, .cursorLine = 2, .cursorColumn = 3};
        QCOMPARE(box.top(), 2);
        QCOMPARE(box.bottom(), 5);
        QCOMPARE(box.left(), 3);
        QCOMPARE(box.right(), 7);
        QCOMPARE(box.lineCount(), 4);
        QVERIFY(!box.isZeroWidth());

        const QStringList rows{
            QStringLiteral("SELECT id,"), QStringLiteral("  x"), QString(), QStringLiteral("FROM\tT")};
        // Copy: columns [2, 6); short lines give "" without padding.
        QCOMPARE(b::copyText(rows, 2, 6, 4), QStringLiteral("LECT\nx\n\nOM\t")); // T starts at column 8

        // Typing over the box: replaced on every line, short lines padded to the column.
        const auto typed = b::replaceEdits(rows, 2, 6, {QStringLiteral("__")}, 4);
        QCOMPARE(typed.size(), std::size_t{4});
        QCOMPARE(typed[0], (b::RowEdit{.row = 0, .start = 2, .end = 6, .text = QStringLiteral("__")}));
        QCOMPARE(typed[1], (b::RowEdit{.row = 1, .start = 2, .end = 3, .text = QStringLiteral("__")}));
        QCOMPARE(typed[2], (b::RowEdit{.row = 2, .start = 0, .end = 0, .text = QStringLiteral("  __")}));
        QCOMPARE(typed[3], (b::RowEdit{.row = 3, .start = 2, .end = 5, .text = QStringLiteral("__")}));
        // Several rows: row by row; rows past the inserted ones just lose the box.
        const auto pasted = b::replaceEdits(rows, 2, 6, {QStringLiteral("1"), QStringLiteral("2")}, 4);
        QCOMPARE(pasted.size(), std::size_t{3}); // the empty line gets nothing and is left alone
        QCOMPARE(pasted[1].text, QStringLiteral("2"));
        QCOMPARE(pasted[2], (b::RowEdit{.row = 3, .start = 2, .end = 5, .text = QString()}));

        // Delete with a width removes the box.
        QCOMPARE(b::deleteEdits(rows, 2, 6, b::DeleteKey::Delete, 4).size(), std::size_t{3});
        QCOMPARE(b::columnAfterDelete(rows, 2, 6, b::DeleteKey::Backspace, 4), 2);
        // A column of carets at 3: Backspace takes the character before, Delete the one at
        // it, on the lines that reach column 3.
        const auto back = b::deleteEdits(rows, 3, 3, b::DeleteKey::Backspace, 4);
        QCOMPARE(back.size(), std::size_t{3});
        QCOMPARE(back[0], (b::RowEdit{.row = 0, .start = 2, .end = 3, .text = {}}));
        QCOMPARE(back[1], (b::RowEdit{.row = 1, .start = 2, .end = 3, .text = {}}));
        QCOMPARE(b::columnAfterDelete(rows, 3, 3, b::DeleteKey::Backspace, 4), 2);
        const auto del = b::deleteEdits(rows, 3, 3, b::DeleteKey::Delete, 4);
        QCOMPARE(del.size(), std::size_t{2}); // "  x" ends at 3: nothing to delete there
        // Carets all past the ends of their lines: Backspace just moves them left.
        QVERIFY(b::deleteEdits({QStringLiteral("ab")}, 9, 9, b::DeleteKey::Backspace, 4).empty());
        QCOMPARE(b::columnAfterDelete({QStringLiteral("ab")}, 9, 9, b::DeleteKey::Backspace, 4), 8);
    }

    /// The editor's Find / Replace: literal, case, whole word, regex, replacements.
    void testTextSearch() {
        using ed::TextMatch;
        const auto starts = [](const ed::TextSearchResult& r) {
            std::vector<qsizetype> out;
            for (const TextMatch& m : r.matches) {
                out.push_back(m.start);
            }
            return out;
        };
        const QString sql = QStringLiteral("SELECT a.ID, a.id_x FROM T a WHERE a.ID = 1.5 -- (id)");

        // Literal: special characters match themselves; case-insensitive by default.
        QCOMPARE(starts(ed::findAll(sql, QStringLiteral("1.5"), {})), std::vector<qsizetype>{sql.indexOf(u"1.5")});
        QCOMPARE(starts(ed::findAll(sql, QStringLiteral("(id)"), {})), std::vector<qsizetype>{sql.indexOf(u"(id)")});
        QCOMPARE(ed::findAll(sql, QStringLiteral("id"), {}).matches.size(), std::size_t{4});
        QCOMPARE(ed::findAll(sql, QStringLiteral("id"), {.caseSensitive = true}).matches.size(), std::size_t{2});
        // Whole word: not inside id_x ('_' is a word character).
        QCOMPARE(ed::findAll(sql, QStringLiteral("id"), {.wholeWord = true}).matches.size(), std::size_t{3});
        QCOMPARE(
            ed::findAll(QStringLiteral("Größe GRÖSSE größe"), QStringLiteral("größe"), {}).matches.size(),
            std::size_t{2});
        QVERIFY(ed::findAll(sql, QString(), {}).matches.empty());

        // Regex: per-line anchors, alternation under whole word, empty matches skipped.
        const QString lines = QStringLiteral("select 1\nSELECT 2\n  select 3");
        QCOMPARE(
            starts(ed::findAll(lines, QStringLiteral("^select"), {.regex = true})), (std::vector<qsizetype>{0, 9}));
        QCOMPARE(
            ed::findAll(sql, QStringLiteral("from|where"), {.wholeWord = true, .regex = true}).matches.size(),
            std::size_t{2});
        QCOMPARE(
            ed::findAll(QStringLiteral("aa b aaa"), QStringLiteral("a*"), {.regex = true}).matches,
            (std::vector<TextMatch>{{.start = 0, .length = 2}, {.start = 5, .length = 3}}));
        const auto bad = ed::findAll(sql, QStringLiteral("(unclosed"), {.regex = true});
        QVERIFY(!bad.valid);
        QVERIFY(!bad.error.isEmpty());
        const auto capped = ed::findAll(QString(50, u'x'), QStringLiteral("x"), {}, 10);
        QCOMPARE(capped.matches.size(), std::size_t{10});
        QVERIFY(capped.truncated);

        // Replacements: literal as is; regex expands groups, escapes and \n / \t.
        const auto lit = ed::findAll(sql, QStringLiteral("a.ID"), {.caseSensitive = true});
        QCOMPARE(
            ed::replacementFor(sql, lit.matches[0], QStringLiteral("a.ID"), QStringLiteral("$1 \\1"), {}),
            QStringLiteral("$1 \\1"));
        const ed::TextSearchOptions re{.regex = true};
        const QString needle = QStringLiteral("(\\w+)\\.(\\w+)");
        const auto r = ed::findAll(sql, needle, re);
        QCOMPARE(r.matches.size(), std::size_t{4}); // a.ID, a.id_x, a.ID, 1.5
        QCOMPARE(
            ed::replacementFor(sql, r.matches[0], needle, QStringLiteral("$2 of \\1 ($0) $$ \\\\"), re),
            QStringLiteral("ID of a (a.ID) $ \\"));
        QCOMPARE(
            ed::replacementFor(sql, r.matches[1], needle, QStringLiteral("$1\\n\\t$2"), re),
            QStringLiteral("a\n\tid_x"));
        // Anchored at the match, with the whole text as context (^ still means line start).
        const auto anchored = ed::findAll(lines, QStringLiteral("^(\\w+) (\\d)"), re);
        QCOMPARE(anchored.matches.size(), std::size_t{2});
        QCOMPARE(
            ed::replacementFor(lines, anchored.matches[1], QStringLiteral("^(\\w+) (\\d)"), QStringLiteral("$2"), re),
            QStringLiteral("2"));
    }

    void testEmojiShortcodeTable() {
        const auto table = ed::emojiShortcodes();
        QVERIFY(table.size() > 1800);
        QSet<QString> names;
        for (const auto& entry : table) {
            const QString name = QString::fromLatin1(entry.name.data(), static_cast<qsizetype>(entry.name.size()));
            QVERIFY2(QRegularExpression(QStringLiteral("^[a-z0-9_+-]+$")).match(name).hasMatch(), qPrintable(name));
            QVERIFY(!entry.emoji.empty());
            names.insert(name);
        }
        QCOMPARE(names.size(), static_cast<qsizetype>(table.size()));
        QCOMPARE(table.front().name, std::string_view("100")); // gemoji order
        QVERIFY(ed::findEmojiShortcode("tada"));
        QCOMPARE(QString::fromUtf8(ed::findEmojiShortcode("tada")->emoji), QStringLiteral("🎉"));
        QCOMPARE(QString::fromUtf8(ed::findEmojiShortcode("+1")->emoji), QStringLiteral("👍"));
        QCOMPARE(QString::fromUtf8(ed::findEmojiShortcode("copyright")->emoji), QStringLiteral("©️"));
        QVERIFY(!ed::findEmojiShortcode("octocat")); // image-only on GitHub: the preview has no glyph
        QVERIFY(!ed::findEmojiShortcode(":tada:"));
    }

    void testEmojiShortcodeQuery() {
        const auto at = [](const QString& text, qsizetype pos = -1) {
            return ed::emojiShortcodeQueryAt(text, pos < 0 ? text.size() : pos);
        };
        auto q = at(QStringLiteral("Hi :sm"));
        QVERIFY(q);
        QCOMPARE(q->start, 3);
        QCOMPARE(q->end, 6);
        QCOMPARE(q->prefix, QStringLiteral("sm"));

        q = at(QStringLiteral(":"));
        QVERIFY(q);
        QCOMPARE(q->start, 0);
        QVERIFY(q->prefix.isEmpty());
        QVERIFY(at(QStringLiteral("(:+")));
        QVERIFY(at(QStringLiteral("**:Ta")));
        QVERIFY(at(QStringLiteral("line\n:x")));

        // Not where ':' does not begin a word, nor after a finished shortcode.
        QVERIFY(!at(QStringLiteral("10:30")));
        QVERIFY(!at(QStringLiteral("Note:")));
        QVERIFY(!at(QStringLiteral("http://x")));
        QVERIFY(!at(QStringLiteral("a ::x")));
        QVERIFY(!at(QStringLiteral("a \\:x")));
        QVERIFY(!at(QStringLiteral(":smile:")));
        QVERIFY(!at(QStringLiteral(":sm ile")));
        QVERIFY(!at(QString(), 0));
        QVERIFY(!at(QStringLiteral(":x"), 5));

        // Inside a finished name the whole `:name:` is replaced; before plain text only up to the cursor.
        q = at(QStringLiteral("a :smile: b"), 5);
        QVERIFY(q);
        QCOMPARE(q->start, 2);
        QCOMPARE(q->end, 9);
        QCOMPARE(q->prefix, QStringLiteral("sm"));
        q = at(QStringLiteral("foo :bar baz"), 5);
        QVERIFY(q);
        QCOMPARE(q->end, 5);

        // Code: an inline span on the line, a fenced block above.
        QVERIFY(!at(QStringLiteral("`code :x")));
        QVERIFY(at(QStringLiteral("`a` :x")));
        QVERIFY(!at(QStringLiteral("```sql\nSELECT :x")));
        QVERIFY(at(QStringLiteral("```\ncode\n```\n:x")));
        QVERIFY(!at(QStringLiteral("~~~~\n```\n:x")));       // ``` does not close ~~~~
        QVERIFY(at(QStringLiteral("~~~~\n```\n~~~~~\n:x"))); // a longer fence does
        QVERIFY(!at(QStringLiteral("````\n```\n:x")));       // nor a shorter one
        QVERIFY(at(QStringLiteral("``` a`b\n:x")));          // not a fence: backtick in its info string
        QVERIFY(!at(QStringLiteral("   ```\n:x")));          // three spaces of indent still open one
    }

    void testEmojiShortcodeMatching() {
        const auto names = [](const QString& prefix) {
            QStringList out;
            for (const auto* entry : ed::matchEmojiShortcodes(prefix)) {
                out.append(QString::fromLatin1(entry->name.data(), static_cast<qsizetype>(entry->name.size())));
            }
            return out;
        };
        QCOMPARE(ed::matchEmojiShortcodes(QString()).size(), ed::emojiShortcodes().size());
        QCOMPARE(names(QString()).front(), QStringLiteral("100"));

        const QStringList smile = names(QStringLiteral("smile"));
        QCOMPARE(smile.front(), QStringLiteral("smile")); // exact first
        QVERIFY(smile.contains(QStringLiteral("smiley")));
        QVERIFY(smile.indexOf(QStringLiteral("smiley")) < smile.indexOf(QStringLiteral("sweat_smile")));
        for (const QString& name : smile) {
            QVERIFY(name.contains(QStringLiteral("smile")));
        }

        // Exact, then starts with, then a word starting with it, then contains it -- and
        // nothing is left out.
        for (const QString& prefix : {QStringLiteral("cat"), QStringLiteral("ok"), QStringLiteral("face")}) {
            const QStringList found = names(prefix);
            int lastTier = 0;
            for (const QString& name : found) {
                const int tier = name == prefix                             ? 0
                                 : name.startsWith(prefix)                  ? 1
                                 : name.contains(QLatin1Char('_') + prefix) ? 2
                                                                            : 3;
                QVERIFY2(tier >= lastTier, qPrintable(prefix + QStringLiteral(": ") + name));
                lastTier = tier;
            }
            qsizetype containing = 0;
            for (const auto& entry : ed::emojiShortcodes()) {
                containing += std::string_view(entry.name).find(prefix.toStdString()) != std::string_view::npos;
            }
            QCOMPARE(found.size(), containing);
        }
        QCOMPARE(names(QStringLiteral("cat")).front(), QStringLiteral("cat"));

        QCOMPARE(names(QStringLiteral("TADA")).front(), QStringLiteral("tada"));
        QCOMPARE(names(QStringLiteral("+1")).front(), QStringLiteral("+1"));
        QVERIFY(names(QStringLiteral("xyzqx")).isEmpty());
        QVERIFY(names(QStringLiteral("é")).isEmpty());
        QVERIFY(names(QStringLiteral("a b")).isEmpty());
    }

    void testMarkdownTableBuilder() {
        using ed::TableAlignment;
        QVERIFY(ed::buildMarkdownTable({}).isEmpty());

        ed::MarkdownTableSpec spec;
        spec.headers = {QStringLiteral("Name"), QStringLiteral("Qty"), QStringLiteral("Price"), QStringLiteral("a|b")};
        spec.alignments = {TableAlignment::Left, TableAlignment::Right, TableAlignment::Center};
        spec.rows = 2;
        const QString md = ed::buildMarkdownTable(spec);
        const QStringList lines = md.split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), 5); // header, delimiter, 2 rows, then the empty tail after the last newline
        QVERIFY(md.endsWith(QLatin1Char('\n')));
        QCOMPARE(lines[0], QStringLiteral("| Name  |   Qty | Price | a\\|b  |"));
        QCOMPARE(lines[1], QStringLiteral("| :---- | ----: | :---: | ----- |"));
        QCOMPARE(lines[2], QStringLiteral("|       |       |       |       |"));
        QCOMPARE(lines[2].size(), lines[0].size());
        QCOMPARE(lines[3], lines[2]);

        // Only a header and its delimiter without rows; a negative count is 0.
        spec.rows = -3;
        QCOMPARE(ed::buildMarkdownTable(spec).count(QLatin1Char('\n')), 2);

        // Long titles widen the column; right/center titles are padded to match.
        ed::MarkdownTableSpec wide;
        wide.headers = {QStringLiteral("Description of it")};
        wide.alignments = {TableAlignment::Center};
        QCOMPARE(ed::buildMarkdownTable(wide), QStringLiteral("| Description of it |\n| :---------------: |\n"));

        // A given width: used as is, but never below the title or a valid delimiter.
        ed::MarkdownTableSpec sized;
        sized.headers = {QStringLiteral("Id"), QStringLiteral("Name"), QStringLiteral("x"), QStringLiteral("Auto")};
        sized.alignments = {TableAlignment::Right, TableAlignment::Default, TableAlignment::Center};
        sized.widths = {8, 2, 1, 0};
        QCOMPARE(
            ed::buildMarkdownTable(sized),
            QStringLiteral(
                "|       Id | Name |   x   | Auto  |\n"
                "| -------: | ---- | :---: | ----- |\n"));
        sized.widths = {8}; // missing entries are automatic
        QVERIFY(ed::buildMarkdownTable(sized).startsWith(QStringLiteral("|       Id | Name  |")));
    }

    // ---- Widgets ----------------------------------------------------------------------

    /// EditorDefaults is what editors start with and follow: the font and line wrapping,
    /// for every open editor at once and for those built later. Nothing is stored.
    void testEditorDefaults() {
        ed::EditorDefaults& defaults = ed::EditorDefaults::instance();
        const auto restore = qScopeGuard([&defaults] {
            defaults.resetFont();
            defaults.setLineWrap(true);
        });
        const QFont fallback = ed::EditorDefaults::fallbackFont();
        QCOMPARE(fallback.pointSize(), 11);
        QCOMPARE(defaults.font(), fallback);
        QVERIFY(defaults.lineWrap());

        ed::CodeEditor first;
        QCOMPARE(first.font().family(), fallback.family());
        QCOMPARE(first.font().pointSize(), 11);
        QVERIFY(first.isLineWrapEnabled());
        QCOMPARE(first.lineWrapMode(), QPlainTextEdit::WidgetWidth);
        QVERIFY(first.lineWrapAction()->isChecked());

        // A font set there reaches the open editor and the next one; tabs stay 4 wide.
        QFont chosen = fallback;
        chosen.setPointSize(17);
        chosen.setItalic(true);
        QSignalSpy fontSpy(&defaults, &ed::EditorDefaults::fontChanged);
        defaults.setFont(chosen);
        QCOMPARE(fontSpy.count(), 1);
        QCOMPARE(defaults.font(), chosen);
        QCOMPARE(first.font().pointSize(), 17);
        QVERIFY(first.font().italic());
        QCOMPARE(
            first.tabStopDistance(),
            qreal(first.fontMetrics().horizontalAdvance(QLatin1Char(' ')) * ed::CodeEditor::kTabWidth));
        ed::MarkdownEditor later;
        QCOMPARE(later.font().pointSize(), 17);

        // An editor's own font holds until the shared one is set again -- also to the same.
        QFont own = chosen;
        own.setPointSize(9);
        first.setFont(own);
        QCOMPARE(first.font().pointSize(), 9);
        QCOMPARE(later.font().pointSize(), 17);
        defaults.setFont(chosen);
        QCOMPARE(first.font().pointSize(), 17);
        defaults.resetFont();
        QCOMPARE(defaults.font(), fallback);
        QCOMPARE(first.font().pointSize(), 11);
        QVERIFY(!later.font().italic());

        // Wrapping: per editor through its action, for all through the defaults.
        QSignalSpy wrapSpy(&first, &ed::CodeEditor::lineWrapChanged);
        first.lineWrapAction()->trigger();
        QVERIFY(!first.isLineWrapEnabled());
        QCOMPARE(first.lineWrapMode(), QPlainTextEdit::NoWrap);
        QCOMPARE(wrapSpy.count(), 1);
        QVERIFY(later.isLineWrapEnabled());
        QVERIFY(defaults.lineWrap());
        defaults.setLineWrap(true); // the value held: the editor that went its own way returns
        QVERIFY(first.isLineWrapEnabled());
        QVERIFY(first.lineWrapAction()->isChecked());
        QCOMPARE(wrapSpy.count(), 2);
        defaults.setLineWrap(false);
        QVERIFY(!first.isLineWrapEnabled());
        QVERIFY(!first.lineWrapAction()->isChecked());
        QVERIFY(!later.isLineWrapEnabled());
        ed::CodeEditor unwrapped;
        QVERIFY(!unwrapped.isLineWrapEnabled());
        QVERIFY(!unwrapped.lineWrapAction()->isChecked());
    }

    /// Icons come from the application, by name; without a provider -- or an icon for a
    /// name -- the widgets do without and the Find bar's buttons show captions.
    void testEditorIconProvider() {
        const auto restore = qScopeGuard([] { ed::setEditorIconProvider({}); });
        const auto highlight = [](QPlainTextEdit* target) {
            return [target](const QList<QTextEdit::ExtraSelection>& h) {
                target->setExtraSelections(h);
            };
        };
        const auto button = [](const ed::EditorFindBar& bar, const char* name) {
            return bar.findChild<QToolButton*>(QString::fromLatin1(name));
        };
        // The bar's own buttons: a QLineEdit's clear button is a QToolButton too.
        const auto buttons = [](const ed::EditorFindBar& bar) {
            QList<QToolButton*> own;
            for (QToolButton* b : bar.findChildren<QToolButton*>()) {
                if (qobject_cast<QLineEdit*>(b->parentWidget()) == nullptr) {
                    own << b;
                }
            }
            return own;
        };

        ed::setEditorIconProvider({});
        QVERIFY(ed::editorIcon(QStringLiteral("line-wrap")).isNull());
        {
            ed::CodeEditor plain;
            ed::EditorFindBar bar(&plain, highlight(&plain));
            QVERIFY(plain.lineWrapAction()->icon().isNull());
            QCOMPARE(plain.lineWrapAction()->text(), QStringLiteral("Wrap Lines"));
            QCOMPARE(buttons(bar).size(), 8);
            for (const QToolButton* b : buttons(bar)) {
                QVERIFY(b->icon().isNull());
                QVERIFY2(!b->text().isEmpty(), qPrintable(b->toolTip()));
            }
            QCOMPARE(button(bar, "FindNext")->text(), QStringLiteral("▼"));
            QCOMPARE(button(bar, "FindClose")->text(), QStringLiteral("✕"));
        }

        // A provider is asked once for each name the library knows, and no other.
        QStringList asked;
        ed::setEditorIconProvider([&asked](const QString& name) {
            asked << name;
            QPixmap image(16, 16);
            image.fill(Qt::red);
            return QIcon(image);
        });
        {
            ed::CodeEditor themed;
            ed::EditorFindBar bar(&themed, highlight(&themed));
            QVERIFY(!themed.lineWrapAction()->icon().isNull());
            for (const char* name : {"FindPrevious", "FindNext", "FindClose"}) {
                QVERIFY(!button(bar, name)->icon().isNull());
                QVERIFY(button(bar, name)->text().isEmpty());
            }
            int iconOnly = 0;
            for (const QToolButton* b : buttons(bar)) {
                iconOnly += b->text().isEmpty() ? 1 : 0;
                QVERIFY(b->text().isEmpty() != b->icon().isNull()); // an icon or a caption
            }
            QCOMPARE(iconOnly, 6); // Replace and Replace All are captions either way
        }
        QStringList expected = ed::editorIconNames();
        asked.sort();
        expected.sort();
        QCOMPARE(asked, expected);

        // An icon set that lacks a name: that one button falls back, the others do not.
        ed::setEditorIconProvider([](const QString& name) {
            if (name == QStringLiteral("find-next")) {
                return QIcon();
            }
            QPixmap image(16, 16);
            image.fill(Qt::blue);
            return QIcon(image);
        });
        QPlainTextEdit basic; // the bar works on any QPlainTextEdit
        ed::EditorFindBar bar(&basic, highlight(&basic));
        QCOMPARE(button(bar, "FindNext")->text(), QStringLiteral("▼"));
        QVERIFY(button(bar, "FindPrevious")->text().isEmpty());
        basic.setPlainText(QStringLiteral("one two one"));
        bar.openFind();
        bar.findField()->setText(QStringLiteral("one"));
        QCOMPARE(bar.matchCount(), 2);
        QCOMPARE(basic.extraSelections().size(), 2);
    }

    /// setTextUndoable() replaces everything as one edit, and Insert toggles overwrite
    /// mode with a signal for a status bar.
    void testCodeEditorUndoableTextAndOverwrite() {
        ed::CodeEditor editor;
        editor.setPlainText(QStringLiteral("first"));
        editor.setTextUndoable(QStringLiteral("second\nthird"));
        QCOMPARE(editor.toPlainText(), QStringLiteral("second\nthird"));
        editor.undo();
        QCOMPARE(editor.toPlainText(), QStringLiteral("first"));
        QVERIFY(editor.lineNumberAreaWidth() > 0);

        editor.setPlainText(QStringLiteral("abc\ndef"));
        QTextCursor start(editor.document());
        editor.setTextCursor(start);
        QSignalSpy spy(&editor, &ed::CodeEditor::overwriteModeChanged);
        QTest::keyClick(&editor, Qt::Key_Insert);
        QVERIFY(editor.isOverwriteMode());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
        QTest::keyClicks(&editor, QStringLiteral("XYZ!"));
        QCOMPARE(editor.toPlainText(), QStringLiteral("XYZ!\ndef"));   // at the line's end it appends
        QTest::keyClick(&editor, Qt::Key_Insert, Qt::ControlModifier); // Copy, not the toggle
        QVERIFY(editor.isOverwriteMode());
        QTest::keyClick(&editor, Qt::Key_Insert);
        QVERIFY(!editor.isOverwriteMode());
        QCOMPARE(spy.count(), 1);
    }

    void testMarkdownTableWizard() {
        ed::MarkdownTableDialog dialog;
        // One column to start, no empty rows, no row numbers in the column list.
        QCOMPARE(dialog.columnCount(), 1);
        QVERIFY(dialog.okButton()->isEnabled());
        QVERIFY(!dialog.columnsTable()->verticalHeader()->isVisibleTo(&dialog));
        QVERIFY(!dialog.rowsCheck()->isChecked());
        QVERIFY(!dialog.rowsSpin()->isEnabled());
        QCOMPARE(dialog.spec().rows, 0);
        QCOMPARE(dialog.markdown().count(QLatin1Char('\n')), 2); // header and delimiter only

        dialog.addColumn(QStringLiteral("Name"));
        dialog.addColumn(QStringLiteral("Column 3"));
        dialog.columnsTable()->item(0, 0)->setText(QStringLiteral("Id"));
        auto* combo = qobject_cast<QComboBox*>(dialog.columnsTable()->cellWidget(0, 1));
        combo->setCurrentIndex(combo->findData(static_cast<int>(ed::TableAlignment::Right)));
        dialog.rowsSpin()->setValue(1);
        QCOMPARE(dialog.spec().rows, 0); // the count only counts once the box is checked
        dialog.rowsCheck()->setChecked(true);
        QVERIFY(dialog.rowsSpin()->isEnabled());

        ed::MarkdownTableSpec spec = dialog.spec();
        QCOMPARE(spec.headers, (QStringList{QStringLiteral("Id"), QStringLiteral("Name"), QStringLiteral("Column 3")}));
        QCOMPARE(spec.alignments[0], ed::TableAlignment::Right);
        QCOMPARE(spec.rows, 1);
        QCOMPARE(dialog.previewEdit()->toPlainText(), dialog.markdown());

        // Adding and reordering keep titles and alignment together.
        const auto button = [&dialog](const QString& text) -> QPushButton* {
            for (auto* b : dialog.findChildren<QPushButton*>()) {
                if (b->text() == text) {
                    return b;
                }
            }
            return nullptr;
        };
        dialog.addColumn(QStringLiteral("Extra"), ed::TableAlignment::Center);
        QCOMPARE(dialog.columnCount(), 4);
        QVERIFY(button(QStringLiteral("Move Up")) == nullptr); // dragging replaced them
        QVERIFY(button(QStringLiteral("Move Down")) == nullptr);
        dialog.moveColumn(0, 1);
        spec = dialog.spec();
        QCOMPARE(spec.headers.at(0), QStringLiteral("Name"));
        QCOMPARE(spec.headers.at(1), QStringLiteral("Id"));
        QCOMPARE(spec.alignments.at(1), ed::TableAlignment::Right);
        QCOMPARE(spec.alignments.at(3), ed::TableAlignment::Center);

        // Width: one spin box per column, 0 ("Auto") by default; it moves with the column.
        QCOMPARE(dialog.columnsTable()->columnCount(), 3);
        auto* width = qobject_cast<QSpinBox*>(dialog.columnsTable()->cellWidget(1, 2));
        QVERIFY(width != nullptr);
        QCOMPARE(width->value(), 0);
        QCOMPARE(width->text(), QStringLiteral("Auto"));
        QCOMPARE(spec.widths, (std::vector<int>{0, 0, 0, 0}));
        width->setValue(12); // "Id"
        QCOMPARE(dialog.spec().widths.at(1), 12);
        QVERIFY(dialog.previewEdit()->toPlainText().contains(QStringLiteral("|           Id |")));
        QCOMPARE(dialog.columnCount(), dialog.addColumn(QStringLiteral("Wide"), ed::TableAlignment::Default, 9999) + 1);
        QCOMPARE(dialog.spec().widths.back(), ed::MarkdownTableDialog::kMaxWidth);
        dialog.columnsTable()->setCurrentCell(4, 0);
        button(QStringLiteral("Remove"))->click();

        // Dragging a row: the drop moves it by value, so alignments go along with titles.
        auto* list = dialog.columnsTable();
        QCOMPARE(list->dragDropMode(), QAbstractItemView::InternalMove);
        QVERIFY(list->dragEnabled());
        QVERIFY(!(list->item(0, 0)->flags() & Qt::ItemIsDropEnabled)); // drops go between rows
        dialog.moveColumn(3, 0);                                       // Extra to the front
        spec = dialog.spec();
        QCOMPARE(
            spec.headers,
            (QStringList{
                QStringLiteral("Extra"), QStringLiteral("Name"), QStringLiteral("Id"), QStringLiteral("Column 3")}));
        QCOMPARE(spec.alignments.at(0), ed::TableAlignment::Center);
        QCOMPARE(spec.alignments.at(2), ed::TableAlignment::Right);
        QCOMPARE(spec.widths, (std::vector<int>{0, 0, 12, 0}));
        QCOMPARE(list->currentRow(), 0);
        dialog.moveColumn(0, 3); // and to the end
        QCOMPARE(dialog.spec().headers.last(), QStringLiteral("Extra"));
        QCOMPARE(dialog.spec().alignments.at(3), ed::TableAlignment::Center);
        QCOMPARE(list->currentRow(), 3);
        dialog.moveColumn(0, 9); // out of range: nothing
        QCOMPARE(dialog.spec().headers.first(), QStringLiteral("Name"));
        QCOMPARE(dialog.previewEdit()->toPlainText(), dialog.markdown());

        // Both list columns can be resized by the user.
        const QHeaderView* header = dialog.columnsTable()->horizontalHeader();
        QCOMPARE(header->sectionResizeMode(0), QHeaderView::Interactive);
        QCOMPARE(header->sectionResizeMode(1), QHeaderView::Interactive);

        // Enter in a title goes on to editing the next one; not past the last.
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto* table = dialog.columnsTable();

        // Add and Remove sit top left, above the list, each with an icon.
        auto* add = button(QStringLiteral("Add Column"));
        QVERIFY(add != nullptr);
        for (QPushButton* b : {add, button(QStringLiteral("Remove"))}) {
            QVERIFY(!b->icon().isNull());
            const QRect r(b->mapTo(&dialog, QPoint(0, 0)), b->size());
            QVERIFY(r.bottom() < table->mapTo(&dialog, QPoint(0, 0)).y());
            QVERIFY(r.left() < dialog.width() / 2);
        }
        QVERIFY(add->mapTo(&dialog, QPoint(0, 0)).x() <= table->mapTo(&dialog, QPoint(0, 0)).x());

        const auto editing = [table](int row) {
            return table->indexWidget(table->model()->index(row, 0)) != nullptr;
        };
        table->setCurrentCell(1, 0);
        table->editItem(table->item(1, 0));
        auto* editor = qobject_cast<QLineEdit*>(table->indexWidget(table->model()->index(1, 0)));
        QVERIFY(editor != nullptr);
        editor->selectAll();
        QTest::keyClicks(editor, QStringLiteral("Key"));
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(editing(2));
        QCOMPARE(table->item(1, 0)->text(), QStringLiteral("Key"));
        QCOMPARE(table->currentRow(), 2);
        QVERIFY(!editing(1));
        table->setCurrentCell(3, 0);
        QTRY_VERIFY(!editing(2)); // moving away closed it
        table->editItem(table->item(3, 0));
        editor = qobject_cast<QLineEdit*>(table->indexWidget(table->model()->index(3, 0)));
        QVERIFY(editor != nullptr);
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(!editing(3));
        QTest::qWait(50); // the queued "edit next" would have run by now
        for (int row = 0; row < dialog.columnCount(); ++row) {
            QVERIFY(!editing(row));
        }
        QCOMPARE(table->currentRow(), 3);

        // The last column cannot be removed.
        auto* remove = button(QStringLiteral("Remove"));
        while (dialog.columnCount() > 1) {
            table->setCurrentCell(0, 0);
            QVERIFY(remove->isEnabled());
            remove->click();
        }
        table->setCurrentCell(0, 0);
        QVERIFY(!remove->isEnabled());
        remove->click();
        QCOMPARE(dialog.columnCount(), 1);
        QVERIFY(dialog.okButton()->isEnabled());
    }

    void testMarkdownHighlighterBlockStates() {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral(
            "---\ntitle: x\n---\n# Heading\n```mermaid\nerDiagram\n```` \nafter\n~~~~\ncode\n~~~\n~~~~\nend\n"));
        ed::MarkdownHighlighter hl(&doc, false);
        hl.rehighlight(); // attaching to a document only schedules the first pass
        QStringList states;
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
            states << QString::number(b.userState());
        }
        // Front matter (1) through its closing line; a ``` fence (10 + 3) that a longer
        // one closes; a ~~~~ fence (10 + 1000 + 4) that a shorter ~~~ does not close.
        QCOMPARE(states.join(QLatin1Char(' ')), QStringLiteral("1 1 0 0 13 13 0 0 1014 1014 1014 0 0 0"));

        // Multiline $$ math block (state 2).
        doc.setPlainText(QStringLiteral("$$\nx = 1\n$$\nafter\n"));
        hl.rehighlight();
        states.clear();
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
            states << QString::number(b.userState());
        }
        QCOMPARE(states.join(QLatin1Char(' ')), QStringLiteral("2 2 0 0 0"));
    }

    /// The emoji picker of MarkdownEditor, with real keys: ':' opens it, typing narrows
    /// it, Enter / Tab insert, Esc closes, and it stays shut where ':' begins no name.
    void testMarkdownEditorEmojiPicker() {
        ed::MarkdownEditor source;
        ed::MarkdownEditor* editor = &source;
        editor->show();
        QVERIFY(QTest::qWaitForWindowExposed(editor));
        QAbstractItemView* popup = editor->emojiCompleter()->popup();
        editor->setFocus();
        editor->setPlainText(QStringLiteral("Hello "));
        editor->moveCursor(QTextCursor::End);

        // A ':' opens the full list, nothing chosen yet; typing narrows and picks the best.
        QTest::keyClicks(editor, QStringLiteral(":"));
        QVERIFY(editor->isEmojiPopupVisible());
        QCOMPARE(editor->emojiPopupNames().size(), static_cast<qsizetype>(ed::emojiShortcodes().size()));
        QVERIFY(!popup->currentIndex().isValid());
        QTest::keyClicks(editor, QStringLiteral("tad"));
        QVERIFY(editor->isEmojiPopupVisible());
        QCOMPARE(editor->emojiPopupNames().front(), QStringLiteral("tada"));
        QVERIFY(popup->currentIndex().isValid());
        QTest::keyClick(popup, Qt::Key_Return); // through QCompleter, as a real key arrives
        QCOMPARE(editor->toPlainText(), QStringLiteral("Hello :tada:"));
        QVERIFY(!editor->isEmojiPopupVisible());

        // Right after the bare ':' Enter still ends the line.
        QTest::keyClicks(editor, QStringLiteral(" :"));
        QVERIFY(editor->isEmojiPopupVisible());
        QTest::keyClick(popup, Qt::Key_Return);
        QVERIFY(!editor->isEmojiPopupVisible());
        QCOMPARE(editor->toPlainText(), QStringLiteral("Hello :tada: :\n"));

        // Down picks the first row; Tab inserts it.
        QTest::keyClicks(editor, QStringLiteral(":"));
        QTest::keyClick(popup, Qt::Key_Down);
        QTest::keyClick(popup, Qt::Key_Tab);
        QCOMPARE(editor->toPlainText(), QStringLiteral("Hello :tada: :\n:100:"));

        // Backspace widens the list again; Esc closes it and keeps the text.
        QTest::keyClicks(editor, QStringLiteral(" :smil"));
        const qsizetype narrow = editor->emojiPopupNames().size();
        QTest::keyClick(popup, Qt::Key_Backspace);
        QVERIFY(editor->emojiPopupNames().size() > narrow);
        QTest::keyClick(popup, Qt::Key_Escape);
        QVERIFY(!editor->isEmojiPopupVisible());
        QVERIFY(editor->toPlainText().endsWith(QStringLiteral(" :smi")));

        // Ctrl+Space inside a finished shortcode replaces all of it.
        editor->setPlainText(QStringLiteral("a :smile: b"));
        QTextCursor c = editor->textCursor();
        c.setPosition(5); // ":sm|ile:"
        editor->setTextCursor(c);
        QTest::keyClick(editor, Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(editor->isEmojiPopupVisible());
        QVERIFY(editor->emojiPopupNames().contains(QStringLiteral("smirk")));
        editor->insertEmojiShortcode(QStringLiteral("smirk"));
        QCOMPARE(editor->toPlainText(), QStringLiteral("a :smirk: b"));

        // A character that is no part of a name closes it; times, words and code never open it.
        editor->setPlainText(QString());
        QTest::keyClicks(editor, QStringLiteral(":)"));
        QVERIFY(!editor->isEmojiPopupVisible());
        QTest::keyClicks(editor, QStringLiteral(" at 10:30, Note:"));
        QVERIFY(!editor->isEmojiPopupVisible());
        QTest::keyClicks(editor, QStringLiteral(" `x :"));
        QVERIFY(!editor->isEmojiPopupVisible());
        editor->setPlainText(QStringLiteral("```\n"));
        editor->moveCursor(QTextCursor::End);
        QTest::keyClicks(editor, QStringLiteral(":"));
        QVERIFY(!editor->isEmojiPopupVisible());
        QTest::keyClick(editor, Qt::Key_Backspace);
        editor->insertPlainText(QStringLiteral("```\n"));
        QTest::keyClicks(editor, QStringLiteral(":"));
        QVERIFY(editor->isEmojiPopupVisible());
        QTest::keyClick(popup, Qt::Key_Escape);
    }

    /// Where a link destination is being typed: after `[text](` on the same line, up to
    /// the cursor; a `#` turns it into a fragment of the path before it. Images, code
    /// and anything after a space or `)` do not count.
    void testMarkdownLinkTargetQuery() {
        const auto at = [](const QString& text, qsizetype pos = -1) {
            return ed::markdownLinkTargetQueryAt(text, pos < 0 ? text.size() : pos);
        };
        auto q = at(QStringLiteral("See [the spec]("));
        QVERIFY(q);
        QCOMPARE(q->start, qsizetype{15});
        QCOMPARE(q->end, qsizetype{15});
        QVERIFY(q->path.isEmpty() && q->prefix.isEmpty() && !q->fragment);

        q = at(QStringLiteral("x [a](docs/or"));
        QVERIFY(q);
        QCOMPARE(q->path, QStringLiteral("docs/or"));
        QCOMPARE(q->prefix, QStringLiteral("docs/or"));

        // A fragment: the whole path before the `#`; what follows narrows.
        q = at(QStringLiteral("[a](docs/orders%20spec.md#tot"));
        QVERIFY(q && q->fragment);
        QCOMPARE(q->path, QStringLiteral("docs/orders%20spec.md"));
        QCOMPARE(q->prefix, QStringLiteral("tot"));
        QCOMPARE(q->start, qsizetype{26});
        q = at(QStringLiteral("[a](#"));
        QVERIFY(q && q->fragment && q->path.isEmpty() && q->prefix.isEmpty());

        // Inside a finished destination: the path part, or the fragment, up to its end.
        const QString done = QStringLiteral("[a](docs/x.md#part) after");
        q = at(done, 7); // "[a](doc|s"
        QVERIFY(q && !q->fragment);
        QCOMPARE(q->start, qsizetype{4});
        QCOMPARE(q->end, qsizetype{13}); // before '#'
        q = at(done, 16);                // "#pa|rt"
        QVERIFY(q && q->fragment);
        QCOMPARE(q->end, qsizetype{18}); // before ')'

        // Not a destination.
        QVERIFY(!at(QStringLiteral("[a](x.md) more")));
        QVERIFY(!at(QStringLiteral("[a](x y")));
        QVERIFY(!at(QStringLiteral("![pic](img")));
        QVERIFY(!at(QStringLiteral("(plain")));
        QVERIFY(!at(QStringLiteral("a](x"))); // no link text on the line
        QVERIFY(!at(QStringLiteral("`[a](x")));
        QVERIFY(!at(QStringLiteral("```\n[a](x")));
        QVERIFY(at(QStringLiteral("```\ncode\n```\n[a](x")));
        QVERIFY(!at(QStringLiteral("[a]\n(x")));

        // Matching: starts with first, then a word of it, then anywhere; case-insensitive.
        const std::vector<ed::MarkdownLinkTarget> all{
            {.target = QStringLiteral("docs/orders%20spec.md"), .label = QStringLiteral("docs/orders spec.md")},
            {.target = QStringLiteral("orders.sql"), .label = {}},
            {.target = QStringLiteral("reorders.md"), .label = {}},
            {.target = QStringLiteral("notes.md"), .label = {}},
        };
        const auto targets = [&](const QString& prefix) {
            QStringList out;
            for (const auto& t : ed::matchMarkdownLinkTargets(all, prefix)) {
                out << t.target;
            }
            return out;
        };
        QCOMPARE(targets({}).size(), 4);
        QCOMPARE(
            targets(QStringLiteral("ORD")),
            (QStringList{
                QStringLiteral("orders.sql"), QStringLiteral("docs/orders%20spec.md"), QStringLiteral("reorders.md")}));
        QCOMPARE(targets(QStringLiteral("orders spec")), QStringList{QStringLiteral("docs/orders%20spec.md")});
        QVERIFY(targets(QStringLiteral("zzz")).isEmpty());
    }

    /// Link completion in MarkdownEditor, with real keys: Ctrl+Space in `[x](` lists the
    /// provider's files, typing narrows, Enter puts the target in; a `#` lists the
    /// headings of the file before it. Without a provider nothing opens.
    void testMarkdownEditorLinkCompletion() {
        ed::MarkdownEditor editor;
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        editor.setFocus();
        QAbstractItemView* popup = editor.linkCompleter()->popup();

        editor.setPlainText(QStringLiteral("See [spec]("));
        editor.moveCursor(QTextCursor::End);
        QTest::keyClick(&editor, Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(!editor.isLinkPopupVisible()); // no provider

        QList<ed::MarkdownLinkTargetQuery> asked;
        editor.setLinkTargetProvider([&asked](const ed::MarkdownLinkTargetQuery& q) {
            asked << q;
            if (!q.fragment) {
                return std::vector<ed::MarkdownLinkTarget>{
                    {.target = QStringLiteral("docs/orders%20spec.md"), .label = QStringLiteral("docs/orders spec.md")},
                    {.target = QStringLiteral("fix.sql"), .label = {}},
                };
            }
            if (q.path == QStringLiteral("docs/orders%20spec.md")) {
                return std::vector<ed::MarkdownLinkTarget>{
                    {.target = QStringLiteral("orders"), .label = QStringLiteral("# Orders")},
                    {.target = QStringLiteral("totals"), .label = QStringLiteral("## Totals")},
                };
            }
            return std::vector<ed::MarkdownLinkTarget>{};
        });
        QTest::keyClick(&editor, Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(editor.isLinkPopupVisible());
        QCOMPARE(
            editor.linkPopupTargets(),
            (QStringList{QStringLiteral("docs/orders%20spec.md"), QStringLiteral("fix.sql")}));
        QVERIFY(popup->currentIndex().isValid());
        QTest::keyClicks(popup, QStringLiteral("ord"));
        QCOMPARE(editor.linkPopupTargets(), QStringList{QStringLiteral("docs/orders%20spec.md")});
        QCOMPARE(asked.size(), 1); // narrowing does not ask again
        QTest::keyClick(popup, Qt::Key_Return);
        QVERIFY(!editor.isLinkPopupVisible());
        QCOMPARE(editor.toPlainText(), QStringLiteral("See [spec](docs/orders%20spec.md"));

        // A '#' lists the file's headings by itself; Tab puts the anchor in.
        QTest::keyClicks(&editor, QStringLiteral("#"));
        QVERIFY(editor.isLinkPopupVisible());
        QCOMPARE(editor.linkPopupTargets(), (QStringList{QStringLiteral("orders"), QStringLiteral("totals")}));
        QCOMPARE(asked.size(), 2);
        QVERIFY(asked.back().fragment);
        QTest::keyClicks(popup, QStringLiteral("tot"));
        QTest::keyClick(popup, Qt::Key_Tab);
        QCOMPARE(editor.toPlainText(), QStringLiteral("See [spec](docs/orders%20spec.md#totals"));

        // A file without headings: '#' opens nothing. ')' closes an open list.
        QTest::keyClicks(&editor, QStringLiteral(") [b](fix.sql#"));
        QVERIFY(!editor.isLinkPopupVisible());
        QTest::keyClick(&editor, Qt::Key_Backspace);
        QTest::keyClick(&editor, Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(editor.isLinkPopupVisible());
        QTest::keyClicks(popup, QStringLiteral(")"));
        QVERIFY(!editor.isLinkPopupVisible());

        // Ctrl+Space inside a finished destination replaces its path, keeps the fragment.
        editor.setPlainText(QStringLiteral("[a](old.md#totals) end"));
        QTextCursor c = editor.textCursor();
        c.setPosition(4); // "[a](|old"
        editor.setTextCursor(c);
        QTest::keyClick(&editor, Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(editor.isLinkPopupVisible());
        editor.insertLinkTarget(QStringLiteral("docs/orders%20spec.md"));
        QCOMPARE(editor.toPlainText(), QStringLiteral("[a](docs/orders%20spec.md#totals) end"));

        // Outside a destination Ctrl+Space is still the emoji picker's; '#' opens nothing.
        editor.setPlainText(QStringLiteral("a :smi"));
        editor.moveCursor(QTextCursor::End);
        QTest::keyClick(&editor, Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(!editor.isLinkPopupVisible());
        QVERIFY(editor.isEmojiPopupVisible());
        QTest::keyClick(editor.emojiCompleter()->popup(), Qt::Key_Escape);
        editor.setPlainText(QString());
        QTest::keyClicks(&editor, QStringLiteral("# Heading"));
        QVERIFY(!editor.isLinkPopupVisible());
    }

    /// Column (box) selection in CodeEditor, with real input: Alt + drag selects a
    /// box that does not run to the line ends, Ctrl+C copies it as a block, typing goes
    /// onto every line (short ones padded) and is one undo step, Alt+Shift+arrows grow a
    /// box from the keyboard, Delete removes it, a copied block pastes as a block, and
    /// Esc or a plain click ends the box.
    void testEditorColumnSelection() {
        QWidget window;
        auto* layout = new QVBoxLayout(&window);
        auto* editor = new ed::CodeEditor(&window);
        layout->addWidget(editor);
        window.resize(900, 600);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        const QString original = QStringLiteral("col_a = 1,\ncol_b = 22,\nx\ncol_c = 333");
        editor->setPlainText(original);
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());
        const auto pointAt = [editor](int line, int index) {
            QTextCursor c(editor->document()->findBlockByNumber(line));
            c.setPosition(c.block().position() + index);
            return editor->cursorRect(c).center();
        };
        const auto lines = [editor] {
            return editor->toPlainText().split(u'\n');
        };

        // Alt + drag from line 0, column 4 to line 3, column 5.
        QTest::mousePress(editor->viewport(), Qt::LeftButton, Qt::AltModifier, pointAt(0, 4));
        QTest::mouseMove(editor->viewport(), pointAt(3, 5));
        QTest::mouseRelease(editor->viewport(), Qt::LeftButton, Qt::AltModifier, pointAt(3, 5));
        QVERIFY(editor->hasBlockSelection());
        QCOMPARE(editor->blockSelection()->top(), 0);
        QCOMPARE(editor->blockSelection()->bottom(), 3);
        QCOMPARE(editor->blockSelection()->left(), 4);
        QCOMPARE(editor->blockSelection()->right(), 5);
        QCOMPARE(editor->blockSelectedText(), QStringLiteral("a\nb\n\nc"));
        QVERIFY(!editor->textCursor().hasSelection()); // a box, not a running selection

        // Ctrl+C: the block on the clipboard, marked as one.
        QTest::keyClick(editor, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("a\nb\n\nc"));
        QVERIFY(QApplication::clipboard()->mimeData()->hasFormat(QString::fromLatin1(ed::CodeEditor::kBlockMimeType)));

        // Typing replaces the box on every line; the short line is padded to the column.
        QTest::keyClicks(editor, QStringLiteral("XY"));
        QCOMPARE(
            lines(),
            (QStringList{
                QStringLiteral("col_XY = 1,"),
                QStringLiteral("col_XY = 22,"),
                QStringLiteral("x   XY"),
                QStringLiteral("col_XY = 333")}));
        QVERIFY(editor->hasBlockSelection());
        QVERIFY(editor->blockSelection()->isZeroWidth()); // a column of carets after "XY"
        QCOMPARE(editor->blockSelection()->left(), 6);
        QTest::keyClick(editor, Qt::Key_Backspace);
        QCOMPARE(lines().at(0), QStringLiteral("col_X = 1,"));
        QCOMPARE(lines().at(2), QStringLiteral("x   X"));
        // Each keystroke on the box is one undo step; undo ends the box.
        editor->undo(); // the Backspace
        editor->undo(); // "Y"
        editor->undo(); // "X" over the box
        QCOMPARE(editor->toPlainText(), original);
        QVERIFY(!editor->hasBlockSelection());

        // Alt+Shift+arrows from the caret: two lines down, one column right.
        QTextCursor c(editor->document());
        c.setPosition(4);
        editor->setTextCursor(c);
        QTest::keyClick(editor, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
        QTest::keyClick(editor, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
        QTest::keyClick(editor, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(editor->blockSelectedText(), QStringLiteral("a\nb\n"));
        QTest::keyClick(editor, Qt::Key_Delete);
        QCOMPARE(lines().at(0), QStringLiteral("col_ = 1,"));
        QCOMPARE(lines().at(1), QStringLiteral("col_ = 22,"));
        QCOMPARE(lines().at(2), QStringLiteral("x"));
        // Esc ends the box (ahead of the window's Cancel), then undo the delete.
        QTest::keyClick(editor, Qt::Key_Escape);
        QVERIFY(!editor->hasBlockSelection());
        editor->undo();
        QCOMPARE(editor->toPlainText(), original);

        // The copied block pastes as a block at a plain caret: one row per line.
        c.setPosition(0);
        editor->setTextCursor(c);
        QTest::keyClick(editor, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(
            lines(),
            (QStringList{
                QStringLiteral("acol_a = 1,"),
                QStringLiteral("bcol_b = 22,"),
                QStringLiteral("x"),
                QStringLiteral("ccol_c = 333")}));
        QVERIFY(!editor->hasBlockSelection());
        editor->undo();
        QCOMPARE(editor->toPlainText(), original);

        // One line of text pastes onto every line of a box.
        QApplication::clipboard()->setText(QStringLiteral("Q"));
        editor->setBlockSelection({.anchorLine = 0, .anchorColumn = 4, .cursorLine = 1, .cursorColumn = 5});
        QTest::keyClick(editor, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(lines().mid(0, 2), (QStringList{QStringLiteral("col_Q = 1,"), QStringLiteral("col_Q = 22,")}));

        // A plain click ends the box.
        QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::NoModifier, pointAt(2, 0));
        QVERIFY(!editor->hasBlockSelection());

        // MarkdownEditor is the same control; a box set and filled through the API.
        ed::MarkdownEditor note;
        note.setPlainText(QStringLiteral("| a | b |\n| c | d |\n"));
        note.setBlockSelection({.anchorLine = 0, .anchorColumn = 2, .cursorLine = 1, .cursorColumn = 3});
        QCOMPARE(note.blockSelectedText(), QStringLiteral("a\nc"));
        note.insertIntoBlock(QStringLiteral("z"));
        QCOMPARE(note.toPlainText(), QStringLiteral("| z | b |\n| z | d |\n"));
        note.undo(); // one step
        QCOMPARE(note.toPlainText(), QStringLiteral("| a | b |\n| c | d |\n"));
    }

    /// The part of an inline suggestion Ctrl+Right accepts.
    void testInlineSuggestionWords() {
        const auto word = [](const char* text) {
            const QString s = QString::fromUtf8(text);
            return s.left(ed::CodeEditor::nextSuggestionWordLength(s));
        };
        QCOMPARE(word("a + b;"), QStringLiteral("a"));
        QCOMPARE(word(" + b;"), QStringLiteral(" +"));
        QCOMPARE(word("  sum_2(x)"), QStringLiteral("  sum_2"));
        QCOMPARE(word("(x)"), QStringLiteral("(")); // a run of punctuation stops at a word
        QCOMPARE(word(");\n}"), QStringLiteral(");"));
        QCOMPARE(word("\n    return x;"), QStringLiteral("\n    "));
        QCOMPARE(word("ünïcödé rest"), QStringLiteral("ünïcödé"));
        QCOMPARE(word(""), QString());
    }

    /// Inline suggestion (ghost text): drawn after the caret, taking up lines, but not in
    /// the document; typing eats into it, Ctrl+Right / Tab accept it as undo steps, edits,
    /// caret moves, Esc and clicks dismiss it.
    void testInlineSuggestion() {
        QWidget window;
        auto* layout = new QVBoxLayout(&window);
        auto* editor = new ed::CodeEditor(&window);
        layout->addWidget(editor);
        window.resize(700, 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        const QString original = QStringLiteral("int add(int a, int b) {\n    return \n}\nint after;");
        editor->setPlainText(original);
        editor->document()->setModified(false);
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());

        // A keyword highlighter, so the suggestion's colour has to live with other formats.
        class Keywords final : public QSyntaxHighlighter {
        public:
            using QSyntaxHighlighter::QSyntaxHighlighter;

        protected:
            void highlightBlock(const QString& text) override {
                QTextCharFormat keyword;
                keyword.setForeground(Qt::blue);
                auto it = QRegularExpression(QStringLiteral(R"(\b(int|return)\b)")).globalMatch(text);
                while (it.hasNext()) {
                    const auto m = it.next();
                    setFormat(int(m.capturedStart()), int(m.capturedLength()), keyword);
                }
            }
        };
        auto* highlighter = new Keywords(editor->document());

        const auto placeCaret = [editor](int line, int index) {
            QTextCursor c(editor->document()->findBlockByNumber(line));
            c.setPosition(c.block().position() + index);
            editor->setTextCursor(c);
        };
        const auto block = [editor](int line) {
            return editor->document()->findBlockByNumber(line);
        };
        const auto blockTop = [editor, &block](int line) {
            return editor->cursorRect(QTextCursor(block(line))).top();
        };
        const auto ghostFormats = [&block](int line) {
            const auto formats = block(line).layout()->formats();
            return std::ranges::count_if(formats, [](const QTextLayout::FormatRange& r) {
                return r.format.foreground().color() != QColor(Qt::blue);
            });
        };
        QSignalSpy accepted(editor, &ed::CodeEditor::inlineSuggestionAccepted);
        QSignalSpy dismissed(editor, &ed::CodeEditor::inlineSuggestionDismissed);

        placeCaret(1, 11); // after "return "
        const qreal closingTop = blockTop(2);
        editor->setInlineSuggestion(QStringLiteral("a + b;\r\n    // sum"));
        editor->grab(); // lays it out
        QVERIFY(editor->hasInlineSuggestion());
        QCOMPARE(editor->inlineSuggestion(), QStringLiteral("a + b;\n    // sum")); // \r dropped
        // Not in the document, but laid out: line 1 now takes two lines, pushing "}" down.
        QCOMPARE(editor->toPlainText(), original);
        QVERIFY(!editor->document()->isModified());
        QVERIFY(!editor->document()->isUndoAvailable());
        QCOMPARE(block(1).layout()->preeditAreaText(), QStringLiteral("a + b;     // sum"));
        QCOMPARE(block(1).layout()->lineCount(), 2);
        QVERIFY(blockTop(2) > closingTop);
        QCOMPARE(ghostFormats(1), 1);
        const QColor placeholder = editor->palette().color(QPalette::PlaceholderText);
        QVERIFY(std::ranges::any_of(block(1).layout()->formats(), [&](const QTextLayout::FormatRange& r) {
            return r.start == 11 && r.length == 17 && r.format.foreground().color() == placeholder;
        }));

        // A highlighter replacing the block's formats: painting puts the colour back.
        highlighter->rehighlight();
        editor->grab();
        QCOMPARE(ghostFormats(1), 1);
        QCOMPARE(block(1).layout()->preeditAreaText(), QStringLiteral("a + b;     // sum"));

        // Typing what it starts with eats into it; no signal.
        QTest::keyClicks(editor, QStringLiteral("a "));
        QCOMPARE(editor->inlineSuggestion(), QStringLiteral("+ b;\n    // sum"));
        QCOMPARE(block(1).text(), QStringLiteral("    return a "));
        QCOMPARE(block(1).layout()->preeditAreaPosition(), 13);
        QCOMPARE(accepted.count(), 0);
        QCOMPARE(dismissed.count(), 0);

        // Ctrl+Right: the next word, one undo step; the rest stays.
        QTest::keyClick(editor, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(block(1).text(), QStringLiteral("    return a +"));
        QCOMPARE(editor->inlineSuggestion(), QStringLiteral(" b;\n    // sum"));
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(accepted.last().at(0).value<qsizetype>(), qsizetype{3}); // "a +"
        QCOMPARE(accepted.last().at(1).toBool(), false);
        QCOMPARE(editor->textCursor().positionInBlock(), 14);

        // Tab: all the rest, lines included.
        QTest::keyClick(editor, Qt::Key_Tab);
        QVERIFY(!editor->hasInlineSuggestion());
        QCOMPARE(
            editor->toPlainText(),
            QStringLiteral("int add(int a, int b) {\n    return a + b;\n    // sum\n}\nint after;"));
        QCOMPARE(accepted.count(), 2);
        QCOMPARE(accepted.last().at(0).value<qsizetype>(), qsizetype{17});
        QCOMPARE(accepted.last().at(1).toBool(), true);
        QCOMPARE(dismissed.count(), 0);
        QCOMPARE(block(1).layout()->preeditAreaPosition(), -1);
        QCOMPARE(ghostFormats(1), 0);
        QCOMPARE(ghostFormats(2), 0);
        editor->undo(); // the Tab
        QCOMPARE(block(1).text(), QStringLiteral("    return a +"));
        editor->undo(); // the Ctrl+Right
        QCOMPARE(block(1).text(), QStringLiteral("    return a "));

        // Typing something else dismisses it.
        editor->setPlainText(original);
        placeCaret(1, 11);
        editor->setInlineSuggestion(QStringLiteral("a + b;"));
        QTest::keyClicks(editor, QStringLiteral("x"));
        QVERIFY(!editor->hasInlineSuggestion());
        QCOMPARE(dismissed.count(), 1);
        QCOMPARE(block(1).layout()->preeditAreaPosition(), -1);
        QCOMPARE(ghostFormats(1), 0);

        // Typing it to its end: it just ends.
        editor->setInlineSuggestion(QStringLiteral("y;"));
        QTest::keyClicks(editor, QStringLiteral("y;"));
        QVERIFY(!editor->hasInlineSuggestion());
        QCOMPARE(dismissed.count(), 1);
        QCOMPARE(accepted.count(), 2);

        // Esc.
        editor->setInlineSuggestion(QStringLiteral(" // done"));
        QTest::keyClick(editor, Qt::Key_Escape);
        QVERIFY(!editor->hasInlineSuggestion());
        QCOMPARE(dismissed.count(), 2);
        QCOMPARE(editor->toPlainText(), QStringLiteral("int add(int a, int b) {\n    return xy;\n}\nint after;"));

        // A caret move by the program: dismissed, and the line keeps its highlighting.
        editor->setInlineSuggestion(QStringLiteral(" // done"));
        placeCaret(0, 0);
        QVERIFY(!editor->hasInlineSuggestion());
        QCOMPARE(dismissed.count(), 3);
        QCOMPARE(block(1).layout()->preeditAreaPosition(), -1);
        QVERIFY(!block(1).layout()->formats().isEmpty()); // "return"

        // A click.
        placeCaret(1, 13);
        editor->setInlineSuggestion(QStringLiteral(" // done"));
        QTextCursor elsewhere(block(3));
        QTest::mouseClick(editor->viewport(), Qt::LeftButton, {}, editor->cursorRect(elsewhere).center());
        QVERIFY(!editor->hasInlineSuggestion());
        QCOMPARE(dismissed.count(), 4);
        QVERIFY(!block(1).layout()->formats().isEmpty());

        // An edit elsewhere (here: before it, from the program) dismisses it too.
        placeCaret(1, 13);
        editor->setInlineSuggestion(QStringLiteral(" // done"));
        QTextCursor(block(0)).insertText(QStringLiteral("// top\n"));
        QVERIFY(!editor->hasInlineSuggestion());
        QCOMPARE(dismissed.count(), 5);
        for (QTextBlock b = editor->document()->begin(); b.isValid(); b = b.next()) {
            QCOMPARE(b.layout()->preeditAreaPosition(), -1);
        }

        // Clearing it from the program: no signal.
        placeCaret(2, 13);
        editor->setInlineSuggestion(QStringLiteral(" // done"));
        editor->clearInlineSuggestion();
        QVERIFY(!editor->hasInlineSuggestion());
        QCOMPARE(dismissed.count(), 5);
        QCOMPARE(block(2).layout()->preeditAreaPosition(), -1);

        // Not shown with a selection, a column selection, or read-only.
        QTextCursor selection = editor->textCursor();
        selection.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor);
        editor->setTextCursor(selection);
        editor->setInlineSuggestion(QStringLiteral("x"));
        QVERIFY(!editor->hasInlineSuggestion());
        placeCaret(2, 13);
        editor->setBlockSelection({.anchorLine = 1, .anchorColumn = 1, .cursorLine = 2, .cursorColumn = 1});
        editor->setInlineSuggestion(QStringLiteral("x"));
        QVERIFY(!editor->hasInlineSuggestion());
        editor->clearBlockSelection();
        editor->setReadOnly(true);
        editor->setInlineSuggestion(QStringLiteral("x"));
        QVERIFY(!editor->hasInlineSuggestion());
        editor->setReadOnly(false);

        // A column selection started while one is shown dismisses it.
        placeCaret(2, 13);
        editor->setInlineSuggestion(QStringLiteral("x"));
        QTest::keyClick(editor, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
        QVERIFY(!editor->hasInlineSuggestion());
        QVERIFY(editor->hasBlockSelection());
        editor->clearBlockSelection();

        // Without a suggestion, Tab is a tab and Ctrl+Right moves by word.
        editor->setPlainText(QStringLiteral("ab cd"));
        QTest::keyClick(editor, Qt::Key_Right, Qt::ControlModifier);
        QVERIFY(editor->textCursor().position() > 0);
        QTest::keyClick(editor, Qt::Key_Tab);
        QVERIFY(editor->toPlainText().contains(u'\t'));
    }

    /// EditorFindBar on a CodeEditor: the shortcuts open it in the editor's pane only,
    /// matches are counted and highlighted, Next wraps, Replace goes on to the next
    /// match, Replace All is one undo step, regex groups expand, Esc closes.
    void testEditorFindReplace() {
        QWidget window;
        auto* layout = new QVBoxLayout(&window);
        // The pane holds the editor and its bar; `outside` is another panel of the window.
        auto* pane = new QWidget(&window);
        auto* paneLayout = new QVBoxLayout(pane);
        auto* editor = new ed::CodeEditor(pane);
        auto* bar = new ed::EditorFindBar(
            editor, [editor](const QList<QTextEdit::ExtraSelection>& h) { editor->setSearchHighlights(h); }, pane);
        paneLayout->addWidget(bar);
        paneLayout->addWidget(editor, 1);
        bar->installShortcuts(pane);
        auto* outside = new QLineEdit(&window);
        layout->addWidget(pane, 1);
        layout->addWidget(outside);
        window.resize(900, 600);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        QVERIFY(!bar->isVisible()); // hidden until asked for
        editor->setPlainText(QStringLiteral("select a.id from t a;\nSELECT id_x FROM u;\nselect ID from v;"));
        editor->setFocus();
        QTRY_VERIFY(editor->hasFocus());

        // Ctrl+F in the editor opens the bar; the field takes the typing.
        QTest::keyClick(editor, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(bar->isVisible());
        QVERIFY(!bar->isReplaceMode());
        QTest::keyClicks(bar->findField(), QStringLiteral("select"));
        QCOMPARE(bar->matchCount(), 3);
        QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("select"));
        QCOMPARE(bar->statusText(), QStringLiteral("1 of 3"));
        // Every match is highlighted, next to the editor's own current-line highlight.
        QCOMPARE(editor->extraSelections().size(), 4);
        QVERIFY(bar->findNext());
        QCOMPARE(bar->statusText(), QStringLiteral("2 of 3"));
        QVERIFY(bar->findNext());
        QVERIFY(bar->findNext()); // wraps to the first
        QCOMPARE(editor->textCursor().selectionStart(), 0);
        QVERIFY(bar->findNext(false)); // and back to the last
        QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("select"));
        QCOMPARE(bar->statusText(), QStringLiteral("3 of 3"));
        bar->setOptions({.caseSensitive = true});
        QCOMPARE(bar->matchCount(), 2);
        bar->setOptions({.wholeWord = true});
        bar->findField()->setText(QStringLiteral("id"));
        QCOMPARE(bar->matchCount(), 2); // a.id and ID, not id_x
        bar->setOptions({});
        QCOMPARE(bar->matchCount(), 3);
        bar->findField()->setText(QStringLiteral("nothing like this"));
        QCOMPARE(bar->statusText(), QStringLiteral("No results"));

        // Esc in the field closes the bar (not the window's Cancel) and back to the editor.
        QTest::keyClick(bar->findField(), Qt::Key_Escape);
        QVERIFY(!bar->isVisible());

        // Ctrl+H: the replace row. A one-line selection seeds the search.
        QTextCursor c = editor->textCursor();
        c.setPosition(0);
        c.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
        c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor); // "select" without the space
        editor->setTextCursor(c);
        editor->setFocus();
        QTest::keyClick(editor, Qt::Key_H, Qt::ControlModifier);
        QVERIFY(bar->isVisible() && bar->isReplaceMode());
        QCOMPARE(bar->findField()->text(), QStringLiteral("select"));
        bar->replaceField()->setText(QStringLiteral("SELECT"));
        bar->replaceCurrent(); // the selection is a match: replaced, on to the next
        QVERIFY(editor->toPlainText().startsWith(QStringLiteral("SELECT a.id")));
        QCOMPARE(editor->textCursor().selectedText().toLower(), QStringLiteral("select"));
        const QString beforeAll = editor->toPlainText();
        QCOMPARE(bar->replaceAll(), 3);
        QCOMPARE(bar->statusText(), QStringLiteral("Replaced 3"));
        QCOMPARE(editor->toPlainText().count(QStringLiteral("SELECT")), 3);
        editor->undo(); // Replace All is one step
        QCOMPARE(editor->toPlainText(), beforeAll);

        // Regex with groups.
        bar->setOptions({.regex = true});
        bar->findField()->setText(QStringLiteral("(\\w+)\\.(\\w+)"));
        bar->replaceField()->setText(QStringLiteral("$2_of_$1"));
        QCOMPARE(bar->replaceAll(), 1);
        QVERIFY(editor->toPlainText().contains(QStringLiteral("id_of_a")));
        bar->findField()->setText(QStringLiteral("(unclosed"));
        QCOMPARE(bar->statusText(), QStringLiteral("Invalid regex"));

        // Closing takes the highlights away again.
        bar->closeBar();
        QCOMPARE(editor->extraSelections().size(), 1);

        // Ctrl+F outside the pane is not the editor's: the bar stays closed.
        outside->setFocus();
        QTRY_VERIFY(outside->hasFocus());
        QTest::keyClick(outside, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(!bar->isVisible());

        // The buttons do what the keys do.
        editor->setFocus();
        QTest::keyClick(editor, Qt::Key_F, Qt::ControlModifier);
        bar->setOptions({});
        bar->findField()->setText(QStringLiteral("from"));
        QCOMPARE(bar->statusText(), QStringLiteral("1 of 3"));
        bar->findChild<QToolButton*>(QStringLiteral("FindNext"))->click();
        QCOMPARE(bar->statusText(), QStringLiteral("2 of 3"));
        bar->findChild<QToolButton*>(QStringLiteral("FindPrevious"))->click();
        QCOMPARE(bar->statusText(), QStringLiteral("1 of 3"));
        bar->findChild<QToolButton*>(QStringLiteral("FindClose"))->click();
        QVERIFY(!bar->isVisible());
    }

    // The library carries its own translations; the application picks the language.
    /// replaceDocument(): a document built on another thread is shown, the editor's font,
    /// tab width and wrapping apply to it, column selection and the inline suggestion follow
    /// its edits (and no longer the old document's), the Find bar searches it.
    void testCodeEditorReplaceDocument() {
        QWidget window;
        auto* layout = new QVBoxLayout(&window);
        auto* editor = new ed::CodeEditor(&window);
        auto* bar = new ed::EditorFindBar(
            editor, [editor](const QList<QTextEdit::ExtraSelection>& h) { editor->setSearchHighlights(h); }, &window);
        layout->addWidget(editor, 1);
        layout->addWidget(bar);
        window.resize(900, 600);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        editor->setLineWrapEnabled(false);
        editor->setPlainText(QStringLiteral("old alpha"));
        QPointer<QTextDocument> original = editor->document();
        bar->openFind();
        bar->findField()->setText(QStringLiteral("alpha"));
        QTRY_COMPARE(bar->matchCount(), 1);

        // Refused: no plain-text layout, another thread, null, the same document.
        QTextDocument rich;
        (void)rich.documentLayout(); // the rich-text layout QTextDocument makes by itself
        QVERIFY(!editor->replaceDocument(&rich));
        QVERIFY(!editor->replaceDocument(nullptr));
        QVERIFY(!editor->replaceDocument(editor->document()));
        QThread elsewhere;
        QTextDocument stranger;
        stranger.setDocumentLayout(new QPlainTextDocumentLayout(&stranger));
        stranger.moveToThread(&elsewhere);
        QVERIFY(!editor->replaceDocument(&stranger));
        stranger.moveToThread(QThread::currentThread());
        QCOMPARE(editor->document(), original.data());

        // Built on a worker, as an application loading a large file would.
        QTextDocument* built = nullptr;
        QThread* worker = QThread::create([&built, target = QThread::currentThread()] {
            auto* doc = new QTextDocument;
            doc->setDocumentLayout(new QPlainTextDocumentLayout(doc));
            doc->setPlainText(QStringLiteral("alpha\tbeta alpha\nsecond line\nthird alpha"));
            doc->moveToThread(target);
            built = doc;
        });
        worker->start();
        QVERIFY(worker->wait(5000));
        delete worker;
        QVERIFY(built);
        built->setParent(editor);
        QSignalSpy replaced(editor, &ed::CodeEditor::documentReplaced);
        QVERIFY(editor->replaceDocument(built));
        QCOMPARE(replaced.count(), 1);
        QCOMPARE(editor->document(), built);
        QCOMPARE(editor->toPlainText(), QStringLiteral("alpha\tbeta alpha\nsecond line\nthird alpha"));
        QVERIFY(original.isNull()); // the editor's own first document is gone
        QCOMPARE(built->defaultFont(), editor->font());
        const qreal tab = editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')) * ed::CodeEditor::kTabWidth;
        QCOMPARE(built->defaultTextOption().tabStopDistance(), tab);
        QCOMPARE(built->defaultTextOption().wrapMode(), QTextOption::NoWrap);
        QCOMPARE(editor->document()->blockCount(), 3);

        // The Find bar searched the new text and follows its edits.
        QCOMPARE(bar->matchCount(), 3);
        QTextCursor end(built);
        end.movePosition(QTextCursor::End);
        end.insertText(QStringLiteral(" alpha"));
        QTRY_COMPARE(bar->matchCount(), 4);

        // Column selection ends on an edit of the new document from elsewhere.
        editor->setBlockSelection(ed::block::BlockSelection{0, 0, 1, 3});
        QVERIFY(editor->hasBlockSelection());
        QTextCursor(built).insertText(QStringLiteral("x"));
        QVERIFY(!editor->hasBlockSelection());

        // A replaced child of the editor is deleted (QPlainTextEdit alone would keep it), with
        // its own children; a document someone else owns is not.
        QPointer<QTextDocument> child = built;
        auto* next = new QTextDocument(editor);
        next->setDocumentLayout(new QPlainTextDocumentLayout(next));
        next->setPlainText(QStringLiteral("next"));
        QPointer<QObject> grandchild = new QObject(next);
        QVERIFY(editor->replaceDocument(next));
        QVERIFY(child.isNull());
        QTextDocument foreign;
        foreign.setDocumentLayout(new QPlainTextDocumentLayout(&foreign));
        foreign.setPlainText(QStringLiteral("alpha"));
        QVERIFY(editor->replaceDocument(&foreign));
        QVERIFY(grandchild.isNull()); // `next` went, with its children
        built = new QTextDocument(editor);
        built->setDocumentLayout(new QPlainTextDocumentLayout(built));
        built->setPlainText(QStringLiteral("alpha alpha\nalpha"));
        QVERIFY(editor->replaceDocument(built)); // `foreign` stays its owner's (the stack)
        QCOMPARE(foreign.toPlainText(), QStringLiteral("alpha"));

        // The inline suggestion follows the new document too: an edit elsewhere ends it.
        QTextCursor caret(built);
        caret.movePosition(QTextCursor::End);
        editor->setTextCursor(caret);
        editor->setInlineSuggestion(QStringLiteral("suggested"));
        QVERIFY(editor->hasInlineSuggestion());
        QSignalSpy dismissed(editor, &ed::CodeEditor::inlineSuggestionDismissed);
        QTextCursor(built).insertText(QStringLiteral("y"));
        QCOMPARE(dismissed.count(), 1);
        QVERIFY(!editor->hasInlineSuggestion());

        // A plain QPlainTextEdit's bar re-connects when told to.
        QPlainTextEdit plain;
        ed::EditorFindBar plainBar(
            &plain, [&plain](const QList<QTextEdit::ExtraSelection>& h) { plain.setExtraSelections(h); });
        plain.setPlainText(QStringLiteral("one"));
        plainBar.show();
        plainBar.findField()->setText(QStringLiteral("two"));
        QTRY_COMPARE(plainBar.matchCount(), 0);
        auto* other = new QTextDocument(&plain);
        other->setDocumentLayout(new QPlainTextDocumentLayout(other));
        other->setPlainText(QStringLiteral("two two"));
        plain.setExtraSelections({}); // the old highlights point into the old document
        plain.setDocument(other);
        plainBar.editorDocumentChanged();
        QCOMPARE(plainBar.matchCount(), 2);
        QTextCursor(other).insertText(QStringLiteral("two "));
        QTRY_COMPARE(plainBar.matchCount(), 3);
    }

    void testEditorTranslations() {
        QCOMPARE(
            ed::editorTranslationLanguages(),
            (QStringList{QStringLiteral("de"), QStringLiteral("bar"), QStringLiteral("it")}));
        QTranslator unknown;
        QVERIFY(!ed::loadEditorTranslation(unknown, QStringLiteral("en")));
        QVERIFY(!ed::loadEditorTranslation(unknown, QStringLiteral("xx")));
        QVERIFY(unknown.isEmpty());

        for (const QString& code : ed::editorTranslationLanguages()) {
            QTranslator translator;
            QVERIFY2(ed::loadEditorTranslation(translator, code), qPrintable(code));
            QVERIFY(!translator.isEmpty());
        }

        QTranslator german;
        QVERIFY(ed::loadEditorTranslation(german, QStringLiteral("de")));
        QCoreApplication::installTranslator(&german);
        const auto uninstall = qScopeGuard([&german] { QCoreApplication::removeTranslator(&german); });
        QPlainTextEdit target;
        const ed::EditorFindBar bar(&target, [](const QList<QTextEdit::ExtraSelection>&) {});
        QCOMPARE(bar.findField()->placeholderText(), QStringLiteral("Suchen"));
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    TestEditor testObject;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&testObject, argc, argv);
}

#include "test_editor.moc"
