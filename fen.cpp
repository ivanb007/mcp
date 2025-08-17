// fen.cpp
// This file implements the FEN (Forsyth-Edwards Notation) functionality
// for the chess engine, converting board states to and from FEN strings.

#include "engine.h"
#include "fen.h"

#include <stdexcept>
#include <iostream>
#include <sstream>
#include <cctype>

//    A FEN string defines a particular position using only the ASCII character set.
//    A FEN string contains six fields separated by a space. The fields are:

//    1) Piece placement (from white's perspective). Each rank is described, starting
//       with rank 8 and ending with rank 1. Within each rank, the contents of each
//       square are described from file A through file H. Following the Standard
//       Algebraic Notation (SAN), each piece is identified by a single letter taken
//       from the standard English names. White pieces are designated using upper-case
//       letters ("PNBRQK") whilst Black uses lowercase ("pnbrqk"). Blank squares are
//       noted using digits 1 through 8 (the number of blank squares), and "/"
//       separates ranks.

//    2) Active color. "w" means white moves next, "b" means black.

//    3) Castling availability. If neither side can castle, this is "-". Otherwise,
//       this has one or more letters: "K" (White can castle kingside), "Q" (White
//       can castle queenside), "k" (Black can castle kingside), and/or "q" (Black
//       can castle queenside).

//    4) En passant target square (in algebraic notation). If there's no en passant
//       target square, this is "-". If a pawn has just made a 2-square move, this
//       is the position "behind" the pawn. Following X-FEN standard, this is recorded only
//       if there is a pawn in position to make an en passant capture, and if there really
//       is a pawn that might have advanced two squares.

//    5) Halfmove clock. This is the number of halfmoves since the last pawn advance
//       or capture. This is used to determine if a draw can be claimed under the fifty-move rule.

//    6) Fullmove number. The number of the full move. 
//       It starts at 1, and is incremented after Black's move.

std::string boardToFEN(const BoardData& board) {
    std::ostringstream oss;

    // Convert the board pieces to FEN format Piece Placement
    for (int r = 0; r < 8; ++r) {
        int empty = 0;
        for (int c = 0; c < 8; ++c) {
            char p = board.pieces[r * 8 + c];
            if (p == '.') {
                empty++;
            } else {
                if (empty > 0) {
                    oss << empty;
                    empty = 0;
                }
                oss << p;
            }
        }
        if (empty > 0) oss << empty;
        if (r < 7) oss << '/';
    }

    oss << ' ' << (board.whiteToMove ? 'w' : 'b') << ' ';

    std::string castling;
    if (board.canCastleK) castling += 'K';
    if (board.canCastleQ) castling += 'Q';
    if (board.canCastlek) castling += 'k';
    if (board.canCastleq) castling += 'q';
    oss << (castling.empty() ? "-" : castling) << ' ';

    if (board.enPassantTarget != -1) {
        int idx = board.enPassantTarget;
        char file = 'a' + (idx % 8);
        char rank = '8' - (idx / 8);
        oss << file << rank;
    } else {
        oss << '-';
    }

    oss << ' ' << board.halfmoveClock << ' ' << board.fullmoveNumber; // Halfmove and fullmove
    return oss.str();
}

BoardData loadFEN(const std::string& fen) {
    // Load a FEN string and return the corresponding BoardData
    BoardData board = {};
    std::istringstream iss(fen);
    std::string part;
    int idx = 0;

    if (!(iss >> part)) throw std::invalid_argument("Invalid FEN: missing board");
    for (char c : part) {
        if (c == '/') continue;
        // If we hit a digit, it means we need to place that many empty squares
        else if (isdigit(c)) {
            int empty = c - '0';
            for (int i = 0; i < empty; ++i) {
                if (idx < 64)
                    board.pieces[idx++] = '.';
            }
        } else {
            if (idx < 64)
                board.pieces[idx++] = c;
            else
                throw std::invalid_argument("Invalid FEN: board overflow");
        }
    }

    if (idx != 64) throw std::invalid_argument("Invalid FEN: board underflow");

    if (!(iss >> part)) throw std::invalid_argument("Invalid FEN: missing color");
    board.whiteToMove = (part == "w");

    if (!(iss >> part)) throw std::invalid_argument("Invalid FEN: missing castling");
    board.canCastleK = part.find('K') != std::string::npos;
    board.canCastleQ = part.find('Q') != std::string::npos;
    board.canCastlek = part.find('k') != std::string::npos;
    board.canCastleq = part.find('q') != std::string::npos;

    if (!(iss >> part)) throw std::invalid_argument("Invalid FEN: missing en passant");
    if (part != "-") {
        char file = part[0];
        char rank = part[1];
        board.enPassantTarget = (8 - (rank - '0')) * 8 + (file - 'a');
    } else {
        board.enPassantTarget = -1;
    }

    if (!(iss >> part)) throw std::invalid_argument("Invalid FEN: missing halfmove");
    board.halfmoveClock = std::stoi(part);

    if (!(iss >> part)) throw std::invalid_argument("Invalid FEN: missing fullmove");
    board.fullmoveNumber = std::stoi(part);

    return board;
}

void printFENBoard(const std::string& fen) {
    std::istringstream iss(fen);
    std::string boardPart;

    if (!(iss >> boardPart)) {
        std::cerr << "Invalid FEN input." << std::endl;
        return;
    }

    std::cout << "  +-----------------+" << std::endl;
    int row = 8;
    std::cout << row << " | ";
    for (char c : boardPart) {
        if (c == '/') {
            row--;
            std::cout << "|" << std::endl << row << " | ";
        } else if (isdigit(c)) {
            for (int i = 0; i < c - '0'; ++i)
                std::cout << ". ";
        } else {
            std::cout << c << " ";
        }
    }
    std::cout << "|" << std::endl;
    std::cout << "  +-----------------+" << std::endl;
    std::cout << "    a b c d e f g h" << std::endl;
}