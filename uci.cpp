// uci.cpp — UCI loop with PV, info metrics, currmove progress, and file logging

#include "uci.h"
#include "engine.h"
#include "threadpool.h"
#include "openingbook.h"
#include "fen.h"
#include "search.h"
#include "thread_context.h"
#include "uci_root_merge.h"

#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <vector>
#include <limits>
#include <cctype>
#include <algorithm>
#include <mutex>

static const int INF = std::numeric_limits<int>::max();

// ---------- File-based logging ----------
static std::ofstream logfile("engine_log.txt", std::ios::app);
#define LOG(msg) do { logfile << "[LOG] " << msg << std::endl; } while(0)

// ---------- UCI globals ----------
std::atomic<bool> stopSearch(false);
std::thread searchThread;
OpeningBook openingBook;

// ---------- UCI configurable options ---------------------
int hashSizeMB = 16;
std::string bookFile = "book.bin";
bool useBook = true;

// ---------- Helpers ----------
static std::string pvToUciString(const std::vector<Move>& pv) {
    std::ostringstream oss;
    for (const auto& mv : pv) {
        oss << moveToUci(mv) << ' ';
    }
    return oss.str();
}

static void joinSearchThread() {
    if (searchThread.joinable()) searchThread.join();
}

static inline std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back()))  s.pop_back();
    return s;
}

void runUciLoop() {
    BoardData board = getInitialBoard();
    std::string line;

    openingBook.load("book.bin"); // Load once

    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string token; iss >> token;

        if (token == "uci") {
            std::cout << "id name Modular Chess Engine\n";
            std::cout << "id author Ivan Bell\n";
            std::cout << "option name Hash type spin default 16 min 1 max 512" << std::endl;
            std::cout << "option name Book type string default book.bin" << std::endl;
            std::cout << "option name UseBook type check default true" << std::endl;
            std::cout << "uciok\n" << std::flush;

        } else if (token == "isready") {
            std::cout << "readyok\n" << std::flush;

        } else if (token == "setoption") {
            // Expected formats:
            // setoption name Hash value 64
            // setoption name Book value mybook.bin
            // setoption name UseBook value false
            std::string word, name, value;
            iss >> word; // "name"
            // read until " value"
            {
                std::string chunk;
                std::ostringstream n;
                while (iss >> chunk) {
                    if (chunk == "value") break;
                    if (!name.empty()) n << ' ';
                    n << chunk;
                }
                name = trim(n.str());
            }
            std::getline(iss, value);
            value = trim(value);

            if (name == "Hash") {
                try { hashSizeMB = std::max(1, std::min(512, std::stoi(value))); } catch(...) {}
                LOG("Hash size set to " + std::to_string(hashSizeMB) + " MB");

            } else if (name == "Book") {
                bookFile = value;
                LOG("Book path set to " + bookFile);
                // Optionally (re)load here:
                openingBook = OpeningBook();
                openingBook.load(bookFile);

            } else if (name == "UseBook") {
                std::string v = value; std::transform(v.begin(), v.end(), v.begin(),
                    [](unsigned char c){ return (char)std::tolower(c); });
                useBook = (v == "true" || v == "1" || v == "on");
                LOG(std::string("Book usage set to ") + (useBook ? "true" : "false"));
            }

        } else if (token == "ucinewgame") {
            board = getInitialBoard();
            stopSearch = false;
            joinSearchThread();
            LOG("New game initialized");

        } else if (token == "position") {
            // Supports: "position startpos [moves ...]" or "position fen <FEN> [moves ...]"
            parsePosition(line, board);
            LOG("Position set to: " + boardToFEN(board));

        } else if (token == "go") {
            // Parse time controls / depth / movetime
            int wtime = -1, btime = -1, winc = 0, binc = 0, movetime = -1, depthLimit = 0, movestogo = 0;
            std::string sub;
            while (iss >> sub) {
                if      (sub == "wtime")      iss >> wtime;
                else if (sub == "btime")      iss >> btime;
                else if (sub == "winc")       iss >> winc;
                else if (sub == "binc")       iss >> binc;
                else if (sub == "movetime")   iss >> movetime;
                else if (sub == "depth")      iss >> depthLimit;
                else if (sub == "movestogo")  iss >> movestogo;
            }

            // Simple time manager
            int timePerMoveMs = 10000; // default
            if (movetime > 0) {
                timePerMoveMs = movetime;
            } else {
                int remaining = board.whiteToMove ? wtime : btime;
                int inc       = board.whiteToMove ? winc  : binc;
                if (remaining > 0) {
                    int slices = (movestogo > 0 ? movestogo : 30);
                    timePerMoveMs = remaining / std::max(1, slices) + inc / 2;
                    timePerMoveMs = std::max(50, timePerMoveMs); // some floor
                }
            }
            if (depthLimit <= 0) depthLimit = 12; // default cap
            LOG("Search budget: " + std::to_string(timePerMoveMs) + "ms, depth cap " + std::to_string(depthLimit));

            // Opening book
            if (useBook) {
                std::string fen = boardToFEN(board);
                if (openingBook.hasMove(fen)) {
                    Move bookMove = openingBook.getMove(fen);
                    // bookMove = bookMoveToFullMove(bookMove, board);
                    LOG("Using book move: " + moveToUci(bookMove));
                    std::cout << "bestmove " << moveToUci(bookMove) << std::endl << std::flush;
                    continue;
                } else {
                    LOG("No book move found");
                }
            }

            // Launch search thread with iterative deepening, PV, info metrics,
            // currmove updates, and thread-local heuristic merge at root.
            stopSearch = false;
            joinSearchThread();
            searchThread = std::thread([board, timePerMoveMs, depthLimit]() {
                auto start   = std::chrono::steady_clock::now();
                auto deadline = start + std::chrono::milliseconds(timePerMoveMs);
                std::atomic<bool> stop(false);

                Move bestMove{};
                int bestEval = 0;

                // Prepare root move list (so we can emit currmove/currmovenumber)
                auto rootMoves = generateMoves(board);
                if (rootMoves.empty()) {
                    std::cout << "bestmove 0000" << std::endl << std::flush;
                    return;
                }

                RootAggregate agg(1u<<20);
                std::mutex mergeMu;

                for (int d = 1; d <= depthLimit; ++d) {
                    if (std::chrono::steady_clock::now() >= deadline || stopSearch.load()) break;

                    // (Optional) ordering of rootMoves can use agg.{history, killers}
                    // after d>1 to refine ordering.

                    // Track best at this depth
                    Move depthBest{}; int  depthBestEval = -INF;
                    std::vector<Move> depthBestPV;

                    // Iterate root moves, emitting currmove/currmovenumber
                    int rootIdx = 0;
                    // Simple parallel for each root move chunk
                    std::atomic<size_t> next{0};
                    const size_t N = rootMoves.size();
                    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
                    std::vector<std::thread> pool;
                    pool.reserve(threads);
                    std::mutex bestMu;

                    auto worker = [&]() {
                        // Ensure the thread-local ctx starts clean for this root iteration
                        g_ctx.resetAll();
                        g_ctx.age = (uint16_t)d;
                        for (;;) {
                            size_t i = next.fetch_add(1, std::memory_order_relaxed);
                            if (i >= N) break;
                            const auto& rootMove = rootMoves[i];
                            int myIdx = (int)(i+1);

                            // Reset ctx for each repetition
                            g_ctx.resetAll();
                            g_ctx.age = 0;

                            // Live progress line BEFORE we start searching this move
                            uint64_t nodesNow = g_nodes.load(std::memory_order_relaxed);
                            uint64_t msNow = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - start).count();
                            uint64_t npsNow = msNow ? (nodesNow * 1000ULL) / msNow : nodesNow * 1000ULL;

                            std::cout << "info currmove " << moveToUci(rootMove)
                                    << " currmovenumber " << myIdx
                                    << " time " << msNow
                                    << " nodes " << nodesNow
                                    << " nps " << npsNow
                                    << std::endl;

                            if (std::chrono::steady_clock::now() >= deadline || stopSearch.load()) break;

                            // Search this root move at depth d
                            BoardData next = applyMove(board, rootMove);
                            g_nodes.store(0, std::memory_order_relaxed); // measure per-move if you like (optional)
                            std::vector<Move> childPV;
                            int alpha = -INF, beta = INF;

                            int eval = alphabetaTimed(
                                next,           // position after rootMove
                                d - 1,          // remaining depth
                                alpha, beta,
                                !board.whiteToMove,
                                deadline, stop,
                                childPV
                            );

                            // Build PV = [rootMove] + childPV
                            std::vector<Move> pv; pv.reserve(1 + childPV.size());
                            pv.push_back(rootMove);
                            pv.insert(pv.end(), childPV.begin(), childPV.end());
                            {
                                // Update best line at this depth
                                std::lock_guard<std::mutex> lk(bestMu);
                                if (eval > depthBestEval) {
                                    depthBestEval = eval;
                                    depthBest = rootMove;
                                    depthBestPV = std::move(pv);
                                }
                            }
                            // Merge this thread's local heuristic tables into the root aggregate
                            {
                                std::lock_guard<std::mutex> lk(mergeMu);
                                agg.mergeFrom(g_ctx.history);
                                agg.mergeFrom(g_ctx.killers);
                                agg.mergeFrom(g_ctx.tt);
                            }
                            
                            if (std::chrono::steady_clock::now() >= deadline) break;
                        }
                    };

                    for (unsigned t=0; t<threads; ++t) pool.emplace_back(worker);
                    for (auto& th : pool) th.join();

                    // Emit depth summary with PV + nodes/time/nps
                    uint64_t ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - start).count();
                    uint64_t nodes = g_nodes.load(std::memory_order_relaxed);
                    uint64_t nps   = ms ? (nodes * 1000ULL) / ms : nodes * 1000ULL;

                    // Update global best from this depth
                    if (!depthBestPV.empty()) {
                        bestMove = depthBest;
                        bestEval = depthBestEval;
                        std::cout << "info depth " << d
                                << " score cp " << bestEval
                                << " time " << ms
                                << " nodes " << nodes
                                << " nps " << nps
                                << " pv " << pvToUciString(depthBestPV)
                                << std::endl;
                    } else {
                        // No PV (timeout or no legal line)
                        std::cout << "info depth " << d
                                << " score cp 0"
                                << " time " << ms
                                << " nodes " << nodes
                                << " nps " << nps
                                << " pv"
                                << std::endl;
                    }

                    if (std::chrono::steady_clock::now() >= deadline) break;
                }

                LOG("Best move selected by search: " + moveToUci(bestMove));
                std::cout << "bestmove " << moveToUci(bestMove) << std::endl << std::flush;
            });

        } else if (token == "stop") {
            stopSearch = true;
            joinSearchThread();
            LOG("Search stopped");

        } else if (token == "quit") {
            stopSearch = true;
            joinSearchThread();
            LOG("Engine quitting...");
            break;
        }
    }

    logfile.close();
}
