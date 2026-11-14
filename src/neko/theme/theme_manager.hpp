#pragma once

#include <QObject>
#include <QPalette>
#include <QProcessEnvironment>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

class QWidget;

namespace neko::theme {

/// Supported visual theme modes.
enum class ThemeMode {
    Light,
    Dark,
    HighContrastDark,
    HighContrastLight,
    Nord,
    Dracula,
    SolarizedDark,
    SolarizedLight,
    Motif,
    System
};

enum class ThemeRestartAction {
    Prompt = 0,
    RestartNow = 1,
    RestartLater = 2
};

/// Process-wide visual theme and Look & Feel manager. Controls application
/// palette, light/dark contrast modes, dynamic style plugin discovery,
/// platform validation, and persistence via QSettings.
///
/// What it stores (keys "ui/theme", "ui/style", "ui/restart_notice_action",
/// "ui/suppress_restart_notice") goes to the QSettings that setSettingsFactory()
/// hands out -- by default a plain QSettings(), i.e. the application's organization
/// and name. Call the setters before the first use of instance().
///
/// Typical start-up, after the QApplication exists:
///
///     auto& themes = neko::theme::ThemeManager::instance();
///     if (!themes.setStyle(ThemeManager::savedStyle())) themes.setStyle("Fusion");
///     themes.setThemeMode(ThemeManager::savedThemeMode());
///
/// UI thread only.
class ThemeManager final : public QObject {
    Q_OBJECT

public:
    static ThemeManager& instance();

    /// Where the theme settings live. The factory is called for every read and write;
    /// an empty one restores the default, QSettings().
    using SettingsFactory = std::function<std::unique_ptr<QSettings>()>;
    static void setSettingsFactory(SettingsFactory factory);

    /// Adjusts the environment of the process restartApplication() starts -- for an
    /// application that must tell the new instance it is a restart (a single-instance
    /// lock it should wait for, say). Empty: the environment is passed on unchanged.
    using RestartEnvironmentHook = std::function<void(QProcessEnvironment& environment)>;
    static void setRestartEnvironmentHook(RestartEnvironmentHook hook);

    // -----------------------------------------------------------------------
    // Visual Palette & Themes
    // -----------------------------------------------------------------------
    [[nodiscard]] ThemeMode themeMode() const noexcept { return currentMode_; }
    [[nodiscard]] bool isDark() const noexcept { return isDark_; }

    void setThemeMode(ThemeMode mode);

    [[nodiscard]] static ThemeMode savedThemeMode();
    static void saveThemeMode(ThemeMode mode);

    [[nodiscard]] static QString themeModeToString(ThemeMode mode);
    [[nodiscard]] static ThemeMode stringToThemeMode(const QString& str);
    [[nodiscard]] static QString themeModeDisplayName(ThemeMode mode);
    /// Every theme, in the order a menu or list shows them; System last.
    [[nodiscard]] static std::vector<ThemeMode> themeModes();

    /// Generates tailored palettes for each look & feel mode.
    [[nodiscard]] static QPalette createLightPalette();
    [[nodiscard]] static QPalette createDarkPalette();
    [[nodiscard]] static QPalette createHighContrastDarkPalette();
    [[nodiscard]] static QPalette createHighContrastLightPalette();
    [[nodiscard]] static QPalette createNordPalette();
    [[nodiscard]] static QPalette createDraculaPalette();
    [[nodiscard]] static QPalette createSolarizedDarkPalette();
    [[nodiscard]] static QPalette createSolarizedLightPalette();
    [[nodiscard]] static QPalette createMotifPalette();
    [[nodiscard]] static QPalette createPaletteForMode(ThemeMode mode);

    // -----------------------------------------------------------------------
    // Readability / Contrast Helpers (WCAG 2.x relative-luminance model)
    // -----------------------------------------------------------------------
    /// Minimum contrast ratio for normal text (WCAG AA).
    static constexpr double kMinTextContrast = 4.5;
    /// Minimum contrast ratio for disabled text and non-essential hints.
    static constexpr double kMinDisabledContrast = 3.0;

    /// WCAG relative luminance in [0, 1].
    [[nodiscard]] static double relativeLuminance(const QColor& c);
    /// WCAG contrast ratio in [1, 21] between two colors.
    [[nodiscard]] static double contrastRatio(const QColor& a, const QColor& b);
    /// Returns @p fg unchanged if it already reaches @p minRatio against @p bg;
    /// otherwise blends it progressively towards white or black (whichever can
    /// achieve more contrast) until the ratio is met, preserving its hue as far
    /// as possible.
    [[nodiscard]] static QColor ensureContrast(const QColor& fg, const QColor& bg, double minRatio);
    /// Adjusts every foreground role of @p pal (all color groups) so it stays
    /// readable against its matching background role.
    static void enforceReadableContrast(QPalette& pal);
    /// A subdued-but-readable text color for secondary labels (e.g. inactive
    /// tabs), derived from WindowText/Window and guaranteed to meet
    /// kMinTextContrast against Window.
    [[nodiscard]] static QColor mutedTextColor(const QPalette& pal);

    // -----------------------------------------------------------------------
    // Widget Style Engines & Dynamic Plugin Discovery
    // -----------------------------------------------------------------------
    /// Scans standard platform and user library paths for Qt6 style plugins,
    /// loads them via QPluginLoader, and validates that each discovered style
    /// can actually be instantiated on the current platform/display server.
    void discoverAvailableStyles();

    [[nodiscard]] QStringList availableStyles() const { return availableStyles_; }
    [[nodiscard]] QString currentStyle() const { return currentStyle_; }
    bool setStyle(const QString& styleKey);

    [[nodiscard]] static QString savedStyle();
    static void saveStyle(const QString& styleKey);

    // -----------------------------------------------------------------------
    // Restart Notice & Application Lifecycle
    // -----------------------------------------------------------------------
    [[nodiscard]] static ThemeRestartAction restartNoticeAction();
    static void setRestartNoticeAction(ThemeRestartAction action);
    [[nodiscard]] static bool isRestartNoticeSuppressed();
    static void setRestartNoticeSuppressed(bool suppressed);
    static void promptRestartNotice(QWidget* parent = nullptr);
    /// Closes the windows first -- a window's closeEvent may ask and be refused
    /// (unsaved work) -- and only when they are gone starts the new process (same
    /// program, same arguments, environment through the restart hook) and quits. A
    /// refusal leaves everything as it was.
    static void restartApplication();

signals:
    void themeChanged(ThemeMode mode, bool isDark);
    void styleChanged(const QString& styleKey);

private:
    explicit ThemeManager(QObject* parent = nullptr);
    void applyTheme(ThemeMode mode);

    ThemeMode currentMode_{ThemeMode::Light};
    bool isDark_{false};

    QStringList availableStyles_;
    QString currentStyle_{QStringLiteral("Fusion")};
};

} // namespace neko::theme
