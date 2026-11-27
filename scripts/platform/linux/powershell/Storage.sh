# Linux PowerShell bootstrap paths.

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

    # Resolve both paths before checking containment.
    build_root="$(realpath -m -- "$repository_root/build")"

    if [[ "$configured_root" == /* ]]; then
        storage_root="$(realpath -m -- "$configured_root")"
    else
        storage_root="$(realpath -m -- "$repository_root/$configured_root")"
    fi

    # Keep helper state beneath build/.
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

    # Publish the canonical root for the caller.
    GAMEWIP_STORAGE_ROOT="$storage_root"
    export GAMEWIP_STORAGE_ROOT
    printf '%s\n' "$storage_root"
}

# ------------------------------------------------------------
# Managed tool root
# ------------------------------------------------------------

gamewip_resolve_managed_tools_root()
{
    local repository_root="$1"
    local storage_root="$2"
    local build_root=""
    local tools_root=""
    local configured_root="${GAMEWIP_MANAGED_TOOLS_ROOT:-tools/managed}"

    build_root="$(realpath -m -- "$repository_root/build")"

    if [[ "$configured_root" == /* ]]; then
        tools_root="$(realpath -m -- "$configured_root")"
    else
        tools_root="$(realpath -m -- "$storage_root/$configured_root")"
    fi

    case "$tools_root/" in
        "$build_root/"*) ;;
        *)
            if [[ ! -d "$tools_root" ]]; then
                printf 'GAMEWIP_MANAGED_TOOLS_ROOT must select a directory under %s or an existing tools directory.\n' "$build_root" >&2
                return 1
            fi
            ;;
    esac

    if [[ "$tools_root" == "$build_root" || "$tools_root" == "$repository_root" ]]; then
        printf 'GAMEWIP_MANAGED_TOOLS_ROOT cannot select %s.\n' "$tools_root" >&2
        return 1
    fi

    GAMEWIP_MANAGED_TOOLS_ROOT="$tools_root"
    export GAMEWIP_MANAGED_TOOLS_ROOT
    printf '%s\n' "$tools_root"
}
