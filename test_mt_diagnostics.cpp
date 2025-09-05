// diagnostic harness that can prove whether threads are interfering with each other
#include "engine.h"
#include "fen.h"
#include "search.h"
#include "thread_context.h"  // thread_local g_ctx (new)

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// ---------- Simple stats ----------
struct Stats {
    int count = 0;
    long long sum = 0;
    long long sumsq = 0;
    int minv = +1000000000;
    int maxv = -1000000000;

    void add(int v) {
        ++count; sum += v; sumsq += 1LL*v*v;
        if (v < minv) minv = v;
        if (v > maxv) maxv = v;
    }
    double mean() const { return count ? double(sum)/count : 0.0; }
    double stddev() const {
        if (count <= 1) return 0.0;
        double m = mean();
        double var = double(sumsq)/count - m*m;
        return var > 0 ? std::sqrt(var) : 0.0;
    }
};

// Format PV vector to UCI string
static std::string pvToUci(const std::vector<Move>& pv) {
    std::ostringstream oss;
    for (const auto& m : pv) oss << moveToUci(m) << ' ';
    return oss.str();
}

// Evaluate one root move at given depth/time, returning eval and PV (from the move onward)
static int evalMoveWithPV(const BoardData& root, const Move& rootMove, int remainingDepth,
                          int timeMs, std::vector<Move>& fullPV)
{
    BoardData next = applyMove(root, rootMove);
    auto now = std::chrono::steady_clock::now();
    auto deadline = (timeMs > 0) ? (now + std::chrono::milliseconds(timeMs))
                                 : (now + std::chrono::hours(24)); // effectively no cutoff
    std::atomic<bool> stop(false);
    std::vector<Move> tail;
    int eval = alphabetaTimed(next, std::max(0, remainingDepth), -100000, 100000,
                              !root.whiteToMove, deadline, stop, tail);

    fullPV.clear();
    fullPV.reserve(1 + tail.size());
    fullPV.push_back(rootMove);
    fullPV.insert(fullPV.end(), tail.begin(), tail.end());
    return eval;
}

// ---------- Diagnostic runner ----------
int main(int argc, char** argv) {
    // Args:
    // 1: FEN (default: startpos)
        // used "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
    // 2: depth (default: 2)
    // 3: timeMs per move (default: 0 -> depth-only; recommended for determinism)
    // 4: threads (default: hw_concurrency or 4)
    // 5: repeats per move in parallel pass (default: 16)
    // 6: stressLoops (repeat the whole parallel pass) (default: 1)
    // 7: useThreadLocals flag (0/1) — if you temporarily keep a non-threadlocal path for A/B (default: 1)
    // 8: resetThreadLocalEachRep (0/1) — reset g_ctx for each repetition, not just per worker (default: 0)
    std::string fen = (argc >= 2)
        ? std::string(argv[1])
        : std::string("r2qkb1r/3n1ppp/p2pbn2/3Np3/Pp2P1P1/1N2BP2/1PP4P/R2QKB1R b KQkq - 1 13");
    int depth   = (argc >= 3) ? std::max(1, std::atoi(argv[2])) : 2;
    int timeMs  = (argc >= 4) ? std::max(0, std::atoi(argv[3])) : 0;
    unsigned threads = (argc >= 5) ? std::max(1, std::atoi(argv[4]))
                                   : (std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 4);
    int repeats = (argc >= 6) ? std::max(1, std::atoi(argv[5])) : 16;
    int stressLoops = (argc >= 7) ? std::max(1, std::atoi(argv[6])) : 1;
    bool useThreadLocals = (argc >= 8) ? (std::atoi(argv[7]) != 0) : true;
    bool resetCtxEachRep = (argc >= 9) ? (std::atoi(argv[8]) != 0) : false;

    BoardData board = loadFEN(fen);
    const bool sideIsWhite = board.whiteToMove;

    std::cout << "Diagnostic: MT vs ST with thread-local heuristics\n";
    std::cout << "FEN: " << fen << "\n";
    std::cout << "depth=" << depth << " (root makes 1 ply; search does " << std::max(0, depth-1) << ") "
              << "timePerMove=" << timeMs << "ms "
              << "threads=" << threads << " "
              << "repeats=" << repeats << " "
              << "stressLoops=" << stressLoops << " "
              << "useThreadLocals=" << (useThreadLocals ? 1 : 0) << " "
              << "resetTLctxEachRep=" << (resetCtxEachRep ? 1 : 0)
              << "\n\n";

    // 1) Generate root moves
    auto rootMoves = generateMoves(board);
    if (rootMoves.empty()) {
        std::cout << "No legal moves.\n";
        return 0;
    }

    // 2) Single-threaded baseline
    struct Entry {
        Move move;
        int baselineEval = 0;
        std::vector<Move> baselinePV;
        Stats parallelStats;
        int mismatches = 0;
    };
    std::vector<Entry> entries(rootMoves.size());
    for (size_t i = 0; i < rootMoves.size(); ++i) {
        entries[i].move = rootMoves[i];
        std::vector<Move> pv;
        entries[i].baselineEval = evalMoveWithPV(board, entries[i].move, depth - 1, 0 /*no cutoff*/, pv);
        entries[i].baselinePV = std::move(pv);
    }

    // Print baseline ranked (normalized by side-to-move)
    {
        std::vector<size_t> idx(rootMoves.size());
        std::iota(idx.begin(), idx.end(), 0);
        auto better = [&](size_t a, size_t b) {
            int ea = entries[a].baselineEval, eb = entries[b].baselineEval;
            int na = sideIsWhite ? ea : -ea;
            int nb = sideIsWhite ? eb : -eb;
            return na > nb;
        };
        std::sort(idx.begin(), idx.end(), better);

        std::cout << "=== Baseline (single-thread) ranking ===\n";
        std::cout << std::left << std::setw(10) << "Move"
                  << std::right << std::setw(10) << "Score"
                  << "    PV\n";
        std::cout << std::string(10 + 10 + 4 + 40, '-') << "\n";
        for (auto i : idx) {
            std::cout << std::left << std::setw(10) << moveToUci(entries[i].move)
                      << std::right << std::setw(10) << entries[i].baselineEval
                      << "    " << pvToUci(entries[i].baselinePV) << "\n";
        }
        std::cout << "\n";
    }

    // 3) Parallel passes using thread-local contexts
    for (int loop = 1; loop <= stressLoops; ++loop) {
        std::cout << "=== Parallel pass " << loop << "/" << stressLoops
                  << " (threads=" << threads << ", repeats=" << repeats << ") ===\n";

        struct WorkItem { size_t moveIndex; int rep; };
        std::vector<WorkItem> work; work.reserve(rootMoves.size() * repeats);
        for (size_t i = 0; i < rootMoves.size(); ++i)
            for (int r = 0; r < repeats; ++r)
                work.push_back({i, r});

        std::atomic<size_t> next{0};
        std::mutex errMu;

        auto worker = [&]() {
            // Ensure fresh thread-local heuristic state for this worker
            if (useThreadLocals) {
                g_ctx.resetAll();
                g_ctx.age = 0; // or loop/depth if you want to track ages
            }
            for (;;) {
                size_t k = next.fetch_add(1, std::memory_order_relaxed);
                if (k >= work.size()) break;
                size_t i = work[k].moveIndex;

                if (useThreadLocals && resetCtxEachRep) {
                    g_ctx.resetAll();
                    g_ctx.age = 0;
                }

                std::vector<Move> pv;
                int eval = evalMoveWithPV(board, entries[i].move, depth - 1, timeMs, pv);

                entries[i].parallelStats.add(eval);
                if (eval != entries[i].baselineEval) {
                    int mcount = ++entries[i].mismatches;
                    std::lock_guard<std::mutex> lk(errMu);
                    std::cerr << "[WARN] Mismatch move=" << moveToUci(entries[i].move)
                              << " rep=" << work[k].rep
                              << " baseline=" << entries[i].baselineEval
                              << " parallel=" << eval
                              << " pv=" << pvToUci(pv) << "\n";
                    (void)mcount;
                }
            }
        };

        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
        for (auto& th : pool) th.join();

        // Report stats per move
        std::cout << std::left << std::setw(10) << "Move"
                  << std::right << std::setw(10) << "Baseline"
                  << std::right << std::setw(12) << "Min"
                  << std::right << std::setw(12) << "Max"
                  << std::right << std::setw(12) << "Mean"
                  << std::right << std::setw(12) << "StdDev"
                  << std::right << std::setw(12) << "Mismatches"
                  << "\n";
        std::cout << std::string(10 + 10 + 12 * 5 + 12, '-') << "\n";
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            const auto& s = e.parallelStats;
            std::cout << std::left  << std::setw(10) << moveToUci(e.move)
                      << std::right << std::setw(10) << e.baselineEval
                      << std::right << std::setw(12) << (s.count ? s.minv : 0)
                      << std::right << std::setw(12) << (s.count ? s.maxv : 0)
                      << std::right << std::setw(12) << std::fixed << std::setprecision(1) << s.mean()
                      << std::right << std::setw(12) << std::fixed << std::setprecision(1) << s.stddev()
                      << std::right << std::setw(12) << e.mismatches
                      << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "Done.\n";
    return 0;
}
