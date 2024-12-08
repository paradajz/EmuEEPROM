find_package(glog 0.6.0 REQUIRED)

add_library(liblibemueeprom-test-common)

target_sources(liblibemueeprom-test-common
    PRIVATE
    main.cpp
)

target_include_directories(liblibemueeprom-test-common
    PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include
)

target_link_libraries(liblibemueeprom-test-common
    PUBLIC
    glog::glog
    gtest
)

add_subdirectory(test)