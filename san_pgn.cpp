// san_pgn.cpp

#include "engine.h"
#include "search.h"
#include "san.h"
#include <sstream>
#include <vector>
#include <iostream>

std::vector<std::string> splitSANMoves(const std::string& pgn) {
    // Split PGN moves by whitespace, ignoring move numbers
    std::istringstream iss(pgn);
    std::string token;
    std::vector<std::string> moves;
    while (iss >> token) {
        // skip move numbers (e.g., 1., 2., etc.)
        if (token.find('.') != std::string::npos) continue;
        moves.push_back(token);
    }
    return moves;
}

std::vector<BoardData> replayPGN(const std::string& pgnText) {
    // Replay a PGN string and return a vector of the board states after each move
    BoardData board = getInitialBoard();
    std::vector<BoardData> history = {board};
    auto moves = splitSANMoves(pgnText);

    for (const auto& san : moves) {
        Move move = parseSAN(san, board);
        if (move.fromRow == -1) {
            std::cerr << "Invalid move: " << san << std::endl;
            break;
        }
        board = applyMove(board, move);
        history.push_back(board);
    }
    return history;
}
