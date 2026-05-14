cmake_minimum_required(VERSION 3.10)

project(cppjudge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cppjudge
    src/main.cpp
    src/judge.cpp
    src/compiler.cpp
    src/runner.cpp
    src/comparer.cpp
)

target_include_directories(cppjudge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)