cmake_minimum_required(VERSION 3.15)
project(VDBCompressor)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Starting configuration...")

# Manual OpenVDB detection for Ubuntu
find_path(OPENVDB_INCLUDE_DIR
    NAMES openvdb/openvdb.h
    PATHS /usr/include
    NO_DEFAULT_PATH
)

find_library(OPENVDB_LIBRARY
    NAMES openvdb
    PATHS /usr/lib/x86_64-linux-gnu /usr/lib
    NO_DEFAULT_PATH
)

if(OPENVDB_INCLUDE_DIR AND OPENVDB_LIBRARY)
    message(STATUS "✓ Found OpenVDB:")
    message(STATUS "  Include: ${OPENVDB_INCLUDE_DIR}")
    message(STATUS "  Library: ${OPENVDB_LIBRARY}")
else()
    message(FATAL_ERROR "✗ OpenVDB not found. Install with: sudo apt-get install libopenvdb-dev")
endif()

# Manual VTK detection
message(STATUS "Looking for VTK...")

find_path(VTK_INCLUDE_DIR
    NAMES vtkVersion.h
    PATHS /usr/include/vtk-9.1 /usr/include/vtk-9.0 /usr/include
    NO_DEFAULT_PATH
)

# Find required VTK libraries - ADDED MORE LIBRARIES
set(VTK_LIB_PATHS /usr/lib/x86_64-linux-gnu /usr/lib)

find_library(VTK_COMMON_CORE
    NAMES vtkCommonCore-9.1 vtkCommonCore-9.0 vtkCommonCore
    PATHS ${VTK_LIB_PATHS}
    NO_DEFAULT_PATH
)

find_library(VTK_COMMON_DATA_MODEL
    NAMES vtkCommonDataModel-9.1 vtkCommonDataModel-9.0 vtkCommonDataModel
    PATHS ${VTK_LIB_PATHS}
    NO_DEFAULT_PATH
)

find_library(VTK_IO_CORE
    NAMES vtkIOCore-9.1 vtkIOCore-9.0 vtkIOCore
    PATHS ${VTK_LIB_PATHS}
    NO_DEFAULT_PATH
)

find_library(VTK_IO_GEOMETRY
    NAMES vtkIOGeometry-9.1 vtkIOGeometry-9.0 vtkIOGeometry
    PATHS ${VTK_LIB_PATHS}
    NO_DEFAULT_PATH
)

find_library(VTK_IO_LEGACY
    NAMES vtkIOLegacy-9.1 vtkIOLegacy-9.0 vtkIOLegacy
    PATHS ${VTK_LIB_PATHS}
    NO_DEFAULT_PATH
)

if(VTK_INCLUDE_DIR AND VTK_COMMON_CORE)
    message(STATUS "✓ Found VTK:")
    message(STATUS "  Include: ${VTK_INCLUDE_DIR}")
    message(STATUS "  CommonCore: ${VTK_COMMON_CORE}")
    set(VTK_FOUND TRUE)
    set(VTK_INCLUDE_DIRS ${VTK_INCLUDE_DIR})
    set(VTK_LIBRARIES 
        ${VTK_COMMON_CORE}
        ${VTK_COMMON_DATA_MODEL}
        ${VTK_IO_CORE}
        ${VTK_IO_GEOMETRY}
        ${VTK_IO_LEGACY}
    )
else()
    message(FATAL_ERROR "✗ VTK not found. Install with: sudo apt-get install libvtk9-dev")
endif()

# Find TBB
find_library(TBB_LIBRARY
    NAMES tbb
    PATHS /usr/lib/x86_64-linux-gnu /usr/lib
    NO_DEFAULT_PATH
)

if(TBB_LIBRARY)
    message(STATUS "✓ Found TBB: ${TBB_LIBRARY}")
else()
    message(FATAL_ERROR "✗ TBB not found. Install with: sudo apt-get install libtbb-dev")
endif()

# Find Imath library
find_library(IMATH_LIBRARY
    NAMES Imath-3_1 Imath
    PATHS /usr/lib/x86_64-linux-gnu /usr/lib
    NO_DEFAULT_PATH
)

if(IMATH_LIBRARY)
    message(STATUS "✓ Found Imath: ${IMATH_LIBRARY}")
else()
    message(FATAL_ERROR "✗ Imath not found. Install with: sudo apt-get install libimath-dev")
endif()

# Find Blosc library
find_library(BLOSC_LIBRARY
    NAMES blosc
    PATHS /usr/lib/x86_64-linux-gnu /usr/lib
    NO_DEFAULT_PATH
)

if(BLOSC_LIBRARY)
    message(STATUS "✓ Found Blosc: ${BLOSC_LIBRARY}")
else()
    message(WARNING "Blosc library not found")
endif()

# Find Zlib library
find_library(ZLIB_LIBRARY
    NAMES z
    PATHS /usr/lib/x86_64-linux-gnu /usr/lib
    NO_DEFAULT_PATH
)

if(ZLIB_LIBRARY)
    message(STATUS "✓ Found Zlib: ${ZLIB_LIBRARY}")
else()
    message(WARNING "Zlib library not found")
endif()

include_directories(src)
include_directories(${OPENVDB_INCLUDE_DIR})
include_directories(${VTK_INCLUDE_DIRS})

add_executable(vdb_compressor 
    src/VDBCompressor.cpp
    src/main.cpp
)

# FIXED: Added VTK_COMMON_DATA_MODEL and other libraries
target_link_libraries(vdb_compressor 
    ${OPENVDB_LIBRARY}
    ${VTK_LIBRARIES}
    ${TBB_LIBRARY}
    ${IMATH_LIBRARY}
    ${BLOSC_LIBRARY}
    ${ZLIB_LIBRARY}
    pthread
    dl
)

# Add the verification tool
add_executable(check_vdb src/check_vdb.cpp)
target_link_libraries(check_vdb 
    ${OPENVDB_LIBRARY}
    ${TBB_LIBRARY}
    ${IMATH_LIBRARY}
    ${BLOSC_LIBRARY}
    ${ZLIB_LIBRARY}
    pthread
    dl
)

message(STATUS "")
message(STATUS "=== Build Configuration Successful ===")
message(STATUS "OpenVDB: ${OPENVDB_LIBRARY}")
message(STATUS "VTK: ${VTK_INCLUDE_DIR}")
message(STATUS "TBB: ${TBB_LIBRARY}")
message(STATUS "Imath: ${IMATH_LIBRARY}")
message(STATUS "======================================")
