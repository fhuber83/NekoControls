// Tests of neko::theme. This target links neko_theme alone -- no editor, no Markdown,
// no application -- plus the icons tests/test_theme_icons.txt compiles in through
// neko_add_icon_set().

#include "neko/theme/responsive_tool_bar.hpp"
#include "neko/theme/theme_manager.hpp"
#include "neko/theme/theme_menus.hpp"
#include "neko/theme/theme_translations.hpp"
#include "neko/theme/themed_icon.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTranslator>
#include <QVBoxLayout>
#include <QtTest>

#include <memory>

namespace th = neko::theme;

namespace {

QSet<QRgb> opaqueColors(const QIcon& icon, QIcon::Mode mode) {
    const QImage img = icon.pixmap(QSize(24, 24), mode).toImage();
    QSet<QRgb> colors;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) == 255) {
                colors.insert(img.pixel(x, y) | 0xff000000u);
            }
        }
    }
    return colors;
}

} // namespace

class TestTheme final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(settingsDir_.isValid());
        // Every theme setting goes to this file, not the user's.
        const QString file = settingsDir_.filePath(QStringLiteral("theme.ini"));
        th::ThemeManager::setSettingsFactory(
            [file] { return std::make_unique<QSettings>(file, QSettings::IniFormat); });
    }

    void testThemeModeNames() {
        const std::vector<th::ThemeMode> modes = th::ThemeManager::themeModes();
        QCOMPARE(modes.size(), std::size_t{10});
        QCOMPARE(modes.back(), th::ThemeMode::System);
        QSet<QString> names;
        for (const th::ThemeMode mode : modes) {
            const QString name = th::ThemeManager::themeModeToString(mode);
            QCOMPARE(th::ThemeManager::stringToThemeMode(name), mode);
            QCOMPARE(th::ThemeManager::stringToThemeMode(name.toUpper()), mode);
            QVERIFY(!th::ThemeManager::themeModeDisplayName(mode).isEmpty());
            names.insert(name);
        }
        QCOMPARE(names.size(), qsizetype{10});
        QCOMPARE(th::ThemeManager::stringToThemeMode(QStringLiteral("unknown")), th::ThemeMode::Light);
    }

    void testPalettesAreReadable() {
        const QPalette light = th::ThemeManager::createLightPalette();
        QVERIFY(light.color(QPalette::Window).lightness() > 200);
        QVERIFY(light.color(QPalette::Text).lightness() < 60);
        const QPalette dark = th::ThemeManager::createDarkPalette();
        QVERIFY(dark.color(QPalette::Window).lightness() < 60);
        QVERIFY(dark.color(QPalette::Text).lightness() > 200);
        QCOMPARE(th::ThemeManager::createHighContrastDarkPalette().color(QPalette::Window), QColor(0, 0, 0));
        QCOMPARE(th::ThemeManager::createHighContrastLightPalette().color(QPalette::Window), QColor(255, 255, 255));

        QVERIFY(qAbs(th::ThemeManager::contrastRatio(Qt::black, Qt::white) - 21.0) < 0.01);
        const QColor bg(30, 32, 36);
        QVERIFY(
            th::ThemeManager::contrastRatio(th::ThemeManager::ensureContrast(QColor(28, 30, 34), bg, 4.5), bg) >= 4.5);

        for (const th::ThemeMode mode : th::ThemeManager::themeModes()) {
            if (mode == th::ThemeMode::System) {
                continue;
            }
            QPalette pal = th::ThemeManager::createPaletteForMode(mode);
            th::ThemeManager::enforceReadableContrast(pal);
            const auto ratio = [&pal](QPalette::ColorRole fg, QPalette::ColorRole bg) {
                return th::ThemeManager::contrastRatio(pal.color(fg), pal.color(bg));
            };
            const QString name = th::ThemeManager::themeModeToString(mode);
            QVERIFY2(ratio(QPalette::WindowText, QPalette::Window) >= 4.5, qPrintable(name));
            QVERIFY2(ratio(QPalette::Text, QPalette::Base) >= 4.5, qPrintable(name));
            QVERIFY2(ratio(QPalette::ButtonText, QPalette::Button) >= 4.5, qPrintable(name));
            QVERIFY2(
                th::ThemeManager::contrastRatio(th::ThemeManager::mutedTextColor(pal), pal.color(QPalette::Window)) >=
                    4.5,
                qPrintable(name));
        }
    }

    void testSwitchingAndSettingsFactory() {
        auto& mgr = th::ThemeManager::instance();
        QSignalSpy changed(&mgr, &th::ThemeManager::themeChanged);

        mgr.setThemeMode(th::ThemeMode::Nord);
        QCOMPARE(mgr.themeMode(), th::ThemeMode::Nord);
        QVERIFY(mgr.isDark());
        QCOMPARE(changed.size(), 1);
        QCOMPARE(QApplication::palette().color(QPalette::Window).lightness() < 128, true);

        // Stored where the factory says, under the keys applications already have.
        const QSettings file(settingsDir_.filePath(QStringLiteral("theme.ini")), QSettings::IniFormat);
        QCOMPARE(file.value(QStringLiteral("ui/theme")).toString(), QStringLiteral("nord"));
        QCOMPARE(th::ThemeManager::savedThemeMode(), th::ThemeMode::Nord);

        mgr.setThemeMode(th::ThemeMode::SolarizedLight);
        QVERIFY(!mgr.isDark());
        mgr.setThemeMode(th::ThemeMode::Light);
        QCOMPARE(th::ThemeManager::savedThemeMode(), th::ThemeMode::Light);

        const QStringList styles = mgr.availableStyles();
        QVERIFY(styles.contains(QStringLiteral("Fusion")));
        QCOMPARE(styles.first(), QStringLiteral("Fusion"));
        QVERIFY(mgr.setStyle(QStringLiteral("fusion")));
        QCOMPARE(mgr.currentStyle(), QStringLiteral("Fusion"));
        QCOMPARE(th::ThemeManager::savedStyle(), QStringLiteral("Fusion"));
        QVERIFY(!mgr.setStyle(QStringLiteral("no_such_style_xyz")));

        const auto saved = th::ThemeManager::restartNoticeAction();
        th::ThemeManager::setRestartNoticeAction(th::ThemeRestartAction::RestartLater);
        QCOMPARE(th::ThemeManager::restartNoticeAction(), th::ThemeRestartAction::RestartLater);
        th::ThemeManager::setRestartNoticeAction(saved);
    }

    void testThemeAndStyleMenus() {
        auto& mgr = th::ThemeManager::instance();
        mgr.setThemeMode(th::ThemeMode::Light);
        // Never a modal restart question in a test.
        th::ThemeManager::setRestartNoticeAction(th::ThemeRestartAction::RestartLater);

        QMenu themes;
        th::populateThemeMenu(&themes, nullptr);
        QCOMPARE(themes.actions().size(), 11); // 10 themes + the separator before System
        QAction* light = nullptr;
        QAction* dracula = nullptr;
        for (QAction* action : themes.actions()) {
            if (action->data().toInt() == static_cast<int>(th::ThemeMode::Light) && !action->isSeparator()) {
                light = action;
            } else if (action->data().toInt() == static_cast<int>(th::ThemeMode::Dracula)) {
                dracula = action;
            }
        }
        QVERIFY(light && dracula);
        QVERIFY(light->isChecked());

        dracula->trigger();
        QCOMPARE(mgr.themeMode(), th::ThemeMode::Dracula);
        QVERIFY(dracula->isChecked() && !light->isChecked());
        // The check follows a change made elsewhere too.
        mgr.setThemeMode(th::ThemeMode::Light);
        QVERIFY(light->isChecked() && !dracula->isChecked());

        QMenu styles;
        th::populateStyleMenu(&styles, nullptr);
        QCOMPARE(styles.actions().size(), mgr.availableStyles().size());
        QVERIFY(styles.actions().front()->isChecked()); // Fusion, current
    }

    void testThemedIconsFollowThePalette() {
        // The application's names, the controls' names, and an override of one of them.
        QVERIFY(th::hasThemedIcon(QStringLiteral("test-plus")));
        QVERIFY(th::hasThemedIcon(QStringLiteral("find-next")));
        QVERIFY(!th::hasThemedIcon(QStringLiteral("no-such-icon")));
        for (const QString& file : QDir(QStringLiteral(":/neko/icons")).entryList({QStringLiteral("*.svg")})) {
            QVERIFY2(!th::themedIcon(QFileInfo(file).completeBaseName()).isNull(), qPrintable(file));
        }
        QFile closeIcon(QStringLiteral(":/neko/icons/close.svg"));
        QVERIFY(closeIcon.open(QIODevice::ReadOnly));
        QVERIFY2(closeIcon.readAll().contains("<circle"), "the application's map overrides the controls' icon");

        const QPalette saved = QGuiApplication::palette();
        QPalette palette = saved;
        palette.setColor(QPalette::Active, QPalette::ButtonText, QColor(0x12, 0x34, 0x56));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xa0, 0xa0, 0xa0));
        QGuiApplication::setPalette(palette);
        const QIcon icon = th::themedIcon(QStringLiteral("test-plus"));
        const auto normal = opaqueColors(icon, QIcon::Normal);
        QVERIFY(normal.contains(qRgb(0x12, 0x34, 0x56)));
        QCOMPARE(normal.size(), qsizetype{1});
        QVERIFY(opaqueColors(icon, QIcon::Disabled).contains(qRgb(0xa0, 0xa0, 0xa0)));
        // The same QIcon repaints in a new palette's colour.
        palette.setColor(QPalette::Active, QPalette::ButtonText, QColor(0xee, 0xee, 0xee));
        QGuiApplication::setPalette(palette);
        QVERIFY(opaqueColors(icon, QIcon::Normal).contains(qRgb(0xee, 0xee, 0xee)));
        QGuiApplication::setPalette(saved);

        QCOMPARE(icon.pixmap(QSize(16, 16), 2.0).size(), QSize(32, 32));

        QTest::ignoreMessage(QtWarningMsg, "themedIcon: no icon 'no-such-icon' in this application's icon set");
        QVERIFY(th::themedIcon(QStringLiteral("no-such-icon")).isNull());
    }

    void testResponsiveToolBarDropsCaptionsBeforeButtons() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* bar = new th::ResponsiveToolBar(&host);
        layout->addWidget(bar);
        layout->addStretch();
        const QStringList captions = {
            QStringLiteral("First"),
            QStringLiteral("Second action"),
            QStringLiteral("Third"),
            QStringLiteral("Fourth caption"),
            QStringLiteral("Last")};
        for (const QString& caption : captions) {
            bar->addLabeledAction(th::themedIcon(QStringLiteral("test-plus")), caption);
        }
        // An action owned elsewhere keeps its owner and takes part too.
        QAction external(th::themedIcon(QStringLiteral("test-trash")), QStringLiteral("External"));
        bar->addLabeledAction(&external);
        const int entries = bar->labeledEntryCount();
        QCOMPARE(entries, 6);

        const auto styleOf = [bar](const QString& text) {
            for (QAction* action : bar->actions()) {
                if (action->text() == text) {
                    auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(action));
                    return button ? button->toolButtonStyle() : Qt::ToolButtonFollowStyle;
                }
            }
            return Qt::ToolButtonFollowStyle;
        };
        for (QAction* action : bar->actions()) {
            QVERIFY(!action->toolTip().trimmed().isEmpty());
        }

        host.show();
        QTest::qWait(20);
        int roomy = 0;
        for (int w = 300; w <= 4000 && roomy == 0; w += 100) {
            host.resize(w, 100);
            QTest::qWait(5);
            if (bar->shownCaptionCount() == entries) {
                roomy = w;
            }
        }
        QVERIFY(roomy > 0);

        host.resize(60, 100);
        QTest::qWait(20);
        QVERIFY(bar->shownCaptionCount() < entries);
        QCOMPARE(styleOf(QStringLiteral("External")), Qt::ToolButtonIconOnly);

        int partial = 0;
        for (int w = 80; w <= roomy && partial == 0; w += 10) {
            host.resize(w, 100);
            QTest::qWait(5);
            const int shown = bar->shownCaptionCount();
            if (shown > 0 && shown < entries) {
                partial = shown;
            }
        }
        QVERIFY(partial > 0);
        QCOMPARE(styleOf(QStringLiteral("First")), Qt::ToolButtonTextBesideIcon);
        QCOMPARE(styleOf(QStringLiteral("External")), Qt::ToolButtonIconOnly);
        QCOMPARE(external.text(), QStringLiteral("External")); // the text itself never changes

        host.resize(roomy, 100);
        QTest::qWait(20);
        QCOMPARE(bar->shownCaptionCount(), entries);
    }

    void testThemeTranslations() {
        QTranslator unknown;
        QVERIFY(!th::loadThemeTranslation(unknown, QStringLiteral("en")));
        for (const QString& code : th::themeTranslationLanguages()) {
            QTranslator translator;
            QVERIFY2(th::loadThemeTranslation(translator, code), qPrintable(code));
        }
        QTranslator german;
        QVERIFY(th::loadThemeTranslation(german, QStringLiteral("de")));
        QCoreApplication::installTranslator(&german);
        QCOMPARE(th::ThemeManager::tr("Restart Now"), QStringLiteral("Jetzt neu starten"));
        QCoreApplication::removeTranslator(&german);
    }

private:
    QTemporaryDir settingsDir_;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    TestTheme testObject;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&testObject, argc, argv);
}

#include "test_theme.moc"
