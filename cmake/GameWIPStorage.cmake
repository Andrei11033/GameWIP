include_guard(GLOBAL)

# Resolve configured GameWIP storage beneath the repository build directory.
get_filename_component(_GAMEWIP_STORAGE_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_GAMEWIP_STORAGE_CONFIG "${_GAMEWIP_STORAGE_SOURCE_ROOT}/scripts/config/project.json")

function(gamewip_get_storage_root output_variable)
    # Resolve the configured root and keep it beneath build/.
    file(READ "${_GAMEWIP_STORAGE_CONFIG}" project_json)
    string(JSON configured_root GET "${project_json}" storage root)

    if(DEFINED GAMEWIP_STORAGE_ROOT AND NOT GAMEWIP_STORAGE_ROOT STREQUAL "")
        set(storage_root "${GAMEWIP_STORAGE_ROOT}")
    elseif(DEFINED ENV{GAMEWIP_STORAGE_ROOT} AND NOT "$ENV{GAMEWIP_STORAGE_ROOT}" STREQUAL "")
        set(storage_root "$ENV{GAMEWIP_STORAGE_ROOT}")
    else()
        set(storage_root "${configured_root}")
    endif()

    if(NOT IS_ABSOLUTE "${storage_root}")
        set(storage_root "${_GAMEWIP_STORAGE_SOURCE_ROOT}/${storage_root}")
    endif()

    get_filename_component(storage_root "${storage_root}" ABSOLUTE)
    get_filename_component(build_root "${_GAMEWIP_STORAGE_SOURCE_ROOT}/build" ABSOLUTE)
    cmake_path(IS_PREFIX build_root "${storage_root}" NORMALIZE storage_root_is_child)

    if(NOT storage_root_is_child OR storage_root STREQUAL build_root)
        message(FATAL_ERROR "GAMEWIP_STORAGE_ROOT must be a child of '${build_root}'; found '${storage_root}'.")
    endif()

    set("${output_variable}" "${storage_root}" PARENT_SCOPE)
endfunction()

function(gamewip_get_storage_path output_variable area)
    # Resolve configured areas beneath the shared root.
    file(READ "${_GAMEWIP_STORAGE_CONFIG}" project_json)
    string(JSON relative_path GET "${project_json}" storage "${area}")
    gamewip_get_storage_root(storage_root)
    set(storage_path "${storage_root}/${relative_path}")
    get_filename_component(storage_path "${storage_path}" ABSOLUTE)
    cmake_path(IS_PREFIX storage_root "${storage_path}" NORMALIZE path_is_child)

    if(NOT path_is_child OR storage_path STREQUAL storage_root)
        message(FATAL_ERROR "GameWIP storage area '${area}' must stay beneath '${storage_root}'.")
    endif()

    set("${output_variable}" "${storage_path}" PARENT_SCOPE)
endfunction()

function(gamewip_get_managed_environment_path output_variable setting)
    # Use explicit roots when set; otherwise follow the storage layout.
    file(READ "${_GAMEWIP_STORAGE_CONFIG}" project_json)
    string(JSON relative_path GET "${project_json}" managedEnvironment "${setting}")
    gamewip_get_storage_root(storage_root)

    if(setting STREQUAL "msys2Root")
        set(override_name GAMEWIP_MSYS2_ROOT)
    elseif(setting STREQUAL "gameWipToolsRoot")
        set(override_name GAMEWIP_MANAGED_TOOLS_ROOT)
    else()
        message(FATAL_ERROR "Unknown managed environment setting '${setting}'.")
    endif()

    if(DEFINED ${override_name} AND NOT "${${override_name}}" STREQUAL "")
        set(managed_path "${${override_name}}")
        set(managed_path_is_relative FALSE)
    elseif(DEFINED ENV{${override_name}} AND NOT "$ENV{${override_name}}" STREQUAL "")
        set(managed_path "$ENV{${override_name}}")
        set(managed_path_is_relative FALSE)
    else()
        set(managed_path "${relative_path}")
        set(managed_path_is_relative TRUE)
    endif()

    if(NOT IS_ABSOLUTE "${managed_path}")
        set(managed_path_is_relative TRUE)
        set(managed_path "${storage_root}/${managed_path}")
    endif()

    get_filename_component(managed_path "${managed_path}" ABSOLUTE)
    get_filename_component(managed_root "${managed_path}" DIRECTORY)
    get_filename_component(managed_root "${managed_root}" ABSOLUTE)
    get_filename_component(build_root "${_GAMEWIP_STORAGE_SOURCE_ROOT}/build" ABSOLUTE)

    if(managed_path_is_relative)
        cmake_path(IS_PREFIX storage_root "${managed_path}" NORMALIZE managed_path_is_child)
    else()
        set(managed_path_is_child TRUE)
    endif()

    cmake_path(IS_PREFIX build_root "${managed_path}" NORMALIZE managed_path_is_in_build)

    # External overrides may reuse installations but cannot create new roots.
    if(NOT managed_path_is_in_build AND NOT IS_DIRECTORY "${managed_path}")
        message(
            FATAL_ERROR
            "Managed environment path '${setting}' may point outside build/ only to an existing installation; new GameWIP-managed roots must stay under '${build_root}'."
        )
    endif()

    if(NOT managed_path_is_child OR managed_path STREQUAL managed_root OR managed_path STREQUAL storage_root OR managed_path STREQUAL build_root)
        message(FATAL_ERROR "Managed environment path '${setting}' cannot be a filesystem root or the storage root.")
    endif()

    set("${output_variable}" "${managed_path}" PARENT_SCOPE)
endfunction()
