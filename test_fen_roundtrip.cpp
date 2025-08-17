
#include "fen.h"
#include "engine.h"
#include <iostream>
#include <vector>
#include <cassert>

void testFENRoundTrip(const std::string& fen) {
    try {
        BoardData board = loadFEN(fen);
        std::string roundtrip = boardToFEN(board);
        std::cout << "Original FEN    : " << fen << std::endl;
        std::cout << "Round-tripped FEN: " << roundtrip << std::endl;
        assert(fen == roundtrip && "FEN mismatch after roundtrip conversion!");
        std::cout << "✅ Round-trip successful\n" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception during test: " << e.what() << std::endl;
        assert(false && "FEN parsing or generation failed.");
    }
}

int main() {
    std::vector<std::string> testFENs = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "rnbqkbnr/pppp1ppp/8/4p3/1P6/5N2/P1PPPPPP/RNBQKB1R b KQkq b3 0 2",
        "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 2 4",
        "r2q1rk1/ppp2ppp/2n2n2/2bp4/2P5/2NP1NP1/PP2PPBP/R1BQ1RK1 w - - 9 10",
        "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
        "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2",
        "rnbqkbnr/p1pppppp/8/8/PpP4P/8/1P1PPPP1/RNBQKBNR b KQkq c3 0 3",
        "rnbqkbnr/p1pppppp/8/8/P6P/R1p5/1P1PPPP1/1NBQKBNR b Kkq - 0 4",
        "8/8/8/8/8/8/8/8 w - - 0 1"
    };

    for (const auto& fen : testFENs) {
        testFENRoundTrip(fen);
    }

    std::cout << "🎉 All FEN round-trip tests passed!" << std::endl;
    return 0;
}
