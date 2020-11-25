project(BoardBenchmarks)

find_package(benchmark REQUIRED)

get_filename_component(BoardBenchmarksDir ${CMAKE_CURRENT_LIST_FILE} PATH)

set(BoardBenchmarksSources
    ${BoardBenchmarksDir}/Main.cpp
)

add_executable(${PROJECT_NAME} ${BoardBenchmarksSources})

target_link_libraries(${PROJECT_NAME}
PUBLIC
    Board
    benchmark::benchmark
)
