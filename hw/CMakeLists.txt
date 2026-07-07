cmake_minimum_required(VERSION 3.16)

# Build option: demo（由 env 文件控制，未设置时默认关闭）
set(HW_BUILD_DEMO_DEFAULT OFF)
if(DEFINED ENV{HW_BUILD_DEMO} AND "$ENV{HW_BUILD_DEMO}" STREQUAL "1")
    set(HW_BUILD_DEMO_DEFAULT ON)
endif()
option(HW_BUILD_DEMO "Build hardware demo program" ${HW_BUILD_DEMO_DEFAULT})

# Collect bus sources
set(HW_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hw_error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hw_mutex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bus/bus_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dev/dev_led.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dev/dev_sht30.c
)

# Collect headers (for IDE convenience, not required for build)
set(HW_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_error.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_mutex.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/bus/bus_i2c.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/dev/dev_template.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/dev/dev_led.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/dev/dev_sht30.h
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

# Tests（由 env BUILD_TESTS 控制）
if(BUILD_TESTS)
    add_executable(test_bus_i2c ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bus_i2c.c)
    target_link_libraries(test_bus_i2c hw)
    add_test(NAME test_bus_i2c COMMAND test_bus_i2c)

    add_executable(test_dev_led ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dev_led.c)
    target_link_libraries(test_dev_led hw)
    add_test(NAME test_dev_led COMMAND test_dev_led)
endif()

