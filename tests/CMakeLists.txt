# Tests CMakeLists.txt
# Test suite for openpsd library

enable_testing()

# Unified test runner
add_executable(openpsd_tests
    openpsd_tests.c
    test_basic.c
    test_background_layer.c
    test_text_layers.c
    test_color_modes.c
)
target_link_libraries(openpsd_tests PRIVATE openpsd)
target_compile_definitions(openpsd_tests PRIVATE
    OPENPSD_TEST_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/tests/samples"
)

add_test(NAME OpenPSDTests COMMAND openpsd_tests)