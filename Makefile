# Counter-Strafe v25.3 C++ — MinGW Makefile
# MULTI-CONFIGURATION BUILD PIPELINE

CXX      = g++
BASE_CXXFLAGS = -std=c++20 -fno-omit-frame-pointer -Wall -Wextra -pedantic -Iinclude -Iinclude/core -Iinclude/ui -DWIN32_LEAN_AND_MEAN -msse2 -ffunction-sections -fdata-sections
BASE_LDFLAGS  = -static -Wl,--gc-sections
BASE_LDLIBS   = -luser32 -lwinmm -lgdi32 -lcomdlg32 -lavrt

# Default target
all: release

# ==============================================================================
# DEBUG_FORENSIC Configuration
# ==============================================================================
DEBUG_CXXFLAGS = $(BASE_CXXFLAGS) -O0 -g -DMARCO_DEBUG_FORENSIC
DEBUG_LDFLAGS  = $(BASE_LDFLAGS)
DEBUG_LDLIBS   = $(BASE_LDLIBS) -ltdh -ldbghelp
DEBUG_OUT      = runtime/bin/marco_debug.exe
DEBUG_OBJDIR   = build/obj/debug
DEBUG_SRCS     = $(wildcard src/core/*.cpp src/ui/*.cpp)
DEBUG_OBJS     = $(patsubst src/%.cpp,$(DEBUG_OBJDIR)/%.o,$(DEBUG_SRCS))

# ==============================================================================
# PROFILE Configuration
# ==============================================================================
PROFILE_CXXFLAGS = $(BASE_CXXFLAGS) -O2 -DMARCO_PROFILE
PROFILE_LDFLAGS  = $(BASE_LDFLAGS) -mwindows -s
PROFILE_LDLIBS   = $(BASE_LDLIBS) -ltdh -ldbghelp
PROFILE_OUT      = runtime/bin/marco_profile.exe
PROFILE_OBJDIR   = build/obj/profile
# Exclude ETW, Diagnostics, and Analysis Toolkit
PROFILE_EXCLUDE  = src/core/etw_controller.cpp src/ui/ui_diagnostics.cpp src/core/analysis_toolkit.cpp
PROFILE_SRCS     = $(filter-out $(PROFILE_EXCLUDE),$(wildcard src/core/*.cpp src/ui/*.cpp))
PROFILE_OBJS     = $(patsubst src/%.cpp,$(PROFILE_OBJDIR)/%.o,$(PROFILE_SRCS))

# ==============================================================================
# RELEASE Configuration
# ==============================================================================
RELEASE_CXXFLAGS = $(BASE_CXXFLAGS) -O3 -DNDEBUG -DMARCO_RELEASE -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable
RELEASE_LDFLAGS  = $(BASE_LDFLAGS) -mwindows -s
RELEASE_LDLIBS   = $(BASE_LDLIBS)
RELEASE_OUT      = runtime/bin/marco.exe
RELEASE_OBJDIR   = build/obj/release
# Exclude ETW, Forensic Dashboards, Diagnostics, Toolkit
RELEASE_EXCLUDE  = src/core/etw_controller.cpp src/ui/ui_diagnostics.cpp src/ui/ui_analysis.cpp src/core/analysis_toolkit.cpp src/core/debug_logger.cpp
RELEASE_SRCS     = $(filter-out $(RELEASE_EXCLUDE),$(wildcard src/core/*.cpp src/ui/*.cpp))
RELEASE_OBJS     = $(patsubst src/%.cpp,$(RELEASE_OBJDIR)/%.o,$(RELEASE_SRCS))

# ==============================================================================
# Targets
# ==============================================================================

debug: $(DEBUG_OUT)
profile: $(PROFILE_OUT)
release: $(RELEASE_OUT)

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

.PHONY: all debug profile release clean
