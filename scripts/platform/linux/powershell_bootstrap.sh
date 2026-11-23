#!/usr/bin/env bash

# ------------------------------------------------------------
# Managed PowerShell release
# ------------------------------------------------------------

# Pin the official release and both supported Linux archive hashes.
GAMEWIP_POWERSHELL_VERSION='7.7.0'
GAMEWIP_POWERSHELL_X64_SHA256='c89d9c682d2c403dfe726f3df948e359675235a5bd6fab535776de69729ca0cb'
GAMEWIP_POWERSHELL_ARM64_SHA256='e59c89776eaa59790d44b3edd14277435225ab08b6e7603763101b89de83d935'
GAMEWIP_POWERSHELL_COMMAND=''

# ------------------------------------------------------------
# Storage root
# ------------------------------------------------------------

gamewip_resolve_storage_root()
{
    local repository_root="$1"
    local build_root=""
    local storage_root=""
    local configured_root="${GAMEWIP_STORAGE_ROOT:-build/gamewip}"

    if ! command -v realpath >/dev/null 2>&1; then
        printf 'The PowerShell bootstrap requires realpath to validate its storage root.\n' >&2
        return 1
    fi

    build_root="$(realpath -m -- "$repository_root/build")"

    if [[ "$configured_root" == /* ]]; then
        storage_root="$(realpath -m -- "$configured_root")"
    else
        storage_root="$(realpath -m -- "$repository_root/$configured_root")"
    fi

    case "$storage_root/" in
        "$build_root/"*) ;;
        *)
            printf 'GAMEWIP_STORAGE_ROOT must be a child of %s; found %s.\n' "$build_root" "$storage_root" >&2
            return 1
            ;;
    esac

    if [[ "$storage_root" == "$build_root" ]]; then
        printf 'GAMEWIP_STORAGE_ROOT cannot be the build directory itself.\n' >&2
        return 1
    fi

    GAMEWIP_STORAGE_ROOT="$storage_root"
    export GAMEWIP_STORAGE_ROOT
    printf '%s\n' "$storage_root"
}

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
        printf 'PowerShell 7 is missing; setup preview cannot run until it is installed.\n' >&2
        printf 'Rerun setup.sh interactively to install the managed runtime.\n' >&2
        return 1
    fi

    local answer=''
    if gamewip_has_argument -Yes "$@"; then
        answer='y'
        printf 'Installing verified PowerShell %s because -Yes was supplied.\n' \
            "$GAMEWIP_POWERSHELL_VERSION" >&2
    elif gamewip_has_argument -NonInteractive "$@" || [[ ! -t 0 ]]; then
        printf 'PowerShell 7 is required by setup.sh. Rerun interactively or use -Yes to install it.\n' >&2
        return 1
    else
        printf 'PowerShell 7 is required by setup.sh.\n' >&2
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

gamewip_finish_build_uninstall()
{
    local repository_root="$1"
    local exit_code="$2"
    local launch_token="$3"
    local repository_path=''
    local build_root=''
    local build_path=''
    local pending_marker=''
    local pending_token=''

    if [[ ! "$launch_token" =~ ^[0-9]+-[0-9]+-[0-9]+$ ]]; then
        printf 'Invalid setup launch token; preserved build/.\n' >&2
        return 0
    fi

    build_root="$repository_root/build"
    pending_marker="$build_root/.gamewip-uninstall-$launch_token.pending"

    if [[ ! -e "$pending_marker" && ! -L "$pending_marker" ]]; then
        return 0
    fi

    if [[ ! -d "$build_root" || -L "$build_root" || ! -f "$pending_marker" || -L "$pending_marker" ]]; then
        printf 'Build cleanup request is linked or invalid; preserved %s.\n' "$build_root" >&2
        return 1
    fi

    pending_token="$(<"$pending_marker")"

    if [[ "$pending_token" != "$launch_token" ]]; then
        printf 'Build cleanup request did not match this setup run; preserved %s.\n' "$build_root" >&2
        return 0
    fi

    if [[ "$exit_code" -ne 0 ]]; then
        if ! rm -f -- "$pending_marker"; then
            printf 'Could not clear the failed build cleanup request: %s\n' "$pending_marker" >&2
            return 1
        fi
        printf 'Setup did not finish cleanly; preserved %s for diagnosis.\n' "$build_root" >&2
        return 0
    fi

    if ! command -v realpath >/dev/null 2>&1; then
        printf 'Cannot verify build cleanup paths; preserved %s.\n' "$build_root" >&2
        return 1
    fi

    repository_path="$(realpath -e -- "$repository_root")" || return 1
    build_path="$(realpath -e -- "$build_root")" || return 1

    if [[ "$build_path" != "$repository_path/build" ]]; then
        printf 'Refusing build cleanup outside the checkout: %s\n' "$build_path" >&2
        return 1
    fi

    if ! rm -rf -- "$build_path"; then
        printf 'Could not remove the checkout build directory: %s\n' "$build_path" >&2
        return 1
    fi

    printf 'Removed checkout build directory: %s\n' "$build_path"
}
