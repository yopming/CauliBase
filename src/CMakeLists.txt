# ---- Library: cauli_base_core ----
add_library(cauli_base_core
  cauli_base.cpp
  key_transform.cpp
  sstable.cpp
  wal.cpp
)

target_include_directories(cauli_base_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(cauli_base_core PRIVATE -Wall -Wextra -pedantic)

# ---- Executable: cauli_base ----
add_executable(cauli_base
  main.cpp
)

target_link_libraries(cauli_base PRIVATE cauli_base_core)
target_compile_options(cauli_base PRIVATE -Wall -Wextra -pedantic)
