// search.cpp

// This file includes:
// - Move generation and evaluation functions
// - Alpha-beta pruning with quiescence search
// - Parallel search using a thread pool
// - Killer move and history heuristic optimizations
// - Depth/time-limited search using std::chrono and std::atomic

#include "search.h"
#include "threadpool.h"
#include "engine.h"

#include <limits>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <iostream>

// Offsets for knight moves (relative index changes on the mailbox64 array)
const int knightOffsets[8] = { -21, -19, -12, -8, 8, 12, 19, 21 };

// Offsets for bishop moves (diagonals)
const int bishopOffsets[4] = { -11, -9, 9, 11 };

// Offsets for rook moves (straight lines)
const int rookOffsets[4] = { -10, -1, 1, 10 };

// Offsets for queen moves (all directions)
const int queenOffsets[8] = { -11, -10, -9, -1, 1, 9, 10, 11 };

// Offsets for king moves (all directions)
const int kingOffsets[8] = { -11, -10, -9, -1, 1, 9, 10, 11 };

const int INF = std::numeric_limits<int>::max();
const int MAX_DEPTH = 64;

std::array<std::vector<Move>, MAX_DEPTH> killerMoves;
std::unordered_map<uint64_t, int> historyHeuristic;

bool isSameMove(const Move& a, const Move& b) {
    return a.fromRow == b.fromRow && a.fromCol == b.fromCol &&
           a.toRow == b.toRow && a.toCol == b.toCol;
}

// void sortMoves(std::vector<Move>& moves, const BoardData& board, int depth) {
//     Zobrist zobrist;
//     std::sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
//         for (const auto& km : killerMoves[depth]) {
//             if (isSameMove(a, km)) return true;
//             if (isSameMove(b, km)) return false;
//         }
//         uint64_t hashA = zobrist.computeHash(applyMove(board, a));
//         uint64_t hashB = zobrist.computeHash(applyMove(board, b));
//         return historyHeuristic[hashA] > historyHeuristic[hashB];
//     });
// }

int evaluate(const BoardData& state) {
    if (state.halfmoveClock >= 100)
        return 0; // Draw evaluation due to 50-move rule

    int score = 0;
    for (int i = 0; i < 64; ++i) {
        char p = state.pieces[i];
        if (p == '.') continue;
        int val;
        switch (tolower(p)) {
            case 'p': val = 10; break;
            case 'n': case 'b': val = 30; break;
            case 'r': val = 50; break;
            case 'q': val = 90; break;
            case 'k': val = 900; break;
            default: val = 0;
        }
        score += isupper(p) ? val : -val;
    }
    return score;
}

bool isCheckMate(const BoardData& board, const Move& move) {
    // Returns true only if the given move has check mated the side to move on board
    int sideToMove = board.whiteToMove ? WHITE : BLACK;
    if (inCheck(board, sideToMove)) {
        // If the side to move is in check is there any legal escape move?
        std::vector<Move> Moves;
        // generate all pseudo-legal moves
        Moves = generateMoves(board);
        for (const auto& m : Moves) {     
            // Is it legal?      
            if (isLegalMove(board, m)) {
                return false; // Found a legal escape move, not checkmate
            }
        }
        return true; // No escape moves, checkmate
    }
    return false; // Not in check, not checkmate
}

bool isLegalMove(const BoardData& board, const Move& move) {
    // Returns true only if the given move on the given board is legal
    int side = board.whiteToMove ? WHITE : BLACK;
    // If castling, do further checks for legality
    if (move.isCastling) {
        if (side == WHITE) {
            if ((move.toCol == 6 && !board.canCastleK) || (move.toCol == 2 && !board.canCastleQ)) {
                return false; // Cannot castle if rights are not available
            }
            // Check if the squares between the king and rook are empty and not attacked by enemy side
            if (move.toCol == 6) { // If White is castling kingside
                if (board.PieceColor(F1) != EMPTY || board.PieceColor(G1) != EMPTY) {
                    return false; // Cannot castle if squares are not empty
                }
                if (attacked(board, F1, BLACK) || attacked(board, G1, BLACK)) {
                    return false; // Cannot castle if squares are attacked
                }
            } else if (move.toCol == 2) { // If White is castling queenside
                if (board.PieceColor(D1) != EMPTY || board.PieceColor(C1) != EMPTY || board.PieceColor(B1) != EMPTY) {
                    return false; // Cannot castle if squares are not empty
                }
                if (attacked(board, D1, BLACK) || attacked(board, C1, BLACK) || attacked(board, B1, BLACK)) {
                    return false; // Cannot castle if squares are attacked
                }
            }
        } else { // If Black is castling
            if ((move.toCol == 6 && !board.canCastlek) || (move.toCol == 2 && !board.canCastleq)) {
                return false; // Cannot castle if rights are not available
            }
            // Check if the squares between the king and rook are empty and not attacked by enemy side
            if (move.toCol == 6) { // If Black is castling kingside
                if (board.PieceColor(F8) != EMPTY || board.PieceColor(G8) != EMPTY) {
                    return false; // Cannot castle if squares are not empty
                }
                if (attacked(board, F8, WHITE) || attacked(board, G8, WHITE)) {
                    return false; // Cannot castle if squares are attacked
                }
            } else if (move.toCol == 2) { // If Black is castling queenside
                if (board.PieceColor(D8) != EMPTY || board.PieceColor(C8) != EMPTY || board.PieceColor(B8) != EMPTY) {
                    return false; // Cannot castle if squares are not empty
                }
                if (attacked(board, D8, WHITE) || attacked(board, C8, WHITE) || attacked(board, B8, WHITE)) {
                    return false; // Cannot castle if squares are attacked
                }
            }
        }
    }
    // Check en passant captures
    if (move.isEnPassant) {
        if ((board.PieceColor(SQUARE(move.toRow, move.toCol)) != EMPTY) || (board.enPassantTarget == '\0'))
            return false;
    }
    // Make the move and see if our side's king is still in check
    BoardData newBoard = applyMove(board, move);
    if (inCheck(newBoard, side)) return false;
    
    return true; // The move is legal
}

bool inCheck(const BoardData& board, int side) {
    // Returns true only if the colour side is in check
    int oside;
    if (side == WHITE) { oside = BLACK; }
    else { oside = WHITE; }
    for (int i = 0; i < 64; ++i) {
        if (board.PieceType(i) == KING && board.PieceColor(i) == side) {
            return attacked(board, i, oside);
        }
    }
    return true; // If no king found, assume in check
}

bool attacked(const BoardData& board, int sq, int side) {
    // Returns true only if the square sq is attacked by at least one piece of colour side
    int i, j, n;

    for (i = 0; i < 64; ++i) {
        if (board.PieceColor(i) == side) {
            // If the colour of the piece on square i matches the side to move
            if (board.PieceType(i) == PAWN) {
                if (side == WHITE) { // Generate White pawn captures
                    if (COL(i) != 0 && i - 9 == sq) {
                        return true;
                    }
                    if (COL(i) != 7 && i - 7 == sq) {
                        return true;
                    }
                } else { // Generate Black pawn captures
                    if (COL(i) != 0 && i + 7 == sq) {
                        return true;
                    }
                    if (COL(i) != 7 && i + 9 == sq) {
                        return true;
                    }
                } 
            } else { // Generate attacks for other piece types
                switch (board.PieceType(i)) {
                    case KNIGHT: // Knight moves
                        for (j = 0; j < 8; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + knightOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (n == sq) return true; // Found an attack on the target square
                                if (board.PieceColor(n) != EMPTY) break; // Stop at enemy piece 
                                // This piece does not slide (e.g. king or knight), so stop after one move in any direction
                                break;
                            }
                        }
                        break;
                    case BISHOP: // Bishop moves
                        for (j = 0; j < 4; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + bishopOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (n == sq) return true; // Found an attack on the target square
                                if (board.PieceColor(n) != EMPTY) break; // Stop at enemy piece
                            }
                        }
                        break;
                    case ROOK: // Rook moves
                        for (j = 0; j < 4; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + rookOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (n == sq) return true; // Found an attack on the target square
                                if (board.PieceColor(n) != EMPTY) break; // Stop at enemy piece
                            }
                        }
                        break;
                    case QUEEN: // Queen moves
                        for (j = 0; j < 8; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + queenOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (n == sq) return true; // Found an attack on the target square
                                if (board.PieceColor(n) != EMPTY) break; // Stop at enemy piece
                            }
                        }
                        break;
                    case KING: // King moves
                        for (j = 0; j < 8; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + kingOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (n == sq) return true; // Found an attack on the target square
                                if (board.PieceColor(n) != EMPTY) break; // Stop at enemy piece
                                // This piece does not slide (e.g. king or knight), so stop after one move in any direction
                                break;
                            }
                        }
                        break;
                    default:
                    // Invalid piece type, do nothing
                    break;   
                }
            }
        }
    }
    return false; // No attack found
}

bool pawn_attack(const BoardData& board, int sq, int side) {
    // Returns true only if the square sq is attacked by at least one pawn of colour side
    int i;

    for (i = 0; i < 64; ++i) {
        if (board.PieceColor(i) == side) {
            // If the colour of the piece on square i matches the side to move
            if (board.PieceType(i) == PAWN) {
                if (side == WHITE) { // Generate White pawn captures
                    if (COL(i) != 0 && i - 9 == sq) {
                        return true;
                    }
                    if (COL(i) != 7 && i - 7 == sq) {
                        return true;
                    }
                } else { // Generate Black pawn captures
                    if (COL(i) != 0 && i + 7 == sq) {
                        return true;
                    }
                    if (COL(i) != 7 && i + 9 == sq) {
                        return true;
                    }
                } 
            } 
        }
    }
    return false; // No attack found
}

// Generate all legal moves for the current board state
// Returns a vector of Move objects containing all possible moves
std::vector<Move> generateMoves(const BoardData& board) {
    std::vector<Move> Moves;
    int i, j, n;
    int side, xside;
    int score = 0;
    char p;

    if (board.whiteToMove) { side = WHITE; xside = BLACK; }
    else { side = BLACK; xside = WHITE; }
    
    for (i = 0; i < 64; ++i) {
        if (board.PieceColor(i) == side) {
            // If the colour of the piece on square i matches the side to move
            if (board.PieceType(i) == PAWN) {
                // Generate pawn moves
                if (side == WHITE) {
                    // White pawns move up the board in steps of -8 (or -16 if they are still on their starting rank)
                    // White pawns start on rank 2 (row 6) squares 48 to 55
                    if (board.PieceType(i - 8) == EMPTY) {
                        if (ROW(i - 8) == 0) {
                            // White pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                Moves.push_back({ROW(i), COL(i), ROW(i - 8), COL(i - 8), false, false, TYPEtoCHAR(k), (1000000 + (k * 10))});
                            }
                        } else {
                            Moves.push_back({ROW(i), COL(i), ROW(i - 8), COL(i - 8)}); // Pawn move one square forward
                        }
                        if (i >= 48 && board.PieceType(i - 16) == EMPTY) {
                            Moves.push_back({ROW(i), COL(i), ROW(i - 16), COL(i - 16)}); // Pawn move two squares forward
                        }
                    }
                    if (COL(i) != 0 && board.PieceType(i - 9) == BLACK) {
                        if (ROW(i - 9) == 0) {
                            // White pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                Moves.push_back({ROW(i), COL(i), ROW(i - 9), COL(i - 9), false, false, TYPEtoCHAR(k), (1000000 + (k * 10))});
                            }
                        } else {
                            // If the move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i - 9) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i - 9), COL(i - 9), false, false, '\0', score});
                        }
                    }
                    if (COL(i) != 7 && board.PieceType(i - 7) == BLACK) {
                        if (ROW(i - 7) == 0) {
                            // White pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                Moves.push_back({ROW(i), COL(i), ROW(i - 7), COL(i - 7), false, false, TYPEtoCHAR(k), (1000000 + (k * 10))});
                            }
                        } else {
                            // If the move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i - 7) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i - 7), COL(i - 7), false, false, '\0', score});
                        }
                    }
                } else { // Black pawns move down the board in steps of +8 (or +16 if they are still on their starting rank)
                    // Black pawns start on rank 7 (row 1) squares 8 to 15
                    if (board.PieceType(i + 8) == EMPTY) {
                        if (ROW(i + 8) == 7) {
                            // Black pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                Moves.push_back({ROW(i), COL(i), ROW(i + 8), COL(i + 8), false, false, TYPEtoCHAR(k), (1000000 + (k * 10))});
                            }
                        } else {
                            Moves.push_back({ROW(i), COL(i), ROW(i + 8), COL(i + 8)}); // Pawn move one square forward
                        }
                        if (i < 16 && board.PieceType(i + 16) == EMPTY) {
                            Moves.push_back({ROW(i), COL(i), ROW(i + 16), COL(i + 16)}); // Pawn move two squares forward
                        }
                    }
                    if (COL(i) != 0 && board.PieceType(i + 7)) {
                        if (ROW(i + 7) == 7) {
                            // Black pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                Moves.push_back({ROW(i), COL(i), ROW(i + 7), COL(i + 7), false, false, TYPEtoCHAR(k), (1000000 + (k * 10))});
                            }
                        } else {
                            // If the move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i + 7) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i + 7), COL(i + 7), false, false, '\0', score});
                        }
                    }
                    if (COL(i) != 7 && board.PieceType(i + 9)) {
                        if (ROW(i + 9) == 7) {
                            // Black pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                Moves.push_back({ROW(i), COL(i), ROW(i + 9), COL(i + 9), false, false, TYPEtoCHAR(k), (1000000 + (k * 10))});
                            }
                        } else {
                            // If the move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i + 9) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i + 9), COL(i + 9), false, false, '\0', score});
                        }
                    }
                }
            } else {// Generate moves for other piece types
                switch (board.PieceType(i)) {
                    case KNIGHT: // Knight moves
                        for (j = 0; j < 8; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + knightOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (board.PieceColor(n) != EMPTY) {
                                    if (board.PieceColor(n) == xside) {
                                        // Move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                                        score = (1000000 + (board.PieceType(n) * 10) - board.PieceType(i));
                                        Moves.push_back({ROW(i), COL(i), ROW(n), COL(n), false, false, '\0', score});
                                    }
                                    break; // Stop at enemy piece
                                }    
                                Moves.push_back({ROW(i), COL(i), ROW(n), COL(n)}); // Add quiet move to empty square
                                // This piece does not slide (e.g. king or knight), so stop after one move in any direction
                                break;
                            }
                        }
                        break;
                    case BISHOP: // Bishop moves
                        for (j = 0; j < 4; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + bishopOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (board.PieceColor(n) != EMPTY) {
                                    if (board.PieceColor(n) == xside) {
                                        // Move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                                        score = (1000000 + (board.PieceType(n) * 10) - board.PieceType(i));
                                        Moves.push_back({ROW(i), COL(i), ROW(n), COL(n), false, false, '\0', score});
                                    }
                                    break; // Stop at enemy piece
                                }    
                                Moves.push_back({ROW(i), COL(i), ROW(n), COL(n)}); // Add quiet move to empty square
                            }
                        }
                        break;
                    case ROOK: // Rook moves
                        for (j = 0; j < 4; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + rookOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (board.PieceColor(n) != EMPTY) {
                                    if (board.PieceColor(n) == xside) {
                                        // Move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                                        score = (1000000 + (board.PieceType(n) * 10) - board.PieceType(i));
                                        Moves.push_back({ROW(i), COL(i), ROW(n), COL(n), false, false, '\0', score});
                                    }
                                    break; // Stop at enemy piece
                                }    
                                Moves.push_back({ROW(i), COL(i), ROW(n), COL(n)}); // Add quiet move to empty square
                            }
                        }
                        break;
                    case QUEEN: // Queen moves
                        for (j = 0; j < 8; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + queenOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (board.PieceColor(n) != EMPTY) {
                                    if (board.PieceColor(n) == xside) {
                                        // Move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                                        score = (1000000 + (board.PieceType(n) * 10) - board.PieceType(i));
                                        Moves.push_back({ROW(i), COL(i), ROW(n), COL(n), false, false, '\0', score});
                                    }
                                    break; // Stop at enemy piece
                                }    
                                Moves.push_back({ROW(i), COL(i), ROW(n), COL(n)}); // Add quiet move to empty square
                            }
                        }
                        break;
                    case KING: // King moves
                        for (j = 0; j < 8; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + kingOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (board.PieceColor(n) != EMPTY) {
                                    if (board.PieceColor(n) == xside) {
                                        // Move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                                        score = (1000000 + (board.PieceType(n) * 10) - board.PieceType(i));
                                        Moves.push_back({ROW(i), COL(i), ROW(n), COL(n), false, false, '\0', score});
                                    }
                                    break; // Stop at enemy piece
                                }    
                                Moves.push_back({ROW(i), COL(i), ROW(n), COL(n)}); // Add quiet move to empty square
                                // This piece does not slide (e.g. king or knight), so stop after one move in any direction
                                break;
                            }
                        }
                        break;
                    default:
                    // Invalid piece type, do nothing
                    break;   
                }
            }
        }
    }

    // Generate castling moves
    if (board.canCastleK && board.whiteToMove) {
        // White can castle kingside
        if (board.pieces[F1] == '.' && board.pieces[G1] == '.' && 
            board.pieces[E1] == 'K' && board.pieces[H1] == 'R') {
                // Confirm that none of the squares between the king and rook are attacked
                Moves.push_back({7, 4, 7, 6, false, true}); // Kingside castling
        }
    }
    if (board.canCastleQ && board.whiteToMove) {   
        // White can castle queenside
        if (board.pieces[59] == '.' && board.pieces[58] == '.' && 
            board.pieces[57] == '.' && board.pieces[60] == 'K' && 
            board.pieces[56] == 'R') {
            Moves.push_back({7, 4, 7, 2, false, true}); // Queenside castling
        }
    }
    if (board.canCastlek && !board.whiteToMove) {
        // Black can castle kingside
        if (board.pieces[5] == '.' && board.pieces[6] == '.' && 
            board.pieces[4] == 'k' && board.pieces[7] == 'r') {
            Moves.push_back({0, 4, 0, 6, false, true}); // Kingside castling
        }
    }
    if (board.canCastleq && !board.whiteToMove) {
        // Black can castle queenside
        if (board.pieces[3] == '.' && board.pieces[2] == '.' && 
            board.pieces[1] == '.' && board.pieces[4] == 'k' && 
            board.pieces[0] == 'r') {
            Moves.push_back({0, 4, 0, 2, false, true}); // Queenside castling
        }
    }

    // Generate en passant captures
    if (board.enPassantTarget != -1) {
        // std::cout << "generateMoves found EP target " << board.enPassantTarget << std::endl;
        // En passant captures are always pawn takes pawn so all have the same score
        score = 1000000 + (PAWN * 10) - PAWN; // true, false, '\0', score
        int targetRow = ROW(board.enPassantTarget);
        int targetCol = COL(board.enPassantTarget);
        // std::cout << "generateMoves board.whiteToMove " << board.whiteToMove << " targetRow " << targetRow << std::endl;
        if (board.whiteToMove && targetRow == 2) {
            // White can capture en passant if it has a pawn in the right place
            // std::cout << "generateMoves board.pieces[board.enPassantTarget + 7] " << board.pieces[board.enPassantTarget + 7] << std::endl;
            if (targetCol != 0 && board.pieces[board.enPassantTarget + 7] == 'P') {
                // std::cout << "generateMoves found EP capture pawn for White " << board.enPassantTarget + 7 << std::endl;
                Moves.push_back({targetRow + 1, targetCol - 1, targetRow, targetCol, true, false, '\0', score}); // Capture using pawn on left
            }
            // std::cout << "generateMoves board.pieces[board.enPassantTarget + 9] " << board.pieces[board.enPassantTarget + 9] << std::endl;
            if (targetCol != 7 && board.pieces[board.enPassantTarget + 9] == 'P') {
                // std::cout << "generateMoves found EP capture pawn for White " << board.enPassantTarget + 9 << std::endl;
                Moves.push_back({targetRow + 1, targetCol + 1, targetRow, targetCol, true, false, '\0', score}); // Capture using pawn on right
            }
        } else if (board.whiteToMove == false && targetRow == 5) {
            // Black can capture en passant
            if (targetCol != 0 && board.pieces[board.enPassantTarget - 9] == 'p') {
                // std::cout << "generateMoves found EP capture pawn for Black " << board.enPassantTarget - 9 << std::endl;
                Moves.push_back({targetRow - 1, targetCol - 1, targetRow, targetCol, true, false, '\0', score}); // Capture using pawn on left
            }
            if (targetCol != 7 && board.pieces[board.enPassantTarget - 7] == 'p') {
                // std::cout << "generateMoves found EP capture pawn for Black " << board.enPassantTarget - 7 << std::endl;
                Moves.push_back({targetRow - 1, targetCol + 1, targetRow, targetCol, true, false, '\0', score}); // Capture using pawn on right
            }
        }
    }
    // Return the list of generated moves
    return Moves;
}

// Generate only capture and promote moves for the current position.
// This function is used by the quiescence search.
std::vector<Move> generateCaptures(const BoardData& board) {
    std::vector<Move> Moves;
    int i, j, n;
    int side, xside;
    int score = 0;
    char p;

    if (board.whiteToMove) { side = WHITE; xside = BLACK; }
    else { side = BLACK; xside = WHITE; }
    
    for (i = 0; i < 64; ++i) {
        if (board.PieceColor(i) == side) {
            // If the colour of the piece on square i matches the side to move
            if (board.PieceType(i) == PAWN) {
                // Generate pawn moves
                if (side == WHITE) {
                    // White pawns move up the board in steps of -8 (or -16 if they are still on their starting rank)
                    // White pawns start on rank 2 (row 6) squares 48 to 55
                    if (COL(i) != 0 && board.PieceType(i - 9) == BLACK) {
                        if (ROW(i - 9) == 0) {
                            // White pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // The promotion is a capture so provide the promotion piece and a high score for promotion moves
                                Moves.push_back({ROW(i), COL(i), ROW(i - 9), COL(i - 9), false, false, TYPEtoCHAR(k), (1000000 + (k * 10))});
                            }
                        } else {
                            // The move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i - 9) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i - 9), COL(i - 9), false, false, '\0', score});
                        }
                    }
                    if (COL(i) != 7 && board.PieceType(i - 7) == BLACK) {
                        if (ROW(i - 7) == 0) {
                            // White pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // The promotion is a capture so provide the promotion piece and a high score for promotion moves
                                Moves.push_back({ROW(i), COL(i), ROW(i - 7), COL(i - 7), false, false, TYPEtoCHAR(k), (1000000 + (k * 10))});
                            }
                        } else {
                            // If the move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i - 7) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i - 7), COL(i - 7), false, false, '\0', score});
                        }
                    }
                } else { // Black pawns move down the board in steps of +8 (or +16 if they are still on their starting rank)
                    // Black pawns start on rank 7 (row 1) squares 8 to 15
                    if (COL(i) != 0 && board.PieceType(i + 7)) {
                        if (ROW(i + 7) == 7) {
                            // Black pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // The promotion is a capture so provide the promotion piece and a high score for promotion moves
                                Moves.push_back({ROW(i), COL(i), ROW(i + 7), COL(i + 7), false, false, TYPEtoCHAR(k), (1000000 + (k * 10))});
                            }
                        } else {
                            // If the move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i + 7) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i + 7), COL(i + 7), false, false, '\0', score});
                        }
                    }
                    if (COL(i) != 7 && board.PieceType(i + 9)) {
                        if (ROW(i + 9) == 7) {
                            // Black pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                Moves.push_back({ROW(i), COL(i), ROW(i + 9), COL(i + 9), false, false, TYPEtoCHAR(k), (1000000 + (k * 10))});
                            }
                        } else {
                            // If the move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i + 9) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i + 9), COL(i + 9), false, false, '\0', score});
                        }
                    }
                }
            } else {// Generate moves for other piece types
                switch (board.PieceType(i)) {
                    case KNIGHT: // Knight moves
                        for (j = 0; j < 8; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + knightOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (board.PieceColor(n) != EMPTY) {
                                    if (board.PieceColor(n) == xside) {
                                        // Move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                                        score = (1000000 + (board.PieceType(n) * 10) - board.PieceType(i));
                                        Moves.push_back({ROW(i), COL(i), ROW(n), COL(n), false, false, '\0', score});
                                    }
                                    break; // Stop at enemy piece
                                }
                                // This piece does not slide (e.g. king or knight), so stop after one move in any direction
                                break;
                            }
                        }
                        break;
                    case BISHOP: // Bishop moves
                        for (j = 0; j < 4; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + bishopOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (board.PieceColor(n) != EMPTY) {
                                    if (board.PieceColor(n) == xside) {
                                        // Move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                                        score = (1000000 + (board.PieceType(n) * 10) - board.PieceType(i));
                                        Moves.push_back({ROW(i), COL(i), ROW(n), COL(n), false, false, '\0', score});
                                    }
                                    break; // Stop at enemy piece
                                }
                            }
                        }
                        break;
                    case ROOK: // Rook moves
                        for (j = 0; j < 4; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + rookOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (board.PieceColor(n) != EMPTY) {
                                    if (board.PieceColor(n) == xside) {
                                        // Move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                                        score = (1000000 + (board.PieceType(n) * 10) - board.PieceType(i));
                                        Moves.push_back({ROW(i), COL(i), ROW(n), COL(n), false, false, '\0', score});
                                    }
                                    break; // Stop at enemy piece
                                }
                            }
                        }
                        break;
                    case QUEEN: // Queen moves
                        for (j = 0; j < 8; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + queenOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (board.PieceColor(n) != EMPTY) {
                                    if (board.PieceColor(n) == xside) {
                                        // Move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                                        score = (1000000 + (board.PieceType(n) * 10) - board.PieceType(i));
                                        Moves.push_back({ROW(i), COL(i), ROW(n), COL(n), false, false, '\0', score});
                                    }
                                    break; // Stop at enemy piece
                                }    
                            }
                        }
                        break;
                    case KING: // King moves
                        for (j = 0; j < 8; ++j) {
                            n = i;
                            while (true) {
                                n = mailbox[mailbox64[n] + kingOffsets[j]]; // Add the mailbox offset to get target square
                                if (n == -1) break; // Stop at invalid squares
                                if (board.PieceColor(n) != EMPTY) {
                                    if (board.PieceColor(n) == xside) {
                                        // Move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                                        score = (1000000 + (board.PieceType(n) * 10) - board.PieceType(i));
                                        Moves.push_back({ROW(i), COL(i), ROW(n), COL(n), false, false, '\0', score});
                                    }
                                    break; // Stop at enemy piece
                                }    
                                // This piece does not slide (e.g. king or knight), so stop after one move in any direction
                                break;
                            }
                        }
                        break;
                    default:
                    // Invalid piece type, do nothing
                    break;   
                }
            }
        }
    }

    // Generate en passant captures
    if (board.enPassantTarget != -1) {
        // std::cout << "generateMoves found EP target " << board.enPassantTarget << std::endl;
        // En passant captures are always pawn takes pawn so all have the same score
        score = 1000000 + (PAWN * 10) - PAWN; // true, false, '\0', score
        int targetRow = ROW(board.enPassantTarget);
        int targetCol = COL(board.enPassantTarget);
        // std::cout << "generateMoves board.whiteToMove " << board.whiteToMove << " targetRow " << targetRow << std::endl;
        if (board.whiteToMove && targetRow == 2) {
            // White can capture en passant if it has a pawn in the right place
            // std::cout << "generateMoves board.pieces[board.enPassantTarget + 7] " << board.pieces[board.enPassantTarget + 7] << std::endl;
            if (targetCol != 0 && board.pieces[board.enPassantTarget + 7] == 'P') {
                // std::cout << "generateMoves found EP capture pawn for White " << board.enPassantTarget + 7 << std::endl;
                Moves.push_back({targetRow + 1, targetCol - 1, targetRow, targetCol, true, false, '\0', score}); // Capture using pawn on left
            }
            // std::cout << "generateMoves board.pieces[board.enPassantTarget + 9] " << board.pieces[board.enPassantTarget + 9] << std::endl;
            if (targetCol != 7 && board.pieces[board.enPassantTarget + 9] == 'P') {
                // std::cout << "generateMoves found EP capture pawn for White " << board.enPassantTarget + 9 << std::endl;
                Moves.push_back({targetRow + 1, targetCol + 1, targetRow, targetCol, true, false, '\0', score}); // Capture using pawn on right
            }
        } else if (board.whiteToMove == false && targetRow == 5) {
            // Black can capture en passant
            if (targetCol != 0 && board.pieces[board.enPassantTarget - 9] == 'p') {
                // std::cout << "generateMoves found EP capture pawn for Black " << board.enPassantTarget - 9 << std::endl;
                Moves.push_back({targetRow - 1, targetCol - 1, targetRow, targetCol, true, false, '\0', score}); // Capture using pawn on left
            }
            if (targetCol != 7 && board.pieces[board.enPassantTarget - 7] == 'p') {
                // std::cout << "generateMoves found EP capture pawn for Black " << board.enPassantTarget - 7 << std::endl;
                Moves.push_back({targetRow - 1, targetCol + 1, targetRow, targetCol, true, false, '\0', score}); // Capture using pawn on right
            }
        }
    }
    // Return the list of generated moves
    return Moves;
}

std::vector<Move> sortMoves(std::vector<Move>& moves, const BoardData& board) {
    // This function sorts the moves based on their score.
    // First check if a sort is necessary.
    if (moves.empty()) return moves; // No moves to sort
    if (moves.size() == 1) return moves; // Only one move, no need to sort.
    // Now search the moves vector for the best move based on the score.
    // Then swap the best move to the front of the vector so that move gets searched next.
    int bestScore = -INF;
    int bestIndex = -1;
    Move move;
    for (size_t i = 0; i < moves.size(); ++i) {
        if (moves[i].score > bestScore) {
            bestScore = moves[i].score;
            bestIndex = i;
        }
    }
    move = moves[0];
    moves[0] = moves[bestIndex];
    moves[bestIndex] = move;
    return moves;
}

int quiescence(BoardData board, int alpha, int beta, bool maximizing,
               std::chrono::steady_clock::time_point deadline, std::atomic<bool>& stop) {
    // Quiescence search is a search that continues until a stable position is reached.
    if (stop.load() || std::chrono::steady_clock::now() > deadline) return 0;

    int stand_pat = evaluate(board);
    if (maximizing) {
        if (stand_pat >= beta) return beta;
        if (stand_pat > alpha) alpha = stand_pat;
    } else {
        if (stand_pat <= alpha) return alpha;
        if (stand_pat < beta) beta = stand_pat;
    }

    auto moves = generateCaptures(board); // Generate only capture moves for the quiescence search.
    if (moves.empty()) return stand_pat; // If no moves, return the static evaluation
    for (const auto& m : moves) {
        int toIdx = SQUARE(m.toRow, m.toCol);
        int fromIdx = SQUARE(m.fromRow, m.fromCol);
        char captured = board.pieces[toIdx];
        if (captured == '.') continue;
        BoardData newBoard = applyMove(board, m);
        int score = quiescence(newBoard, alpha, beta, !maximizing, deadline, stop);
        if (maximizing) {
            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        } else {
            if (score <= alpha) return alpha;
            if (score < beta) beta = score;
        }
    }
    return maximizing ? alpha : beta;
}

int alphabetaTimed(BoardData board, int depth, int alpha, int beta, bool maximizing,
                   std::chrono::steady_clock::time_point deadline, std::atomic<bool>& stop) {
    if (stop.load() || std::chrono::steady_clock::now() > deadline) return 0;
    if (board.halfmoveClock >= 100) return 0; // Draw by 50-move rule
    if (depth == 0) 
        // If we are at maximum depth, perform a quiescence search to get a reaonable score and return it.
        // This is to avoid the horizon effect and ensure we evaluate only stable positions.
        return quiescence(board, alpha, beta, maximizing, deadline, stop);

    auto moves = generateMoves(board); // Generate all possible moves for the current board state.
    if (moves.empty()) return evaluate(board);

    moves = sortMoves(moves, board); // Sort moves by score for better performance

    if (maximizing) {
        int maxEval = -INF;
        // Iterate through all moves and apply the alphabeta search recursively.
        // If a move leads to a better (higher) score, update the alpha value.
        // If beta is less than or equal to alpha, we can prune the search.
        for (auto& m : moves) {
            BoardData newBoard = applyMove(board, m);
            int eval = alphabetaTimed(newBoard, depth - 1, alpha, beta, false, deadline, stop);
            if (eval > maxEval) {
                maxEval = eval;
                if (eval >= beta) {
                    killerMoves[depth].push_back(m);
                    break;
                }
            }
            alpha = std::max(alpha, eval);
            uint64_t h = Zobrist().computeHash(newBoard);
            historyHeuristic[h] += depth * depth;
        }
        return maxEval;
    } else {
        int minEval = INF;
        // Iterate through all moves and apply the alphabeta search recursively.
        // If a move leads to a better (lower) score, update the beta value.
        // If beta is less than or equal to alpha, we can prune the search.
        for (auto& m : moves) {
            BoardData newBoard = applyMove(board, m);
            int eval = alphabetaTimed(newBoard, depth - 1, alpha, beta, true, deadline, stop);
            if (eval < minEval) {
                minEval = eval;
                if (eval <= alpha) {
                    killerMoves[depth].push_back(m);
                    break;
                }
            }
            beta = std::min(beta, eval);
            uint64_t h = Zobrist().computeHash(newBoard);
            historyHeuristic[h] += depth * depth;
        }
        return minEval;
    }
}

Move findBestMoveParallel(BoardData board, int depth, int timeLimitMs) {
    auto moves = generateMoves(board);
    if (moves.empty()) return {0, 0, 0, 0}; // No moves available
    if (moves.size() == 1) return moves[0]; // Only one move available return it
    if (depth < 1) depth = 1; // Ensure depth is at least 1
    if (timeLimitMs < 100) timeLimitMs = 100; // Ensure time limit is at least 100ms

    if (depth == 1) {
        // If depth is 1, just return the best move based on evaluation
        int bestScore = -INF;
        Move bestMove = moves[0];
        for (const auto& m : moves) {
            BoardData nextBoard = applyMove(board, m);
            int score = evaluate(nextBoard);
            if (score > bestScore) {
                bestScore = score;
                bestMove = m;
            }
        }
        return bestMove;
    }

    // Create a thread pool with the number of threads equal to the number of available cores.
    ThreadPool pool(std::thread::hardware_concurrency());
    // Create vector to hold futures for the results of each task.
    std::vector<std::future<int>> futures;

    // Create atomic flag to stop the search if time limit is reached.
    std::atomic<bool> localStop(false);
    // Set the deadline for the search based on the time limit.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeLimitMs);

    // Enqueue tasks for each move and collect futures.
    // This allows us to run the alphabeta search in parallel for each move.
    for (const auto& m : moves) {
        BoardData next = applyMove(board, m);
        futures.emplace_back(pool.enqueue([=, &localStop]() {
            return alphabetaTimed(next, depth - 1, -INF, INF, false, deadline, localStop);
        }));
    }

    int bestScore = -INF;
    Move bestMove = moves[0];
    // Wait for each future to complete and determine the best move.
    for (size_t i = 0; i < moves.size(); ++i) {
        // If the time limit is reached, we stop processing further.
        if (std::chrono::steady_clock::now() > deadline) {
            localStop = true;
            break;
        }
        // Get the result of the future.
        // This will block until the task is complete.
        // If the task was stopped due to time limit, it will return 0.
        // Otherwise, it will return the evaluation score for the move.
        int score = futures[i].get();
        if (score > bestScore) {
            bestScore = score;
            bestMove = moves[i];
        }
    }
    return bestMove;
}
