cmake_minimum_required(VERSION 3.16)
project(bytevm C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

include_directories(include src)

file(GLOB CORE_SOURCES "src/*.c" "src/assembler/*.c")
add_executable(bytevm ${CORE_SOURCES})

enable_testing()
file(GLOB TEST_UNIT_SOURCES "tests/unit/*.c")
set(TEST_SOURCES tests/test_main.c ${TEST_UNIT_SOURCES})
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*main\\.c$")
add_executable(run_tests ${TEST_SOURCES} ${CORE_SOURCES})
add_test(NAME unit_tests COMMAND run_tests)
