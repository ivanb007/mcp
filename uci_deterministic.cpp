// uci_deterministic.cpp (or place in uci.cpp and call this function from main for A/B)
#include "uci.h"
#include "engine.h"
#include "search.h"
#include "fen.h"
#include <iostream>
#include <sstream>
#include <atomic>
#include <chrono>
#include <vector>
#include <limits>

static const int INF = std::numeric_limits<int>::max();

// This UCI loop ignores time controls, books, threads, and root-parallel search.
// It uses EXACT depth search, single-thread, and prints PV + score.
void runUciLoop_Deterministic() {
    BoardData board = getInitialBoard();
    std::string line;

    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string tok; iss >> tok;

        if (tok == "uci") {
            std::cout << "id name MyChessEngine (Deterministic)\n";
            std::cout << "id author YourName\n";
            // only expose a depth cap to make intent crystal clear
            std::cout << "option name MaxDepth type spin default 12 min 1 max 64\n";
            std::cout << "uciok\n" << std::flush;
        } else if (tok == "isready") {
            std::cout << "readyok\n" << std::flush;
        } else if (tok == "ucinewgame") {
            board = getInitialBoard();
        } else if (tok == "position") {
            parsePosition(line, board);
        } else if (tok == "go") {
            // parse only "depth N"
            int depth = 6;
            std::string s;
            while (iss >> s) {
                if (s == "depth") iss >> depth;
            }
            if (depth < 1) depth = 1;

            // Deterministic: single-thread, no time cutoff, no book
            auto far_future = std::chrono::steady_clock::now() + std::chrono::minutes(1);
            std::atomic<bool> stop(false);

            std::vector<Move> pv;
            int eval = alphabetaTimed(
                board,
                depth,
                -INF, INF,
                board.whiteToMove,
                far_future,
                stop,
                pv
            );

            // Emit one final "info" with PV
            std::cout << "info depth " << depth
                      << " score cp " << eval
                      << " pv ";
            for (const auto& m : pv) std::cout << moveToUci(m) << ' ';
            std::cout << std::endl;

            // Best move = pv[0], or 0000 if none
            if (!pv.empty()) {
                std::cout << "bestmove " << moveToUci(pv.front()) << std::endl;
            } else {
                std::cout << "bestmove 0000" << std::endl;
            }
            std::cout.flush();
        } else if (tok == "quit") {
            break;
        }
    }
}
