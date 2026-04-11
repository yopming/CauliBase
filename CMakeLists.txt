cmake_minimum_required(VERSION 3.16)
project(cauli_base CXX)

set(CMAKE_CXX_STANDARD 21)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cauli_base
  src/main.cpp 
)

target_include_directories(cauli_base PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(cauli_base PRIVATE -Wall -Wextra -pedantic)
