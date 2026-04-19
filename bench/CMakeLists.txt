add_executable(cauli_bench
  cauli_bench.cpp
)

target_link_libraries(cauli_bench PRIVATE cauli_base_core)
target_compile_options(cauli_bench PRIVATE -Wall -Wextra -pedantic)
