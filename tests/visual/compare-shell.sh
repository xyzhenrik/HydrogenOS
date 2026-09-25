#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

if [[ $# -ne 6 ]]; then
    echo "usage: compare-shell.sh SHELL COMPARATOR SCALE LABEL BASELINE OUTPUT_DIR" >&2
    exit 2
fi

shell_binary="$1"
comparator="$2"
scale_factor="$3"
scale_label="$4"
baseline="$5"
output_dir="$6"

mkdir -p "${output_dir}"
actual="${output_dir}/shell-${scale_label}-actual.png"
difference="${output_dir}/shell-${scale_label}-diff.png"

export LANG=C.UTF-8
export LC_ALL=C.UTF-8
export QT_QPA_PLATFORM=offscreen
export QT_QUICK_BACKEND=software
export QT_SCALE_FACTOR="${scale_factor}"
export QSG_RHI_BACKEND=software

"${shell_binary}" --visual-test "${actual}"

if [[ ! -f "${baseline}" ]]; then
    echo "error: visual baseline is missing: ${baseline}" >&2
    echo "generate it with tests/visual/update-baselines.sh" >&2
    exit 1
fi

if ! "${comparator}" "${baseline}" "${actual}" "${difference}"; then
    echo "visual comparison failed; inspect ${actual} and ${difference}" >&2
    exit 1
fi

rm -f "${actual}" "${difference}"
