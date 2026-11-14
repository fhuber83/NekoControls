#include "neko/theme/theme_menus.hpp"

#include "neko/theme/theme_manager.hpp"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QPointer>

namespace neko::theme {

void populateThemeMenu(QMenu* menu, QWidget* dialogParent) {
    auto* group = new QActionGroup(menu);
    group->setExclusive(true);
    const QPointer<QWidget> parent(dialogParent);

    for (const ThemeMode mode : ThemeManager::themeModes()) {
        if (mode == ThemeMode::System) {
            menu->addSeparator();
        }
        QAction* action = menu->addAction(ThemeManager::themeModeDisplayName(mode));
        action->setCheckable(true);
        action->setData(static_cast<int>(mode));
        action->setChecked(ThemeManager::instance().themeMode() == mode);
        group->addAction(action);
        QObject::connect(action, &QAction::triggered, menu, [mode, parent] {
            if (ThemeManager::instance().themeMode() != mode) {
                ThemeManager::instance().setThemeMode(mode);
                ThemeManager::promptRestartNotice(parent);
            }
        });
    }

    QObject::connect(&ThemeManager::instance(), &ThemeManager::themeChanged, menu, [group](ThemeMode mode, bool) {
        for (QAction* action : group->actions()) {
            action->setChecked(action->data().toInt() == static_cast<int>(mode));
        }
    });
}

void populateStyleMenu(QMenu* menu, QWidget* dialogParent) {
    auto* group = new QActionGroup(menu);
    group->setExclusive(true);
    const QPointer<QWidget> parent(dialogParent);
    const QString current = ThemeManager::instance().currentStyle();

    for (const QString& style : ThemeManager::instance().availableStyles()) {
        QAction* action = menu->addAction(style);
        action->setCheckable(true);
        action->setData(style);
        action->setChecked(current.compare(style, Qt::CaseInsensitive) == 0);
        group->addAction(action);
        QObject::connect(action, &QAction::triggered, menu, [style, parent] {
            if (ThemeManager::instance().currentStyle().compare(style, Qt::CaseInsensitive) != 0) {
                ThemeManager::instance().setStyle(style);
                ThemeManager::promptRestartNotice(parent);
            }
        });
    }

    QObject::connect(&ThemeManager::instance(), &ThemeManager::styleChanged, menu, [group](const QString& style) {
        for (QAction* action : group->actions()) {
            action->setChecked(action->data().toString().compare(style, Qt::CaseInsensitive) == 0);
        }
    });
}

} // namespace neko::theme
