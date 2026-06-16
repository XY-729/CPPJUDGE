cmake_minimum_required(VERSION 3.10)

project(cppjudge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Library target (all sources except main) ──────────────
add_library(cppjudge_lib STATIC
    src/judge.cpp
    src/compiler.cpp
    src/runner.cpp
    src/comparer.cpp
)

target_include_directories(cppjudge_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── Main executable ───────────────────────────────────────
add_executable(cppjudge
    src/main.cpp
)

target_link_libraries(cppjudge PRIVATE cppjudge_lib)

# ── CTest ─────────────────────────────────────────────────
include(CTest)
enable_testing()

# ── Optional: nsjail ──────────────────────────────────────
find_program(NSJAIL_EXECUTABLE nsjail)
if(NSJAIL_EXECUTABLE)
    message(STATUS "nsjail found: ${NSJAIL_EXECUTABLE}")
else()
    message(STATUS "nsjail not found — nsjail tests will be skipped")
endif()

# ── Unit tests ────────────────────────────────────────────
function(add_cppjudge_unit_test test_name test_source)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE cppjudge_lib)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        LABELS "unit;portable"
        TIMEOUT 10
    )
endfunction()

add_cppjudge_unit_test(test_comparer tests/unit/test_comparer.cpp)
add_cppjudge_unit_test(test_sandbox_types tests/unit/test_sandbox_types.cpp)
add_cppjudge_unit_test(test_verdict_strings tests/unit/test_verdict_strings.cpp)
add_cppjudge_unit_test(test_config tests/unit/test_config.cpp)

# ── Integration tests ─────────────────────────────────────
function(add_cppjudge_integration_test test_name test_script)
    add_test(
        NAME ${test_name}
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/${test_script}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "CPPJUDGE_BIN=$<TARGET_FILE:cppjudge>"
        LABELS "integration;portable"
        TIMEOUT 120
        RUN_SERIAL TRUE
    )
endfunction()

add_cppjudge_integration_test(test_integration_builtin tests/integration/test_builtin_verdicts.sh)
add_cppjudge_integration_test(test_integration_illegal_config tests/integration/test_illegal_config.sh)

if(NSJAIL_EXECUTABLE)
    add_cppjudge_integration_test(test_integration_nsjail tests/integration/test_nsjail_verdicts.sh)
    set_tests_properties(test_integration_nsjail PROPERTIES
        LABELS "integration;nsjail"
        TIMEOUT 120
        RUN_SERIAL TRUE
    )
endif()

# ── Legacy script-based tests ─────────────────────────────
function(add_cppjudge_regression_test test_name test_script)
    add_test(
        NAME ${test_name}
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/${test_script}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "CPPJUDGE_BIN=$<TARGET_FILE:cppjudge>"
        LABELS "regression;portable"
        TIMEOUT 300
        RUN_SERIAL TRUE
    )
endfunction()

add_cppjudge_regression_test(test_regression_default scripts/run_tests.sh)
add_cppjudge_regression_test(test_regression_security scripts/run_security_tests.sh)
add_cppjudge_regression_test(test_regression_structured_error scripts/run_structured_error_tests.sh)

set_tests_properties(test_regression_security PROPERTIES
    LABELS "regression;security;builtin"
)

if(NSJAIL_EXECUTABLE)
    add_cppjudge_regression_test(test_regression_nsjail scripts/run_nsjail_tests.sh)
    set_tests_properties(test_regression_nsjail PROPERTIES
        LABELS "regression;nsjail"
    )
endif()
