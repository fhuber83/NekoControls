#include "neko/markdown/markdown_page.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>

// The page, its script and the bundled markdown-it, KaTeX, Mermaid and MathJax
// (CMakeLists.txt, markdown_preview.qrc.in). Required, not belt-and-braces: resources
// in a STATIC library register only when their initializer is referenced, and the
// macro must sit at global scope -- inside a namespace it declares the initializer
// there and fails to link.
static void ensureMarkdownPreviewResourcesInitialized() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(neko_markdown_preview);
        return true;
    }();
    (void)initialized;
}

namespace neko::markdown {

namespace {

const QUrl kPageUrl(QStringLiteral("qrc:/neko/markdown/preview.html"));
const QByteArray kNoteScheme = QByteArrayLiteral("neko-note");
const QString kScriptSource = QStringLiteral("qrc:/neko/markdown/preview.js");

/// The image types a document may show. The scheme serves nothing else, so even a
/// request the filter let through cannot read a document's neighbouring files as text.
QByteArray imageMimeType(const QString& path) {
    static const std::pair<const char*, const char*> types[] = {
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"webp", "image/webp"},
        {"svg", "image/svg+xml"},
        {"bmp", "image/bmp"},
    };
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const auto& [ext, mime] : types) {
        if (suffix == QLatin1String(ext)) {
            return QByteArray(mime);
        }
    }
    return {};
}

/// Serves the document's images: `<base href>` is its folder in this scheme, so a
/// relative `![x](img/a.png)` arrives here. Which folder a page may read is the
/// request filter's business; this only refuses what is not an existing image.
class NoteFileSchemeHandler final : public QWebEngineUrlSchemeHandler {
public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;

    void requestStarted(QWebEngineUrlRequestJob* job) override {
        const QString path = MarkdownPage::localPathOf(job->requestUrl());
        const QByteArray mime = imageMimeType(path);
        if (mime.isEmpty()) {
            job->fail(QWebEngineUrlRequestJob::RequestDenied);
            return;
        }
        auto* file = new QFile(path, job);
        if (!file->open(QIODevice::ReadOnly)) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        job->reply(mime, file);
    }
};

/// One profile for every page: off the record (no cache, cookies or storage on
/// disk), with everything a document's page does not need switched off.
QWebEngineProfile* markdownProfile() {
    static QWebEngineProfile* profile = [] {
        auto* p = new QWebEngineProfile(QCoreApplication::instance()); // no storage name: off the record
        p->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
        p->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
        auto* s = p->settings();
        s->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
        s->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false); // images: neko-note:
        s->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
        s->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
        s->setAttribute(QWebEngineSettings::LocalStorageEnabled, false);
        s->setAttribute(QWebEngineSettings::PluginsEnabled, false);
        s->setAttribute(QWebEngineSettings::NavigateOnDropEnabled, false);
        s->setAttribute(QWebEngineSettings::ErrorPageEnabled, false);
        p->installUrlSchemeHandler(kNoteScheme, new NoteFileSchemeHandler(p));
        return p;
    }();
    return profile;
}

/// The document's folder as a `neko-note:` URL ending in "/", or "" for none.
QString noteBaseUrl(const QString& dir) {
    if (dir.isEmpty()) {
        return {};
    }
    QUrl url;
    url.setScheme(QString::fromLatin1(kNoteScheme));
    url.setPath(QUrl::fromLocalFile(QDir(dir).absolutePath() + QLatin1Char('/')).path());
    return url.toString(QUrl::FullyEncoded);
}

} // namespace

/// What a page may load: its own resources, data: and blob: URLs, and images below the
/// document's folder through `neko-note:`. Nothing from the network, no file: URLs.
class MarkdownRequestFilter final : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;

    void setAllowedDirectory(const QString& dir) {
        const QMutexLocker lock(&mutex_);
        allowedDir_ = dir.isEmpty() ? QString() : QDir::cleanPath(QDir(dir).absolutePath());
    }

    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        const QUrl url = info.requestUrl();
        const QString scheme = url.scheme().toLower();
        if (scheme == QLatin1String("qrc") || scheme == QLatin1String("data") || scheme == QLatin1String("blob")) {
            return;
        }
        if (scheme == QLatin1String(kNoteScheme)) {
            const QMutexLocker lock(&mutex_);
            if (!allowedDir_.isEmpty() && MarkdownPage::localPathOf(url).startsWith(allowedDir_ + QLatin1Char('/'))) {
                return;
            }
        }
        info.block(true);
    }

private:
    QMutex mutex_;
    QString allowedDir_;
};

MarkdownPage::MarkdownPage(QObject* parent)
    : QWebEnginePage(markdownProfile(), parent),
      filter_(new MarkdownRequestFilter(this)) {
    ensureMarkdownPreviewResourcesInitialized();
    setUrlRequestInterceptor(filter_);
    connect(this, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        loaded_ = ok;
        if (!ok) {
            return;
        }
        for (const QString& script : std::exchange(pendingScripts_, {})) {
            runJavaScript(script);
        }
    });
    connect(this, &QWebEnginePage::titleChanged, this, [this](const QString& title) {
        if (title.startsWith(QLatin1String("rendered:"))) {
            if (lastLine_ >= 0) {
                scrollToSourceLine(lastLine_);
            }
            emit renderFinished();
        } else if (title.startsWith(QLatin1String("exported:"))) {
            runJavaScript(QStringLiteral("window.nekoExportResult"), [this](const QVariant& v) {
                const QVariantMap result = v.toMap();
                QList<MarkdownExportImage> images;
                for (const QVariant& item : result.value(QStringLiteral("images")).toList()) {
                    MarkdownExportImage img;
                    if (item.typeId() == QMetaType::QVariantMap) {
                        const QVariantMap m = item.toMap();
                        const QString s = m.value(QStringLiteral("png")).toString();
                        const qsizetype comma = s.indexOf(QLatin1Char(','));
                        if (comma >= 0) {
                            img.png = QByteArray::fromBase64(s.mid(comma + 1).toLatin1());
                        }
                        img.svg = m.value(QStringLiteral("svg")).toString().toUtf8();
                    } else {
                        const QString s = item.toString();
                        const qsizetype comma = s.indexOf(QLatin1Char(','));
                        if (comma >= 0) {
                            img.png = QByteArray::fromBase64(s.mid(comma + 1).toLatin1());
                        }
                    }
                    images << img;
                }
                emit exportContentReady(result.value(QStringLiteral("html")).toString(), images);
            });
        }
    });
    load(kPageUrl);
}

QUrl MarkdownPage::pageUrl() {
    return kPageUrl;
}

bool MarkdownPage::isCustomLinkScheme(const QString& scheme) {
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*$"));
    static const QStringList own{
        QStringLiteral("http"),
        QStringLiteral("https"),
        QStringLiteral("mailto"),
        QStringLiteral("file"),
        QStringLiteral("qrc"),
        QStringLiteral("data"),
        QStringLiteral("blob"),
        QStringLiteral("javascript"),
        QStringLiteral("vbscript"),
        QString::fromLatin1(kNoteScheme),
    };
    return valid.match(scheme).hasMatch() && !own.contains(scheme.toLower());
}

QString MarkdownPage::localPathOf(const QUrl& url) {
    if (url.scheme().compare(QLatin1String(kNoteScheme), Qt::CaseInsensitive) != 0) {
        return {};
    }
    // neko-note:/home/me/a.png -> /home/me/a.png; Windows: neko-note:/C:/a.png -> C:/a.png
    QUrl file;
    file.setScheme(QStringLiteral("file"));
    file.setPath(url.path());
    return QDir::cleanPath(file.toLocalFile());
}

void MarkdownPage::run(const QString& script) {
    if (loaded_) {
        runJavaScript(script);
    } else {
        pendingScripts_ << script;
    }
}

void MarkdownPage::render(const MarkdownPreviewContent& c, bool dark, Mode mode) {
    filter_->setAllowedDirectory(c.baseDirectory);
    QJsonArray chips;
    for (const MarkdownChip& chip : c.chips) {
        chips.append(
            QJsonObject{
                {QStringLiteral("text"), chip.text},
                {QStringLiteral("value"), chip.value},
                {QStringLiteral("toolTip"), chip.toolTip},
                {QStringLiteral("dashed"), chip.dashed},
            });
    }
    QJsonArray schemes;
    for (const MarkdownLinkScheme& scheme : c.linkSchemes) {
        if (isCustomLinkScheme(scheme.scheme)) {
            schemes.append(
                QJsonObject{
                    {QStringLiteral("scheme"), scheme.scheme.toLower()},
                    {QStringLiteral("marker"), scheme.marker},
                });
        }
    }
    const QJsonObject payload{
        {QStringLiteral("body"), c.body},
        {QStringLiteral("bodyStartLine"), c.bodyStartLine},
        {QStringLiteral("baseUrl"), noteBaseUrl(c.baseDirectory)},
        {QStringLiteral("dark"), dark},
        {QStringLiteral("export"), mode == Mode::Export},
        {QStringLiteral("chips"), chips},
        {QStringLiteral("linkSchemes"), schemes},
        {QStringLiteral("backgroundColor"), c.backgroundColor},
    };
    // A newer render replaces one still waiting for the page.
    pendingScripts_.removeIf([](const QString& s) { return s.startsWith(QLatin1String("window.nekoRender(")); });
    run(QStringLiteral("window.nekoRender(") +
        QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)) + QStringLiteral(")"));
}

void MarkdownPage::scrollToSourceLine(int line) {
    lastLine_ = line;
    if (loaded_) {
        runJavaScript(QStringLiteral("window.nekoScrollToLine(%1)").arg(line));
    }
}

void MarkdownPage::requestExportContent() {
    run(QStringLiteral("window.nekoExportContent()"));
}

void MarkdownPage::javaScriptConsoleMessage(
    JavaScriptConsoleMessageLevel level, const QString& message, int line, const QString& source) {
    // Only our own script speaks this way; a document cannot run script (no raw HTML).
    if (source == kScriptSource) {
        if (message.startsWith(QLatin1String("neko:custom-link "))) {
            emit customLinkActivated(message.mid(QLatin1String("neko:custom-link ").size()));
            return;
        }
        if (message.startsWith(QLatin1String("neko:document-link "))) {
            emit documentLinkActivated(localPathOf(QUrl(message.mid(QLatin1String("neko:document-link ").size()))));
            return;
        }
    }
    QWebEnginePage::javaScriptConsoleMessage(level, message, line, source);
}

bool MarkdownPage::acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) {
    const QString scheme = url.scheme().toLower();
    if (url == kPageUrl && isMainFrame && type != NavigationTypeLinkClicked) {
        return true; // the page's own load
    }
    if (type == NavigationTypeLinkClicked &&
        (scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("mailto"))) {
        QDesktopServices::openUrl(url);
    }
    return false;
}

void registerMarkdownPreviewScheme() {
    QWebEngineUrlScheme scheme(kNoteScheme);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme);
    QWebEngineUrlScheme::registerScheme(scheme);
}

} // namespace neko::markdown
