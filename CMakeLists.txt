cmake_minimum_required(VERSION 3.22)

project(EmuEEPROM)

include(CTest)

add_library(libemueeprom STATIC)

target_sources(libemueeprom
    PRIVATE
    src/emueeprom.cpp
)

target_include_directories(libemueeprom
    PUBLIC
    include
)

add_custom_target(libemueeprom-format
    COMMAND echo Checking code formatting...
    COMMAND ${CMAKE_CURRENT_LIST_DIR}/scripts/code_format.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
)

set_target_properties(libemueeprom-format PROPERTIES EXCLUDE_FROM_ALL TRUE)

if (CMAKE_EXPORT_COMPILE_COMMANDS STREQUAL ON)
    add_custom_target(libemueeprom-lint
        COMMAND ${CMAKE_CURRENT_LIST_DIR}/scripts/clang_tidy_wrapper.sh --output=${CMAKE_CURRENT_BINARY_DIR}/clang-tidy-fixes.yml
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    set_target_properties(libemueeprom-lint PROPERTIES EXCLUDE_FROM_ALL TRUE)
    add_dependencies(libemueeprom-lint libemueeprom)
endif()

if (BUILD_TESTING_EMU_EEPROM STREQUAL ON)
    add_subdirectory(tests)
endif()