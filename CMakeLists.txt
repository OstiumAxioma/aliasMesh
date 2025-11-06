# ==============================
# MaskReMeshApp CMake Build File
# ==============================
cmake_minimum_required(VERSION 3.14)
project(MaskReMeshApp LANGUAGES CXX)

# ---------- 基础设置 ----------
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------- 查找 VTK ----------
if(NOT DEFINED VTK_DIR)
    set(VTK_PATHS
        "D:/code/vtk8.2.0/VTK-8.2.0/lib/cmake/vtk-8.2"
        "C:/VTK/lib/cmake/vtk-8.2"
        "C:/Program Files/VTK/lib/cmake/vtk-8.2"
        "$ENV{VTK_DIR}"
        "$ENV{VTK_ROOT}/lib/cmake/vtk-8.2"
    )
    foreach(path ${VTK_PATHS})
        if(EXISTS "${path}/VTKConfig.cmake")
            set(VTK_DIR "${path}")
            message(STATUS "Found VTK at: ${VTK_DIR}")
            break()
        endif()
    endforeach()
endif()

if(NOT DEFINED VTK_DIR)
    message(FATAL_ERROR
        "VTK 8.2 not found. Please set VTK_DIR manually.\n"
        "Example:\n  cmake -DVTK_DIR=D:/code/vtk8.2.0/VTK-8.2.0/lib/cmake/vtk-8.2 .."
    )
endif()

find_package(VTK 8.2 REQUIRED)
include(${VTK_USE_FILE})

# ---------- 源文件 ----------
set(SOURCES
    src/main.cpp
    src/MaskReMesh.cpp
)

set(HEADERS
    header/MaskReMesh.h
)

# ---------- 生成可执行文件 ----------
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/header
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ---------- 一次性链接所有 VTK 库 ----------
target_link_libraries(${PROJECT_NAME} PRIVATE ${VTK_LIBRARIES})

# ---------- MSVC 特定设置 ----------
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

message(STATUS "====================================")
message(STATUS "  Project: MaskReMeshApp")
message(STATUS "  VTK_DIR: ${VTK_DIR}")
message(STATUS "  Generator: ${CMAKE_GENERATOR}")
message(STATUS "====================================")