# Verified PowerShell runtime installation.

# ------------------------------------------------------------
# Verified local installation
# ------------------------------------------------------------

gamewip_install_powershell()
(
    set -euo pipefail

    local repository_root="$1"
    local architecture=''
    local storage_root=''
    local archive_name=''
    local expected_sha256=''
    local install_root=''
    local tools_root=''
    local staging_root=''
    local archive_path=''
    local extract_root=''
    local download_url=''
    local command_name=''

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
            printf 'PowerShell bootstrap does not support host architecture: %s\n' "$(uname -m)" >&2
            return 1
            ;;
    esac

    for command_name in curl sha256sum tar mktemp mkdir chmod mv rm realpath; do
        if ! command -v "$command_name" >/dev/null 2>&1; then
            printf 'PowerShell bootstrap requires %s. Install it, then rerun setup.sh.\n' "$command_name" >&2
            return 1
        fi
    done

    archive_name="powershell-$GAMEWIP_POWERSHELL_VERSION-linux-$architecture.tar.gz"
    storage_root="$(gamewip_resolve_storage_root "$repository_root")"
    GAMEWIP_STORAGE_ROOT="$storage_root"
    export GAMEWIP_STORAGE_ROOT
    tools_root="$storage_root/tools/powershell"
    install_root="$tools_root/$GAMEWIP_POWERSHELL_VERSION"

    if [[ -e "$install_root" || -L "$install_root" ]]; then
        if gamewip_is_managed_powershell \
            "$install_root/pwsh" \
            "$install_root/.gamewip-tool" \
            "$architecture" \
            "$expected_sha256"; then
            printf '%s\n' "$install_root/pwsh"
            return 0
        fi

        printf 'PowerShell install path exists but is not usable: %s\n' "$install_root" >&2
        return 1
    fi

    mkdir -p "$tools_root"
    staging_root="$(mktemp -d "$tools_root/.${GAMEWIP_POWERSHELL_VERSION}.XXXXXX")"
    trap 'rm -rf -- "$staging_root"' EXIT

    archive_path="$staging_root/$archive_name"
    download_url="https://github.com/PowerShell/PowerShell/releases/download/v$GAMEWIP_POWERSHELL_VERSION/$archive_name"
    printf 'Downloading PowerShell %s for Linux %s.\n' "$GAMEWIP_POWERSHELL_VERSION" "$architecture" >&2

    if [[ -t 2 ]]; then
        curl --fail --location --retry 3 --progress-bar --output "$archive_path" "$download_url"
    else
        curl --fail --location --retry 3 --silent --show-error --output "$archive_path" "$download_url"
    fi

    if ! printf '%s  %s\n' "$expected_sha256" "$archive_path" | sha256sum --check --status; then
        printf 'PowerShell archive checksum verification failed.\n' >&2
        return 1
    fi

    extract_root="$staging_root/extracted"
    mkdir "$extract_root"
    tar -xzf "$archive_path" -C "$extract_root"
    chmod +x "$extract_root/pwsh"

    if ! gamewip_is_supported_powershell "$extract_root/pwsh"; then
        printf 'The verified archive did not provide a usable PowerShell 7 runtime.\n' >&2
        return 1
    fi

    printf '%s\n' \
        "version=$GAMEWIP_POWERSHELL_VERSION" \
        "archive=$archive_name" \
        "sha256=$expected_sha256" \
        >"$extract_root/.gamewip-tool"

    if ! mv -T -- "$extract_root" "$install_root"; then
        if ! gamewip_is_managed_powershell \
            "$install_root/pwsh" \
            "$install_root/.gamewip-tool" \
            "$architecture" \
            "$expected_sha256"; then
            printf 'Could not install PowerShell into %s.\n' "$install_root" >&2
            return 1
        fi
    fi

    printf '%s\n' "$install_root/pwsh"
)
