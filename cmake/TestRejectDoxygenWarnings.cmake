# Verifies that the Doxygen warning filter keeps its operator exemption narrow.

foreach(required_variable IN ITEMS DOXYGEN_EXECUTABLE PROJECT_SOURCE_DIR TEST_ROOT)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required for Doxygen warning-filter validation.")
    endif()
endforeach()

set(filter_script "${PROJECT_SOURCE_DIR}/cmake/RejectDoxygenWarnings.cmake")
set(fixture_source "${TEST_ROOT}/doxygen_warning_filter_fixture.h")
set(fixture_config "${TEST_ROOT}/Doxyfile")
set(raw_log "${TEST_ROOT}/raw.log")
set(allowed_log "${TEST_ROOT}/allowed.log")
set(filtered_log "${TEST_ROOT}/filtered.log")

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

file(
    WRITE "${fixture_source}"
    [=[/** @file */

namespace GameWIP::DoxygenFixture
{
    enum class Effect
    {
        None = 0,
        Value = 1
    };

    constexpr Effect operator|(Effect left, Effect right) noexcept
    {
        return left;
    }

    int missingDeclaration();
}
]=]
)
file(
    WRITE "${fixture_config}"
    "PROJECT_NAME = Doxygen warning-filter fixture\n"
    "OUTPUT_DIRECTORY = \"${TEST_ROOT}/output\"\n"
    "INPUT = \"${fixture_source}\"\n"
    "RECURSIVE = NO\n"
    "GENERATE_HTML = NO\n"
    "GENERATE_LATEX = NO\n"
    "EXTRACT_ALL = NO\n"
    "WARN_IF_UNDOCUMENTED = YES\n"
    "WARN_IF_DOC_ERROR = YES\n"
    "WARN_IF_INCOMPLETE_DOC = YES\n"
    "WARN_IF_UNDOC_ENUM_VAL = NO\n"
    "WARN_LOGFILE = \"${raw_log}\"\n"
)

execute_process(
    COMMAND "${DOXYGEN_EXECUTABLE}" "${fixture_config}"
    RESULT_VARIABLE doxygen_result
    OUTPUT_VARIABLE doxygen_output
    ERROR_VARIABLE doxygen_error
)
if(NOT doxygen_result EQUAL 0)
    message(FATAL_ERROR "The Doxygen warning-filter fixture failed to build.\n${doxygen_output}${doxygen_error}")
endif()

file(STRINGS "${raw_log}" raw_warning_lines)
file(READ "${raw_log}" raw_contents)
set(allowed_warning "")
set(normal_warning_found OFF)
foreach(warning_line IN LISTS raw_warning_lines)
    string(FIND "${warning_line}" "Member operator|" operator_marker)
    if(NOT operator_marker EQUAL -1)
        set(allowed_warning "${warning_line}\n")
    endif()
    string(FIND "${warning_line}" "Member missingDeclaration()" normal_marker)
    if(NOT normal_marker EQUAL -1)
        set(normal_warning_found ON)
    endif()
endforeach()
if(NOT allowed_warning)
    message(FATAL_ERROR "The Doxygen fixture did not produce the approved undocumented operator warning.\n${raw_contents}")
endif()
if(NOT normal_warning_found)
    message(FATAL_ERROR "The Doxygen fixture did not produce the normal undocumented declaration warning.\n${raw_contents}")
endif()

file(WRITE "${allowed_log}" "${allowed_warning}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DWARNING_LOG=${allowed_log}" -P "${filter_script}"
    RESULT_VARIABLE allowed_result
    OUTPUT_VARIABLE allowed_output
    ERROR_VARIABLE allowed_error
)
if(NOT allowed_result EQUAL 0)
    message(FATAL_ERROR "The approved undocumented operator warning was rejected.\n${allowed_output}${allowed_error}")
endif()
file(READ "${allowed_log}" allowed_contents)
if(NOT allowed_contents STREQUAL "")
    message(FATAL_ERROR "The approved undocumented operator warning was not filtered.")
endif()

file(COPY_FILE "${raw_log}" "${filtered_log}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DWARNING_LOG=${filtered_log}" -P "${filter_script}"
    RESULT_VARIABLE normal_result
    OUTPUT_VARIABLE normal_output
    ERROR_VARIABLE normal_error
)
if(normal_result EQUAL 0)
    message(FATAL_ERROR "A genuinely undocumented normal declaration was incorrectly accepted.")
endif()
file(READ "${filtered_log}" filtered_contents)
string(FIND "${filtered_contents}" "Member operator|" filtered_operator_marker)
if(NOT filtered_operator_marker EQUAL -1)
    message(FATAL_ERROR "The approved undocumented operator warning was not filtered from the mixed Doxygen log.")
endif()
string(FIND "${filtered_contents}" "Member missingDeclaration()" filtered_normal_marker)
if(filtered_normal_marker EQUAL -1)
    message(FATAL_ERROR "The genuinely undocumented normal declaration warning was lost.")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
