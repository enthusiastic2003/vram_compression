# -------------------------------
# Compiler and flags
# -------------------------------
CXX = g++
CC = gcc
CXXFLAGS = -std=c++17 -Wall -g -O2
CFLAGS = -std=c11 -Wall -g -O2

# -------------------------------
# Directories
# -------------------------------
SRC_DIR = src
PACKAGES_DIR = packages
INC_DIRS = -Iincludes -Ipackages -Ipackages/glad/include -Ipackages/imgui
OBJ_DIR = obj
TARGET = viz3d
IMGUI_DIR = packages/imgui

IMGUI_SOURCES = $(IMGUI_DIR)/imgui.cpp \
                $(IMGUI_DIR)/imgui_draw.cpp \
                $(IMGUI_DIR)/imgui_widgets.cpp \
				$(IMGUI_DIR)/imgui_demo.cpp \
                $(IMGUI_DIR)/imgui_tables.cpp \
                $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
                $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp

# System libraries (using pkg-config for GLFW)
GLFW_FLAGS = $(shell pkg-config --cflags --libs glfw3)

# Platform detection and libraries
ifeq ($(OS),Windows_NT)
    # Windows (MSYS2/MinGW)
    SYS_LIBS = -lopengl32 -lgdi32
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        # Linux
        SYS_LIBS = -ldl -lpthread -lm -lGL -lX11
    else ifeq ($(UNAME_S),Darwin)
        # macOS
        SYS_LIBS = -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
    else
        # Fallback
        SYS_LIBS = -ldl -lpthread -lm -lGL
    endif
endif

# -------------------------------
# Source files
# -------------------------------
SOURCES_CPP = $(shell find $(SRC_DIR) -name '*.cpp')
SOURCES_C   = $(shell find $(PACKAGES_DIR) -name '*.c')
SOURCES_CPP += $(IMGUI_SOURCES)

# -------------------------------
# Object files (mirroring folder structure)
# -------------------------------
OBJECTS_CPP = $(patsubst %,$(OBJ_DIR)/%,$(SOURCES_CPP:.cpp=.o))
OBJECTS_C   = $(patsubst %,$(OBJ_DIR)/%,$(SOURCES_C:.c=.o))
OBJECTS     = $(OBJECTS_CPP) $(OBJECTS_C)

# -------------------------------
# Default target
# -------------------------------
all: $(TARGET)

# -------------------------------
# Link
# -------------------------------
$(TARGET): $(OBJECTS)
	@echo "Linking..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(GLFW_FLAGS) $(SYS_LIBS)

# -------------------------------
# Compilation rules
# -------------------------------
# C++ files
$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling (C++) $<..."
	$(CXX) $(CXXFLAGS) $(INC_DIRS) -c $< -o $@

# C files
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "Compiling (C) $<..."
	$(CC) $(CFLAGS) $(INC_DIRS) -c $< -o $@

# -------------------------------
# Clean
# -------------------------------
clean:
	@echo "Cleaning up..."
	@rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean