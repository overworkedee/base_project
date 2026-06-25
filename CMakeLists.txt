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

# Library search paths
link_directories(${PROJECT_SOURCE_DIR}/part)

# Add executable
add_executable(hello_world
    ${PROJECT_SOURCE_DIR}/user/main.c
)

# Install
install(TARGETS hello_world DESTINATION ${INSTALL_DIR}/bin)
