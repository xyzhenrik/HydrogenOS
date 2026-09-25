#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

measure_script="$1"
output_path="$2"

mkdir -p "$(dirname "$output_path")"
sleep 3 &
subject_pid=$!
trap 'kill "$subject_pid" 2>/dev/null || true' EXIT

"$measure_script" --pid "$subject_pid" --duration 1 --output "$output_path"

grep -q '"schema_version": 1' "$output_path"
grep -q '"kind": "idle_resources"' "$output_path"
grep -q '"duration_seconds": 1' "$output_path"
grep -q '"eligible": false' "$output_path"
