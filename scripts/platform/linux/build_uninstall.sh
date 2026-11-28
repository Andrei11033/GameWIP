# Deferred build-directory cleanup after setup uninstall.

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

    # Validate the cleanup request for this setup run.
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

    # Preserve build/ when setup fails so its diagnostics remain available.
    if [[ "$exit_code" -ne 0 ]]; then
        if ! rm -f -- "$pending_marker"; then
            printf 'Could not clear the failed build cleanup request: %s\n' "$pending_marker" >&2
            return 1
        fi
        printf 'Setup did not finish cleanly; preserved %s for diagnosis.\n' "$build_root" >&2
        return 0
    fi

    # Resolve and verify the checkout-owned build path before removal.
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
