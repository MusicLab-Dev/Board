cmake_minimum_required(VERSION 3.13 FATAL_ERROR)
project(Board)

get_filename_component(BoardRoot ${CMAKE_CURRENT_LIST_FILE} PATH)

include(${BoardRoot}/CompileOptions/CompileOptions.cmake)

add_submodule_directory(${BoardRoot}/Protocol)

include(${BoardRoot}/Board/Board.cmake)

if(TESTS)
    enable_testing()
    include(${BoardRoot}/Tests/BoardTests.cmake)
endif()

if(BENCHMARKS)
    include(${BoardRoot}/Benchmarks/BoardBenchmarks.cmake)
endif()