# inspired by https://stackoverflow.com/questions/30573481/how-to-write-a-makefile-with-separate-source-and-header-directories


.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.PHONY: all macos linux clean 

clean:
	$(RM) -r build

ifeq ($(PLATFORM),)

HOST_OS := $(shell uname -s)

ifeq ($(HOST_OS),Darwin)
all: macos
else ifeq ($(HOST_OS),Linux)
all: linux
else
all:
	$(error Unsupported host $(HOST_OS). Supported: macos,linux)
endif

macos linux:
	$(MAKE) PLATFORM=$@ all 

else 

SRC_DIR := src
IMGUI_DIR := include/imgui
PLATFORM_DIR := $(SRC_DIR)/platform/$(PLATFORM)

BUILD_DIR := build/$(PLATFORM)
OBJ_DIR = $(BUILD_DIR)/obj
GEN_DIR = $(BUILD_DIR)/include/generated
EXE := $(BUILD_DIR)/bin/emu

CPP_SRC := $(shell find $(SRC_DIR)/emulation -type f -name '*.cpp')
CPP_SRC += $(wildcard $(IMGUI_DIR)/*.cpp)
CPP_SRC += $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp
OBJC_SRC :=
GENERATED_HEADERS :=

CPPFLAGS += -I$(BUILD_DIR)/include -Iinclude
CPPFLAGS += -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -DNEWMAIN
CXXFLAGS = -std=c++17 -Wall -Wformat -g -O0
CFLAGS := -Wformat

ifeq ($(PLATFORM),macos)

ifeq ($(origin CXX),default)
CXX := clang++
endif

CPP_SRC += $(shell find $(PLATFORM_DIR) -type f -name '*.cpp')
OBJC_SRC += $(shell find $(PLATFORM_DIR) -type f -name '*.mm')
OBJC_SRC += $(IMGUI_DIR)/backends/imgui_impl_metal.mm

CPPFLAGS += -I/usr/local/include/metal-cpp
CPPFLAGS += -I/opt/homebrew/include -I/opt/homebrew/include/SDL2
OBJCXXFLAGS += -fobjc-weak -fobjc-arc

LDFLAGS += -L/opt/homebrew/lib 
LDLIBS += -lSDL2
LDLIBS += -framework Foundation -framework Metal -framework QuartzCore 

SHADER_SRC := $(shell find $(PLATFORM_DIR) -type f -name '*.metal')
SHADER_OBJ := $(patsubst %.metal,$(OBJ_DIR)/%.air,$(SHADER_SRC))
SHADER_LIB := $(OBJ_DIR)/shaders.metallib
SHADER_H := $(GEN_DIR)/metal_shaders.generated.h 
GENERATED_HEADERS += $(SHADER_H)

$(OBJ_DIR)/%.air: %.metal 
	@mkdir -p "$(@D)"
	xcrun -sdk macosx metal $(METALFLAGS) -c "$<" -o "$@"

$(SHADER_LIB): $(SHADER_OBJ) 
	@mkdir -p "$(@D)"
	xcrun -sdk macosx metallib -o "$@" $(SHADER_OBJ)

$(SHADER_H): $(SHADER_LIB) 
	@mkdir -p "$(@D)"
	xxd -i -n obj_shaders_metallib "$<" > "$@.tmp"
	mv "$@.tmp" "$@"

else ifeq ($(PLATFORM),linux)

CC := gcc

CPP_SRC += $(shell find $(PLATFORM_DIR) -type f -name '*.cpp')
CPP_SRC += $(shell find $(PLATFORM_DIR) -type f -name '*.c')
CPP_SRC += $(IMGUI_DIR)/backends/imgui_impl_vulkan.cpp

CPPFLAGS += -I/usr/local/include -I/usr/local/include/SDL2 
LDFLAGS += -L/usr/local/lib 
LDLIBS += -lSDL2 -lvulkan

PLATFORM_SRC := $(shell find $(PLATFORM_DIR) -name '*.c')
PLATFORM_OBJ := $(addprefix $(OBJ_DIR)/,$(addsuffix .o,$(PLATFORM_SRC)))
PLATFORM_EXE := $(BUILD_DIR)/bin/platform-test

$(PLATFORM_EXE): $(PLATFORM_OBJ)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o "$@" $^

platform: $(PLATFORM_EXE)

else
$(error Unsupported platform $(PLATFORM). Supported: macos,linux)
endif

SOURCES := $(sort $(CPP_SRC) $(OBJC_SRC))
OBJ := $(addprefix $(OBJ_DIR)/,$(addsuffix .o,$(SOURCES)))
DEP := $(OBJ:.o=.d)

all: $(EXE)

$(EXE): $(OBJ)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o "$@" $(OBJ) $(LDLIBS)

$(OBJ): | $(GENERATED_HEADERS)

$(OBJ_DIR)/%.cpp.o: %.cpp
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(OBJ_DIR)/%.c.o: %.c
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(OBJ_DIR)/%.mm.o: %.mm
	@mkdir -p "$(@D)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OBJCXXFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

-include $(DEP)

endif # PLATFORM
