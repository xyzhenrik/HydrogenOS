#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v podman >/dev/null 2>&1; then
    echo "error: podman is required to build the developer image" >&2
    exit 1
fi

podman build \
    --file "${repo_root}/image/Containerfile" \
    --tag localhost/hydrogen-os:dev \
    "${repo_root}"

podman image inspect localhost/hydrogen-os:dev \
    --format 'Built {{.Id}} ({{.Size}} bytes)'

