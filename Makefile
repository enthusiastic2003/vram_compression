# ---------------------------------------------------------------------------
# Project: viz3d
# ---------------------------------------------------------------------------

# --- Compilation Settings ---
TARGET_NAME := viz3d
BUILD_DIR   := build
BIN_DIR     := bin

CXX      := g++
CC       := gcc
CXX_STD  := -std=c++17
C_STD    := -std=c11

# Defaults to release, use 'make BUILD=debug' to override
BUILD ?= release

# Base Flags
CPPFLAGS := -Wall -MMD -MP # -MMD -MP generates dependency files (.d)
CFLAGS   := $(C_STD)
CXXFLAGS := $(CXX_STD)
LDFLAGS  := 

# --- Build Type Configuration ---
ifeq ($(BUILD), debug)
    CPPFLAGS += -g -O0
    DEST_DIR := $(BIN_DIR)/debug
else
    CPPFLAGS += -O3 -DNDEBUG
    DEST_DIR := $(BIN_DIR)/release
endif

TARGET := $(DEST_DIR)/$(TARGET_NAME)

# --- Directories ---
SRC_DIR      := src
PACKAGES_DIR := packages
SHADER_DIR   := shaders
IMGUI_DIR    := $(PACKAGES_DIR)/imgui

# --- Libraries & Includes ---

# 1. Includes
INCLUDES := -Iincludes \
            -I$(PACKAGES_DIR) \
            -I$(PACKAGES_DIR)/glad/include \
            -I$(IMGUI_DIR) \
            -I$(IMGUI_DIR)/backends

# 2. GLFW (via pkg-config)
INCLUDES += $(shell pkg-config --cflags glfw3)
LIBS     += $(shell pkg-config --libs glfw3)

# 3. System Libraries (Linux specific based on your CMake)
LIBS     += -ldl -lpthread -lm -lGL -lX11

# 4. OpenVDB & NanoVDB
# Note: OpenVDB usually requires TBB. Adjust -L path if installed elsewhere.
INCLUDES += -I/usr/local/include
LIBS     += -L/usr/local/lib -lopenvdb -ltbb
CPPFLAGS += -DUSE_OPENVDB -DUSE_NANOVDB

# 5. GLM (Check packages, fallback to system)
ifneq ("$(wildcard $(PACKAGES_DIR)/glm/glm/glm.hpp)","")
    INCLUDES += -I$(PACKAGES_DIR)/glm
else
    # Assuming system GLM is in default path, otherwise add -I/path/to/glm
endif

# 6. ImGui Definitions
CPPFLAGS += -DUSE_IMGUI

# --- Source File Discovery ---

# Recursive search for .cpp files in src/
SRCS_CPP := $(shell find $(SRC_DIR) -name '*.cpp')

# ImGui Sources
SRCS_IMGUI := $(IMGUI_DIR)/imgui.cpp \
              $(IMGUI_DIR)/imgui_draw.cpp \
              $(IMGUI_DIR)/imgui_widgets.cpp \
              $(IMGUI_DIR)/imgui_demo.cpp \
              $(IMGUI_DIR)/imgui_tables.cpp \
              $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
              $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

# GLAD Sources
SRCS_C := $(shell find $(PACKAGES_DIR)/glad -name '*.c')

# Combine sources
ALL_SRCS_CPP := $(SRCS_CPP) $(SRCS_IMGUI)
ALL_SRCS_C   := $(SRCS_C)

# --- Object Generation ---
# Map source files to object files in the build directory
OBJS := $(ALL_SRCS_CPP:%.cpp=$(BUILD_DIR)/%.o) $(ALL_SRCS_C:%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------

.PHONY: all clean run copy_shaders directories

all: directories $(TARGET) copy_shaders

# Link the final executable
$(TARGET): $(OBJS)
	@echo "Linking $@"
	@$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)

# Compile C++ source
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling C++ $<"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Compile C source
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "Compiling C   $<"
	@$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Copy shaders
copy_shaders:
	@if [ -d "$(SHADER_DIR)" ]; then \
		echo "Copying shaders to $(DEST_DIR)/shaders"; \
		mkdir -p $(DEST_DIR)/shaders; \
		cp -r $(SHADER_DIR)/* $(DEST_DIR)/shaders/; \
	fi

# Create directories
directories:
	@mkdir -p $(DEST_DIR)
	@mkdir -p $(BUILD_DIR)

# Clean build artifacts
clean:
	@echo "Cleaning..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)

# Run the executable
run: all
	@echo "Running $(TARGET)..."
	@./$(TARGET)

# Include dependency files
-include $(DEPS)