// san.cpp
// This file implements the SAN (Standard Algebraic Notation) functionality
// for the chess engine, including parsing and generating moves in SAN format.

#include "san.h"
#include "search.h"  // for 

#include <cctype>
#include <algorithm>
#include <iostream>

bool samePiece(char a, char b) {
    return tolower(a) == tolower(b);
}

std::string sanFromMove(const Move& move, const BoardData& board) {
    // std::cout << "Generating SAN for move: " << move.fromRow << "," << move.fromCol << " to " << move.toRow << "," << move.toCol << std::endl;
    // std::cout << "SQUARE: " << SQUARE(move.fromRow, move.fromCol) << std::endl;
    char piece = board.pieces[SQUARE(move.fromRow, move.fromCol)];
    std::string san;
    int side;
    if (board.whiteToMove) side = WHITE;
    else side = BLACK;
    int oside;
    if (board.whiteToMove) oside = BLACK;
    else side = WHITE;

    if (move.isCastling) {
        if (move.toCol == 6) return "O-O";
        else if (move.toCol == 2) return "O-O-O";
        else throw std::invalid_argument("Illegal castling move attempted");
    }

    bool isCapture = board.pieces[SQUARE(move.toRow, move.toCol)] != '.';
    bool isPawn = tolower(piece) == 'p';

    if (move.isEnPassant) {
        isCapture = true;
    }

    // If moving piece is not a pawn then san begins with the upper case piece
    if (!isPawn) san += toupper(piece);

    // When two or more identical pieces can move to the same square, 
    // the following code distinguishes them using:
    // - File-only disambiguation: if the pieces are on different files
    // - Rank-only disambiguation: if the pieces are on different ranks
    // - File+Rank disambiguation: if the pieces are on the same file and rank
    int samePieceCount = 0;
    int from = SQUARE(move.fromRow, move.fromCol);
    int to = SQUARE(move.toRow, move.toCol);
    bool sameFile = false, sameRank = false;
    auto moves = generateMoves(board);
    for (const auto& m : moves) {
        int mfrom = SQUARE(m.fromRow, m.fromCol);
        int mto = SQUARE(m.toRow, m.toCol);
        if (to == mto && from != mfrom) {
            // std::cout << "Found ambiguous move from: " << mfrom << " to " << mto << std::endl;
            char other = board.pieces[mfrom];
            if (samePiece(other, piece)) {
                samePieceCount++;
                if (m.fromCol == move.fromCol) sameFile = true;
                if (m.fromRow == move.fromRow) sameRank = true;
            }
        }
    }

    if (samePieceCount > 0) {
        if (!sameFile) san += ('a' + move.fromCol);
        else if (!sameRank) san += ('8' - move.fromRow);
        else {
            san += ('a' + move.fromCol);
            san += ('8' - move.fromRow);
        }
    }

    if (isCapture) {
        if (isPawn) san += ('a' + move.fromCol);
        san += 'x';
    }

    san += ('a' + move.toCol);
    san += ('8' - move.toRow);

    if (move.promotion) {
        san += '=';
        san += toupper(move.promotion);
    }

    // Apply the move and see if the other side is now in check or even checkmate
    BoardData newBoard = applyMove(board, move);
    if (inCheck(newBoard, oside)) {
        if (isCheckMate(newBoard, move)) {
            san += '#';
        } else san += '+';
    }
    return san;
}

Move parseSAN(const std::string& san, const BoardData& board) {
    // Matches SAN input against all legal moves in the current position
    if (san.empty()) return {-1, -1, -1, -1}; // Invalid move
    // Remove any trailing check or mate symbols
    std::string cleaned = san;
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '+'), cleaned.end());
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '#'), cleaned.end());

    auto legalMoves = generateMoves(board);
    for (const auto& move : legalMoves) {
        std::string testSan = sanFromMove(move, board);
        if (testSan == cleaned)
            return move;
    }

    return {-1, -1, -1, -1}; // invalid move fallback
}