# Marco Engine — MinGW Makefile (headless core)
# Two targets only: release -> ./marco.exe, debug -> ./marco_debug.exe

CXX      = g++
WINDRES  ?= windres
CMAKE    ?= cmake
CMAKE_TEST_BUILD ?= build/make-tests
BASE_CXXFLAGS = -std=c++20 -fno-omit-frame-pointer -Wall -Wextra -pedantic -Iinclude -Iinclude/core -DWIN32_LEAN_AND_MEAN -msse2 -ffunction-sections -fdata-sections
BASE_LDFLAGS  = -static -Wl,--gc-sections
BASE_LDLIBS   = -luser32 -lwinmm -lws2_32 -lavrt -lshell32

# Keep this manifest in lockstep with CMakeLists.txt.
COMMON_CORE_SRCS = \
	src/core/bhop.cpp src/core/config_io.cpp \
	src/core/counterstrafe_controller.cpp src/core/debug_logger.cpp \
	src/core/injection.cpp src/core/syscall_dispatch.cpp src/core/input_capture.cpp \
	src/core/input_router.cpp src/core/ipc_server.cpp src/core/main.cpp \
	src/core/movement_reconstruction.cpp src/core/runtime_config.cpp \
	src/core/state_engine.cpp src/core/state_reconciliation.cpp \
	src/core/target_platform.cpp src/core/telemetry.cpp \
	src/core/timer_lifecycle.cpp src/core/timing.cpp \
	src/core/topology.cpp src/core/workspace.cpp src/core/system_tray.cpp
DIAGNOSTIC_CORE_SRCS = \
	src/core/analysis_toolkit.cpp src/core/etw_controller.cpp

# Default target
all: release

# ==============================================================================
# DEBUG (console, diagnostics)
# ==============================================================================
DEBUG_CXXFLAGS = $(BASE_CXXFLAGS) -O0 -g
DEBUG_LDFLAGS  = $(BASE_LDFLAGS)
DEBUG_LDLIBS   = $(BASE_LDLIBS) -ltdh -ldbghelp
DEBUG_OUT      = marco_debug.exe
DEBUG_OBJDIR   = build/obj/debug
DEBUG_SRCS     = $(COMMON_CORE_SRCS) $(DIAGNOSTIC_CORE_SRCS)
DEBUG_OBJS     = $(patsubst src/%.cpp,$(DEBUG_OBJDIR)/%.o,$(DEBUG_SRCS)) $(DEBUG_OBJDIR)/resource.o

# ==============================================================================
# RELEASE (headless daemon, no console window)
# ==============================================================================
RELEASE_CXXFLAGS = $(BASE_CXXFLAGS) -O3 -DNDEBUG -DMARCO_RELEASE -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable
RELEASE_LDFLAGS  = $(BASE_LDFLAGS) -mwindows -s
RELEASE_LDLIBS   = $(BASE_LDLIBS)
RELEASE_OUT      = marco.exe
RELEASE_OBJDIR   = build/obj/release
RELEASE_SRCS     = $(COMMON_CORE_SRCS)
RELEASE_OBJS     = $(patsubst src/%.cpp,$(RELEASE_OBJDIR)/%.o,$(RELEASE_SRCS)) $(RELEASE_OBJDIR)/resource.o

# ==============================================================================
# Targets
# ==============================================================================
debug: $(DEBUG_OUT)
release: $(RELEASE_OUT)

test check:
	$(CMAKE) -S . -B $(CMAKE_TEST_BUILD) -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(CMAKE_TEST_BUILD) --target check --parallel

stress-safe:
	$(CMAKE) -S . -B $(CMAKE_TEST_BUILD) -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(CMAKE_TEST_BUILD) --target stress-safe --parallel

$(DEBUG_OUT): $(DEBUG_OBJS)
	$(CXX) $(DEBUG_LDFLAGS) -o $@ $^ $(DEBUG_LDLIBS)

$(RELEASE_OUT): $(RELEASE_OBJS)
	$(CXX) $(RELEASE_LDFLAGS) -o $@ $^ $(RELEASE_LDLIBS)

$(DEBUG_OBJDIR)/resource.o $(RELEASE_OBJDIR)/resource.o: resources/resource.rc resources/icon.ico include/core/resource.h
	@mkdir -p $(dir $@)
	$(WINDRES) -Iinclude/core -Iresources -i $< -o $@

$(DEBUG_OBJDIR)/core/%.o: src/core/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(DEBUG_CXXFLAGS) -c -o $@ $<

$(RELEASE_OBJDIR)/core/%.o: src/core/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(RELEASE_CXXFLAGS) -c -o $@ $<

clean:
	@rm -rf build/obj $(RELEASE_OUT) $(DEBUG_OUT)

.PHONY: all debug release test check stress-safe clean
