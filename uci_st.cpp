// uci_st.cpp — single-threaded UCI loop (no background threads)
// Build this as a separate target/binary, e.g., my_engine_st

#include "uci.h"
#include "engine.h"
#include "search.h"        // alphabetaTimed(..., std::vector<Move>& pv), g_nodes (ok)
#include "openingbook.h"
#include "fen.h"

#include <iostream>
#include <sstream>
#include <chrono>
#include <atomic>
#include <vector>
#include <limits>
#include <fstream>
#include <cctype>
#include <algorithm>

static const int INF = std::numeric_limits<int>::max();

// ---------- File logging (optional) ----------
static std::ofstream logfile("engine_log_st.txt", std::ios::app);
#define LOG(msg) do { logfile << "[LOG] " << msg << std::endl; } while(0)

// ---------- Options ----------
static int         hashSizeMB = 16;
static std::string bookFile   = "book.bin";
static bool        useBook    = true;

// ---------- Helpers ----------
static inline std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back()))  s.pop_back();
    return s;
}

static std::string pvToUci(const std::vector<Move>& pv) {
    std::ostringstream oss;
    for (const auto& m : pv) oss << moveToUci(m) << ' ';
    return oss.str();
}

// Single-threaded, blocking UCI loop
void runUciLoop() {
    BoardData  board = getInitialBoard();
    OpeningBook openingBook; // load lazily when used
    bool bookLoaded = false;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string token; iss >> token;

        if (token == "uci") {
            std::cout << "id name MyChessEngine-ST\n";
            std::cout << "id author YourName\n";
            std::cout << "option name Hash type spin default 16 min 1 max 512\n";
            std::cout << "option name Book type string default book.bin\n";
            std::cout << "option name UseBook type check default true\n";
            std::cout << "uciok\n" << std::flush;

        } else if (token == "isready") {
            std::cout << "readyok\n" << std::flush;

        } else if (token == "setoption") {
            // setoption name <Name> value <Value>
            std::string word, name, value;
            iss >> word; // "name"
            {
                // read until "value"
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
                try { hashSizeMB = std::max(1, std::min(512, std::stoi(value))); } catch (...) {}
                LOG("Hash size set to " + std::to_string(hashSizeMB) + " MB");
            } else if (name == "Book") {
                bookFile = value;
                bookLoaded = false; // reload on-demand
                LOG("Book path set to " + bookFile);
            } else if (name == "UseBook") {
                std::string v = value; std::transform(v.begin(), v.end(), v.begin(),
                    [](unsigned char c){ return (char)std::tolower(c); });
                useBook = (v == "true" || v == "1" || v == "on");
                LOG(std::string("UseBook = ") + (useBook ? "true" : "false"));
            }

        } else if (token == "ucinewgame") {
            board = getInitialBoard();
            LOG("New game initialized");

        } else if (token == "position") {
            // supports: "position startpos [moves ...]" or "position fen <FEN> [moves ...]"
            parsePosition(line, board);
            LOG("Position: " + boardToFEN(board));

        } else if (token == "go") {
            // Parse only what we support in ST path
            int wtime=-1, btime=-1, winc=0, binc=0, movetime=-1, depthLimit=0, movestogo=0;
            std::string sub;
            while (iss >> sub) {
                if      (sub == "wtime")     iss >> wtime;
                else if (sub == "btime")     iss >> btime;
                else if (sub == "winc")      iss >> winc;
                else if (sub == "binc")      iss >> binc;
                else if (sub == "movetime")  iss >> movetime;
                else if (sub == "depth")     iss >> depthLimit;
                else if (sub == "movestogo") iss >> movestogo;
            }

            // Time manager (very simple)
            int timePerMoveMs = 1000;
            if (movetime > 0) {
                timePerMoveMs = movetime;
            } else {
                int remaining = board.whiteToMove ? wtime : btime;
                int inc       = board.whiteToMove ? winc  : binc;
                if (remaining > 0) {
                    int slices = (movestogo > 0 ? movestogo : 30);
                    timePerMoveMs = remaining / std::max(1, slices) + inc/2;
                    timePerMoveMs = std::max(50, timePerMoveMs);
                }
            }
            if (depthLimit <= 0) depthLimit = 12; // default cap

            // Opening book (optional)
            if (useBook) {
                if (!bookLoaded) {
                    // If you have a load() method, call it here.
                    // openingBook.load(bookFile);
                    bookLoaded = true; // set true regardless to avoid reloading loop
                }
                std::string fen = boardToFEN(board);
                if (openingBook.hasMove(fen)) {
                    Move bookMove = openingBook.getMove(fen);
                    LOG("Book bestmove " + moveToUci(bookMove));
                    std::cout << "bestmove " << moveToUci(bookMove) << std::endl << std::flush;
                    continue;
                }
            }

            // Single-threaded iterative deepening with deadline
            auto start    = std::chrono::steady_clock::now();
            auto deadline = start + std::chrono::milliseconds(timePerMoveMs);
            std::atomic<bool> stop(false);

            Move bestMove{};
            int  bestEval = 0;

            for (int d = 1; d <= depthLimit; ++d) {
                if (std::chrono::steady_clock::now() >= deadline) break;

                std::vector<Move> pv;
                int eval = alphabetaTimed(board, d, -INF, INF, board.whiteToMove, deadline, stop, pv);

                if (!pv.empty()) {
                    bestMove = pv.front();
                    bestEval = eval;

                    // Emit UCI info line with nodes/time/nps if available
                    uint64_t ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - start).count();
                    extern std::atomic<uint64_t> g_nodes;
                    uint64_t nodes = g_nodes.load(std::memory_order_relaxed);
                    uint64_t nps   = ms ? (nodes * 1000ULL) / ms : nodes * 1000ULL;

                    std::cout << "info depth " << d
                              << " score cp " << bestEval
                              << " time " << ms
                              << " nodes " << nodes
                              << " nps " << nps
                              << " pv " << pvToUci(pv)
                              << std::endl;
                } else {
                    // No PV (e.g., no legal moves)
                    break;
                }
            }

            if (bestMove.fromRow==0 && bestMove.fromCol==0 && bestMove.toRow==0 && bestMove.toCol==0) {
                std::cout << "bestmove 0000" << std::endl << std::flush;
            } else {
                LOG("ST bestmove " + moveToUci(bestMove) + " score " + std::to_string(bestEval));
                std::cout << "bestmove " << moveToUci(bestMove) << std::endl << std::flush;
            }

        } else if (token == "stop") {
            // Single-threaded: nothing to cancel; just acknowledge by staying quiet or logging.
            LOG("stop (no-op in ST)");

        } else if (token == "quit") {
            LOG("quit");
            break;
        }
    }

    logfile.close();
}
