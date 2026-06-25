cmake_minimum_required(VERSION 3.16)

# Build option: demo
option(HW_BUILD_DEMO "Build hardware demo program" OFF)

# Collect bus sources
set(HW_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hw_error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hw_mutex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bus/bus_i2c.c
)

# Collect headers (for IDE convenience, not required for build)
set(HW_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_error.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_mutex.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/bus/bus_i2c.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/dev/dev_template.h
)

# Static library
add_library(hw STATIC ${HW_SOURCES} ${HW_HEADERS})
target_include_directories(hw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hw PRIVATE pthread)

# Demo
if(HW_BUILD_DEMO)
    add_executable(hw_demo ${CMAKE_CURRENT_SOURCE_DIR}/demo/demo_main.c)
    target_link_libraries(hw_demo hw)
endif()

# Tests
enable_testing()
add_executable(test_bus_i2c ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bus_i2c.c)
target_link_libraries(test_bus_i2c hw)
add_test(NAME test_bus_i2c COMMAND test_bus_i2c)
