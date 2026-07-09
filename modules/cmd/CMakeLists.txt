cmake_minimum_required(VERSION 3.16)

set(CMD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_dispatcher.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_subscription.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_handler_led.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_handler_sensor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cmd_handler_system.c
)

set(CMD_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_frame.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_transport.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_server.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_dispatcher.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_subscription.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cmd/cmd_handler_ctx.h
)

# 静态库 libcmd.a
add_library(cmd STATIC ${CMD_SOURCES} ${CMD_HEADERS})
target_include_directories(cmd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cmd PUBLIC hw log pthread)

# 单元测试（仅 host 编译）
if(BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    add_executable(test_cmd_protocol ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cmd_protocol.c)
    target_link_libraries(test_cmd_protocol cmd)
    add_test(NAME test_cmd_protocol COMMAND test_cmd_protocol)
endif()
