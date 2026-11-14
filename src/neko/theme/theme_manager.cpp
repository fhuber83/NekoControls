#include "neko/theme/theme_manager.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLibrary>
#include <QMessageBox>
#include <QPluginLoader>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <memory>

namespace neko::theme {

namespace {
constexpr auto kThemeSettingKey = "ui/theme";
constexpr auto kStyleSettingKey = "ui/style";
constexpr auto kSuppressRestartNoticeKey = "ui/suppress_restart_notice";
constexpr auto kRestartNoticeActionKey = "ui/restart_notice_action";

ThemeManager::SettingsFactory& settingsFactory() {
    static ThemeManager::SettingsFactory factory;
    return factory;
}

ThemeManager::RestartEnvironmentHook& restartEnvironmentHook() {
    static ThemeManager::RestartEnvironmentHook hook;
    return hook;
}

std::unique_ptr<QSettings> openSettings() {
    if (const auto& factory = settingsFactory()) {
        if (auto settings = factory()) {
            return settings;
        }
    }
    return std::make_unique<QSettings>();
}
} // namespace

void ThemeManager::setSettingsFactory(SettingsFactory factory) {
    settingsFactory() = std::move(factory);
}

void ThemeManager::setRestartEnvironmentHook(RestartEnvironmentHook hook) {
    restartEnvironmentHook() = std::move(hook);
}

ThemeManager& ThemeManager::instance() {
    static ThemeManager s_instance;
    return s_instance;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent) {
    discoverAvailableStyles();

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme /*scheme*/) {
            if (currentMode_ == ThemeMode::System) {
                applyTheme(ThemeMode::System);
            }
        });
    }
#endif
}

void ThemeManager::discoverAvailableStyles() {
    // 1. Register candidate plugin directories across common platform locations
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList searchRoots = {
        appDir + QStringLiteral("/plugins"),
        appDir + QStringLiteral("/styles"),
        QDir::homePath() + QStringLiteral("/.local/lib/qt6/plugins"),
        QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/plugins"),
        QStringLiteral("/usr/lib64/qt6/plugins"),
        QStringLiteral("/usr/lib/qt6/plugins"),
        QStringLiteral("/usr/local/lib/qt6/plugins"),
        QStringLiteral("/opt/qt6/plugins"),
    };

    for (const QString& root : searchRoots) {
        if (QDir(root).exists()) {
            QCoreApplication::addLibraryPath(root);
        }
    }

    // Direct inspection of any available styles/ subdirectories to register plugins with QStyleFactory
    for (const QString& libPath : QCoreApplication::libraryPaths()) {
        const QDir stylesDir(libPath + QStringLiteral("/styles"));
        if (!stylesDir.exists()) {
            continue;
        }
        for (const QFileInfo& fi : stylesDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
            if (QLibrary::isLibrary(fi.absoluteFilePath())) {
                QPluginLoader loader(fi.absoluteFilePath());
                loader.load(); // Registers any exported QStylePlugin into QStyleFactory
            }
        }
    }

    // 2. Filter for styles that successfully instantiate on the current platform
    availableStyles_.clear();
    const QStringList candidateKeys = QStyleFactory::keys();
    for (const QString& key : candidateKeys) {
        std::unique_ptr<QStyle> test(QStyleFactory::create(key));
        if (test) {
            // Style engine is valid and operational on current platform/display server
            availableStyles_.append(key);
        }
    }

    // Always sort with "Fusion" first if available (cross-platform baseline)
    std::sort(availableStyles_.begin(), availableStyles_.end(), [](const QString& a, const QString& b) {
        if (a.compare(QStringLiteral("Fusion"), Qt::CaseInsensitive) == 0)
            return true;
        if (b.compare(QStringLiteral("Fusion"), Qt::CaseInsensitive) == 0)
            return false;
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    if (QApplication::style()) {
        const QString objName = QApplication::style()->objectName();
        for (const QString& st : availableStyles_) {
            if (st.compare(objName, Qt::CaseInsensitive) == 0) {
                currentStyle_ = st;
                break;
            }
        }
    }
}

QString ThemeManager::savedStyle() {
    const auto settingsPtr = openSettings();
    QSettings& settings = *settingsPtr;
    return settings.value(QString::fromLatin1(kStyleSettingKey), QStringLiteral("Fusion")).toString();
}

void ThemeManager::saveStyle(const QString& styleKey) {
    const auto settingsPtr = openSettings();
    QSettings& settings = *settingsPtr;
    settings.setValue(QString::fromLatin1(kStyleSettingKey), styleKey);
    settings.sync();
}

ThemeRestartAction ThemeManager::restartNoticeAction() {
    const auto settingsPtr = openSettings();
    QSettings& settings = *settingsPtr;
    const QString val = settings.value(QString::fromLatin1(kRestartNoticeActionKey)).toString();
    if (val == QStringLiteral("restart_now")) {
        return ThemeRestartAction::RestartNow;
    }
    if (val == QStringLiteral("restart_later")) {
        return ThemeRestartAction::RestartLater;
    }
    if (settings.contains(QString::fromLatin1(kSuppressRestartNoticeKey)) &&
        settings.value(QString::fromLatin1(kSuppressRestartNoticeKey)).toBool()) {
        return ThemeRestartAction::RestartLater;
    }
    return ThemeRestartAction::Prompt;
}

void ThemeManager::setRestartNoticeAction(ThemeRestartAction action) {
    const auto settingsPtr = openSettings();
    QSettings& settings = *settingsPtr;
    switch (action) {
    case ThemeRestartAction::RestartNow:
        settings.setValue(QString::fromLatin1(kRestartNoticeActionKey), QStringLiteral("restart_now"));
        settings.setValue(QString::fromLatin1(kSuppressRestartNoticeKey), true);
        break;
    case ThemeRestartAction::RestartLater:
        settings.setValue(QString::fromLatin1(kRestartNoticeActionKey), QStringLiteral("restart_later"));
        settings.setValue(QString::fromLatin1(kSuppressRestartNoticeKey), true);
        break;
    case ThemeRestartAction::Prompt:
    default:
        settings.setValue(QString::fromLatin1(kRestartNoticeActionKey), QStringLiteral("prompt"));
        settings.setValue(QString::fromLatin1(kSuppressRestartNoticeKey), false);
        break;
    }
    settings.sync();
}

bool ThemeManager::isRestartNoticeSuppressed() {
    return restartNoticeAction() != ThemeRestartAction::Prompt;
}

void ThemeManager::setRestartNoticeSuppressed(bool suppressed) {
    if (suppressed) {
        if (restartNoticeAction() == ThemeRestartAction::Prompt) {
            setRestartNoticeAction(ThemeRestartAction::RestartLater);
        }
    } else {
        setRestartNoticeAction(ThemeRestartAction::Prompt);
    }
}

void ThemeManager::promptRestartNotice(QWidget* parent) {
    // Prevent blocking headless unit tests, test mode, or offscreen display runs
    if (QGuiApplication::platformName() == QStringLiteral("offscreen") || QStandardPaths::isTestModeEnabled() ||
        qEnvironmentVariableIsSet("CTEST_INTERACTIVE_DEBUG_MODE")) {
        return;
    }

    const auto action = restartNoticeAction();
    if (action == ThemeRestartAction::RestartNow) {
        restartApplication();
        return;
    }
    if (action == ThemeRestartAction::RestartLater) {
        return;
    }

    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(tr("Restart Recommended"));
    msgBox.setText(tr("You need to restart to apply this theme."));
    msgBox.setInformativeText(
        tr("Some user interface components (such as native title bars, window borders, and dock frame headers) "
           "only refresh completely after restarting the application."));
    msgBox.setIcon(QMessageBox::Information);

    auto* restartNowBtn = msgBox.addButton(tr("Restart Now"), QMessageBox::AcceptRole);
    auto* restartLaterBtn = msgBox.addButton(tr("Restart Later"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(restartLaterBtn);

    auto* dontAskAgainCheck = new QCheckBox(tr("Don't ask again"), &msgBox);
    msgBox.setCheckBox(dontAskAgainCheck);

    msgBox.exec();

    const bool restartNow = (msgBox.clickedButton() == restartNowBtn);

    if (dontAskAgainCheck->isChecked()) {
        setRestartNoticeAction(restartNow ? ThemeRestartAction::RestartNow : ThemeRestartAction::RestartLater);
    }

    if (restartNow) {
        restartApplication();
    }
}

void ThemeManager::restartApplication() {
    // Close first: a window's closeEvent may ask (unsaved work) and the user may say
    // no -- then nothing must have been started.
    QApplication::closeAllWindows();
    for (const QWidget* w : QApplication::topLevelWidgets()) {
        if (w->isVisible() && !w->windowFlags().testFlag(Qt::ToolTip) && !w->windowFlags().testFlag(Qt::Popup)) {
            return; // a window refused to close: stay
        }
    }
    // The new process starts before this one has quit; an application with a
    // single-instance lock tells it so through the restart hook.
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments(QCoreApplication::arguments().mid(1));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (const auto& hook = restartEnvironmentHook()) {
        hook(env);
    }
    process.setProcessEnvironment(env);
    process.startDetached();
    QApplication::quit();
}

bool ThemeManager::setStyle(const QString& styleKey) {
    QString canonicalName = styleKey;
    for (const QString& st : availableStyles_) {
        if (st.compare(styleKey, Qt::CaseInsensitive) == 0) {
            canonicalName = st;
            break;
        }
    }

    QStyle* style = QStyleFactory::create(canonicalName);
    if (!style) {
        return false;
    }
    QApplication::setStyle(style);
    currentStyle_ = canonicalName;
    saveStyle(currentStyle_);

    // Re-apply the current palette to the newly set style engine
    applyTheme(currentMode_);
    emit styleChanged(currentStyle_);
    return true;
}

ThemeMode ThemeManager::savedThemeMode() {
    const auto settingsPtr = openSettings();
    QSettings& settings = *settingsPtr;
    const QString val = settings.value(QString::fromLatin1(kThemeSettingKey), QStringLiteral("light")).toString();
    return stringToThemeMode(val);
}

void ThemeManager::saveThemeMode(ThemeMode mode) {
    const auto settingsPtr = openSettings();
    QSettings& settings = *settingsPtr;
    settings.setValue(QString::fromLatin1(kThemeSettingKey), themeModeToString(mode));
    settings.sync();
}

QString ThemeManager::themeModeToString(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Dark:
        return QStringLiteral("dark");
    case ThemeMode::HighContrastDark:
        return QStringLiteral("high_contrast_dark");
    case ThemeMode::HighContrastLight:
        return QStringLiteral("high_contrast_light");
    case ThemeMode::Nord:
        return QStringLiteral("nord");
    case ThemeMode::Dracula:
        return QStringLiteral("dracula");
    case ThemeMode::SolarizedDark:
        return QStringLiteral("solarized_dark");
    case ThemeMode::SolarizedLight:
        return QStringLiteral("solarized_light");
    case ThemeMode::Motif:
        return QStringLiteral("motif");
    case ThemeMode::System:
        return QStringLiteral("system");
    case ThemeMode::Light:
    default:
        return QStringLiteral("light");
    }
}

ThemeMode ThemeManager::stringToThemeMode(const QString& str) {
    if (str.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::Dark;
    }
    if (str.compare(QStringLiteral("high_contrast_dark"), Qt::CaseInsensitive) == 0 ||
        str.compare(QStringLiteral("highcontrastdark"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::HighContrastDark;
    }
    if (str.compare(QStringLiteral("high_contrast_light"), Qt::CaseInsensitive) == 0 ||
        str.compare(QStringLiteral("highcontrastlight"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::HighContrastLight;
    }
    if (str.compare(QStringLiteral("nord"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::Nord;
    }
    if (str.compare(QStringLiteral("dracula"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::Dracula;
    }
    if (str.compare(QStringLiteral("solarized_dark"), Qt::CaseInsensitive) == 0 ||
        str.compare(QStringLiteral("solarizeddark"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::SolarizedDark;
    }
    if (str.compare(QStringLiteral("solarized_light"), Qt::CaseInsensitive) == 0 ||
        str.compare(QStringLiteral("solarizedlight"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::SolarizedLight;
    }
    if (str.compare(QStringLiteral("motif"), Qt::CaseInsensitive) == 0 ||
        str.compare(QStringLiteral("cde"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::Motif;
    }
    if (str.compare(QStringLiteral("system"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::System;
    }
    return ThemeMode::Light;
}

QString ThemeManager::themeModeDisplayName(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Dark:
        return QStringLiteral("🌙 Dark");
    case ThemeMode::HighContrastDark:
        return QStringLiteral("⚡ High Contrast Dark");
    case ThemeMode::HighContrastLight:
        return QStringLiteral("🔲 High Contrast Light");
    case ThemeMode::Nord:
        return QStringLiteral("❄️ Nord");
    case ThemeMode::Dracula:
        return QStringLiteral("🧛 Dracula");
    case ThemeMode::SolarizedDark:
        return QStringLiteral("🌅 Solarized Dark");
    case ThemeMode::SolarizedLight:
        return QStringLiteral("📜 Solarized Light");
    case ThemeMode::Motif:
        return QStringLiteral("🏛️ Classic Motif / CDE");
    case ThemeMode::System:
        return QStringLiteral("💻 System");
    case ThemeMode::Light:
    default:
        return QStringLiteral("☀️ Light (Default)");
    }
}

std::vector<ThemeMode> ThemeManager::themeModes() {
    return {
        ThemeMode::Light,
        ThemeMode::Dark,
        ThemeMode::HighContrastDark,
        ThemeMode::HighContrastLight,
        ThemeMode::Nord,
        ThemeMode::Dracula,
        ThemeMode::SolarizedDark,
        ThemeMode::SolarizedLight,
        ThemeMode::Motif,
        ThemeMode::System,
    };
}

void ThemeManager::setThemeMode(ThemeMode mode) {
    saveThemeMode(mode);
    applyTheme(mode);
}

void ThemeManager::applyTheme(ThemeMode mode) {
    currentMode_ = mode;
    bool dark = false;

    if (mode == ThemeMode::System) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (auto* hints = QGuiApplication::styleHints()) {
            dark = (hints->colorScheme() == Qt::ColorScheme::Dark);
        } else {
            dark = (QApplication::palette().color(QPalette::Window).lightness() < 128);
        }
#else
        dark = (QApplication::palette().color(QPalette::Window).lightness() < 128);
#endif
    } else {
        switch (mode) {
        case ThemeMode::Dark:
        case ThemeMode::HighContrastDark:
        case ThemeMode::Nord:
        case ThemeMode::Dracula:
        case ThemeMode::SolarizedDark:
            dark = true;
            break;
        case ThemeMode::Light:
        case ThemeMode::HighContrastLight:
        case ThemeMode::SolarizedLight:
        case ThemeMode::Motif:
        default:
            dark = false;
            break;
        }
    }

    isDark_ = dark;
    QPalette pal =
        (mode == ThemeMode::System) ? (dark ? createDarkPalette() : createLightPalette()) : createPaletteForMode(mode);
    enforceReadableContrast(pal);

    QApplication::setPalette(pal);
    emit themeChanged(currentMode_, isDark_);
}

QPalette ThemeManager::createPaletteForMode(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Dark:
        return createDarkPalette();
    case ThemeMode::HighContrastDark:
        return createHighContrastDarkPalette();
    case ThemeMode::HighContrastLight:
        return createHighContrastLightPalette();
    case ThemeMode::Nord:
        return createNordPalette();
    case ThemeMode::Dracula:
        return createDraculaPalette();
    case ThemeMode::SolarizedDark:
        return createSolarizedDarkPalette();
    case ThemeMode::SolarizedLight:
        return createSolarizedLightPalette();
    case ThemeMode::Motif:
        return createMotifPalette();
    case ThemeMode::System:
    case ThemeMode::Light:
    default:
        return createLightPalette();
    }
}

QPalette ThemeManager::createLightPalette() {
    QPalette pal;
    const QColor window(248, 249, 250);
    const QColor windowText(31, 35, 40);
    const QColor base(255, 255, 255);
    const QColor altBase(243, 244, 246);
    const QColor text(31, 35, 40);
    const QColor button(240, 242, 245);
    const QColor buttonText(31, 35, 40);
    const QColor highlight(9, 105, 218);
    const QColor highlightedText(255, 255, 255);
    const QColor disabledText(140, 149, 159);
    const QColor mid(208, 215, 222);
    const QColor dark(140, 149, 159);

    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, windowText);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, altBase);
    pal.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    pal.setColor(QPalette::ToolTipText, windowText);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, buttonText);
    pal.setColor(QPalette::BrightText, QColor(207, 34, 46));
    pal.setColor(QPalette::Link, QColor(9, 105, 218));
    pal.setColor(QPalette::LinkVisited, QColor(130, 80, 223));
    pal.setColor(QPalette::Highlight, highlight);
    pal.setColor(QPalette::HighlightedText, highlightedText);
    pal.setColor(QPalette::PlaceholderText, QColor(101, 109, 118));
    pal.setColor(QPalette::Mid, mid);
    pal.setColor(QPalette::Dark, dark);
    pal.setColor(QPalette::Light, QColor(255, 255, 255));
    pal.setColor(QPalette::Midlight, QColor(234, 238, 242));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(220, 224, 230));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    return pal;
}

QPalette ThemeManager::createDarkPalette() {
    QPalette pal;
    const QColor window(30, 32, 36);
    const QColor windowText(240, 243, 246);
    const QColor base(22, 24, 27);
    const QColor altBase(35, 38, 43);
    const QColor text(240, 243, 246);
    const QColor button(45, 49, 57);
    const QColor buttonText(240, 243, 246);
    const QColor highlight(47, 129, 247);
    const QColor highlightedText(255, 255, 255);
    const QColor disabledText(118, 131, 144);
    const QColor mid(68, 76, 86);
    const QColor dark(28, 30, 34);

    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, windowText);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, altBase);
    pal.setColor(QPalette::ToolTipBase, window);
    pal.setColor(QPalette::ToolTipText, windowText);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, buttonText);
    pal.setColor(QPalette::BrightText, QColor(248, 81, 73));
    pal.setColor(QPalette::Link, QColor(88, 166, 255));
    pal.setColor(QPalette::LinkVisited, QColor(188, 140, 255));
    pal.setColor(QPalette::Highlight, highlight);
    pal.setColor(QPalette::HighlightedText, highlightedText);
    pal.setColor(QPalette::PlaceholderText, QColor(139, 148, 158));
    pal.setColor(QPalette::Mid, mid);
    pal.setColor(QPalette::Dark, dark);
    pal.setColor(QPalette::Light, QColor(55, 62, 71));
    pal.setColor(QPalette::Midlight, QColor(45, 51, 59));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(40, 48, 60));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    return pal;
}

QPalette ThemeManager::createHighContrastDarkPalette() {
    QPalette pal;
    const QColor window(0, 0, 0);
    const QColor windowText(255, 255, 255);
    const QColor base(0, 0, 0);
    const QColor altBase(18, 18, 18);
    const QColor text(255, 255, 255);
    const QColor button(24, 24, 24);
    const QColor buttonText(255, 255, 255);
    const QColor highlight(0, 212, 255);   // Electric cyan
    const QColor highlightedText(0, 0, 0); // Pure black text on bright cyan
    const QColor disabledText(160, 160, 160);
    const QColor mid(255, 255, 255); // Pure white border lines for maximum structural clarity
    const QColor dark(100, 100, 100);

    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, windowText);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, altBase);
    pal.setColor(QPalette::ToolTipBase, window);
    pal.setColor(QPalette::ToolTipText, windowText);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, buttonText);
    pal.setColor(QPalette::BrightText, QColor(255, 80, 80));
    pal.setColor(QPalette::Link, QColor(0, 230, 255));
    pal.setColor(QPalette::LinkVisited, QColor(255, 120, 255));
    pal.setColor(QPalette::Highlight, highlight);
    pal.setColor(QPalette::HighlightedText, highlightedText);
    pal.setColor(QPalette::PlaceholderText, QColor(200, 200, 200));
    pal.setColor(QPalette::Mid, mid);
    pal.setColor(QPalette::Dark, dark);
    pal.setColor(QPalette::Light, QColor(60, 60, 60));
    pal.setColor(QPalette::Midlight, QColor(40, 40, 40));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(60, 60, 60));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    return pal;
}

QPalette ThemeManager::createHighContrastLightPalette() {
    QPalette pal;
    const QColor window(255, 255, 255);
    const QColor windowText(0, 0, 0);
    const QColor base(255, 255, 255);
    const QColor altBase(240, 240, 240);
    const QColor text(0, 0, 0);
    const QColor button(235, 235, 235);
    const QColor buttonText(0, 0, 0);
    const QColor highlight(0, 64, 192); // Bold cobalt blue
    const QColor highlightedText(255, 255, 255);
    const QColor disabledText(100, 100, 100);
    const QColor mid(0, 0, 0); // Pure black border lines for maximal clarity
    const QColor dark(120, 120, 120);

    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, windowText);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, altBase);
    pal.setColor(QPalette::ToolTipBase, window);
    pal.setColor(QPalette::ToolTipText, windowText);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, buttonText);
    pal.setColor(QPalette::BrightText, QColor(200, 0, 0));
    pal.setColor(QPalette::Link, QColor(0, 40, 180));
    pal.setColor(QPalette::LinkVisited, QColor(120, 0, 140));
    pal.setColor(QPalette::Highlight, highlight);
    pal.setColor(QPalette::HighlightedText, highlightedText);
    pal.setColor(QPalette::PlaceholderText, QColor(80, 80, 80));
    pal.setColor(QPalette::Mid, mid);
    pal.setColor(QPalette::Dark, dark);
    pal.setColor(QPalette::Light, QColor(255, 255, 255));
    pal.setColor(QPalette::Midlight, QColor(220, 220, 220));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(200, 200, 200));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    return pal;
}

QPalette ThemeManager::createNordPalette() {
    QPalette pal;
    const QColor window(46, 52, 64);        // Nord0
    const QColor windowText(236, 239, 244); // Nord6
    const QColor base(36, 41, 51);          // Deep Polar Night
    const QColor altBase(59, 66, 82);       // Nord1
    const QColor text(236, 239, 244);       // Nord6
    const QColor button(59, 66, 82);        // Nord1
    const QColor buttonText(236, 239, 244);
    const QColor highlight(136, 192, 208);    // Nord8 frost cyan
    const QColor highlightedText(46, 52, 64); // Dark text on frost
    const QColor disabledText(140, 150, 170);
    const QColor mid(76, 86, 106); // Nord3
    const QColor dark(30, 34, 42);

    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, windowText);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, altBase);
    pal.setColor(QPalette::ToolTipBase, window);
    pal.setColor(QPalette::ToolTipText, windowText);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, buttonText);
    pal.setColor(QPalette::BrightText, QColor(191, 97, 106)); // Nord11 red
    pal.setColor(QPalette::Link, QColor(136, 192, 208));
    pal.setColor(QPalette::LinkVisited, QColor(180, 142, 173));
    pal.setColor(QPalette::Highlight, highlight);
    pal.setColor(QPalette::HighlightedText, highlightedText);
    pal.setColor(QPalette::PlaceholderText, QColor(180, 190, 205));
    pal.setColor(QPalette::Mid, mid);
    pal.setColor(QPalette::Dark, dark);
    pal.setColor(QPalette::Light, QColor(67, 76, 94));
    pal.setColor(QPalette::Midlight, QColor(59, 66, 82));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(50, 58, 72));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    return pal;
}

QPalette ThemeManager::createDraculaPalette() {
    QPalette pal;
    const QColor window(40, 42, 54);        // Dracula background
    const QColor windowText(248, 248, 242); // Dracula foreground
    const QColor base(30, 31, 41);          // Darker editor background
    const QColor altBase(52, 55, 70);       // Selection
    const QColor text(248, 248, 242);
    const QColor button(68, 71, 90); // Current line
    const QColor buttonText(248, 248, 242);
    const QColor highlight(189, 147, 249);    // Dracula purple
    const QColor highlightedText(40, 42, 54); // Dark text on purple
    const QColor disabledText(130, 140, 170);
    const QColor mid(98, 114, 164); // Dracula comment / border
    const QColor dark(25, 26, 33);

    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, windowText);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, altBase);
    pal.setColor(QPalette::ToolTipBase, window);
    pal.setColor(QPalette::ToolTipText, windowText);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, buttonText);
    pal.setColor(QPalette::BrightText, QColor(255, 85, 85));    // Dracula red
    pal.setColor(QPalette::Link, QColor(139, 233, 253));        // Dracula cyan
    pal.setColor(QPalette::LinkVisited, QColor(255, 121, 198)); // Dracula pink
    pal.setColor(QPalette::Highlight, highlight);
    pal.setColor(QPalette::HighlightedText, highlightedText);
    pal.setColor(QPalette::PlaceholderText, QColor(160, 170, 195));
    pal.setColor(QPalette::Mid, mid);
    pal.setColor(QPalette::Dark, dark);
    pal.setColor(QPalette::Light, QColor(68, 71, 90));
    pal.setColor(QPalette::Midlight, QColor(55, 58, 74));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(50, 52, 68));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    return pal;
}

QPalette ThemeManager::createSolarizedDarkPalette() {
    QPalette pal;
    const QColor window(0, 43, 54);         // base03
    const QColor windowText(147, 161, 161); // base1
    const QColor base(0, 33, 43);           // deeper base04
    const QColor altBase(7, 54, 66);        // base02
    const QColor text(147, 161, 161);
    const QColor button(7, 54, 66); // base02
    const QColor buttonText(147, 161, 161);
    const QColor highlight(38, 139, 210);        // blue
    const QColor highlightedText(253, 246, 227); // base3
    const QColor disabledText(88, 110, 117);     // base01
    const QColor mid(88, 110, 117);              // base01
    const QColor dark(0, 27, 34);

    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, windowText);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, altBase);
    pal.setColor(QPalette::ToolTipBase, window);
    pal.setColor(QPalette::ToolTipText, windowText);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, buttonText);
    pal.setColor(QPalette::BrightText, QColor(220, 50, 47));    // red
    pal.setColor(QPalette::Link, QColor(42, 161, 152));         // cyan
    pal.setColor(QPalette::LinkVisited, QColor(108, 113, 196)); // violet
    pal.setColor(QPalette::Highlight, highlight);
    pal.setColor(QPalette::HighlightedText, highlightedText);
    pal.setColor(QPalette::PlaceholderText, QColor(101, 123, 131)); // base00
    pal.setColor(QPalette::Mid, mid);
    pal.setColor(QPalette::Dark, dark);
    pal.setColor(QPalette::Light, QColor(7, 54, 66));
    pal.setColor(QPalette::Midlight, QColor(5, 48, 59));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(10, 48, 58));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    return pal;
}

QPalette ThemeManager::createSolarizedLightPalette() {
    QPalette pal;
    const QColor window(253, 246, 227);    // base3
    const QColor windowText(88, 110, 117); // base01
    const QColor base(255, 255, 248);      // warm bright paper
    const QColor altBase(238, 232, 213);   // base2
    const QColor text(88, 110, 117);
    const QColor button(238, 232, 213); // base2
    const QColor buttonText(88, 110, 117);
    const QColor highlight(38, 139, 210); // blue
    const QColor highlightedText(255, 255, 255);
    const QColor disabledText(147, 161, 161); // base1
    const QColor mid(147, 161, 161);          // base1
    const QColor dark(203, 75, 22);           // orange

    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, windowText);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, altBase);
    pal.setColor(QPalette::ToolTipBase, window);
    pal.setColor(QPalette::ToolTipText, windowText);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, buttonText);
    pal.setColor(QPalette::BrightText, QColor(220, 50, 47));    // red
    pal.setColor(QPalette::Link, QColor(42, 161, 152));         // cyan
    pal.setColor(QPalette::LinkVisited, QColor(108, 113, 196)); // violet
    pal.setColor(QPalette::Highlight, highlight);
    pal.setColor(QPalette::HighlightedText, highlightedText);
    pal.setColor(QPalette::PlaceholderText, QColor(131, 148, 150)); // base0
    pal.setColor(QPalette::Mid, mid);
    pal.setColor(QPalette::Dark, dark);
    pal.setColor(QPalette::Light, QColor(255, 255, 255));
    pal.setColor(QPalette::Midlight, QColor(245, 239, 220));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(220, 215, 195));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    return pal;
}

QPalette ThemeManager::createMotifPalette() {
    QPalette pal;
    const QColor window(174, 178, 186);  // Classic CDE/Motif gray-slate
    const QColor windowText(0, 0, 0);    // Solid black text
    const QColor base(255, 255, 255);    // White editor base
    const QColor altBase(190, 194, 202); // Subtle alternating row
    const QColor text(0, 0, 0);
    const QColor button(174, 178, 186); // Classic beveled button
    const QColor buttonText(0, 0, 0);
    const QColor highlight(0, 85, 102); // Classic Motif deep teal
    const QColor highlightedText(255, 255, 255);
    const QColor disabledText(110, 114, 122);
    const QColor mid(90, 94, 102);
    const QColor dark(70, 74, 82);

    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, windowText);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, altBase);
    pal.setColor(QPalette::ToolTipBase, window);
    pal.setColor(QPalette::ToolTipText, windowText);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, buttonText);
    pal.setColor(QPalette::BrightText, QColor(200, 0, 0));
    pal.setColor(QPalette::Link, QColor(0, 85, 102));
    pal.setColor(QPalette::LinkVisited, QColor(100, 40, 120));
    pal.setColor(QPalette::Highlight, highlight);
    pal.setColor(QPalette::HighlightedText, highlightedText);
    pal.setColor(QPalette::PlaceholderText, QColor(90, 95, 105));
    pal.setColor(QPalette::Mid, mid);
    pal.setColor(QPalette::Dark, dark);
    pal.setColor(QPalette::Light, QColor(220, 224, 232));
    pal.setColor(QPalette::Midlight, QColor(195, 200, 208));

    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(140, 145, 155));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    return pal;
}

// ---------------------------------------------------------------------------
// Readability / Contrast Helpers
// ---------------------------------------------------------------------------

namespace {

double linearizeChannel(double c) {
    return (c <= 0.04045) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
}

QColor mixColors(const QColor& a, const QColor& b, double t) {
    const auto lerp = [t](double x, double y) {
        return x + ((y - x) * t);
    };
    QColor out;
    out.setRgbF(
        static_cast<float>(lerp(a.redF(), b.redF())),
        static_cast<float>(lerp(a.greenF(), b.greenF())),
        static_cast<float>(lerp(a.blueF(), b.blueF())),
        a.alphaF());
    return out;
}

} // namespace

double ThemeManager::relativeLuminance(const QColor& c) {
    const QColor rgb = c.toRgb();
    return (0.2126 * linearizeChannel(rgb.redF())) + (0.7152 * linearizeChannel(rgb.greenF())) +
           (0.0722 * linearizeChannel(rgb.blueF()));
}

double ThemeManager::contrastRatio(const QColor& a, const QColor& b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor ThemeManager::ensureContrast(const QColor& fg, const QColor& bg, double minRatio) {
    if (contrastRatio(fg, bg) >= minRatio) {
        return fg;
    }
    // Push towards whichever extreme can give the background more contrast.
    const QColor white(255, 255, 255);
    const QColor black(0, 0, 0);
    const QColor target = (contrastRatio(white, bg) >= contrastRatio(black, bg)) ? white : black;

    constexpr int kSteps = 40;
    for (int i = 1; i <= kSteps; ++i) {
        const QColor candidate = mixColors(fg, target, static_cast<double>(i) / kSteps);
        if (contrastRatio(candidate, bg) >= minRatio) {
            return candidate;
        }
    }
    QColor best = target;
    best.setAlphaF(fg.alphaF());
    return best; // Best achievable for this background
}

void ThemeManager::enforceReadableContrast(QPalette& pal) {
    struct Pair {
        QPalette::ColorRole fg;
        QPalette::ColorRole bg;
    };
    // Every foreground role paired with each background it is drawn on.
    static constexpr Pair kPairs[] = {
        {QPalette::WindowText, QPalette::Window},
        {QPalette::Text, QPalette::Base},
        {QPalette::Text, QPalette::AlternateBase},
        {QPalette::ButtonText, QPalette::Button},
        {QPalette::ToolTipText, QPalette::ToolTipBase},
        {QPalette::HighlightedText, QPalette::Highlight},
        {QPalette::PlaceholderText, QPalette::Base},
        {QPalette::PlaceholderText, QPalette::Window},
        {QPalette::Link, QPalette::Base},
        {QPalette::Link, QPalette::Window},
        {QPalette::LinkVisited, QPalette::Base},
        {QPalette::LinkVisited, QPalette::Window},
    };

    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const double minRatio = (group == QPalette::Disabled) ? kMinDisabledContrast : kMinTextContrast;
        for (const Pair& p : kPairs) {
            const QColor fixed = ensureContrast(pal.color(group, p.fg), pal.color(group, p.bg), minRatio);
            pal.setColor(group, p.fg, fixed);
        }
    }
}

QColor ThemeManager::mutedTextColor(const QPalette& pal) {
    const QColor window = pal.color(QPalette::Active, QPalette::Window);
    const QColor muted = mixColors(pal.color(QPalette::Active, QPalette::WindowText), window, 0.30);
    return ensureContrast(muted, window, kMinTextContrast);
}

} // namespace neko::theme
