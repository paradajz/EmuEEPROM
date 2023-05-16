add_library(framework)

target_sources(framework
    PRIVATE
    src/Framework.cpp
)

target_include_directories(framework
    PUBLIC
    include
)

target_link_libraries(framework
    PUBLIC
    glog
    pthread
    gtest
)