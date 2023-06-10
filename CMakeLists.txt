cmake_minimum_required(VERSION 3.22)

project(EmuEEPROM)

include(CTest)

add_library(emueeprom-lib STATIC)

target_sources(emueeprom-lib
    PRIVATE
    src/EmuEEPROM.cpp
)

target_include_directories(emueeprom-lib
    PUBLIC
    include
)

add_custom_target(emueeprom-format
    COMMAND echo Checking code formatting...
    COMMAND find . -regex '.*\.\(cpp\|hpp\|h\|cc\|cxx\|c\)' -not -path '**CMakeFiles/**/*' -not -path '**generated/**/*' -not -path '**/modules/**/*' -exec clang-format -style=file -i {} +
    COMMAND git diff
    COMMAND git diff -s --exit-code
)

set_target_properties(emueeprom-format PROPERTIES EXCLUDE_FROM_ALL TRUE)

if (CMAKE_EXPORT_COMPILE_COMMANDS STREQUAL ON)
    add_custom_target(emueeprom-lint
        COMMAND ${CMAKE_CURRENT_LIST_DIR}/scripts/clang_tidy_wrapper.sh --output=${CMAKE_CURRENT_BINARY_DIR}/clang-tidy-fixes.yml
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    set_target_properties(emueeprom-lint PROPERTIES EXCLUDE_FROM_ALL TRUE)
    add_dependencies(emueeprom-lint emueeprom-lib)
endif()

if (BUILD_TESTING_EMU_EEPROM STREQUAL ON)
    add_subdirectory(tests)
endif()