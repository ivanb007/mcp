
#include "engine.h"
#include "search.h"
#include "fen.h"
#include "threadpool.h"
#include <iostream>
#include <chrono>
#include <atomic>

int main() {
    // std::atomic<bool> stopSearch(false);
    // std::thread searchThread;
    int timePerMove = 10000;
    // std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    std::string fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
    // std::string fen = "5rk1/r1pq1ppp/5n2/p2p4/1n2P1b1/2b2pP1/1PP1BP1P/R2QK2R w KQ - 0 19";
    // std::string fen = "r1bqk2r/1p2bppp/p1nppn2/6B1/3NP3/2N5/PPPQ1PPP/1K1R1B1R b kq - 3 9";
    // std::string fen = "r2q1rk1/1p1bbppp/p1Nppn2/6B1/4P3/2N2P2/PPPQ2PP/1K1R1B1R b - - 0 11";
    // std::string fen = "r1b1kb1r/2p2ppp/p1B5/1p6/8/2p2N2/PPP2PPP/R1BqR1K1 b kq - 0 12";
    // std::string fen = "rnb1kb1r/1p2pppp/pq1p1n2/8/3NP1P1/2N2P2/PPP4P/R1BQKB1R b KQkq - 0 7";
    // "1r1q1rk1/pp2ppBp/n2p1np1/5N2/4P3/2N2P1P/PPPQ3P/2KR1B1R b - - 0 13" // black makes illegal move h8g8
    BoardData board = loadFEN(fen);

    std::cout << "Starting position: " << fen << std::endl;

    int depth = 4;
    int alpha = -10000;
    int beta = 10000;

    std::vector<Move> pvLine;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    std::atomic<bool> stop(false);

    int eval = alphabetaTimed(board, depth, alpha, beta, board.whiteToMove, deadline, stop, pvLine);
    std::cout << "Evaluation result from alphabetaTimed: " << eval << std::endl;

    // Emit one final "info" with PV
    std::cout << "info depth " << depth
                << " score cp " << eval
                << " pv ";
    for (const auto& m : pvLine) std::cout << moveToUci(m) << ' ';
    std::cout << std::endl;

    // Best move = pv[0], or 0000 if none
    if (!pvLine.empty()) {
        std::cout << "bestmove " << moveToUci(pvLine.front()) << std::endl;
    } else {
        std::cout << "bestmove 0000" << std::endl;
    }
    std::cout.flush();

    // Move bestMove = findBestMoveParallel(board, 4, timePerMove);
    // std::cout << "bestmove " << moveToUci(bestMove) << std::endl;
    // std::cout.flush();

    // std::vector<Move> Moves = generateMoves(board);
    // for (const auto& m : Moves) {
    //     std::cout << moveToUci(m) << std::endl;
    // }

    return 0;
}
