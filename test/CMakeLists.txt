add_executable(cauli_unit_tests
  command_test.cpp
  cauli_base_test.cpp
  key_transform_test.cpp
  main_test.cpp
  record_test.cpp
  shuffling_performance_test.cpp
  sstable_test.cpp
  wal_test.cpp
  ${PROJECT_SOURCE_DIR}/src/main.cpp
)

target_link_libraries(cauli_unit_tests PRIVATE 
  cauli_base_core
  doctest::doctest_with_main
)

target_include_directories(cauli_unit_tests PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(cauli_unit_tests PRIVATE CAULI_BASE_DISABLE_MAIN)

get_target_property(DOCTEST_SOURCE_DIR doctest::doctest SOURCE_DIR)
include(${DOCTEST_SOURCE_DIR}/scripts/cmake/doctest.cmake)

doctest_discover_tests(cauli_unit_tests)

#add_test(NAME cauli_tests COMMAND cauli_unit_tests)
