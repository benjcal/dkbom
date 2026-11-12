# Test the reader, the writer and the model directly. The integration tests
# drive a real copy of the project through kicad-cli, and skip when it is absent.

find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

add_executable(tst_bom tst_bom.cpp)

target_link_libraries(tst_bom PRIVATE dkbom_core Qt6::Test)

target_compile_definitions(tst_bom PRIVATE
  DKBOM_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/project"
)

add_test(NAME bom COMMAND tst_bom)

add_executable(tst_pricing tst_pricing.cpp)

target_link_libraries(tst_pricing PRIVATE dkbom_core Qt6::Test)

add_test(NAME pricing COMMAND tst_pricing)
