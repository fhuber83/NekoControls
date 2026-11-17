#include "neko/markdown/markdown_preview.hpp"

#include "neko/markdown/markdown_page.hpp"

#include <QEvent>
#include <QVBoxLayout>
#include <QWebEngineView>

namespace neko::markdown {

namespace {

bool isDark(const QWidget* w) {
    return w->palette().color(QPalette::Base).lightness() < 128;
}

/// The preview: a MarkdownPage in a QWebEngineView.
class WebEngineMarkdownPreview final : public MarkdownPreview {
public:
    explicit WebEngineMarkdownPreview(QWidget* parent)
        : MarkdownPreview(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        view_ = new QWebEngineView(this);
        view_->setContextMenuPolicy(Qt::NoContextMenu);
        page_ = new MarkdownPage(view_);
        view_->setPage(page_);
        layout->addWidget(view_);
        connect(page_, &MarkdownPage::customLinkActivated, this, &MarkdownPreview::customLinkActivated);
        connect(page_, &MarkdownPage::documentLinkActivated, this, &MarkdownPreview::documentLinkActivated);
        connect(page_, &MarkdownPage::localLinkActivated, this, &MarkdownPreview::localLinkActivated);
        connect(page_, &MarkdownPage::renderFinished, this, &MarkdownPreview::renderFinished);
    }

    ~WebEngineMarkdownPreview() override {
        // The page goes before the shared profile can.
        delete view_;
    }

    void scrollToSourceLine(int line) override { page_->scrollToSourceLine(line); }

protected:
    void render(const MarkdownPreviewContent& content) override {
        const bool dark =
            !content.backgroundColor.isEmpty() ? (QColor(content.backgroundColor).lightness() < 128) : isDark(this);
        if (!content.backgroundColor.isEmpty()) {
            view_->page()->setBackgroundColor(QColor(content.backgroundColor));
        } else {
            view_->page()->setBackgroundColor(palette().color(QPalette::Base));
        }
        page_->render(content, dark);
    }

    void changeEvent(QEvent* event) override {
        MarkdownPreview::changeEvent(event);
        if (event->type() == QEvent::PaletteChange) {
            page_->render(content(), isDark(this)); // light <-> dark
        }
    }

private:
    QWebEngineView* view_ = nullptr;
    MarkdownPage* page_ = nullptr;
};

} // namespace

void MarkdownPreview::setContent(MarkdownPreviewContent content) {
    content_ = std::move(content);
    render(content_);
}

MarkdownPreview* createMarkdownPreview(QWidget* parent) {
    return new WebEngineMarkdownPreview(parent);
}

} // namespace neko::markdown
