cmake_minimum_required(VERSION 3.10)

project(cppjudge)

set(CMAKE_CXX_STANDARD 17)

add_executable(cppjudge
    src/main.cpp
    src/judge.cpp
    src/compiler.cpp
    src/runner.cpp
    src/comparer.cpp
)