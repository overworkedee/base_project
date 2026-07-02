cmake_minimum_required(VERSION 3.16)

set(LOG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/log.c
)

set(LOG_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/log/log.h
)

# 静态库 liblog.a
add_library(log STATIC ${LOG_SOURCES} ${LOG_HEADERS})
target_include_directories(log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(log PUBLIC hw PRIVATE pthread)

# 单元测试（由 env BUILD_TESTS 控制，且仅 host 编译）
if(BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    add_executable(test_log ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_log.c)
    target_link_libraries(test_log log)
    add_test(NAME test_log COMMAND test_log)
endif()

# Demo（由 env LOG_BUILD_DEMO 控制）
if(LOG_DEMO_ENABLED)
    add_executable(log_demo ${CMAKE_CURRENT_SOURCE_DIR}/demo/log_demo.c)
    target_link_libraries(log_demo log)
endif()
