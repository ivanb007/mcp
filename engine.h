
#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct Move {
    int fromRow, fromCol, toRow, toCol;
};

struct BoardData {
    char pieces[64];           // Flat 8x8 board: row * 8 + col
    bool whiteToMove;          // Whose turn is it
};

// Engine API
BoardData getInitialBoard();
void parsePosition(const std::string& input, BoardData& board);
BoardData applyMove(BoardData board, Move m);
std::string moveToUci(const Move& m);

// Zobrist hashing support
struct Zobrist {
    uint64_t pieceHash[12][64];
    uint64_t whiteToMoveHash;

    Zobrist();
    uint64_t computeHash(const BoardData& board);
};
