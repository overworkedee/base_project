cmake_minimum_required(VERSION 3.16)
project(hello_world LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Cross-compilation toolchain
if(DEFINED ENV{CROSS_COMPILE})
    set(CMAKE_C_COMPILER $ENV{CROSS_COMPILE}gcc)
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/user)
include_directories(${PROJECT_SOURCE_DIR}/hw/include)

# Library search paths
link_directories(${PROJECT_SOURCE_DIR}/part)

# CTest
enable_testing()

# HW IO library
add_subdirectory(${PROJECT_SOURCE_DIR}/hw)

# 日志 DEBUG 等级编译期裁剪（由 env 文件控制）
if(DEFINED ENV{LOG_ENABLE_DEBUG} AND $ENV{LOG_ENABLE_DEBUG} EQUAL 0)
    add_definitions(-DLOG_ENABLE_DEBUG=0)
else()
    add_definitions(-DLOG_ENABLE_DEBUG=1)
endif()

# Modules 目录
add_subdirectory(${PROJECT_SOURCE_DIR}/modules)

# Add executable
add_executable(hello_world
    ${PROJECT_SOURCE_DIR}/user/main.c
)
target_link_libraries(hello_world hw)

# Install
install(TARGETS hello_world DESTINATION ${INSTALL_DIR}/bin)
