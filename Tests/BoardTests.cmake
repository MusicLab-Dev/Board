project(BoardTests)

find_package(GTest REQUIRED)

get_filename_component(BoardTestsDir ${CMAKE_CURRENT_LIST_FILE} PATH)

set(BoardTestsSources
    ${BoardTestsDir}/tests_Scheduler.cpp
    ${BoardTestsDir}/tests_ConfigTable.cpp
)

add_executable(${PROJECT_NAME} ${BoardTestsSources})

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME}
PUBLIC
    Board
    GTest::GTest GTest::Main
)