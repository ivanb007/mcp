cmake_minimum_required(VERSION 3.23)

project(
    mcp
    VERSION 0.1.0
    LANGUAGES C CXX
)

add_executable(mcp engine.cpp main.cpp openingbook.cpp search.cpp threadpool.cpp uci.cpp)

include(CTest)
enable_testing()

