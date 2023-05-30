add_executable(test
    test.cpp
)

target_link_libraries(test
    PRIVATE
    common
    emueeprom
)

target_compile_definitions(test
    PRIVATE
    TEST
)

target_compile_definitions(emueeprom
    PUBLIC
    EMU_EEPROM_INCLUDE_CONFIG
)

target_include_directories(emueeprom
    PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

add_test(
    NAME test
    COMMAND $<TARGET_FILE:test>
)
