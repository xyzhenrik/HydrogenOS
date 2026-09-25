# hydrogen-settingsd

The first Hydrogen user-session service owns accessibility/material preferences
and the explicit diagnostic-consent flag.

- Bus name: `org.hydrogen.Settings`
- Object path: `/org/hydrogen/Settings1`
- Interface: `org.hydrogen.Settings1`
- Storage: `$XDG_CONFIG_HOME/hydrogen/settings-v1.json`

The checked-in introspection XML is part of the public contract. Any incompatible
change requires a new interface version and an ADR. Diagnostics default to off.

