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
#include <thread>

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
    //   argv[4] = threads (optional; default = std::thread::hardware_concurrency() or 4)
    std::string fen = (argc >= 2)
        ? std::string(argv[1])
        : std::string("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    int depth   = (argc >= 3) ? std::max(1, std::atoi(argv[2])) : 2;
    int timeMs  = (argc >= 4) ? std::max(0, std::atoi(argv[3])) : 0; // 0 => huge deadline
    unsigned th = (argc >= 5) ? std::max(1, std::atoi(argv[4]))
                              : (std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 4);

    BoardData board = loadFEN(fen);
    std::cout << "Position: " << fen << "\n";
    std::cout << "Search depth per move: " << depth << ", budget: " << timeMs
              << " ms, threads: " << th << "\n\n";

    auto moves = generateMoves(board);
    if (moves.empty()) {
        std::cout << "No legal moves.\n";
        return 0;
    }

    std::vector<ScoredMove> list(moves.size());  // pre-sized for lock-free writes
    const bool sideIsWhite = board.whiteToMove;

    // Task distributor
    std::atomic<size_t> nextIdx{0};
    auto worker = [&]() {
        for (;;) {
            size_t i = nextIdx.fetch_add(1, std::memory_order_relaxed);
            if (i >= moves.size()) break;

            const auto& m = moves[i];
            // std::cout << "This is thread: " << i << " evaluating move " << moveToUci(m) << std::endl;
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

            int norm = sideIsWhite ? eval : -eval;

            list[i] = ScoredMove{m, eval, norm, std::move(fullPV)};
        }
    };

    // Launch N workers
    std::vector<std::thread> pool;
    pool.reserve(th);
    for (unsigned t = 0; t < th; ++t) pool.emplace_back(worker);
    for (auto& thd : pool) thd.join();

    // Order by normalized score (best for side to move on top)
    std::sort(list.begin(), list.end(), betterForSide);

    // Print — identical format to the single-threaded version
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
