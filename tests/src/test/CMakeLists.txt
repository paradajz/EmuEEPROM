add_executable(libemueeprom-test
    test.cpp
)

target_link_libraries(libemueeprom-test
    PRIVATE
    liblibemueeprom-test-common
    libemueeprom
)

target_compile_definitions(libemueeprom-test
    PRIVATE
    TEST
)

target_compile_definitions(libemueeprom
    PUBLIC
    EMU_EEPROM_PAGE_SIZE=128
)

add_test(
    NAME test_build
    COMMAND
    "${CMAKE_COMMAND}"
    --build "${CMAKE_BINARY_DIR}"
    --config "$<CONFIG>"
    --target libemueeprom-test
)

set_tests_properties(test_build
    PROPERTIES
    FIXTURES_SETUP
    test_fixture
)

add_test(
    NAME test
    COMMAND $<TARGET_FILE:libemueeprom-test>
)

set_tests_properties(test
    PROPERTIES
    FIXTURES_REQUIRED
    test_fixture
)