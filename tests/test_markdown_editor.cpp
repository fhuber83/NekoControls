// Markdown document editor library test suite: src/markdown_editor only.
//
// This target links neko_markdown_editor alone -- and with it the two libraries it
// is made of, neko_editor and neko_markdown -- no engine, no UI. That keeps the
// library usable by other projects: if src/markdown_editor ever includes src/core or
// src/ui, this binary stops linking. It is also the whole test of the library, so it
// can be taken along with it; it needs nothing from tests/.
//
// The preview is the real one (Qt WebEngine, offscreen). The source editor's controls
// are tested in tests/test_editor.cpp, the page in tests/test_markdown.cpp, and what
// Neko Grid makes of the widget -- a note tab with a file, Note Info, export -- in
// tests/test_ui.cpp.

#include "neko/editor/editor_find_bar.hpp"
#include "neko/editor/editor_icons.hpp"
#include "neko/editor/markdown_editor.hpp"
#include "neko/markdown/markdown_preview.hpp"
#include "neko/markdown_editor/markdown_document_editor.hpp"
#include "neko/markdown_editor/markdown_editor_translations.hpp"
#include "neko/markdown_editor/markdown_outline.hpp"
#include "neko/markdown_editor/markdown_structure.hpp"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QPixmap>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolBar>
#include <QTranslator>
#include <QTreeWidget>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QtTest>

namespace mde = neko::markdown_editor;
namespace md = neko::markdown;
namespace ed = neko::editor;

Q_DECLARE_METATYPE(neko::markdown_editor::MarkdownViewMode)

namespace {

constexpr int kRenderTimeoutMs = 60000;

/// Runs `script` in `page` and waits for its result (Qt WebEngine answers
/// asynchronously); an invalid QVariant after `timeoutMs`.
QVariant evalJs(QWebEnginePage* page, const QString& script, int timeoutMs = 10000) {
    QVariant result;
    bool done = false;
    page->runJavaScript(script, [&](const QVariant& v) {
        result = v;
        done = true;
    });
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return result;
}

/// A highlighter an application might bring instead of the built-in one.
class CountingHighlighter final : public QSyntaxHighlighter {
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;
    int blocks = 0;

protected:
    void highlightBlock(const QString&) override { ++blocks; }
};

QTreeWidget* outlineTree(const mde::MarkdownOutline* outline) {
    return outline->findChild<QTreeWidget*>(QStringLiteral("MarkdownOutlineTree"));
}

} // namespace

class TestMarkdownEditor : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<mde::MarkdownViewMode>(); }

    // --- markdown_structure: widget-free ------------------------------------------------

    void testFrontMatterLineCount() {
        QCOMPARE(mde::frontMatterLineCount(QString()), 0);
        QCOMPARE(mde::frontMatterLineCount(QStringLiteral("# Title\n\nText.\n")), 0);
        QCOMPARE(mde::frontMatterLineCount(QStringLiteral("---\ntitle: A\n---\n# Title\n")), 3);
        // "..." closes too, blanks after the dashes are allowed, and so are CRLF line ends.
        QCOMPARE(mde::frontMatterLineCount(QStringLiteral("---  \ntitle: A\ntags: [x]\n...\nBody\n")), 4);
        QCOMPARE(mde::frontMatterLineCount(QStringLiteral("---\r\ntitle: A\r\n---\r\nBody\r\n")), 3);
        // The closing line may be the last one, with or without a line break.
        QCOMPARE(mde::frontMatterLineCount(QStringLiteral("---\ntitle: A\n---")), 3);
        QCOMPARE(mde::frontMatterLineCount(QStringLiteral("---\n---\n")), 2);
        // Without a closing line the dashes are a rule; so are dashes that are not line 1.
        QCOMPARE(mde::frontMatterLineCount(QStringLiteral("---\ntitle: A\nBody\n")), 0);
        QCOMPARE(mde::frontMatterLineCount(QStringLiteral("---")), 0);
        QCOMPARE(mde::frontMatterLineCount(QStringLiteral("\n---\ntitle: A\n---\n")), 0);
        QCOMPARE(mde::frontMatterLineCount(QStringLiteral("----\ntitle: A\n---\n")), 0);
        // A byte order mark before the first line does not hide it.
        QCOMPARE(mde::frontMatterLineCount(QChar(0xFEFF) + QStringLiteral("---\ntitle: A\n---\nBody\n")), 3);
    }

    void testHeadingExtraction() {
        const QString doc = QStringLiteral(
            "---\n"
            "# front matter comment\n"
            "title: Guide\n"
            "---\n"
            "# Introduction\n"
            "Some text.\n\n"
            "## Details ##\n"
            "### Deep Dive\n"
            "##\n"
            "```markdown\n"
            "# Not a heading\n"
            "```\n"
            "~~~\n"
            "## Also not a heading\n"
            "~~~\n"
            "    # Indented code\n"
            "Setext One\n"
            "==========\n"
            "Setext Two\n"
            "----------\n");
        const auto headings = mde::extractMarkdownHeadings(doc);
        QCOMPARE(headings.size(), std::size_t{5});
        QCOMPARE(headings[0].level, 1);
        QCOMPARE(headings[0].text, QStringLiteral("Introduction"));
        QCOMPARE(headings[0].line, 4);
        QVERIFY(doc.mid(headings[0].offset).startsWith(QStringLiteral("# Introduction")));

        QCOMPARE(headings[1].level, 2);
        QCOMPARE(headings[1].text, QStringLiteral("Details"));
        QCOMPARE(headings[1].line, 7);

        QCOMPARE(headings[2].level, 3);
        QCOMPARE(headings[2].text, QStringLiteral("Deep Dive"));
        QCOMPARE(headings[2].line, 8);

        QCOMPARE(headings[3].level, 1);
        QCOMPARE(headings[3].text, QStringLiteral("Setext One"));
        QCOMPARE(headings[3].line, 17);
        QVERIFY(doc.mid(headings[3].offset).startsWith(QStringLiteral("Setext One")));

        QCOMPARE(headings[4].level, 2);
        QCOMPARE(headings[4].text, QStringLiteral("Setext Two"));
        QCOMPARE(headings[4].line, 19);

        // The caller's own idea of where the body starts: from line 0 the front matter's
        // comment line is a heading and "title: Guide" over the closing dashes a Setext one;
        // from line 8 the first two headings are not looked at.
        const auto all = mde::extractMarkdownHeadings(doc, 0);
        QCOMPARE(all.size(), std::size_t{7});
        QCOMPARE(all[0].text, QStringLiteral("front matter comment"));
        QCOMPARE(all[1].text, QStringLiteral("title: Guide"));
        QCOMPARE(all[1].level, 2);
        const auto late = mde::extractMarkdownHeadings(doc, 8);
        QCOMPARE(late.size(), std::size_t{3});
        QCOMPARE(late[0].text, QStringLiteral("Deep Dive"));
        QCOMPARE(late[0].line, 8);

        QVERIFY(mde::extractMarkdownHeadings(QString()).empty());
        QVERIFY(mde::extractMarkdownHeadings(QStringLiteral("- item\n---\n> quote\n===\n")).empty());
    }

    void testDefaultContent() {
        const auto plain = mde::defaultMarkdownContent(QStringLiteral("# A\n\nText.\n"), QStringLiteral("/docs"));
        QCOMPARE(plain.body, QStringLiteral("# A\n\nText.\n"));
        QCOMPARE(plain.bodyStartLine, 0);
        QCOMPARE(plain.baseDirectory, QStringLiteral("/docs"));
        QVERIFY(plain.title.isEmpty() && plain.chips.isEmpty() && plain.linkSchemes.isEmpty());

        // A front matter is not Markdown: the body starts below it.
        const auto withHeader = mde::defaultMarkdownContent(QStringLiteral("---\ntitle: A\n---\n# A\n"));
        QCOMPARE(withHeader.body, QStringLiteral("# A\n"));
        QCOMPARE(withHeader.bodyStartLine, 3);
        QVERIFY(withHeader.baseDirectory.isEmpty());

        const auto headerOnly = mde::defaultMarkdownContent(QStringLiteral("---\ntitle: A\n---"));
        QVERIFY(headerOnly.body.isEmpty());
        QCOMPARE(headerOnly.bodyStartLine, 3);
    }

    // --- MarkdownOutline ---------------------------------------------------------------

    void testOutlineTreeFollowsHeadingLevels() {
        mde::MarkdownOutline outline;
        QTreeWidget* tree = outlineTree(&outline);
        QVERIFY(tree);
        const QString doc = QStringLiteral("# One\n## One A\n### Deep\n## One B\n# Two\n#### Skipped levels\n");
        outline.setHeadings(mde::extractMarkdownHeadings(doc));
        QCOMPARE(tree->topLevelItemCount(), 2);
        QTreeWidgetItem* one = tree->topLevelItem(0);
        QCOMPARE(one->text(0), QStringLiteral("One"));
        QCOMPARE(one->childCount(), 2);
        QCOMPARE(one->child(0)->text(0), QStringLiteral("One A"));
        QCOMPARE(one->child(0)->child(0)->text(0), QStringLiteral("Deep"));
        QCOMPARE(one->child(1)->text(0), QStringLiteral("One B"));
        // A heading that skips levels hangs below the nearest shallower one.
        QCOMPARE(tree->topLevelItem(1)->child(0)->text(0), QStringLiteral("Skipped levels"));
        QVERIFY(one->isExpanded() && one->child(0)->isExpanded());

        // A click hands out where the heading is.
        QSignalSpy jumps(&outline, &mde::MarkdownOutline::jumpRequested);
        emit tree->itemClicked(one->child(1), 0);
        QCOMPARE(jumps.size(), 1);
        QCOMPARE(jumps.at(0).at(0).toLongLong(), doc.indexOf(QStringLiteral("## One B")));
        QCOMPARE(jumps.at(0).at(1).toInt(), 3);

        // What the user collapsed stays collapsed over a new scan.
        one->setExpanded(false);
        outline.setHeadings(mde::extractMarkdownHeadings(doc + QStringLiteral("# Three\n")));
        QCOMPARE(tree->topLevelItemCount(), 3);
        QVERIFY(!tree->topLevelItem(0)->isExpanded());
        QVERIFY(tree->topLevelItem(1)->isExpanded());

        outline.setHeadings({});
        QCOMPARE(tree->topLevelItemCount(), 0);
    }

    // --- MarkdownDocumentEditor --------------------------------------------------------

    void testStartsEmptyInSplitWithItsToolBar() {
        mde::MarkdownDocumentEditor doc;
        QCOMPARE(doc.viewMode(), mde::MarkdownViewMode::Split);
        QVERIFY(doc.text().isEmpty());
        QVERIFY(!doc.isModified());
        QVERIFY(doc.baseDirectory().isEmpty());
        QVERIFY(doc.editor() && doc.findBar() && doc.preview() && doc.outline());
        QVERIFY(doc.editor()->isVisibleTo(&doc));
        QVERIFY(doc.preview()->isVisibleTo(&doc));
        QVERIFY(!doc.outline()->isVisibleTo(&doc));
        QVERIFY(!doc.findBar()->isVisibleTo(&doc));
        QVERIFY(doc.splitAction()->isChecked());
        QVERIFY(!doc.sourceAction()->isChecked() && !doc.previewAction()->isChecked());

        // The tool bar holds the six actions, the views first.
        QVERIFY(doc.toolBar());
        QList<QAction*> shown;
        for (QAction* action : doc.toolBar()->actions()) {
            if (!action->isSeparator()) {
                shown << action;
            }
        }
        const QList<QAction*> expected{
            doc.sourceAction(),
            doc.splitAction(),
            doc.previewAction(),
            doc.tableAction(),
            doc.lineWrapAction(),
            doc.outlineAction()};
        QCOMPARE(shown, expected);
        for (QAction* action : expected) {
            QVERIFY(!action->text().isEmpty());
            QVERIFY2(action->toolTip() != action->text(), qPrintable(action->text()));
        }
        QVERIFY(doc.lineWrapAction()->isCheckable() && doc.outlineAction()->isCheckable());
        QVERIFY(!doc.tableAction()->isCheckable());

        // Without one, the actions are there all the same, for the application's own bar.
        mde::MarkdownDocumentEditor bare(mde::MarkdownToolBar::None);
        QVERIFY(!bare.toolBar());
        QVERIFY(!bare.findChild<QToolBar*>());
        QVERIFY(bare.sourceAction() && bare.tableAction() && bare.outlineAction());
        QVERIFY(bare.actions().contains(bare.cycleViewModeAction()));
        QCOMPARE(bare.cycleViewModeAction()->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    }

    void testActionsTakeTheApplicationsIcons() {
        QStringList asked;
        ed::setEditorIconProvider([&asked](const QString& name) {
            asked << name;
            QPixmap pixmap(16, 16);
            pixmap.fill(Qt::red);
            return QIcon(pixmap);
        });
        const auto restore = qScopeGuard([] { ed::setEditorIconProvider({}); });

        mde::MarkdownDocumentEditor doc;
        for (QAction* action :
             {doc.sourceAction(),
              doc.splitAction(),
              doc.previewAction(),
              doc.tableAction(),
              doc.lineWrapAction(),
              doc.outlineAction()}) {
            QVERIFY2(!action->icon().isNull(), qPrintable(action->text()));
        }
        // Every name this library asks for is one it lists, and it asks for all of them.
        const QStringList names = mde::markdownEditorIconNames();
        QCOMPARE(names.size(), 6);
        for (const QString& name : names) {
            QVERIFY2(asked.contains(name), qPrintable(name));
        }
        for (const QString& name : std::as_const(asked)) {
            QVERIFY2(names.contains(name) || ed::editorIconNames().contains(name), qPrintable(name));
        }

        // And without a provider the actions do with their text.
        ed::setEditorIconProvider({});
        mde::MarkdownDocumentEditor plain;
        QVERIFY(plain.sourceAction()->icon().isNull());
        QVERIFY(!plain.sourceAction()->text().isEmpty());
    }

    void testTextAndModified() {
        mde::MarkdownDocumentEditor doc;
        doc.setText(QStringLiteral("# Title\n"));
        QCOMPARE(doc.text(), QStringLiteral("# Title\n"));
        QVERIFY(!doc.isModified());
        QSignalSpy modified(doc.editor()->document(), &QTextDocument::modificationChanged);

        doc.editor()->appendPlainText(QStringLiteral("More."));
        QVERIFY(doc.isModified());
        QCOMPARE(modified.size(), 1);
        doc.setModified(false); // saved
        QVERIFY(!doc.isModified());
        QCOMPARE(doc.text(), QStringLiteral("# Title\n\nMore."));

        // A new text is a new document: unmodified, and not an edit to undo.
        doc.editor()->appendPlainText(QStringLiteral("Again."));
        QVERIFY(doc.isModified());
        doc.setText(QStringLiteral("Other."));
        QVERIFY(!doc.isModified());
        QVERIFY(!doc.editor()->document()->isUndoAvailable());
    }

    void testPreviewFollowsTheSourceWhileShown() {
        mde::MarkdownDocumentEditor doc;
        // Nothing is rendered for a document that has no text yet.
        QVERIFY(doc.preview()->content().body.isEmpty());

        doc.setText(QStringLiteral("---\ntitle: Orders\n---\n# Orders\n\nText.\n"));
        QCOMPARE(doc.preview()->content().body, QStringLiteral("# Orders\n\nText.\n"));
        QCOMPARE(doc.preview()->content().bodyStartLine, 3);
        QCOMPARE(doc.previewContent().body, doc.preview()->content().body);

        // Split: after the delay, not on every key.
        doc.editor()->appendPlainText(QStringLiteral("More."));
        QVERIFY(!doc.preview()->content().body.contains(QStringLiteral("More.")));
        QVERIFY(doc.previewContent().body.contains(QStringLiteral("More.")));
        QTRY_VERIFY_WITH_TIMEOUT(doc.preview()->content().body.contains(QStringLiteral("More.")), 2000);

        // refreshPreview() does not wait.
        doc.editor()->appendPlainText(QStringLiteral("At once."));
        doc.refreshPreview();
        QVERIFY(doc.preview()->content().body.contains(QStringLiteral("At once.")));

        // Source: nothing is rendered while the preview is hidden; showing it catches up.
        doc.setViewMode(mde::MarkdownViewMode::Source);
        QVERIFY(doc.editor()->isVisibleTo(&doc));
        QVERIFY(!doc.preview()->isVisibleTo(&doc));
        doc.editor()->appendPlainText(QStringLiteral("Hidden edit."));
        QTest::qWait(mde::MarkdownDocumentEditor::kPreviewDelayMs + 100);
        QVERIFY(!doc.preview()->content().body.contains(QStringLiteral("Hidden edit.")));
        doc.setViewMode(mde::MarkdownViewMode::Preview);
        QVERIFY(!doc.editor()->isVisibleTo(&doc));
        QVERIFY(doc.preview()->isVisibleTo(&doc));
        QVERIFY(doc.preview()->content().body.contains(QStringLiteral("Hidden edit.")));
    }

    void testViewModeSignalsActionsAndShortcut() {
        mde::MarkdownDocumentEditor doc;
        QSignalSpy changed(&doc, &mde::MarkdownDocumentEditor::viewModeChanged);
        QSignalSpy chosen(&doc, &mde::MarkdownDocumentEditor::viewModeChosen);

        // The application sets a view: changed, not chosen; and only when it differs.
        doc.setViewMode(mde::MarkdownViewMode::Source);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.at(0).at(0).value<mde::MarkdownViewMode>(), mde::MarkdownViewMode::Source);
        QVERIFY(doc.sourceAction()->isChecked() && !doc.splitAction()->isChecked());
        doc.setViewMode(mde::MarkdownViewMode::Source);
        QCOMPARE(changed.size(), 1);
        QVERIFY(chosen.isEmpty());

        // The user picks one: both.
        doc.previewAction()->trigger();
        QCOMPARE(doc.viewMode(), mde::MarkdownViewMode::Preview);
        QCOMPARE(changed.size(), 2);
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(chosen.at(0).at(0).value<mde::MarkdownViewMode>(), mde::MarkdownViewMode::Preview);
        QVERIFY(doc.previewAction()->isChecked() && !doc.sourceAction()->isChecked());

        // Ctrl+Shift+M: Preview -> Source -> Split -> Preview.
        QAction* cycle = doc.cycleViewModeAction();
        QCOMPARE(cycle->shortcutContext(), Qt::WidgetWithChildrenShortcut);
        cycle->trigger();
        QCOMPARE(doc.viewMode(), mde::MarkdownViewMode::Source);
        cycle->trigger();
        QCOMPARE(doc.viewMode(), mde::MarkdownViewMode::Split);
        cycle->trigger();
        QCOMPARE(doc.viewMode(), mde::MarkdownViewMode::Preview);
        QCOMPARE(chosen.size(), 4);
        QCOMPARE(changed.size(), 5);
    }

    /// Split shows both panes side by side, whichever view came before. The WebEngine
    /// preview hints 0x0, and a splitter shares space out by hint.
    void testSplitShowsBothPanes() {
        for (const auto start :
             {mde::MarkdownViewMode::Source, mde::MarkdownViewMode::Split, mde::MarkdownViewMode::Preview}) {
            mde::MarkdownDocumentEditor doc;
            doc.setViewMode(start);
            doc.resize(1000, 600);
            doc.show();
            QVERIFY(QTest::qWaitForWindowExposed(&doc));
            for (const auto mode :
                 {mde::MarkdownViewMode::Split,
                  mde::MarkdownViewMode::Source,
                  mde::MarkdownViewMode::Split,
                  mde::MarkdownViewMode::Preview,
                  mde::MarkdownViewMode::Split}) {
                doc.setViewMode(mode);
                QApplication::processEvents();
                if (mode == mde::MarkdownViewMode::Split) {
                    QVERIFY2(doc.preview()->width() > 300, qPrintable(QString::number(doc.preview()->width())));
                    QVERIFY2(doc.editor()->width() > 300, qPrintable(QString::number(doc.editor()->width())));
                }
            }
        }
    }

    void testContentProviderAndBaseDirectory() {
        mde::MarkdownDocumentEditor doc;
        doc.setText(QStringLiteral("See [the ticket](issue:42).\n"));
        QVERIFY(doc.preview()->content().linkSchemes.isEmpty());

        // The application's provider: a chip, a title, a link scheme. A shown preview
        // is rendered again with it.
        int calls = 0;
        doc.setContentProvider([&calls](const QString& text, const QString& baseDirectory) {
            ++calls;
            md::MarkdownPreviewContent content = mde::defaultMarkdownContent(text, baseDirectory);
            content.title = QStringLiteral("Demo");
            content.chips = {{.text = QStringLiteral("Project"), .value = QStringLiteral("Demo")}};
            content.linkSchemes = {{.scheme = QStringLiteral("issue"), .marker = QStringLiteral("# ")}};
            return content;
        });
        QVERIFY(calls > 0);
        QCOMPARE(doc.preview()->content().title, QStringLiteral("Demo"));
        QCOMPARE(doc.preview()->content().chips.size(), 1);
        QCOMPARE(doc.preview()->content().linkSchemes.size(), 1);
        QCOMPARE(doc.preview()->content().linkSchemes.at(0).scheme, QStringLiteral("issue"));

        // The folder goes to the provider, and changing it renders again.
        QVERIFY(doc.preview()->content().baseDirectory.isEmpty());
        doc.setBaseDirectory(QStringLiteral("/somewhere/docs"));
        QCOMPARE(doc.baseDirectory(), QStringLiteral("/somewhere/docs"));
        QCOMPARE(doc.preview()->content().baseDirectory, QStringLiteral("/somewhere/docs"));
        const int before = calls;
        doc.setBaseDirectory(QStringLiteral("/somewhere/docs"));
        QCOMPARE(calls, before);

        // Hidden, it is not rendered for a new folder until it shows again.
        doc.setViewMode(mde::MarkdownViewMode::Source);
        doc.setBaseDirectory(QStringLiteral("/elsewhere"));
        QCOMPARE(doc.preview()->content().baseDirectory, QStringLiteral("/somewhere/docs"));
        doc.setViewMode(mde::MarkdownViewMode::Split);
        QCOMPARE(doc.preview()->content().baseDirectory, QStringLiteral("/elsewhere"));

        // An empty provider is the default again.
        doc.setContentProvider({});
        QVERIFY(doc.preview()->content().chips.isEmpty());
        QVERIFY(doc.preview()->content().linkSchemes.isEmpty());
        QCOMPARE(doc.preview()->content().baseDirectory, QStringLiteral("/elsewhere"));

        // The preview's link signals come out of the widget.
        QSignalSpy custom(&doc, &mde::MarkdownDocumentEditor::customLinkActivated);
        QSignalSpy other(&doc, &mde::MarkdownDocumentEditor::documentLinkActivated);
        QSignalSpy local(&doc, &mde::MarkdownDocumentEditor::localLinkActivated);
        emit doc.preview()->localLinkActivated(QStringLiteral("other.md"));
        QCOMPARE(local.size(), 1);
        QCOMPARE(local.at(0).at(0).toString(), QStringLiteral("other.md"));
        emit doc.preview()->customLinkActivated(QStringLiteral("issue:42"));
        emit doc.preview()->documentLinkActivated(QStringLiteral("/elsewhere/other.md"));
        QCOMPARE(custom.size(), 1);
        QCOMPARE(custom.at(0).at(0).toString(), QStringLiteral("issue:42"));
        QCOMPARE(other.size(), 1);
        QCOMPARE(other.at(0).at(0).toString(), QStringLiteral("/elsewhere/other.md"));
    }

    void testInsertTableSeparatesBlocks() {
        mde::MarkdownDocumentEditor doc;
        doc.setViewMode(mde::MarkdownViewMode::Source);
        const QString table = QStringLiteral("| A |\n| --- |\n");

        // Mid-paragraph: blank line before and after.
        doc.setText(QStringLiteral("Before.\nAfter."));
        QTextCursor c = doc.editor()->textCursor();
        c.setPosition(7); // end of "Before."
        doc.editor()->setTextCursor(c);
        doc.insertTableMarkdown(table);
        QCOMPARE(doc.text(), QStringLiteral("Before.\n\n| A |\n| --- |\n\nAfter."));
        QVERIFY(doc.isModified());
        // One undo step.
        doc.editor()->undo();
        QCOMPARE(doc.text(), QStringLiteral("Before.\nAfter."));

        // In the middle of a line the rest becomes its own paragraph.
        c = doc.editor()->textCursor();
        c.setPosition(3);
        doc.editor()->setTextCursor(c);
        doc.insertTableMarkdown(table);
        QCOMPARE(doc.text(), QStringLiteral("Bef\n\n| A |\n| --- |\n\nore.\nAfter."));

        // A selection is replaced.
        doc.setText(QStringLiteral("Old"));
        doc.editor()->selectAll();
        doc.insertTableMarkdown(table);
        QCOMPARE(doc.text(), table);

        // Empty document: just the table. An empty table: nothing.
        doc.setText(QString());
        doc.insertTableMarkdown(table);
        QCOMPARE(doc.text(), table);
        doc.insertTableMarkdown(QString());
        QCOMPARE(doc.text(), table);
        QCOMPARE(doc.viewMode(), mde::MarkdownViewMode::Source);

        // From Preview the view changes, so the user sees where the table went.
        doc.setViewMode(mde::MarkdownViewMode::Preview);
        doc.insertTableMarkdown(table);
        QCOMPARE(doc.viewMode(), mde::MarkdownViewMode::Split);
    }

    void testOutlineFollowsTheSourceAndJumps() {
        mde::MarkdownDocumentEditor doc;
        doc.resize(1000, 700);
        doc.show();
        QVERIFY(QTest::qWaitForWindowExposed(&doc));
        doc.setText(QStringLiteral(
            "---\ntitle: Guide\n---\n"
            "# Overview\nIntro text.\n\n"
            "## Setup\nSteps.\n\n"
            "### Advanced\nDetails.\n"));

        QTreeWidget* tree = outlineTree(doc.outline());
        QVERIFY(tree);
        QVERIFY(!doc.outline()->isVisible());
        QCOMPARE(tree->topLevelItemCount(), 0); // not scanned while hidden
        doc.outlineAction()->setChecked(true);
        QVERIFY(doc.outline()->isVisible());
        QCOMPARE(tree->topLevelItemCount(), 1);
        QTreeWidgetItem* overview = tree->topLevelItem(0);
        QCOMPARE(overview->text(0), QStringLiteral("Overview"));
        QCOMPARE(overview->child(0)->text(0), QStringLiteral("Setup"));
        QCOMPARE(overview->child(0)->child(0)->text(0), QStringLiteral("Advanced"));

        // A click moves the source's cursor to the heading.
        emit tree->itemClicked(overview->child(0)->child(0), 0);
        QCOMPARE(doc.editor()->textCursor().block().text(), QStringLiteral("### Advanced"));
        // In Preview the source stays where it is (the preview scrolls).
        doc.setViewMode(mde::MarkdownViewMode::Preview);
        emit tree->itemClicked(tree->topLevelItem(0), 0);
        QCOMPARE(doc.editor()->textCursor().block().text(), QStringLiteral("### Advanced"));
        doc.setViewMode(mde::MarkdownViewMode::Split);

        // Edits arrive after the delay; a new text at once.
        doc.editor()->appendPlainText(QStringLiteral("# Appendix"));
        QCOMPARE(tree->topLevelItemCount(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(tree->topLevelItemCount(), 2, 3000);
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Appendix"));
        doc.setText(QStringLiteral("# Only\n"));
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Only"));

        // Headings are looked for where the provider says the body starts.
        doc.setContentProvider([](const QString& text, const QString& baseDirectory) {
            md::MarkdownPreviewContent content{.body = text.section(QLatin1Char('\n'), 2), .bodyStartLine = 2};
            content.baseDirectory = baseDirectory;
            return content;
        });
        doc.setText(QStringLiteral("# Header line\nmeta\n# Body\n"));
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Body"));

        doc.outlineAction()->setChecked(false);
        QVERIFY(!doc.outline()->isVisible());
    }

    void testLineWrapActionIsTheEditors() {
        mde::MarkdownDocumentEditor doc;
        QCOMPARE(doc.lineWrapAction()->isChecked(), doc.editor()->isLineWrapEnabled());
        const bool start = doc.editor()->isLineWrapEnabled();
        doc.lineWrapAction()->trigger();
        QCOMPARE(doc.editor()->isLineWrapEnabled(), !start);
        doc.editor()->setLineWrapEnabled(start); // Alt+Z, or the application's setting
        QCOMPARE(doc.lineWrapAction()->isChecked(), start);
    }

    void testHighlighterCanBeReplaced() {
        mde::MarkdownDocumentEditor doc;
        const QPointer<QSyntaxHighlighter> builtIn = doc.findChild<QSyntaxHighlighter*>();
        QVERIFY(builtIn);
        QCOMPARE(builtIn->document(), doc.editor()->document());

        auto* mine = new CountingHighlighter(static_cast<QObject*>(nullptr));
        doc.setHighlighter(mine);
        QVERIFY(!builtIn); // gone, not a second one on the same document
        QCOMPARE(mine->document(), doc.editor()->document());
        QCOMPARE(mine->parent(), &doc);
        doc.setText(QStringLiteral("# A\n\nB\n"));
        QVERIFY(mine->blocks > 0);

        // A palette change must not reach for the highlighter that is gone.
        QPalette dark = doc.palette();
        dark.setColor(QPalette::Base, Qt::black);
        doc.setPalette(dark);
        QApplication::processEvents();

        const QPointer<QSyntaxHighlighter> second = mine;
        doc.setHighlighter(nullptr);
        QVERIFY(!second);
        doc.setText(QStringLiteral("plain\n"));
        QCOMPARE(doc.text(), QStringLiteral("plain\n"));
    }

    /// The whole thing with the real page: the source is rendered, the front matter is
    /// not, and the preview comes up again after an edit.
    void testRendersInWebEngine() {
        mde::MarkdownDocumentEditor doc;
        doc.resize(1000, 700);
        doc.show();
        QVERIFY(QTest::qWaitForWindowExposed(&doc));
        QSignalSpy rendered(doc.preview(), &md::MarkdownPreview::renderFinished);
        doc.setText(QStringLiteral("---\ntitle: Hidden header\n---\n# Shown heading\n\nA paragraph.\n"));
        QTRY_VERIFY_WITH_TIMEOUT(rendered.size() >= 1, kRenderTimeoutMs);

        auto* view = doc.preview()->findChild<QWebEngineView*>();
        QVERIFY(view);
        const QString heading =
            evalJs(view->page(), QStringLiteral("(document.querySelector('h1') || {textContent: ''}).textContent"))
                .toString();
        QCOMPARE(heading.trimmed(), QStringLiteral("Shown heading"));
        const QString body = evalJs(view->page(), QStringLiteral("document.body.innerText")).toString();
        QVERIFY2(body.contains(QStringLiteral("A paragraph.")), qPrintable(body));
        QVERIFY2(!body.contains(QStringLiteral("Hidden header")), qPrintable(body));

        const qsizetype before = rendered.size();
        doc.editor()->appendPlainText(QStringLiteral("Typed later."));
        QTRY_VERIFY_WITH_TIMEOUT(rendered.size() > before, kRenderTimeoutMs);
        const QString after = evalJs(view->page(), QStringLiteral("document.body.innerText")).toString();
        QVERIFY2(after.contains(QStringLiteral("Typed later.")), qPrintable(after));
    }

    // The library carries its own translations; the application picks the language.
    void testMarkdownEditorTranslations() {
        QTranslator unknown;
        QVERIFY(!mde::loadMarkdownEditorTranslation(unknown, QStringLiteral("en")));
        QVERIFY(unknown.isEmpty());
        for (const QString& code : mde::markdownEditorTranslationLanguages()) {
            QTranslator translator;
            QVERIFY2(mde::loadMarkdownEditorTranslation(translator, code), qPrintable(code));
        }

        QTranslator german;
        QVERIFY(mde::loadMarkdownEditorTranslation(german, QStringLiteral("de")));
        QCoreApplication::installTranslator(&german);
        const auto uninstall = qScopeGuard([&german] { QCoreApplication::removeTranslator(&german); });
        const mde::MarkdownDocumentEditor doc;
        QCOMPARE(doc.sourceAction()->text(), QStringLiteral("Quelltext"));
        QCOMPARE(doc.splitAction()->text(), QStringLiteral("Geteilt"));
    }
};

int main(int argc, char* argv[]) {
    // Offscreen from a desktop session, Qt's offscreen platform takes GLX from the X
    // server in DISPLAY -- which some drivers refuse with a fatal X error the first time
    // Qt WebEngine starts. The tests need no GLX.
    if (!qEnvironmentVariableIsSet("QT_QPA_OFFSCREEN_NO_GLX")) {
        qputenv("QT_QPA_OFFSCREEN_NO_GLX", QByteArrayLiteral("1"));
    }
    // What every application using the preview does before its QApplication exists.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    neko::markdown::registerMarkdownPreviewScheme();
    QApplication app(argc, argv);
    TestMarkdownEditor testObject;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&testObject, argc, argv);
}

#include "test_markdown_editor.moc"
