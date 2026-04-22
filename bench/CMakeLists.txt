add_executable(cauli_bench
  cauli_bench.cpp
)

target_link_libraries(cauli_bench PRIVATE cauli_base_core)
target_compile_options(cauli_bench PRIVATE -Wall -Wextra -pedantic)

add_executable(shuffle_order_experiment
  shuffle_order_experiment.cpp
)

target_link_libraries(shuffle_order_experiment PRIVATE cauli_base_core)
target_compile_options(shuffle_order_experiment PRIVATE -Wall -Wextra -pedantic)
