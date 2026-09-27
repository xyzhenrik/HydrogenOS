#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
shader="${repo_root}/design/shaders/glass-highlight.frag"

require_source() {
    local source="$1"
    if ! grep -Fq "$source" "$shader"; then
        echo "error: glass highlight shader must contain: $source" >&2
        exit 1
    fi
}

require_source "roundedRectangleMask(uv, resolution, cornerRadius)"
require_source "fragColor = vec4(tint * alpha, alpha);"

if grep -Eq 'smoothstep\((0\.92, 0\.15|0\.16, 0\.0|0\.08, 0\.0)' "$shader"; then
    echo "error: reversed smoothstep edges are undefined across GPU drivers" >&2
    exit 1
fi

echo "Shader policy checks passed"
