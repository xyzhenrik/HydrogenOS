#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${repo_root}"

required_files=(
    AGENTS.md
    CODE_OF_CONDUCT.md
    CONTRIBUTING.md
    LICENSE
    README.md
    SECURITY.md
    docs/MASTER_PLAN.md
    docs/THREAT_MODEL.md
    docs/architecture/README.md
    rust-toolchain.toml
)

for file in "${required_files[@]}"; do
    if [[ ! -s "${file}" ]]; then
        echo "error: required file is missing or empty: ${file}" >&2
        exit 1
    fi
done

if grep -RInE --exclude-dir=.git --exclude='check-repository.sh' \
    '(BEGIN (RSA|OPENSSH|EC) PRIVATE KEY|gh[pousr]_[A-Za-z0-9_]{20,})' .; then
    echo "error: possible secret found" >&2
    exit 1
fi

if grep -RIn --include='*.qml' --include='*.cpp' --include='*.rs' \
    -E 'Apple|macOS|iOS' design shell settings services; then
    echo "error: product source must not depend on Apple naming or assets" >&2
    exit 1
fi

echo "Repository policy checks passed"

