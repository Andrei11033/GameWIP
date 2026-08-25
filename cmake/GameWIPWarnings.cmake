include_guard(GLOBAL)

# Configures first-party C++ compiler warnings after external targets have been added.
#
# Public helper:
# - gamewip_enable_project_warnings()
#
# Inputs:
# - GAMEWIP_WARNINGS_AS_ERRORS controls whether supported compilers treat warnings as errors.
#
# Side effects:
# - Adds directory-scoped options guarded to C++ compilation only.
#
# Failure contract:
# - Unknown compiler families produce a configure warning and receive no project warning profile.

function(gamewip_enable_project_warnings)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(gamewip_warning_options /W4)
        if(GAMEWIP_WARNINGS_AS_ERRORS)
            list(APPEND gamewip_warning_options /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        set(gamewip_warning_options
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wformat=2
            -Wcast-align
            -Wcast-qual
            -Wold-style-cast
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wswitch-enum
            -Wimplicit-fallthrough
            -Wnull-dereference
            -Wdouble-promotion
            -Wundef
            -Wmissing-declarations
            -Wzero-as-null-pointer-constant
            -Wextra-semi
            -Wvla
            -Walloca
            -Wno-missing-field-initializers
        )
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            list(
                APPEND gamewip_warning_options
                -Wduplicated-cond
                -Wduplicated-branches
                -Wlogical-op
                -Wdangling-pointer=2
                -Wuse-after-free=3
                -Warray-bounds=2
                -Wformat-overflow=2
                -Wformat-truncation=2
                -Wstringop-overflow=4
                -Wsuggest-override
            )
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
            list(
                APPEND gamewip_warning_options
                -Wthread-safety
                -Wcomma
                -Wconditional-uninitialized
                -Wshorten-64-to-32
                -Wunsafe-buffer-usage
            )
        endif()
        if(GAMEWIP_WARNINGS_AS_ERRORS)
            list(APPEND gamewip_warning_options -Werror)
        endif()
    else()
        message(WARNING "GameWIP has no compiler warning profile for ${CMAKE_CXX_COMPILER_ID}.")
        return()
    endif()

    foreach(gamewip_warning IN LISTS gamewip_warning_options)
        add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${gamewip_warning}>")
    endforeach()
endfunction()
