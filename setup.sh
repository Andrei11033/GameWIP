#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$repository_root/scripts/lib/powershell_bootstrap.sh"

if ! gamewip_require_setup_powershell "$repository_root" "$@"; then
    exit 1
fi

export PATH="${GAMEWIP_POWERSHELL_COMMAND%/*}:$PATH"

if [[ "${1-}" == "--help" || "${1-}" == "-h" || "${1-}" == "-?" ]]; then
    exec "$GAMEWIP_POWERSHELL_COMMAND" -NoProfile -File "$repository_root/scripts/setup/Setup.ps1" help
fi

exec "$GAMEWIP_POWERSHELL_COMMAND" -NoProfile -File "$repository_root/scripts/setup/Setup.ps1" "$@"
