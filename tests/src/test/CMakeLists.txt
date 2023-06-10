add_executable(emueeprom-test
    test.cpp
)

target_link_libraries(emueeprom-test
    PRIVATE
    emueeprom-test-common
    emueeprom-lib
)

target_compile_definitions(emueeprom-test
    PRIVATE
    TEST
)

target_compile_definitions(emueeprom-lib
    PUBLIC
    EMU_EEPROM_PAGE_SIZE=128
)

add_test(
    NAME test_build
    COMMAND
    "${CMAKE_COMMAND}"
    --build "${CMAKE_BINARY_DIR}"
    --config "$<CONFIG>"
    --target emueeprom-test
)

set_tests_properties(test_build
    PROPERTIES
    FIXTURES_SETUP
    test_fixture
)

add_test(
    NAME test
    COMMAND $<TARGET_FILE:emueeprom-test>
)

set_tests_properties(test
    PROPERTIES
    FIXTURES_REQUIRED
    test_fixture
)