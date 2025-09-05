cmake_minimum_required(VERSION 3.23)

project(
    mcp
    VERSION 0.1.0
    LANGUAGES C CXX
)

add_executable(mcp 
    engine.cpp 
    fen.cpp 
    main.cpp 
    openingbook.cpp 
    san_pgn.cpp 
    san.cpp 
    search.cpp 
    threadpool.cpp 
    uci.cpp
    polyglot_random.cpp
    thread_context.cpp
    uci_deterministic.cpp
)

include(CTest)
enable_testing()

add_executable(my_engine_st
    main_st.cpp
    uci_st.cpp
    engine.cpp engine.h
    fen.cpp fen.h
    search.cpp search.h
    openingbook.cpp openingbook.h
    polyglot_random.cpp
    thread_context.cpp thread_context.h
    # (exclude threadpool.cpp and thread_context.cpp to enforce single-thread build)
)

target_compile_features(my_engine_st PRIVATE cxx_std_20)