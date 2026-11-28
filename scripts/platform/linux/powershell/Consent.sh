# Setup consent for the managed PowerShell runtime.

# ------------------------------------------------------------
# Setup consent
# ------------------------------------------------------------

gamewip_has_argument()
{
    local expected="${1,,}"
    shift

    local argument=''
    for argument in "$@"; do
        if [[ "${argument,,}" == "$expected" ]]; then
            return 0
        fi
    done

    return 1
}

gamewip_require_setup_powershell()
{
    local repository_root="$1"
    shift

    if gamewip_find_powershell "$repository_root"; then
        return 0
    fi

    if gamewip_has_argument -Preview "$@"; then
        printf 'PowerShell %s or newer is required; setup preview cannot install it.\n' \
            "$GAMEWIP_POWERSHELL_VERSION" >&2
        printf 'Rerun setup.sh interactively to install the managed runtime.\n' >&2
        return 1
    fi

    local answer=''
    if gamewip_has_argument -Yes "$@"; then
        answer='y'
        printf 'Installing verified PowerShell %s because -Yes was supplied.\n' \
            "$GAMEWIP_POWERSHELL_VERSION" >&2
    elif gamewip_has_argument -NonInteractive "$@" || [[ ! -t 0 ]]; then
        printf 'PowerShell %s or newer is required by setup.sh. Rerun interactively or use -Yes to install it.\n' \
            "$GAMEWIP_POWERSHELL_VERSION" >&2
        return 1
    else
        printf 'PowerShell %s or newer is required by setup.sh.\n' \
            "$GAMEWIP_POWERSHELL_VERSION" >&2
        printf 'Install the verified PowerShell %s runtime under %s/tools/powershell? [y/N] ' \
            "$GAMEWIP_POWERSHELL_VERSION" "${GAMEWIP_STORAGE_ROOT:-build/gamewip}" >&2

        if ! IFS= read -r answer; then
            answer=''
        fi
    fi

    case "${answer,,}" in
        y | yes)
            GAMEWIP_POWERSHELL_COMMAND="$(gamewip_install_powershell "$repository_root")" || return 1
            ;;
        *)
            printf 'PowerShell installation declined.\n' >&2
            return 1
            ;;
    esac
}
