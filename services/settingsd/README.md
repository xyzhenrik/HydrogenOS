# hydrogen-settingsd

The first Hydrogen user-session service owns accessibility/material preferences
and the explicit diagnostic-consent flag.

- Bus name: `org.hydrogen.Settings`
- Object path: `/org/hydrogen/Settings1`
- Interface: `org.hydrogen.Settings1`
- Storage: `$XDG_CONFIG_HOME/hydrogen/settings-v1.json`

The checked-in introspection XML is part of the public contract. Any incompatible
change requires a new interface version and an ADR. Diagnostics default to off.

The Qt Quick consumer is documented in `settings/README.md`. The service may be
started before or after the UI; the UI watches the well-known bus name and
reloads state when ownership changes.

The product image installs a standard D-Bus activation file backed by a
`Type=dbus` systemd user service. Calling the well-known name starts the daemon
on demand; it is not enabled as an unconditional login service.
