#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

usage() {
    echo "usage: $0 --pid PID[,PID...] --duration SECONDS --output PATH [--hardware-label LABEL] [--scope shell-prototype|hydrogen-session]" >&2
}

pids=""
duration="300"
output=""
hardware_label=""
scope="shell-prototype"

while (($# > 0)); do
    case "$1" in
        --pid) pids="${2:-}"; shift 2 ;;
        --duration) duration="${2:-}"; shift 2 ;;
        --output) output="${2:-}"; shift 2 ;;
        --hardware-label) hardware_label="${2:-}"; shift 2 ;;
        --scope) scope="${2:-}"; shift 2 ;;
        *) usage; exit 2 ;;
    esac
done

if [[ -z "$pids" || -z "$output" || ! "$duration" =~ ^[1-9][0-9]*$ ||
      ("$scope" != "shell-prototype" && "$scope" != "hydrogen-session") ]]; then
    usage
    exit 2
fi

IFS=',' read -r -a pid_list <<< "$pids"
for pid in "${pid_list[@]}"; do
    if [[ ! "$pid" =~ ^[1-9][0-9]*$ || ! -r "/proc/$pid/stat" ]]; then
        echo "cannot read process $pid" >&2
        exit 1
    fi
done

read_ticks() {
    local total=0 pid rest user_ticks system_ticks
    for pid in "${pid_list[@]}"; do
        rest="$(sed 's/^.*) //' "/proc/$pid/stat")"
        user_ticks="$(awk '{print $12}' <<< "$rest")"
        system_ticks="$(awk '{print $13}' <<< "$rest")"
        total=$((total + user_ticks + system_ticks))
    done
    echo "$total"
}

read_pss_kib() {
    local total=0 pid value
    for pid in "${pid_list[@]}"; do
        value="$(awk '/^Pss:/ {print $2}' "/proc/$pid/smaps_rollup")"
        total=$((total + value))
    done
    echo "$total"
}

start_ticks="$(read_ticks)"
sleep "$duration"
end_ticks="$(read_ticks)"
pss_kib="$(read_pss_kib)"
clock_ticks="$(getconf CLK_TCK)"
cpu_percent="$(awk -v delta="$((end_ticks - start_ticks))" -v hz="$clock_ticks" -v seconds="$duration" 'BEGIN { printf "%.6f", (delta / hz / seconds) * 100 }')"

mkdir -p "$(dirname "$output")"
escaped_label="${hardware_label//\\/\\\\}"
escaped_label="${escaped_label//\"/\\\"}"
cat > "$output" <<EOF
{
  "schema_version": 1,
  "kind": "idle_resources",
  "duration_seconds": $duration,
  "process_count": ${#pid_list[@]},
  "metrics": {
    "average_cpu_percent": $cpu_percent,
    "pss_kib": $pss_kib
  },
  "environment": {
    "hardware_label": "$escaped_label",
    "kernel": "$(uname -r)",
    "architecture": "$(uname -m)"
  },
  "qualification": {
    "eligible": $([[ "$duration" -ge 300 && -n "$hardware_label" && "$scope" == "hydrogen-session" ]] && echo true || echo false),
    "scope": "$scope"
  }
}
EOF
