# ---------------------------------------------------------------------------
# Project: viz3d
# ---------------------------------------------------------------------------

# --- Compilation Settings ---
TARGET_NAME := viz3d
BUILD_DIR   := build
BIN_DIR     := bin

CXX      := g++
CC       := gcc
NVCC     := nvcc  # <--- NEW: CUDA Compiler
CXX_STD  := -std=c++17
C_STD    := -std=c11

# Defaults to release, use 'make BUILD=debug' to override
BUILD ?= release

# Base Flags
CPPFLAGS := -Wall -MMD -MP # -MMD -MP generates dependency files (.d)
CFLAGS   := $(C_STD)
CXXFLAGS := $(CXX_STD)
LDFLAGS  := -Wl,-rpath,/usr/local/lib # Add runtime path for libraries

# --- CUDA Flags (NEW) ---
# -x cu: Treat input as CUDA
# -dc: Generate relocatable device code (crucial for linking)
# --ptxas-options=-v: Verbose PTX assembly (optional, good for debug)
#NVCCFLAGS := -x cu -dc -std=c++17 
NVCCFLAGS := -x cu -std=c++17 --compiler-options '-fPIC' --expt-relaxed-constexpr --extended-lambda
# Add architecture flags if known (e.g. -arch=sm_60). Leaving auto for now.

# --- Build Type Configuration ---
ifeq ($(BUILD), debug)
    CPPFLAGS += -g -O0
    NVCCFLAGS += -g -G # -G enables device debug symbols
    DEST_DIR := $(BIN_DIR)/debug
else
    CPPFLAGS += -O3 -DNDEBUG
    NVCCFLAGS += -O3
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
INCLUDES += -I/usr/local/include
LIBS     += -L/usr/local/lib -lopenvdb -ltbb
CPPFLAGS += -DUSE_OPENVDB -DUSE_NANOVDB -DNANOVDB_USE_OPENVDB -DNANOVDB_USE_CUDA

# 5. CUDA Libraries (NEW)
# We need to link against the CUDA runtime
LIBS     += -lcudart

# 6. GLM
ifneq ("$(wildcard $(PACKAGES_DIR)/glm/glm/glm.hpp)","")
    INCLUDES += -I$(PACKAGES_DIR)/glm
endif

# 7. ImGui Definitions
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

# CUDA Sources (NEW)
SRCS_CU := $(shell find $(SRC_DIR) -name '*.cu')

# Combine sources
ALL_SRCS_CPP := $(SRCS_CPP) $(SRCS_IMGUI)
ALL_SRCS_C   := $(SRCS_C)

# --- Object Generation ---
# Map source files to object files in the build directory
OBJS_CPP := $(ALL_SRCS_CPP:%.cpp=$(BUILD_DIR)/%.o)
OBJS_C   := $(ALL_SRCS_C:%.c=$(BUILD_DIR)/%.o)
OBJS_CU  := $(SRCS_CU:%.cu=$(BUILD_DIR)/%.o) # NEW: CUDA Objects

OBJS := $(OBJS_CPP) $(OBJS_C) $(OBJS_CU)
DEPS := $(OBJS:.o=.d)

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------

.PHONY: all clean run copy_shaders directories

all: directories $(TARGET) copy_shaders

# Link the final executable
# Note: When linking CUDA objects, it's often safer to use nvcc for linking, 
# or ensure -lcudart is passed to g++. Here we use g++ with -lcudart.
# We also add a specific "device link" step if separate compilation is used,
# but for simple setups, standard linking often works if -cudart is present.
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

# Compile CUDA source (NEW)
$(BUILD_DIR)/%.o: %.cu
	@mkdir -p $(dir $@)
	@echo "Compiling CUDA $<"
	@$(NVCC) $(NVCCFLAGS) $(INCLUDES) -c $< -o $@

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