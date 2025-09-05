
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

void runUCITest() {
    FILE* pipe = popen("/home/ivan/github/mcp/build/my_engine.exe", "w");
    if (!pipe) {
        std::cerr << "Failed to start engine process." << std::endl;
        return;
    }

    const char* commands[] = {
        "uci",
        "isready",
        "setoption name UseBook value false",
        "ucinewgame",
        "position startpos moves e2e4 e7e5",
        "go movetime 1000",
        "quit"
    };

    for (const char* cmd : commands) {
        std::cout << ">> " << cmd << std::endl;
        fprintf(pipe, "%s\n", cmd);
        fflush(pipe);
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }

    pclose(pipe);
}

int main() {
    runUCITest();
    std::cout << "✅ UCI test harness finished. Check engine output manually or redirect stdout." << std::endl;
    return 0;
}
