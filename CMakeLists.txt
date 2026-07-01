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

# 日志编译控制（由 env 文件控制，未设置时默认启用）
set(LOG_DEBUG_ENABLED 1)
if(DEFINED ENV{LOG_ENABLE_DEBUG} AND "$ENV{LOG_ENABLE_DEBUG}" STREQUAL "0")
    set(LOG_DEBUG_ENABLED 0)
endif()
add_definitions(-DLOG_ENABLE_DEBUG=${LOG_DEBUG_ENABLED})

set(LOG_DEMO_ENABLED 1)
if(DEFINED ENV{LOG_BUILD_DEMO} AND "$ENV{LOG_BUILD_DEMO}" STREQUAL "0")
    set(LOG_DEMO_ENABLED 0)
endif()
add_definitions(-DLOG_BUILD_DEMO=${LOG_DEMO_ENABLED})

# Modules 目录
add_subdirectory(${PROJECT_SOURCE_DIR}/modules)

# Add executable
add_executable(hello_world
    ${PROJECT_SOURCE_DIR}/user/main.c
)
target_link_libraries(hello_world hw)

# Install
install(TARGETS hello_world DESTINATION ${INSTALL_DIR}/bin)
