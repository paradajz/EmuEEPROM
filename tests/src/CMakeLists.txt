find_package(glog 0.6.0 REQUIRED)

add_library(common)

target_sources(common
    PRIVATE
    main.cpp
)

target_include_directories(common
    PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include
)

target_link_libraries(common
    PUBLIC
    glog::glog
    gtest
)

add_subdirectory(test)