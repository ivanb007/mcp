
#include "openingbook.h"
#include "engine.h"
#include "fen.h"
#include <iostream>
#include <cassert>
#include <cstdint>

void testPolyglotKeyCalculation() {
    OpeningBook book;
    book.load("/home/ivan/github/mcp/default_book.bin"); // assume this file is present and valid

    // Famous opening position: King's Pawn Opening
    std::string fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
    bool has = book.hasMove(fen);
    std::cout << "Book has move for King's Pawn: " << std::boolalpha << has << std::endl;
    assert(has && "Book should have a move for this FEN if opening book includes it");

    if (has) {
        Move move = book.getMove(fen);
        std::string uci = moveToUci(move);
        std::cout << "Returned book move: " << uci << std::endl;
    }
}

void testFallbackNoBookMove() {
    OpeningBook book;
    book.load("/home/ivan/github/mcp/default_book.bin");

    std::string fen = "8/8/8/8/8/8/8/8 w - - 0 1";  // empty board
    bool has = book.hasMove(fen);
    std::cout << "Book has move for empty board: " << std::boolalpha << has << std::endl;
    assert(!has && "Book should not return a move for an empty board");
}

int main() {
    testPolyglotKeyCalculation();
    testFallbackNoBookMove();
    std::cout << "All opening book tests passed.\n";
    return 0;
}
