# LangBridge - Java <-> C++ source translator
# Build:  mingw32-make  /  make  (choose based on your toolchain)
# The Makefile auto-probes: cl (MSVC) -> g++ -> clang++

SRCS := src/preprocess.cpp src/lexer.cpp src/parser.cpp src/analyze.cpp src/gen_common.cpp \
        src/gen_j2c.cpp src/gen_c2j.cpp src/main.cpp
OBJSDIR := build/obj

CXX ?=
CXXFLAGS ?= -std=c++17 -O2 -Wall
LDFLAGS ?=

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(findstring MINGW,$(UNAME_S)),)
ifeq ($(findstring MSYS,$(UNAME_S)),)
ifeq ($(findstring CYGWIN,$(UNAME_S)),)
  IS_WINDOWS := 0
else
  IS_WINDOWS := 1
endif
else
  IS_WINDOWS := 1
endif
else
  IS_WINDOWS := 1
endif

# --- detect C++ compiler ---
ifeq ($(CXX),)
  CXX := g++
  ifeq (,$(shell command -v g++ 2>/dev/null))
    CXX := clang++
  endif
  ifeq (,$(shell command -v clang++ 2>/dev/null))
    CXX := cl
  endif
endif

# --- MSVC mode: cl needs INCLUDE/LIB from vcvars64.bat; provide a separate entry ---
ifeq ($(findstring cl,$(CXX)),cl)
  $(error "MSVC mode: please use tools/build_local_msvc.bat or run cl inside a 'VsDevCmd' shell")
endif

OBJS := $(patsubst src/%.cpp,$(OBJSDIR)/%.o,$(SRCS))
TARGET := build/langbridge
ifeq ($(IS_WINDOWS),1)
  TARGET := build/langbridge.exe
endif

all: $(TARGET)

$(OBJSDIR)/%.o: src/%.cpp include/bridge.hpp | $(OBJSDIR)
	$(CXX) $(CXXFLAGS) -Iinclude -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJSDIR):
	mkdir -p $@

clean:
	rm -rf build/obj

.PHONY: all clean