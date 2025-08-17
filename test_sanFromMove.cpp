
#include "engine.h"
#include "fen.h"
#include "search.h"
#include "san.h"
#include <cassert>
#include <iostream>

void testCheckMateNotation() {
    bool testPassed = false;
    // This FEN represents a position where White can give checkmate with Qxf7#
    std::string fen = "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4";
    // printFENBoard(fen);
    BoardData board = loadFEN(fen);
    auto moves = generateMoves(board);
    for (const auto& m : moves) {
        auto san = sanFromMove(m, board);
        if (san.find('#') != std::string::npos) {
            std::cout << "Check Mate SAN found: " << san << std::endl;
            assert(san == "Qxf7#"); // Check if the SAN matches the expected checkmate notation
            testPassed = true;
            break;
        }
    }
    if (!testPassed) {
        std::cerr << "Check Mate SAN Test failed: No checkmate move found." << std::endl;
    } else {
        std::cout << "Check Mate SAN Test passed!" << std::endl;
    }
}

void testCheckNotation() {
    bool testPassed = false;
    std::string fen = "rnbqkbnr/ppp2ppp/8/3pp3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 4";
    // printFENBoard(fen);
    // This FEN represents a position where Black is in check after the move Bb5
    BoardData board = loadFEN(fen);
    auto moves = generateMoves(board);
    for (const auto& m : moves) {
        auto san = sanFromMove(m, board);
        // std::cout << "SAN: " << san << std::endl;
        if (san.find('+') != std::string::npos) {
            std::cout << "Check SAN found: " << san << std::endl;
            assert(san == "Bb5+"); // Check if the SAN matches the expected checkmate notation
            testPassed = true;
            break;
        }
    }
    if (!testPassed) {
        std::cerr << "Check SAN Test failed: No check move found." << std::endl;
    } else {
        std::cout << "Check SAN Test passed!" << std::endl;
    }
}

void testEnPassant() {
    bool testPassed = false;
    std::string fen = "rnbqkbnr/ppp1pp1p/6p1/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3";
    // printFENBoard(fen);
    // This FEN represents a position where White can capture the pawn on d6 en passant
    BoardData board = loadFEN(fen);
    auto moves = generateMoves(board);
    for (const auto& m : moves) {
        auto san = sanFromMove(m, board);
        // std::cout << "UCI: " << moveToUci(m) << std::endl;
        // std::cout << "SAN: " << san << std::endl;
        if (m.isEnPassant) {
            std::cout << "En passant SAN: " << san << std::endl;
            assert(san.find('x') != std::string::npos);
            testPassed = true;
        }
    }
    if (!testPassed) {
        std::cerr << "En passant SAN Test failed: No en passant move found." << std::endl;
    } else {
        std::cout << "En passant SAN Test passed!" << std::endl;
    }
}

void testDisambiguation() {
    bool testPassed = false;
    std::string fen = "r1bqkb1r/pppppp1p/2n2np1/8/3N4/2N5/PPPPPPPP/R1BQKB1R w KQkq - 0 4";
    // printFENBoard(fen);
    // This FEN represents a position where two knights can move to the same square (b5).
    // The knights are on c3 and d4 but both can move to b5 so the SAN must be disambiguated.
    BoardData board = loadFEN(fen);
    auto moves = generateMoves(board);
    for (const auto& m : moves) {
        // std::cout << "UCI: " << moveToUci(m) << std::endl;
        if (SQUARE(m.toRow, m.toCol) == 25) { // b5 square
            std::string san = sanFromMove(m, board);
            std::cout << "Disambiguated SAN: " << san << std::endl;
            assert(san.find('N') != std::string::npos);
            testPassed = true;
        }
    }
    if (!testPassed) {
        std::cerr << "Disambiguation SAN Test failed: No en passant move found." << std::endl;
    } else {
        std::cout << "Disambiguation SAN Test passed!" << std::endl;
    }
}

void testPromotion() {
    bool testPassed = false;
    std::string fen = "7k/P7/8/8/8/8/7p/7K w - - 0 1";
    // printFENBoard(fen);
    // This FEN represents a position where White can promote a pawn on the 7th rank.
    // The pawn is on a7, and can promote to a queen on a8.
    BoardData board = loadFEN(fen);
    auto moves = generateMoves(board);
    for (const auto& m : moves) {
        // std::cout << "UCI: " << moveToUci(m) << std::endl;
        if (m.promotion == 'q' || m.promotion == 'Q') {
            std::string san = sanFromMove(m, board);
            std::cout << "Promotion SAN: " << san << std::endl;
            assert(san.find('=') != std::string::npos);
            testPassed = true;
        }
    }
    if (!testPassed) {
        std::cerr << "Promotion SAN Test failed: No promotion move found." << std::endl;
    } else {
        std::cout << "Promotion SAN Test passed!" << std::endl;
    }
}

int main() {
    testCheckMateNotation();
    testCheckNotation();
    testEnPassant();
    testDisambiguation();
    testPromotion();
    std::cout << "All SAN tests finished!" << std::endl;
    return 0;
}
