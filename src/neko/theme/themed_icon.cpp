#include "neko/theme/themed_icon.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>
#include <QtLogging>

#include <memory>

namespace neko::theme {

namespace {

/// Draws one SVG with `currentColor` replaced by the palette colour for the mode.
class TintedSvgIconEngine final : public QIconEngine {
public:
    explicit TintedSvgIconEngine(QByteArray svg)
        : svg_(std::move(svg)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State /*state*/) override {
        if (QSvgRenderer* renderer = rendererFor(colorFor(mode))) {
            renderer->render(painter, QRectF(rect));
        }
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QPixmap pm(size * scale);
        pm.fill(Qt::transparent);
        pm.setDevicePixelRatio(scale);
        QPainter painter(&pm);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return pm;
    }

    [[nodiscard]] QIconEngine* clone() const override { return new TintedSvgIconEngine(svg_); }
    [[nodiscard]] QString key() const override { return QStringLiteral("NekoTintedSvg"); }

private:
    [[nodiscard]] static QColor colorFor(QIcon::Mode mode) {
        const QPalette palette = QGuiApplication::palette();
        switch (mode) {
        case QIcon::Disabled:
            return palette.color(QPalette::Disabled, QPalette::ButtonText);
        case QIcon::Selected:
            return palette.color(QPalette::Active, QPalette::HighlightedText);
        case QIcon::Normal:
        case QIcon::Active:
            break;
        }
        return palette.color(QPalette::Active, QPalette::ButtonText);
    }

    /// One renderer per colour seen; a theme switch adds at most a few.
    QSvgRenderer* rendererFor(const QColor& color) {
        auto& slot = renderers_[color.rgba()];
        if (!slot) {
            QByteArray tinted = svg_;
            tinted.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
            slot = std::make_shared<QSvgRenderer>(tinted);
            slot->setAspectRatioMode(Qt::KeepAspectRatio);
            if (!slot->isValid()) {
                slot.reset();
            }
        }
        return slot.get();
    }

    QByteArray svg_;
    QHash<QRgb, std::shared_ptr<QSvgRenderer>> renderers_;
};

QString iconPath(const QString& name) {
    return QStringLiteral(":/neko/icons/%1.svg").arg(name);
}

} // namespace

QIcon themedIcon(const QString& name) {
    QFile file(iconPath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("themedIcon: no icon '%s' in this application's icon set", qPrintable(name));
        return {};
    }
    return QIcon(new TintedSvgIconEngine(file.readAll()));
}

bool hasThemedIcon(const QString& name) {
    return QFile::exists(iconPath(name));
}

} // namespace neko::theme
