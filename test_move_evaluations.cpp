// test_move_evaluations.cpp
// A self-contained test program to generate and rank all legal moves for a given FEN. 
// It uses the existing loadFEN, generateMoves, applyMove, and evaluate functions.

#include "engine.h"
#include "fen.h"
#include "search.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <string>

// A struct to hold moves and their evaluations
struct ScoredMove {
    Move move;
    int score;
};

// Comparison for sorting: higher score first
bool compareMoves(const ScoredMove& a, const ScoredMove& b) {
    return a.score > b.score;
}

int main() {
    // Test FENs - uncomment the one you want to test
    // std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";  // The starting position
    // std::string fen = "rnb1kb1r/1p2pppp/pq1p1n2/8/3NP1P1/2N2P2/PPP4P/R1BQKB1R b KQkq - 0 7";
    std::string fen = "1r1k3r/5p1p/p4p2/1p1Rp3/8/2P3P1/PPN2P1P/2K4R b - - 0 21";

    // Load position
    BoardData board = loadFEN(fen);
    std::cout << "Analyzing position: " << fen << std::endl;

    // int side = board.whiteToMove ? WHITE : BLACK;
    // std::string sideToMove = "Black";
    // if (side == WHITE) sideToMove = "White";
    // if (inCheck(board, side)) {
    //     std::cout << "Side to move: " << sideToMove << " is in check" << std::endl;
    // }

    // Generate all legal moves
    std::vector<Move> moves = generateMoves(board);
    std::vector<ScoredMove> scoredMoves;

    // Evaluate each move by applying it and calling evaluate()
    for (const auto& m : moves) {
        BoardData newBoard = applyMove(board, m);
        int eval = evaluate(newBoard);
        scoredMoves.push_back({m, eval});
    }

    // Sort by score (descending)
    std::sort(scoredMoves.begin(), scoredMoves.end(), compareMoves);

    // Print moves and their scores
    std::cout << "Legal moves ordered by evaluation:\n";
    for (const auto& sm : scoredMoves) {
        std::cout << moveToUci(sm.move) << " -> " << sm.score << std::endl;
    }

    return 0;
}
