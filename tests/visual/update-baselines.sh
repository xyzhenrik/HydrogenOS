#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${1:-${repo_root}/build}"
shell_binary="${build_dir}/shell/hydrogen-shell"
baseline_dir="${repo_root}/tests/visual/baselines"

if [[ ! -x "${shell_binary}" ]]; then
    echo "error: build the project first; missing ${shell_binary}" >&2
    exit 1
fi

mkdir -p "${baseline_dir}"
for specification in "100:1.0" "150:1.5" "200:2.0"; do
    label="${specification%%:*}"
    scale="${specification##*:}"
    export LANG=C.UTF-8
    export LC_ALL=C.UTF-8
    export QT_QPA_PLATFORM=offscreen
    export QT_QUICK_BACKEND=software
    export QT_SCALE_FACTOR="${scale}"
    export QSG_RHI_BACKEND=software
    "${shell_binary}" --visual-test "${baseline_dir}/shell-${label}.png"
    echo "updated shell-${label}.png"
done
