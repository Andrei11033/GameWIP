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

launch_token="${BASHPID:-$$}-$RANDOM-$RANDOM"

if GAMEWIP_SETUP_LAUNCH_TOKEN="$launch_token" \
    "$GAMEWIP_POWERSHELL_COMMAND" \
    -NoProfile \
    -File "$repository_root/scripts/setup/Setup.ps1" \
    "$@"; then
    setup_exit_code=0
else
    setup_exit_code=$?
fi

if ! gamewip_finish_setup_uninstall "$repository_root" "$setup_exit_code" "$launch_token"; then
    if [[ "$setup_exit_code" -eq 0 ]]; then
        setup_exit_code=1
    fi
fi

exit "$setup_exit_code"
