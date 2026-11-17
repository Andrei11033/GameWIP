#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$repository_root/scripts/lib/powershell_bootstrap.sh"

if ! gamewip_find_powershell "$repository_root"; then
    printf 'PowerShell 7 is required. Run bash ./setup.sh to install the managed runtime.\n' >&2
    exit 127
fi

export PATH="${GAMEWIP_POWERSHELL_COMMAND%/*}:$PATH"

if [[ "${1-}" == "--help" || "${1-}" == "-h" || "${1-}" == "-?" ]]; then
    exec "$GAMEWIP_POWERSHELL_COMMAND" -NoProfile -File "$repository_root/scripts/GameWIP.ps1" help
fi

exec "$GAMEWIP_POWERSHELL_COMMAND" -NoProfile -File "$repository_root/scripts/GameWIP.ps1" "$@"
