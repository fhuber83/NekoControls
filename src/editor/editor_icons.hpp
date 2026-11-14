#pragma once

#include <QIcon>
#include <QString>
#include <QStringList>

#include <functional>

namespace nekogrid::editor {

/// The icons this library's widgets show. The library ships no images: it asks the
/// application for an icon by logical name, so the widgets look like the rest of the
/// application and follow its theme. The names (editorIconNames()):
///
///   "line-wrap"                       CodeEditor::lineWrapAction()
///   "find-previous", "find-next",
///   "match-case", "whole-word",
///   "regex", "close"                  EditorFindBar's buttons
///
/// Without a provider, or where it returns a null icon, the widgets do without: the
/// action has its text alone, the Find bar's buttons show a short caption ("Aa", ".*").
///
/// The provider is asked when a widget is built, not when it paints: install it before
/// the first editor, and return icons that follow a theme change by themselves (an
/// icon engine that reads the palette when it paints).
///
/// UI thread only.
using EditorIconProvider = std::function<QIcon(const QString& name)>;

/// Installs `provider` for the process; an empty one removes it.
void setEditorIconProvider(EditorIconProvider provider);

/// The application's icon for `name`; null without a provider.
[[nodiscard]] QIcon editorIcon(const QString& name);

/// Every name the library asks for -- for an application's test that its icon set
/// has them all.
[[nodiscard]] QStringList editorIconNames();

} // namespace nekogrid::editor
