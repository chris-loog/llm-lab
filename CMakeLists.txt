cmake_minimum_required(VERSION 3.16)
project(llm-lab C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(llm-lab src/main.c)
target_include_directories(llm-lab PRIVATE include)
target_compile_options(llm-lab PRIVATE -Wall -Wextra -Wpedantic)
