cmake_minimum_required(VERSION 3.10)
project(viz3d LANGUAGES C CXX)

# -------------------------------
# Compiler settings
# -------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Separate debug and release flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# -------------------------------
# Options
# -------------------------------
option(BUILD_WITH_IMGUI "Build with ImGui support" ON)
option(COPY_SHADERS "Copy shaders to build directory" ON)

# -------------------------------
# Directories
# -------------------------------
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")
set(PACKAGES_DIR "${CMAKE_SOURCE_DIR}/packages")
set(SHADER_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(DATA_DIR "${CMAKE_SOURCE_DIR}/data")
set(IMGUI_DIR "${PACKAGES_DIR}/imgui")
set(TARGET_NAME "viz3d")

# -------------------------------
# GLM - Header-only library
# -------------------------------
# Check if GLM is in packages directory
if(EXISTS "${PACKAGES_DIR}/glm/glm/glm.hpp")
    set(GLM_INCLUDE_DIR "${PACKAGES_DIR}/glm")
    message(STATUS "Found GLM in packages directory: ${GLM_INCLUDE_DIR}")
else()
    # Try to find system GLM
    find_package(glm QUIET)
    if(glm_FOUND)
        message(STATUS "Found system GLM")
    else()
        message(WARNING "GLM not found. Please install GLM or place it in packages/glm/")
    endif()
endif()

# -------------------------------
# Include directories
# -------------------------------
include_directories(
    "${CMAKE_SOURCE_DIR}/includes"
    "${PACKAGES_DIR}"
    "${PACKAGES_DIR}/glad/include"
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

if(GLM_INCLUDE_DIR)
    include_directories("${GLM_INCLUDE_DIR}")
endif()

# -------------------------------
# Collect source files
# -------------------------------
file(GLOB_RECURSE SOURCES_CPP "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE SOURCES_C "${PACKAGES_DIR}/glad/*.c")

# ImGui sources (conditional)
if(BUILD_WITH_IMGUI)
    set(IMGUI_SOURCES
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp"
        "${IMGUI_DIR}/imgui_demo.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
    )
    add_compile_definitions(USE_IMGUI)
endif()

set(ALL_SOURCES ${SOURCES_CPP} ${SOURCES_C} ${IMGUI_SOURCES})

# -------------------------------
# GLFW
# -------------------------------
find_package(PkgConfig REQUIRED)
pkg_search_module(GLFW3 REQUIRED glfw3)
include_directories(${GLFW3_INCLUDE_DIRS})

# -------------------------------
# Platform-specific libraries
# -------------------------------
if(WIN32)
    set(SYS_LIBS opengl32 gdi32)
elseif(APPLE)
    find_library(COCOA_LIBRARY Cocoa REQUIRED)
    find_library(IOKIT_LIBRARY IOKit REQUIRED)
    find_library(COREVIDEO_LIBRARY CoreVideo REQUIRED)
    find_library(OPENGL_LIBRARY OpenGL REQUIRED)
    set(SYS_LIBS
        ${OPENGL_LIBRARY}
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
        ${COREVIDEO_LIBRARY}
    )
else() # Linux and others
    set(SYS_LIBS dl pthread m GL X11)
endif()

# -------------------------------
# Target definition
# -------------------------------
add_executable(${TARGET_NAME} ${ALL_SOURCES})

target_link_libraries(${TARGET_NAME}
    ${GLFW3_LIBRARIES}
    ${SYS_LIBS}
)

# -------------------------------
# Output directories
# -------------------------------
set_target_properties(${TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# -------------------------------
# Copy shaders to build directory
# -------------------------------
if(COPY_SHADERS AND EXISTS "${SHADER_DIR}")
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${SHADER_DIR}"
        "${CMAKE_BINARY_DIR}/bin/shaders"
        COMMENT "Copying shaders to build directory..."
    )
endif()



# -------------------------------
# Print configuration summary
# -------------------------------
message(STATUS "")
message(STATUS "=== Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "ImGui support: ${BUILD_WITH_IMGUI}")
message(STATUS "Copy shaders: ${COPY_SHADERS}")
message(STATUS "Output directory: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "===========================")
message(STATUS "")

# -------------------------------
# Clean target
# -------------------------------
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/CMakeFiles"
    COMMAND ${CMAKE_COMMAND} -E remove "${CMAKE_BINARY_DIR}/CMakeCache.txt"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/bin"
    COMMAND ${CMAKE_COMMAND} -E remove "${CMAKE_BINARY_DIR}/cmake_install.cmake"
    COMMAND ${CMAKE_COMMAND} -E remove "${CMAKE_BINARY_DIR}/Makefile"
    COMMENT "Cleaning all build files..."
)

# -------------------------------
# Install target (optional)
# -------------------------------
install(TARGETS ${TARGET_NAME}
    RUNTIME DESTINATION bin
)

if(EXISTS "${SHADER_DIR}")
    install(DIRECTORY "${SHADER_DIR}/"
        DESTINATION bin/shaders
    )
endif()