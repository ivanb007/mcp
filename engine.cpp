
#include "engine.h"
#include <sstream>
#include <random>
#include <chrono>

BoardData getInitialBoard() {
    BoardData board;
    const char* init =
        "rnbqkbnr"
        "pppppppp"
        "........"
        "........"
        "........"
        "........"
        "PPPPPPPP"
        "RNBQKBNR";

    for (int i = 0; i < 64; ++i)
        board.pieces[i] = init[i];
    board.whiteToMove = true;
    return board;
}

BoardData applyMove(BoardData board, Move m) {
    int from = m.fromRow * 8 + m.fromCol;
    int to = m.toRow * 8 + m.toCol;
    board.pieces[to] = board.pieces[from];
    board.pieces[from] = '.';
    board.whiteToMove = !board.whiteToMove;
    return board;
}

void parsePosition(const std::string& input, BoardData& board) {
    std::istringstream iss(input);
    std::string token;
    iss >> token >> token;  // skip "position startpos"
    board = getInitialBoard();
    if (iss >> token && token == "moves") {
        while (iss >> token) {
            int fromCol = token[0] - 'a';
            int fromRow = 8 - (token[1] - '0');
            int toCol = token[2] - 'a';
            int toRow = 8 - (token[3] - '0');
            board = applyMove(board, {fromRow, fromCol, toRow, toCol});
        }
    }
}

std::string moveToUci(const Move& m) {
    std::string uci;
    uci += 'a' + m.fromCol;
    uci += '8' - m.fromRow;
    uci += 'a' + m.toCol;
    uci += '8' - m.toRow;
    return uci;
}

namespace {
    int pieceToIndex(char p) {
        switch (p) {
            case 'P': return 0; case 'N': return 1; case 'B': return 2;
            case 'R': return 3; case 'Q': return 4; case 'K': return 5;
            case 'p': return 6; case 'n': return 7; case 'b': return 8;
            case 'r': return 9; case 'q': return 10; case 'k': return 11;
            default: return -1;
        }
    }
}

Zobrist::Zobrist() {
    std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    for (int i = 0; i < 12; ++i)
        for (int j = 0; j < 64; ++j)
            pieceHash[i][j] = rng();
    whiteToMoveHash = rng();
}

uint64_t Zobrist::computeHash(const BoardData& board) {
    uint64_t h = 0;
    for (int sq = 0; sq < 64; ++sq) {
        int idx = pieceToIndex(board.pieces[sq]);
        if (idx != -1) h ^= pieceHash[idx][sq];
    }
    if (board.whiteToMove)
        h ^= whiteToMoveHash;
    return h;
}
