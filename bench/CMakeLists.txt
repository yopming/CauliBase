add_executable(cauli_bench
  cauli_bench.cpp
)

target_link_libraries(cauli_bench PRIVATE cauli_base_core)
target_compile_options(cauli_bench PRIVATE -Wall -Wextra -pedantic)

add_executable(ycsb_workload_generator
  ycsb_workload.cpp
  ycsb_workload_generator.cpp
)

target_include_directories(ycsb_workload_generator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(ycsb_workload_generator PRIVATE -Wall -Wextra -pedantic)

add_executable(ycsb_bench
  ycsb_bench.cpp
  ycsb_workload.cpp
)

target_link_libraries(ycsb_bench PRIVATE cauli_base_core)
target_include_directories(ycsb_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(ycsb_bench PRIVATE -Wall -Wextra -pedantic)

add_executable(shuffle_order_experiment
  shuffle_order_experiment.cpp
)

target_link_libraries(shuffle_order_experiment PRIVATE cauli_base_core)
target_compile_options(shuffle_order_experiment PRIVATE -Wall -Wextra -pedantic)
