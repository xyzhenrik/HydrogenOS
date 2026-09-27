# Hydrogen Shell

The current executable is a windowed M2 shell prototype. It contains the panel,
Control Center, design-system demonstration surface, and an interactive dock.
It is not yet a replacement for Plasma Shell or a production Wayland session.

## Dock activation boundary

`HydrogenDock` owns presentation, accessibility, focus, and keyboard navigation.
Activating an entry emits its desktop application ID; the QML component never
starts a process directly. The current shell records this request for tests but
does not claim that an application launched. A future launcher service or
portal-backed adapter must validate installed desktop entries before connecting
the signal to system behavior.

The preview entries are:

- `org.kde.dolphin`
- `org.mozilla.firefox`
- `org.hydrogen.Settings`
- `org.kde.konsole`

Left and Right move between entries with wrap-around, Home and End jump to the
edges, and Enter or Space activates the focused entry. Every entry remains in
the normal Tab focus chain. Reduced Motion disables hover scaling transitions;
the containing surface inherits the selected Hydrogen material level.
