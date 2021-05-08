cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(Board)

get_filename_component(BoardDir ${CMAKE_CURRENT_LIST_FILE} PATH)

set(BoardSources
    ${BoardDir}/ConfigTable.hpp
    ${BoardDir}/ConfigTable.ipp
    ${BoardDir}/ConfigTable.cpp
    ${BoardDir}/HardwareModule.cpp
    ${BoardDir}/HardwareModule.hpp
    ${BoardDir}/HardwareModule.ipp
    ${BoardDir}/Module.hpp
    ${BoardDir}/NetworkModule.cpp
    ${BoardDir}/NetworkModule.hpp
    ${BoardDir}/NetworkModule.ipp
    ${BoardDir}/Scheduler.cpp
    ${BoardDir}/Scheduler.hpp
    ${BoardDir}/Scheduler.ipp
    ${BoardDir}/Types.hpp
    ${BoardDir}/GPIO.hpp
    ${BoardDir}/GPIO.cpp
)

add_library(${PROJECT_NAME} ${BoardSources})

target_include_directories(${PROJECT_NAME} PUBLIC ${BoardDir}/..)

if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "armv6l")
    set(WiringPiLib "wiringPi")
else()
    set(WiringPiLib "")
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC Protocol ${WiringPiLib})

if(CODE_COVERAGE)
    target_compile_options(${PROJECT_NAME} PUBLIC --coverage)
    target_link_options(${PROJECT_NAME} PUBLIC --coverage)
endif()

set(BoardAppSources
    ${BoardDir}/Main.cpp
)

set(Application ${PROJECT_NAME}App)

add_executable(${Application} ${BoardAppSources})

target_link_libraries(${Application} Board)
