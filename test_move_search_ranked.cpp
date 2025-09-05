#include "engine.h"
#include "fen.h"
#include "search.h"   // alphabetaTimed(..., std::vector<Move>& pv)
#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <chrono>
#include <atomic>
#include <sstream>
#include <iomanip>

// Hold a move, its raw eval, normalized eval (from side-to-move POV), and PV
struct ScoredMove {
    Move move;
    int scoreCp;         // raw evaluation in centipawns (white positive)
    int normScoreCp;     // normalized for side to move
    std::vector<Move> pv;
};

static std::string pvToUci(const std::vector<Move>& pv) {
    std::ostringstream oss;
    for (const auto& m : pv) oss << moveToUci(m) << ' ';
    return oss.str();
}

// Rank comparator: higher normalized score first
static bool betterForSide(const ScoredMove& a, const ScoredMove& b) {
    return a.normScoreCp > b.normScoreCp;
}

int main(int argc, char** argv) {
    // Args:
    //   argv[1] = FEN (optional; defaults to startpos)
    //   argv[2] = depth (optional; default 2)
    //   argv[3] = time per move in ms (optional; default 0 => effectively "no time limit")
    std::string fen = (argc >= 2)
        ? std::string(argv[1])
        : std::string("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    int depth = (argc >= 3) ? std::max(1, std::atoi(argv[2])) : 2;
    int timeMs = (argc >= 4) ? std::max(0, std::atoi(argv[3])) : 0;

    BoardData board = loadFEN(fen);
    std::cout << "Position: " << fen << "\n";
    std::cout << "Search depth per move: " << depth << ", budget: " << timeMs << " ms\n\n";

    auto moves = generateMoves(board);
    if (moves.empty()) {
        std::cout << "No legal moves.\n";
        return 0;
    }

    std::vector<ScoredMove> list;
    list.reserve(moves.size());

    const bool sideIsWhite = board.whiteToMove;
    auto globalStart = std::chrono::steady_clock::now();

    for (const auto& m : moves) {
        BoardData next = applyMove(board, m);

        // Depth-remaining after making the root move
        int remDepth = std::max(0, depth - 1);

        // Build a deadline: if timeMs == 0, set it far in the future for deterministic depth-only search
        auto now = std::chrono::steady_clock::now();
        auto deadline = (timeMs > 0)
                ? (now + std::chrono::milliseconds(timeMs))
                : (now + std::chrono::hours(24)); // effectively "no cutoff" for shallow depths

        std::atomic<bool> stop(false);
        std::vector<Move> pv;
        // We already made one ply (the candidate), so search the remainder.
        int eval = alphabetaTimed(
            next,
            remDepth,
            -100000, 100000,
            !sideIsWhite,          // next side to move
            deadline,
            stop,
            pv
        );

        // Build full PV starting with the root move:
        std::vector<Move> fullPV;
        fullPV.reserve(1 + pv.size());
        fullPV.push_back(m);
        fullPV.insert(fullPV.end(), pv.begin(), pv.end());

        // Normalize the score so "bigger is better for the side to move"
        int norm = sideIsWhite ? eval : -eval;

        list.push_back(ScoredMove{m, eval, norm, std::move(fullPV)});
    }

    // Order by normalized score (best for side to move on top)
    std::sort(list.begin(), list.end(), betterForSide);

    // Pretty print
    std::cout << std::left << std::setw(10) << "Move"
              << std::right << std::setw(10) << "Score"
              << std::right << std::setw(12) << "NormScore"
              << "    PV\n";
    std::cout << std::string(10+10+12+4+40, '-') << "\n";
    for (const auto& sm : list) {
        std::cout << std::left  << std::setw(10) << moveToUci(sm.move)
                  << std::right << std::setw(10) << sm.scoreCp
                  << std::right << std::setw(12) << sm.normScoreCp
                  << "    " << pvToUci(sm.pv) << "\n";
    }

    return 0;
}
