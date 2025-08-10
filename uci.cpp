
#include "uci.h"
#include "engine.h"
#include "threadpool.h"
#include "openingbook.h"
#include "search.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>

std::atomic<bool> stopSearch(false);
std::thread searchThread;
int timeLimitMs = 1000;  // default 1 second per move
OpeningBook openingBook;

void runUciLoop() {
    BoardData state = getInitialBoard();
    std::string line;
    openingBook.load("book.bin"); // Load once

    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "uci") {
            std::cout << "id name ModularChessEngine\n";
            std::cout << "id author You\n";
            std::cout << "uciok\n";
        } else if (token == "isready") {
            std::cout << "readyok\n";
        } else if (token == "position") {
            parsePosition(line, state);
        } else if (token == "go") {
            stopSearch = false;
            timeLimitMs = 1000;

            std::string sub;
            while (iss >> sub) {
                if (sub == "movetime") {
                    iss >> timeLimitMs;
                } else if (sub == "wtime" || sub == "btime") {
                    int timeRemaining;
                    iss >> timeRemaining;
                    timeLimitMs = timeRemaining / 30;
                }
            }

            // Check opening book
            if (openingBook.hasMove(state)) {
                Move bookMove = openingBook.getMove(state);
                std::cout << "bestmove " << moveToUci(bookMove) << "\n";
                std::cout.flush();
                continue;
            }

            if (searchThread.joinable()) searchThread.join();
            searchThread = std::thread([state]() {
                auto start = std::chrono::steady_clock::now();
                Move bestMove = findBestMoveParallel(state, 4, timeLimitMs);
                auto end = std::chrono::steady_clock::now();

                if (!stopSearch.load()) {
                    std::cout << "bestmove " << moveToUci(bestMove) << "\n";
                    std::cout.flush();
                }
            });
        } else if (token == "stop") {
            stopSearch = true;
            if (searchThread.joinable()) searchThread.join();
        } else if (token == "quit") {
            stopSearch = true;
            if (searchThread.joinable()) searchThread.join();
            break;
        }
    }
}
