#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v pwsh >/dev/null 2>&1; then
    printf 'PowerShell 7 (pwsh) is required. See scripts/setup/README.md.\n' >&2
    exit 127
fi

if [[ "${1-}" == "--help" || "${1-}" == "-h" || "${1-}" == "-?" ]]; then
    exec pwsh -NoProfile -File "$repository_root/scripts/setup/Setup.ps1" help
fi

exec pwsh -NoProfile -File "$repository_root/scripts/setup/Setup.ps1" "$@"
