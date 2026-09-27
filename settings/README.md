# Hydrogen Settings

The Qt Quick settings application uses `SettingsBackend` as its typed UI
boundary. The production backend talks asynchronously to the existing
`org.hydrogen.Settings1` session-bus contract; QML tests inject an in-memory
backend with the same properties and methods.

Run the service and UI in separate terminals:

```sh
cargo run -p hydrogen-settingsd
make run-settings
```

The UI remains usable as an explanatory offline view when the service is not
running and reconnects after the bus name gets a new owner. Writes are
optimistic, disable controls while pending, and roll back on an error.

The sidebar exposes every M2 settings category as a keyboard-operable,
single-selection control. Appearance is the first functional page; the other
categories use an explicit roadmap placeholder until their system integrations
and D-Bus contracts are implemented.

## Dependency review

`Qt6::DBus` is part of the already required Qt 6 base stack and uses the system
D-Bus implementation already required by the architecture. Qt is maintained by
the Qt Project and available under GPL-compatible LGPL/GPL terms. This adapter
adds no network access, privileged process, or third-party package beyond those
existing platform foundations. Calls are asynchronous to avoid blocking the UI
thread, and the adapter watches one exact session-bus name.

## Current contract limitation

`org.hydrogen.Settings1` exposes request methods but no change signals. The UI
therefore observes its own writes and reloads after service owner changes, but
does not learn about a concurrent client's write until a reload. Adding signals
changes the public contract and requires compatibility review and an ADR.
