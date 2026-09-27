#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
unit="${repo_root}/image/system_files/usr/lib/systemd/user/hydrogen-settingsd.service"
activation="${repo_root}/image/system_files/usr/share/dbus-1/services/org.hydrogen.Settings.service"
containerfile="${repo_root}/image/Containerfile"

require_line() {
    local file="$1"
    local line="$2"
    if ! grep -Fqx "$line" "$file"; then
        echo "error: $file must contain: $line" >&2
        exit 1
    fi
}

require_line "$unit" "Type=dbus"
require_line "$unit" "BusName=org.hydrogen.Settings"
require_line "$unit" "ExecStart=/usr/bin/hydrogen-settingsd"
require_line "$unit" "NoNewPrivileges=yes"
require_line "$unit" "ProtectSystem=strict"
require_line "$activation" "Name=org.hydrogen.Settings"
require_line "$activation" "Exec=/usr/bin/hydrogen-settingsd"
require_line "$activation" "SystemdService=hydrogen-settingsd.service"

if grep -Fq "enable hydrogen-settingsd.service" "$containerfile"; then
    echo "error: the on-demand settings service must not be enabled at login" >&2
    exit 1
fi

echo "Session service policy checks passed"
