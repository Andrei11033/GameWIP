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
        set(gamewip_warning_options -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
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

