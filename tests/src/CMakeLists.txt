find_package(glog 0.6.0 REQUIRED)

add_library(emueeprom-test-common)

target_sources(emueeprom-test-common
    PRIVATE
    main.cpp
)

target_include_directories(emueeprom-test-common
    PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include
)

target_link_libraries(emueeprom-test-common
    PUBLIC
    glog::glog
    gtest
)

add_subdirectory(test)