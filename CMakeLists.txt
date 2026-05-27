cmake_minimum_required(VERSION 3.20)
project(counter_strafe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/runtime/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/lib)

# Source files
set(SOURCES
    src/main.cpp
    src/timing.cpp
    src/physics.cpp
    src/injection.cpp
    src/state_engine.cpp
    src/input_capture.cpp
    src/overlay.cpp
)

set(HEADERS
    src/config.h
    src/types.h
    src/logger.h
    src/timing.h
    src/state.h
    src/physics.h
    src/injection.h
    src/state_engine.h
    src/input_capture.h
    src/overlay.h
)

# Windows GUI application (no console window)
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

# Include path
target_include_directories(${PROJECT_NAME} PRIVATE src)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    user32
    winmm
    gdi32
)

# MSVC optimizations
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /O2        # Full optimization
        /Ob2       # Aggressive inlining
        /Oi        # Intrinsic functions
        /GL        # Whole program optimization
        /GS-       # No buffer security checks (perf)
        /fp:fast   # Fast floating point
        /W4        # High warning level
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        /LTCG      # Link-time code generation
    )
endif()

# Optional: Debug build with logging
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE CS_DEBUG=1)
endif()
