
#include "search.h"
#include "threadpool.h"
#include "engine.h"
#include <limits>
#include <algorithm>
#include <unordered_map>

const int INF = std::numeric_limits<int>::max();
const int MAX_DEPTH = 64;

std::array<std::vector<Move>, MAX_DEPTH> killerMoves;
std::unordered_map<uint64_t, int> historyHeuristic;

bool isSameMove(const Move& a, const Move& b) {
    return a.fromRow == b.fromRow && a.fromCol == b.fromCol &&
           a.toRow == b.toRow && a.toCol == b.toCol;
}

void sortMoves(std::vector<Move>& moves, const BoardData& board, int depth) {
    Zobrist zobrist;
    std::sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
        for (const auto& km : killerMoves[depth]) {
            if (isSameMove(a, km)) return true;
            if (isSameMove(b, km)) return false;
        }
        uint64_t hashA = zobrist.computeHash(applyMove(board, a));
        uint64_t hashB = zobrist.computeHash(applyMove(board, b));
        return historyHeuristic[hashA] > historyHeuristic[hashB];
    });
}

int evaluate(const BoardData& state) {
    int score = 0;
    for (int i = 0; i < 64; ++i) {
        char p = state.pieces[i];
        if (p == '.') continue;
        int val;
        switch (tolower(p)) {
            case 'p': val = 10; break;
            case 'n': case 'b': val = 30; break;
            case 'r': val = 50; break;
            case 'q': val = 90; break;
            case 'k': val = 900; break;
            default: val = 0;
        }
        score += isupper(p) ? val : -val;
    }
    return score;
}

std::vector<Move> generateMoves(const BoardData& board) {
    std::vector<Move> quietMoves;
    std::vector<Move> captureMoves;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            char p = board.pieces[r * 8 + c];
            if (p == '.' || (board.whiteToMove && islower(p)) || (!board.whiteToMove && isupper(p)))
                continue;
            if (c + 1 < 8) {
                int toIdx = r * 8 + c + 1;
                char target = board.pieces[toIdx];
                if (target != '.' && ((isupper(p) && islower(target)) || (islower(p) && isupper(target))))
                    captureMoves.push_back({r, c, r, c + 1});
                else if (target == '.')
                    quietMoves.push_back({r, c, r, c + 1});
            }
        }
    }
    std::vector<Move> allMoves = captureMoves;
    allMoves.insert(allMoves.end(), quietMoves.begin(), quietMoves.end());
    return allMoves;
}

int quiescence(BoardData board, int alpha, int beta, bool maximizing,
               std::chrono::steady_clock::time_point deadline, std::atomic<bool>& stop) {
    if (stop.load() || std::chrono::steady_clock::now() > deadline) return 0;
    int stand_pat = evaluate(board);
    if (maximizing) {
        if (stand_pat >= beta) return beta;
        if (stand_pat > alpha) alpha = stand_pat;
    } else {
        if (stand_pat <= alpha) return alpha;
        if (stand_pat < beta) beta = stand_pat;
    }

    auto moves = generateMoves(board);
    for (const auto& m : moves) {
        int toIdx = m.toRow * 8 + m.toCol;
        int fromIdx = m.fromRow * 8 + m.fromCol;
        char captured = board.pieces[toIdx];
        if (captured == '.') continue;
        BoardData newBoard = applyMove(board, m);
        int score = quiescence(newBoard, alpha, beta, !maximizing, deadline, stop);
        if (maximizing) {
            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        } else {
            if (score <= alpha) return alpha;
            if (score < beta) beta = score;
        }
    }
    return maximizing ? alpha : beta;
}

int alphabetaTimed(BoardData board, int depth, int alpha, int beta, bool maximizing,
                   std::chrono::steady_clock::time_point deadline, std::atomic<bool>& stop) {
    if (stop.load() || std::chrono::steady_clock::now() > deadline) return 0;
    if (depth == 0) return quiescence(board, alpha, beta, maximizing, deadline, stop);

    auto moves = generateMoves(board);
    if (moves.empty()) return evaluate(board);
    sortMoves(moves, board, depth);

    if (maximizing) {
        int maxEval = -INF;
        for (auto& m : moves) {
            BoardData newBoard = applyMove(board, m);
            int eval = alphabetaTimed(newBoard, depth - 1, alpha, beta, false, deadline, stop);
            if (eval > maxEval) {
                maxEval = eval;
                if (eval >= beta) {
                    killerMoves[depth].push_back(m);
                    break;
                }
            }
            alpha = std::max(alpha, eval);
            uint64_t h = Zobrist().computeHash(newBoard);
            historyHeuristic[h] += depth * depth;
        }
        return maxEval;
    } else {
        int minEval = INF;
        for (auto& m : moves) {
            BoardData newBoard = applyMove(board, m);
            int eval = alphabetaTimed(newBoard, depth - 1, alpha, beta, true, deadline, stop);
            if (eval < minEval) {
                minEval = eval;
                if (eval <= alpha) {
                    killerMoves[depth].push_back(m);
                    break;
                }
            }
            beta = std::min(beta, eval);
            uint64_t h = Zobrist().computeHash(newBoard);
            historyHeuristic[h] += depth * depth;
        }
        return minEval;
    }
}

Move findBestMoveParallel(BoardData board, int depth, int timeLimitMs) {
    auto moves = generateMoves(board);
    ThreadPool pool(std::thread::hardware_concurrency());
    std::vector<std::future<int>> futures;
    std::atomic<bool> localStop(false);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeLimitMs);

    for (const auto& m : moves) {
        BoardData next = applyMove(board, m);
        futures.emplace_back(pool.enqueue([=, &localStop]() {
            return alphabetaTimed(next, depth - 1, -INF, INF, false, deadline, localStop);
        }));
    }

    int bestScore = -INF;
    Move bestMove = moves[0];
    for (size_t i = 0; i < moves.size(); ++i) {
        if (std::chrono::steady_clock::now() > deadline) {
            localStop = true;
            break;
        }
        int score = futures[i].get();
        if (score > bestScore) {
            bestScore = score;
            bestMove = moves[i];
        }
    }
    return bestMove;
}
