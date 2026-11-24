# PowerShell runtime discovery.

# ------------------------------------------------------------
# Runtime discovery
# ------------------------------------------------------------

gamewip_is_supported_powershell()
{
    local executable="$1"

    [[ -x "$executable" ]] || return 1

    "$executable" -NoLogo -NoProfile -NonInteractive \
        -Command 'if ($PSVersionTable.PSVersion.Major -ge 7) { exit 0 }; exit 1' \
        >/dev/null 2>&1
}

gamewip_is_managed_powershell()
{
    local executable="$1"
    local marker="$2"
    local architecture="$3"
    local expected_sha256="$4"
    local expected_marker=''
    local existing_marker=''

    [[ -f "$marker" ]] || return 1
    expected_marker="$(printf 'version=%s\narchive=powershell-%s-linux-%s.tar.gz\nsha256=%s' \
        "$GAMEWIP_POWERSHELL_VERSION" "$GAMEWIP_POWERSHELL_VERSION" "$architecture" "$expected_sha256")"
    existing_marker="$(<"$marker")"

    [[ "$existing_marker" == "$expected_marker" ]] && gamewip_is_supported_powershell "$executable"
}

gamewip_find_powershell()
{
    local repository_root="$1"
    local storage_root=''
    local candidate=''
    local architecture=''
    local expected_sha256=''

    if ! storage_root="$(gamewip_resolve_storage_root "$repository_root")"; then
        return 1
    fi
    GAMEWIP_STORAGE_ROOT="$storage_root"
    export GAMEWIP_STORAGE_ROOT

    if candidate="$(command -v pwsh 2>/dev/null)" && gamewip_is_supported_powershell "$candidate"; then
        GAMEWIP_POWERSHELL_COMMAND="$candidate"
        return 0
    fi

    case "$(uname -m)" in
        x86_64 | amd64)
            architecture='x64'
            expected_sha256="$GAMEWIP_POWERSHELL_X64_SHA256"
            ;;
        aarch64 | arm64)
            architecture='arm64'
            expected_sha256="$GAMEWIP_POWERSHELL_ARM64_SHA256"
            ;;
        *)
            return 1
            ;;
    esac

    candidate="$storage_root/tools/powershell/$GAMEWIP_POWERSHELL_VERSION/pwsh"
    if gamewip_is_managed_powershell \
        "$candidate" \
        "${candidate%/*}/.gamewip-tool" \
        "$architecture" \
        "$expected_sha256"; then
        GAMEWIP_POWERSHELL_COMMAND="$candidate"
        return 0
    fi

    GAMEWIP_POWERSHELL_COMMAND=''
    return 1
}
