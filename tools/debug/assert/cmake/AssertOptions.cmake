# AssertOptions.cmake
#
# Library-local helpers for resolving ASSERT_ENABLED / ASSERT_CHECKS_ENABLED.
# These stay in the Assert library cmake folder because they describe Assert's
# local build contract, not a project-wide rule.

function(internal_assert_resolve_auto_option output_variable option_value)
    if(option_value STREQUAL "ON")
        set(${output_variable} 1 PARENT_SCOPE)
    elseif(option_value STREQUAL "OFF")
        set(${output_variable} 0 PARENT_SCOPE)
    elseif(
        ASSERT_BUILD_TYPE_VALUE STREQUAL "RELEASE"
        OR ASSERT_BUILD_TYPE_VALUE STREQUAL "RELWITHDEBINFO"
        OR ASSERT_BUILD_TYPE_VALUE STREQUAL "MINSIZEREL"
    )
        set(${output_variable} 0 PARENT_SCOPE)
    else()
        set(${output_variable} 1 PARENT_SCOPE)
    endif()
endfunction()

function(internal_assert_public_option output_variable option_value)
    if(option_value STREQUAL "ON")
        set(${output_variable} 1 PARENT_SCOPE)
    elseif(option_value STREQUAL "OFF")
        set(${output_variable} 0 PARENT_SCOPE)
    elseif(ASSERT_MULTI_CONFIG)
        set(${output_variable} "$<IF:${ASSERT_RELEASE_CONFIG_EXPRESSION},0,1>" PARENT_SCOPE)
    else()
        internal_assert_resolve_auto_option(resolved_value "${option_value}")
        set(${output_variable} ${resolved_value} PARENT_SCOPE)
    endif()
endfunction()
