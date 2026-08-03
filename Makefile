# Counter-Strafe v25.3 C++ — MinGW Makefile
# MULTI-CONFIGURATION BUILD PIPELINE

CXX      = g++
CMAKE    ?= cmake
CMAKE_TEST_BUILD ?= build/make-tests
BASE_CXXFLAGS = -std=c++20 -fno-omit-frame-pointer -Wall -Wextra -pedantic -Iinclude -Iinclude/core -Iinclude/ui -DWIN32_LEAN_AND_MEAN -msse2 -ffunction-sections -fdata-sections
BASE_LDFLAGS  = -static -Wl,--gc-sections
BASE_LDLIBS   = -luser32 -lwinmm -lgdi32 -lcomdlg32 -lavrt

# Keep these explicit manifests in lockstep with CMakeLists.txt.
COMMON_CORE_SRCS = \
	src/core/bhop.cpp src/core/config_io.cpp \
	src/core/counterstrafe_controller.cpp src/core/debug_logger.cpp \
	src/core/injection.cpp src/core/input_capture.cpp \
	src/core/input_router.cpp src/core/main.cpp \
	src/core/movement_reconstruction.cpp src/core/runtime_config.cpp \
	src/core/state_engine.cpp src/core/state_reconciliation.cpp \
	src/core/target_platform.cpp src/core/telemetry.cpp \
	src/core/timer_lifecycle.cpp src/core/timing.cpp \
	src/core/topology.cpp src/core/workspace.cpp
DIAGNOSTIC_CORE_SRCS = \
	src/core/analysis_toolkit.cpp src/core/etw_controller.cpp
UI_SRCS = \
	src/ui/ui_analysis.cpp src/ui/ui_dashboard.cpp \
	src/ui/ui_layout.cpp src/ui/ui_main.cpp src/ui/ui_settings.cpp

# Default target
all: release

# ==============================================================================
# DEBUG_FORENSIC Configuration
# ==============================================================================
DEBUG_CXXFLAGS = $(BASE_CXXFLAGS) -O0 -g
DEBUG_LDFLAGS  = $(BASE_LDFLAGS)
DEBUG_LDLIBS   = $(BASE_LDLIBS) -ltdh -ldbghelp
DEBUG_OUT      = runtime/bin/marco_debug.exe
DEBUG_OBJDIR   = build/obj/debug
DEBUG_SRCS     = $(COMMON_CORE_SRCS) $(DIAGNOSTIC_CORE_SRCS) $(UI_SRCS)
DEBUG_OBJS     = $(patsubst src/%.cpp,$(DEBUG_OBJDIR)/%.o,$(DEBUG_SRCS))

# ==============================================================================
# PROFILE Configuration
# ==============================================================================
PROFILE_CXXFLAGS = $(BASE_CXXFLAGS) -O2 -DMARCO_PROFILE
PROFILE_LDFLAGS  = $(BASE_LDFLAGS) -mwindows -s
PROFILE_LDLIBS   = $(BASE_LDLIBS) -ltdh -ldbghelp
PROFILE_OUT      = runtime/bin/marco_profile.exe
PROFILE_OBJDIR   = build/obj/profile
# Profile now includes ETW, Diagnostics, and Analysis Toolkit for forensics
PROFILE_SRCS     = $(COMMON_CORE_SRCS) $(DIAGNOSTIC_CORE_SRCS) $(UI_SRCS)
PROFILE_OBJS     = $(patsubst src/%.cpp,$(PROFILE_OBJDIR)/%.o,$(PROFILE_SRCS))

# ==============================================================================
# RELEASE Configuration
# ==============================================================================
RELEASE_CXXFLAGS = $(BASE_CXXFLAGS) -O3 -DNDEBUG -DMARCO_RELEASE -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable
RELEASE_LDFLAGS  = $(BASE_LDFLAGS) -mwindows -s
RELEASE_LDLIBS   = $(BASE_LDLIBS)
RELEASE_OUT      = runtime/bin/marco.exe
RELEASE_OBJDIR   = build/obj/release
RELEASE_SRCS     = $(COMMON_CORE_SRCS) $(UI_SRCS)
RELEASE_OBJS     = $(patsubst src/%.cpp,$(RELEASE_OBJDIR)/%.o,$(RELEASE_SRCS))

# ==============================================================================
# Targets
# ==============================================================================

debug: $(DEBUG_OUT)
profile: $(PROFILE_OUT)
release: $(RELEASE_OUT)

test check:
	$(CMAKE) -S . -B $(CMAKE_TEST_BUILD) -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(CMAKE_TEST_BUILD) --target check --parallel

stress-safe:
	$(CMAKE) -S . -B $(CMAKE_TEST_BUILD) -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(CMAKE_TEST_BUILD) --target stress-safe --parallel

$(DEBUG_OUT): $(DEBUG_OBJS)
	@mkdir -p runtime/bin
	$(CXX) $(DEBUG_LDFLAGS) -o $@ $^ $(DEBUG_LDLIBS)

$(PROFILE_OUT): $(PROFILE_OBJS)
	@mkdir -p runtime/bin
	$(CXX) $(PROFILE_LDFLAGS) -o $@ $^ $(PROFILE_LDLIBS)

$(RELEASE_OUT): $(RELEASE_OBJS)
	@mkdir -p runtime/bin
	$(CXX) $(RELEASE_LDFLAGS) -o $@ $^ $(RELEASE_LDLIBS)

$(DEBUG_OBJDIR)/core/%.o: src/core/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(DEBUG_CXXFLAGS) -c -o $@ $<

$(DEBUG_OBJDIR)/ui/%.o: src/ui/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(DEBUG_CXXFLAGS) -c -o $@ $<

$(PROFILE_OBJDIR)/core/%.o: src/core/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(PROFILE_CXXFLAGS) -c -o $@ $<

$(PROFILE_OBJDIR)/ui/%.o: src/ui/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(PROFILE_CXXFLAGS) -c -o $@ $<

$(RELEASE_OBJDIR)/core/%.o: src/core/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(RELEASE_CXXFLAGS) -c -o $@ $<

$(RELEASE_OBJDIR)/ui/%.o: src/ui/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(RELEASE_CXXFLAGS) -c -o $@ $<

clean:
	@rm -rf build/obj runtime/bin

.PHONY: all debug profile release test check stress-safe clean
