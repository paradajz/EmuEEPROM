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
    EMU_EEPROM_PAGE_SIZE=128
)

add_test(
    NAME test
    COMMAND $<TARGET_FILE:test>
)
