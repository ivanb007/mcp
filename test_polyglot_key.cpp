
#include "openingbook.h"

#include <iostream>
#include <cassert>
#include <iomanip>
#include <sstream>
#include <string>
#include <cstdint>

// Polyglot hash random values (defined in external table)
extern uint64_t polyglotRandom[781]; // should be initialized with standard Polyglot values

// Helper to convert uint64_t to hex string
std::string toHex(uint64_t key) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << key;
    return oss.str();
}

int main() {
    // std::string fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";    // Key = 463b96181691fc9c
    // std::string fen = "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3";  // Key = 22a48b5a8e47ff78
    // std::string fen = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2";  // Key = 0756b94461c50fb0
    // std::string fen = "rnbqkbnr/p1pppppp/8/8/PpP4P/8/1P1PPPP1/RNBQKBNR b KQkq c3 0 3";  // Key = 3c8123ea7b067637
    std::string fen = "rnbqkbnr/p1pppppp/8/8/P6P/R1p5/1P1PPPP1/1NBQKBNR b Kkq - 0 4";  // Key = 5c3f9b829b279560

    std::istringstream iss(fen);
    std::string piecePlacement, activeColor, castling, ep;
    iss >> piecePlacement >> activeColor >> castling >> ep;
    std::cout << "piecePlacement " << piecePlacement << std::endl;
    std::cout << "activeColor " << activeColor << std::endl;
    std::cout << "castling " << castling << std::endl;
    std::cout << "ep " << ep << std::endl;

    // Process piece placement
    int row = 7, col = 0;
    for (char c : piecePlacement) {
        if (c == '/') { row--; col = 0; std::cout << std::endl; continue; }
        if (isdigit(c)) {
            col += c - '0';
            for (int i = 0; i < c - '0'; ++i) {
                std::cout << '.';
            }
        } else {
            int index = pieceIndex(c);
            int sq = squareIndex(row, col);
            if (index >= 0)
                std::cout << c;
            col++;
        }
    }
    std::cout << std::endl;

    uint64_t key = computePolyglotKeyFromFEN(fen);
    std::string expected = "5c3f9b829b279560"; // Expected key for the given FEN

    std::string keyHex = toHex(key);
    std::cout << "Computed key: 0x" << keyHex << std::endl;
    assert(keyHex == expected && "Polyglot key mismatch!");

    std::cout << "Polyglot key test passed." << std::endl;
    return 0;
}
