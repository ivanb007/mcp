#include "engine.h"
#include "fen.h"
#include "search.h"

#include <iostream>
#include <vector>
#include <cassert>

void testEscapeFromCheck() {
    // White king on e1 is in check from black rook on e8
    std::string fen = "4r3/8/8/8/8/8/8/4K3 w - - 0 1";
    BoardData board = loadFEN(fen);
    assert(inCheck(board, WHITE) && "White king should be in check");

    auto moves = generateMoves(board);
    std::cout << "Generated " << moves.size() << " legal moves to escape check." << std::endl;

    for (const auto& move : moves) {
        BoardData newBoard = applyMove(board, move);
        bool stillInCheck = inCheck(newBoard, WHITE); // black to move after white
        std::string uci = moveToUci(move);
        std::cout << "Move: " << uci << (stillInCheck ? " ❌ still in check!" : " ✅ escapes") << std::endl;
        assert(!stillInCheck && "Move must escape from check");
    }

    std::cout << "✅ All moves successfully escape check." << std::endl;
}

int main() {
    testEscapeFromCheck();
    return 0;
}
