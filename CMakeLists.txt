cmake_minimum_required(VERSION 3.16)
project(project_app LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# All executables output to out/bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/out/bin)

# Cross-compilation toolchain
if(DEFINED ENV{CROSS_COMPILE})
    set(CMAKE_C_COMPILER $ENV{CROSS_COMPILE}gcc)
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/user)
include_directories(${PROJECT_SOURCE_DIR}/hw/include)
include_directories(${PROJECT_SOURCE_DIR}/modules/log/include)

# Library search paths
link_directories(${PROJECT_SOURCE_DIR}/part)

# CTest（由 env 文件 BUILD_TESTS 控制，正式版本默认关闭）
set(BUILD_TESTS 0)
if(DEFINED ENV{BUILD_TESTS} AND "$ENV{BUILD_TESTS}" STREQUAL "1")
    set(BUILD_TESTS 1)
endif()
if(BUILD_TESTS)
    enable_testing()
endif()

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

set(CMD_DEMO_ENABLED 1)
if(DEFINED ENV{CMD_BUILD_DEMO} AND "$ENV{CMD_BUILD_DEMO}" STREQUAL "0")
    set(CMD_DEMO_ENABLED 0)
endif()
add_definitions(-DCMD_BUILD_DEMO=${CMD_DEMO_ENABLED})

# Modules 目录
add_subdirectory(${PROJECT_SOURCE_DIR}/modules)

# Add executable
add_executable(project_app
    ${PROJECT_SOURCE_DIR}/user/main.c
    ${PROJECT_SOURCE_DIR}/user/app_signal.c
)
target_link_libraries(project_app hw log cmd)

# Install
install(TARGETS project_app DESTINATION ${INSTALL_DIR}/bin)
