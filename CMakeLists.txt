cmake_minimum_required(VERSION 3.22)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(EmuEEPROM)

add_library(emueeprom STATIC)

target_sources(emueeprom
    PRIVATE
    src/EmuEEPROM.cpp
)

target_include_directories(emueeprom
    PUBLIC
    include
)

if (BUILD_TESTING STREQUAL ON)
    add_subdirectory(tests)
endif()