cmake_minimum_required(VERSION 3.20)

# Two first-class configurations only: Debug (marco_debug.exe) and Release (marco.exe).
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose Debug or Release" FORCE)
endif()

project(marco LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# This is the authoritative application source manifest.  Keep the matching
# lists in Makefile in lockstep; do not use wildcards here, so source additions
# require an intentional build-manifest review.
set(MARCO_COMMON_CORE_SOURCES
    src/core/bhop.cpp
    src/core/config_io.cpp
    src/core/counterstrafe_controller.cpp
    src/core/debug_logger.cpp
    src/core/injection.cpp
    src/core/input_capture.cpp
    src/core/input_router.cpp
    src/core/ipc_server.cpp
    src/core/main.cpp
    src/core/movement_reconstruction.cpp
    src/core/runtime_config.cpp
    src/core/state_engine.cpp
    src/core/state_reconciliation.cpp
    src/core/target_platform.cpp
    src/core/telemetry.cpp
    src/core/timer_lifecycle.cpp
    src/core/timing.cpp
    src/core/topology.cpp
    src/core/workspace.cpp
)
set(MARCO_DIAGNOSTIC_CORE_SOURCES
    src/core/analysis_toolkit.cpp
    src/core/etw_controller.cpp
)

add_library(marco_build_options INTERFACE)
target_include_directories(marco_build_options INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/core"
)
target_compile_definitions(marco_build_options INTERFACE WIN32_LEAN_AND_MEAN)

# Keep buffer security enabled.  In particular, never add /GS- to this project.
if(MSVC)
    target_compile_options(marco_build_options INTERFACE /W4 /permissive- /GS)
    target_link_options(marco_build_options INTERFACE /guard:cf)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(marco_build_options INTERFACE
        -Wall -Wextra -Wpedantic
        -fno-omit-frame-pointer -fstack-protector-strong
        -ffunction-sections -fdata-sections
    )
    target_link_options(marco_build_options INTERFACE
        -Wl,--gc-sections -Wl,--dynamicbase -Wl,--nxcompat
    )
endif()

# Headless daemon: Release has no console window, Debug keeps one for logs.
add_executable(marco ${MARCO_COMMON_CORE_SOURCES})
set_target_properties(marco PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
# Release intentionally omits diagnostics-only implementation units.
target_sources(marco PRIVATE
    $<$<NOT:$<CONFIG:Release>>:${MARCO_DIAGNOSTIC_CORE_SOURCES}>
)
target_link_libraries(marco PRIVATE
    marco_build_options user32 winmm ws2_32 avrt
    $<$<NOT:$<CONFIG:Release>>:tdh>
    $<$<NOT:$<CONFIG:Release>>:dbghelp>
)
target_compile_definitions(marco PRIVATE
    $<$<CONFIG:Release>:MARCO_RELEASE>
)
if(MINGW)
    target_link_options(marco PRIVATE -static)
endif()

# Binaries land in the repository root next to marco.ini.
set_target_properties(marco PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
    OUTPUT_NAME_DEBUG marco_debug
    OUTPUT_NAME_RELEASE marco
)

# Every test has one and only one main() and is linked from an explicit source
# list.  Tests are EXCLUDE_FROM_ALL so a normal application build cannot run
# or accidentally pull a test main into marco.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
function(marco_add_safe_test target)
    add_executable(${target} EXCLUDE_FROM_ALL
        tests/support/desktop_isolation_guard.cpp
        ${ARGN}
    )
    target_link_libraries(${target} PRIVATE marco_build_options)
    # The test programs use assert() as their failure boundary; Release sets
    # NDEBUG globally, which would otherwise compile out both checks and any
    # setup calls inside those expressions.
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/scripts/test/check_no_input_imports.py"
                $<TARGET_FILE:${target}>
        COMMENT "Auditing ${target} for real desktop-input imports"
        VERBATIM
    )
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
    set_tests_properties(${target} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
        LABELS "safe"
    )
endfunction()

marco_add_safe_test(marco_test_injection
    tests/unit/test_injection.cpp
    src/core/injection.cpp
)
# This causes DefaultSendInput to be a local fake.  The binary neither imports
# nor calls user32!SendInput; the test installs its deterministic backend.
target_compile_definitions(marco_test_injection PRIVATE MARCO_INJECTION_TESTING)

marco_add_safe_test(marco_test_physics
    tests/unit/test_physics.cpp
    src/core/movement_reconstruction.cpp
    tests/unit/movement_runtime_config_stubs.cpp
)
marco_add_safe_test(marco_test_hybrid
    tests/unit/test_hybrid.cpp
    src/core/movement_reconstruction.cpp
    tests/unit/movement_runtime_config_stubs.cpp
)
marco_add_safe_test(marco_test_debug_logger
    tests/integration/test_debug_logger.cpp
    src/core/debug_logger.cpp
    tests/integration/debug_logger_stubs.cpp
)
marco_add_safe_test(marco_test_runtime_config
    tests/integration/test_runtime_config.cpp
    src/core/runtime_config.cpp
    tests/integration/runtime_config_stubs.cpp
)
marco_add_safe_test(marco_test_target_publication
    tests/unit/test_target_publication.cpp
)

marco_add_safe_test(marco_test_bhop_injection_gate
    tests/unit/test_bhop_injection_gate.cpp
)
marco_add_safe_test(marco_test_bhop_injection_queue
    tests/unit/test_bhop_injection_queue.cpp
)

marco_add_safe_test(marco_test_timing_lifecycle
    tests/integration/test_timing_lifecycle.cpp
    src/core/timing.cpp
)
target_compile_definitions(marco_test_timing_lifecycle PRIVATE
    MARCO_TIMING_TESTING)
marco_add_safe_test(marco_test_message_pump
    tests/integration/test_message_pump_budget.cpp
)
marco_add_safe_test(marco_test_engine_dispatch_gate
    tests/integration/test_engine_dispatch_gate.cpp
    src/core/injection.cpp
)
target_compile_definitions(marco_test_engine_dispatch_gate PRIVATE
    MARCO_INJECTION_TESTING)
marco_add_safe_test(marco_test_diagnostic_rings
    tests/integration/test_diagnostic_rings.cpp
    src/core/analysis_toolkit.cpp
)
marco_add_safe_test(marco_test_routed_input_queue
    tests/unit/test_routed_input_queue.cpp
)
marco_add_safe_test(marco_test_diagonal_engine_stress
    tests/integration/test_diagonal_engine_stress.cpp
    tests/integration/diagonal_engine_stubs.cpp
    src/core/input_router.cpp
    src/core/counterstrafe_controller.cpp
    src/core/timer_lifecycle.cpp
    src/core/injection.cpp
    src/core/movement_reconstruction.cpp
)
target_compile_definitions(marco_test_diagonal_engine_stress PRIVATE
    MARCO_INJECTION_TESTING MARCO_RELEASE)
set_tests_properties(marco_test_diagonal_engine_stress PROPERTIES TIMEOUT 25)
marco_add_safe_test(marco_test_config_io_atomic
    tests/integration/test_config_io_atomic.cpp
    tests/integration/config_io_stubs.cpp
    src/core/config_io.cpp
)
add_test(NAME marco_test_no_input_import_parser
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/test_no_input_import_parser.py")
set_tests_properties(marco_test_no_input_import_parser PROPERTIES
    LABELS "safe")
add_test(NAME marco_test_hook_deferral
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/test_hook_deferral.py")
set_tests_properties(marco_test_hook_deferral PROPERTIES
    LABELS "safe")

# Pure-math fixture producer.  It is deliberately linked against the test
# RuntimeConfig stub rather than runtime configuration, telemetry, hooks, or
# any input-injection implementation.
add_executable(marco_physics_fixture_simulator EXCLUDE_FROM_ALL
    tests/regression/deterministic_simulator.cpp
    src/core/movement_reconstruction.cpp
    tests/unit/movement_runtime_config_stubs.cpp
)
target_link_libraries(marco_physics_fixture_simulator PRIVATE marco_build_options)

add_test(NAME marco_physics_fixture_regression
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test/run_regression.py"
        --simulator "$<TARGET_FILE:marco_physics_fixture_simulator>"
        --fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/regression/fixtures/physics_v27_baseline.csv"
)
set_tests_properties(marco_physics_fixture_regression PROPERTIES
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    LABELS "safe;regression"
)

set(MARCO_SAFE_STRESS_ITERATIONS 300 CACHE STRING
    "Iterations for the desktop-safe fake-backend stress target")
add_custom_target(stress-safe
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test/run_desktop_safe_stress.py"
            --build-dir "$<TARGET_FILE_DIR:marco_test_injection>"
            --iterations "${MARCO_SAFE_STRESS_ITERATIONS}"
    DEPENDS
        marco_test_injection
        marco_test_physics
        marco_test_hybrid
        marco_test_debug_logger
        marco_test_runtime_config
        marco_test_target_publication
        marco_test_bhop_injection_gate
        marco_test_bhop_injection_queue
        marco_test_timing_lifecycle
        marco_test_message_pump
        marco_test_engine_dispatch_gate
        marco_test_diagnostic_rings
        marco_test_routed_input_queue
        marco_test_diagonal_engine_stress
        marco_test_config_io_atomic
    USES_TERMINAL
)

add_custom_target(check
    COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
    DEPENDS
        marco_test_injection
        marco_test_physics
        marco_test_hybrid
        marco_test_debug_logger
        marco_test_runtime_config
        marco_test_target_publication
        marco_test_bhop_injection_gate
        marco_test_bhop_injection_queue
        marco_test_timing_lifecycle
        marco_test_message_pump
        marco_test_engine_dispatch_gate
        marco_test_diagnostic_rings
        marco_test_routed_input_queue
        marco_test_diagonal_engine_stress
        marco_test_config_io_atomic
        marco_physics_fixture_simulator
    USES_TERMINAL
)
