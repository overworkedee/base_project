set(VISION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vision_impl.cpp
)

set(VISION_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/vision/vision.h
)

add_library(vision STATIC ${VISION_SOURCES} ${VISION_HEADERS})

target_include_directories(vision
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/part/opencv-arm64/include/opencv4
)

target_link_directories(vision PRIVATE ${PROJECT_SOURCE_DIR}/part/libs)

target_link_libraries(vision PUBLIC
    -Wl,--start-group
    ${OPENCV_LIBS}
    -Wl,--end-group
    ${OPENCV_SYS_LIBS}
)
