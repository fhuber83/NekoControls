#pragma once

#include "editor/markdown_editor.hpp"
#include "markdown/markdown_preview.hpp"

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QAction;
class QActionGroup;
class QSplitter;
class QSyntaxHighlighter;
class QTimer;
class QToolBar;

namespace nekogrid::editor {
class EditorFindBar;
class MarkdownHighlighter;
} // namespace nekogrid::editor

namespace nekogrid::markdown_editor {

class MarkdownOutline;

/// How a MarkdownDocumentEditor shows its document.
enum class MarkdownViewMode : int {
    Source = 0,  ///< the source editor alone
    Split = 1,   ///< source left, preview right, scrolled together
    Preview = 2, ///< the rendered document alone
};

/// Whether a MarkdownDocumentEditor has a tool bar of its own.
enum class MarkdownToolBar {
    BuiltIn, ///< a QToolBar above the document with the widget's actions
    None,    ///< none: the application puts the actions into a bar or menu of its own
};

/// Builds what the preview shows from the source text and the document's folder ("" for
/// a document that was never saved). Called on the UI thread, on every refresh.
using MarkdownContentProvider =
    std::function<markdown::MarkdownPreviewContent(const QString& text, const QString& baseDirectory)>;

/// What the preview shows unless the application says otherwise: the text below its
/// front matter (frontMatterLineCount()) as the body, the line it starts on, and the
/// folder. No chips, no title, no link schemes.
[[nodiscard]] markdown::MarkdownPreviewContent
defaultMarkdownContent(const QString& text, const QString& baseDirectory = {});

/// The icons this library's actions ask the application for, through the editor
/// library's provider (editor::setEditorIconProvider): "note-source", "note-split",
/// "preview" for the three views, "insert-table", "line-wrap", "outline". Without a
/// provider, or where it returns a null icon, an action has its text alone. For an
/// application's test that its icon set has them all.
[[nodiscard]] QStringList markdownEditorIconNames();

/// One Markdown document, edited: its source in an editor::MarkdownEditor (gutter,
/// column selection, emoji picker, Find / Replace bar, highlighting), its rendering in
/// a markdown::MarkdownPreview, side by side or one at a time, and an outline of its
/// headings that can be shown at the left.
///
/// The widget holds text, not a file: setText() / text() / isModified() are the
/// document, and opening, saving and naming it are the application's. What the widget
/// needs to know of the file is its folder (setBaseDirectory()), which relative images
/// and links resolve against.
///
/// **Preview.** It follows the source after kPreviewDelayMs of quiet, and only while it
/// is shown; switching to a view that shows it brings it up to date first. What it
/// gets is built by the content provider -- defaultMarkdownContent() unless
/// setContentProvider() replaced it, which is how an application adds chips, a title
/// or its own link schemes. In Split the preview scrolls with the source. A click on
/// one of the content's link schemes comes out as customLinkActivated(), a link to
/// another Markdown file as documentLinkActivated().
///
/// **Outline.** Hidden until outlineAction() is checked; it follows the source after
/// kOutlineDelayMs of quiet. A click on a heading moves the source's cursor there and
/// scrolls the preview to it, whichever of the two is shown. Headings are looked for
/// from the line the provider's content says the body starts on.
///
/// **Actions.** The widget owns an action per view (checkable, exclusive), "Table...",
/// "Wrap" and "Outline". With MarkdownToolBar::BuiltIn they sit in toolBar(); an
/// application may add its own actions there. With MarkdownToolBar::None it places
/// them itself. Ctrl+Shift+M cycles the views while the focus is inside the widget
/// (cycleViewModeAction(); add it to an enclosing widget to widen that), and the Find
/// bar's Ctrl+F / Ctrl+H / F3 work while it is in the source pane.
///
/// The widget stores nothing. A view chosen by the user (an action, the shortcut) is
/// announced by viewModeChosen(), for an application that wants to remember it;
/// setViewMode() is not.
///
/// UI thread only. The application must have done what markdown::MarkdownPreview asks
/// for before its QApplication exists (markdown::registerMarkdownPreviewScheme(),
/// Qt::AA_ShareOpenGLContexts).
class MarkdownDocumentEditor : public QWidget {
    Q_OBJECT

public:
    static constexpr int kPreviewDelayMs = 250;
    static constexpr int kOutlineDelayMs = 500;

    /// Starts empty, unmodified, in Split, with a tool bar of its own.
    explicit MarkdownDocumentEditor(QWidget* parent = nullptr);
    explicit MarkdownDocumentEditor(MarkdownToolBar toolBar, QWidget* parent = nullptr);

    // --- The parts ---------------------------------------------------------------------

    /// The source editor. Its document is the text: connect to it for
    /// modificationChanged() or contentsChanged().
    [[nodiscard]] editor::MarkdownEditor* editor() const noexcept { return editor_; }
    /// The source's Find / Replace bar, hidden until Ctrl+F / Ctrl+H.
    [[nodiscard]] editor::EditorFindBar* findBar() const noexcept { return findBar_; }
    [[nodiscard]] markdown::MarkdownPreview* preview() const noexcept { return preview_; }
    [[nodiscard]] MarkdownOutline* outline() const noexcept { return outline_; }
    /// The widget's own tool bar; null with MarkdownToolBar::None.
    [[nodiscard]] QToolBar* toolBar() const noexcept { return toolBar_; }

    // --- The actions -------------------------------------------------------------------

    [[nodiscard]] QAction* sourceAction() const noexcept { return sourceAct_; }
    [[nodiscard]] QAction* splitAction() const noexcept { return splitAct_; }
    [[nodiscard]] QAction* previewAction() const noexcept { return previewAct_; }
    /// "Table...": insertTable().
    [[nodiscard]] QAction* tableAction() const noexcept { return tableAct_; }
    /// Checkable; follows and sets editor()->isLineWrapEnabled() for this document.
    [[nodiscard]] QAction* lineWrapAction() const noexcept { return wrapAct_; }
    /// Checkable; shows and hides outline().
    [[nodiscard]] QAction* outlineAction() const noexcept { return outlineAct_; }
    /// Source -> Split -> Preview -> Source, Ctrl+Shift+M. Has no text or icon: it is a
    /// shortcut, already added to this widget.
    [[nodiscard]] QAction* cycleViewModeAction() const noexcept { return cycleAct_; }

    // --- The document ------------------------------------------------------------------

    /// Replaces the text (opening a file): the preview and a shown outline follow at
    /// once, the undo history is dropped, and the document counts as unmodified.
    void setText(const QString& text);
    [[nodiscard]] QString text() const;
    /// Changes since setText() or the last setModified(false).
    [[nodiscard]] bool isModified() const;
    /// setModified(false) is what an application calls after saving.
    void setModified(bool modified);

    /// The document's folder; "" until set.
    [[nodiscard]] const QString& baseDirectory() const noexcept { return baseDirectory_; }
    /// A shown preview is rendered again, since relative images resolve differently.
    void setBaseDirectory(const QString& directory);

    // --- The views ---------------------------------------------------------------------

    [[nodiscard]] MarkdownViewMode viewMode() const noexcept { return mode_; }
    /// Shows `mode` and gives its pane the focus. Emits viewModeChanged() when it
    /// differs, never viewModeChosen().
    void setViewMode(MarkdownViewMode mode);

    // --- The preview -------------------------------------------------------------------

    /// Replaces the content provider; an empty one restores defaultMarkdownContent(). A
    /// shown preview is rendered again.
    void setContentProvider(MarkdownContentProvider provider);
    /// What the preview gets for the text as it is now.
    [[nodiscard]] markdown::MarkdownPreviewContent previewContent() const;
    /// Renders now instead of after the delay.
    void refreshPreview();

    // --- Editing -----------------------------------------------------------------------

    /// Asks for columns, titles, alignment and a row count (editor::MarkdownTableDialog)
    /// and inserts the table at the cursor.
    void insertTable();
    /// Inserts `table` at the cursor, replacing a selection, as one undo step. It is set
    /// off from the text around it by blank lines, and the cursor ends up after it. In
    /// Preview the view changes to Split, so the user sees where it went. Nothing
    /// happens for an empty string.
    void insertTableMarkdown(const QString& table);

    /// Replaces the source's highlighter -- an editor::MarkdownHighlighter that follows
    /// the palette -- with `highlighter`, which must not be attached to a document yet;
    /// the widget attaches it and takes ownership. Null leaves the source without one.
    void setHighlighter(QSyntaxHighlighter* highlighter);

signals:
    void viewModeChanged(nekogrid::markdown_editor::MarkdownViewMode mode);
    /// The user picked a view: one of the three actions or Ctrl+Shift+M.
    void viewModeChosen(nekogrid::markdown_editor::MarkdownViewMode mode);
    /// A link of one of the content's link schemes was clicked in the preview
    /// (markdown::MarkdownPreview::customLinkActivated).
    void customLinkActivated(const QString& url);
    /// A link to another Markdown file was clicked in the preview
    /// (markdown::MarkdownPreview::documentLinkActivated).
    void documentLinkActivated(const QString& path);

protected:
    void changeEvent(QEvent* event) override;

private:
    void chooseViewMode(MarkdownViewMode mode);
    void applyViewMode();
    void invalidatePreview();
    void onTextChanged();
    void refreshOutline();
    void syncPreviewScroll();
    void jumpTo(qsizetype offset, int line);

    editor::MarkdownEditor* editor_ = nullptr;
    QWidget* editorPane_ = nullptr; ///< the editor and its find bar, hidden in Preview
    editor::EditorFindBar* findBar_ = nullptr;
    markdown::MarkdownPreview* preview_ = nullptr;
    MarkdownOutline* outline_ = nullptr;
    QToolBar* toolBar_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QSyntaxHighlighter* highlighter_ = nullptr;
    editor::MarkdownHighlighter* ownHighlighter_ = nullptr; ///< highlighter_ while it is the built-in one
    QTimer* previewTimer_ = nullptr;
    QTimer* outlineTimer_ = nullptr;
    QActionGroup* modeGroup_ = nullptr;
    QAction* sourceAct_ = nullptr;
    QAction* splitAct_ = nullptr;
    QAction* previewAct_ = nullptr;
    QAction* tableAct_ = nullptr;
    QAction* wrapAct_ = nullptr;
    QAction* outlineAct_ = nullptr;
    QAction* cycleAct_ = nullptr;
    MarkdownContentProvider provider_;
    QString baseDirectory_;
    MarkdownViewMode mode_ = MarkdownViewMode::Split;
    bool previewStale_ = true; ///< the preview does not show the text as it is now
};

} // namespace nekogrid::markdown_editor
