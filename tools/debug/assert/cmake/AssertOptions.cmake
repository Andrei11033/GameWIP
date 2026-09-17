# AssertOptions.cmake
#
# Library-local helpers for resolving ASSERT_ENABLED / ASSERT_CHECKS_ENABLED.
# These stay in the Assert library cmake folder because they describe Assert's
# local build contract, not a project-wide rule.

# Writes a numeric result to the caller scope using the normalized build type.
# The caller validates option_value; AUTO follows release/debug policy.
function(internal_assert_resolve_auto_option output_variable option_value)
    # Explicit ON/OFF values take precedence over the build configuration.
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

# Writes a consumer compile-definition value to the caller scope. Multi-config AUTO
# stays a generator expression so Debug and Release share one configured build tree.
function(internal_assert_public_option output_variable option_value)
    if(option_value STREQUAL "ON")
        set(${output_variable} 1 PARENT_SCOPE)
    elseif(option_value STREQUAL "OFF")
        set(${output_variable} 0 PARENT_SCOPE)
    elseif(ASSERT_MULTI_CONFIG)
        # AUTO is deferred until generation when configurations share the same target.
        set(${output_variable} "$<IF:${ASSERT_RELEASE_CONFIG_EXPRESSION},0,1>" PARENT_SCOPE)
    else()
        internal_assert_resolve_auto_option(resolved_value "${option_value}")

        set(${output_variable} ${resolved_value} PARENT_SCOPE)
    endif()
endfunction()
