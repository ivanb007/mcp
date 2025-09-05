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
#include "thread_context.h"
#include "fen.h"

#include <limits>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <chrono>
#include <atomic>
#include <cctype>

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

std::atomic<uint64_t> g_nodes{0};  // <-- define the counter

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

    int score[2];   // The accumulated score for each color
    int color;      // Piece Color 
    int pt;         // Piece Type
    int file = 0;

    g_ctx.eval.clear();  // Initialise the evaluation matrix before every evaluation

    // Calculate the new values for pawn_rank, piece_mat & pawn_mat
    for (int i = 0; i < 64; ++i) {
        color = state.PieceColor(i);    // Get Piece Color 
        pt = state.PieceType(i);        // Get Piece Type
        if (color == EMPTY)
            continue;
        if (pt == PAWN) {
            g_ctx.eval.pawn_mat[color] += piece_value[PAWN];
            file = COL(i) + 1;  // add 1 to the column because of the extra files in the array at 0 and 9
            if (color == WHITE) {
                if (g_ctx.eval.pawn_rank[WHITE][file] < ROW(i)) g_ctx.eval.pawn_rank[BLACK][file] = ROW(i);
            }
            else {
                if (g_ctx.eval.pawn_rank[BLACK][file] > ROW(i)) g_ctx.eval.pawn_rank[BLACK][file] = ROW(i);
            }
        }
        else
            g_ctx.eval.piece_mat[color] += piece_value[pt];
    }
    
    // Now initialise the scores and evaluate each piece
    score[WHITE] = g_ctx.eval.piece_mat[WHITE] + g_ctx.eval.pawn_mat[WHITE];
	score[BLACK] = g_ctx.eval.piece_mat[BLACK] + g_ctx.eval.pawn_mat[BLACK];
    for (int i = 0; i < 64; ++i) {
        pt = state.PieceType(i); // Get Piece Type
        if (pt == EMPTY) continue;
        switch (pt) {
            case PAWN:
                if (color == WHITE) score[WHITE] += eval_white_pawn(i);
                else score[BLACK] += eval_black_pawn(i);
                break;
            case KNIGHT:
                if (color == WHITE) score[WHITE] += knight_pcsq[i];
                else score[BLACK] += knight_pcsq[mirror[i]];
                break;
            case BISHOP:
                if (color == WHITE) score[WHITE] += bishop_pcsq[i];
                else score[BLACK] += bishop_pcsq[mirror[i]];
                break;
            case ROOK:
                if (color == WHITE) {
                    if (g_ctx.eval.pawn_rank[WHITE][COL(i) + 1] == 0) {
						if (g_ctx.eval.pawn_rank[BLACK][COL(i) + 1] == 7)
							score[WHITE] += ROOK_OPEN_FILE_BONUS;
						else
							score[WHITE] += ROOK_SEMI_OPEN_FILE_BONUS;
					}
					if (ROW(i) == 1)
						score[WHITE] += ROOK_ON_SEVENTH_BONUS;
                }
                else {
                    if (g_ctx.eval.pawn_rank[BLACK][COL(i) + 1] == 7) {
						if (g_ctx.eval.pawn_rank[BLACK][COL(i) + 1] == 0)
							score[BLACK] += ROOK_OPEN_FILE_BONUS;
						else
							score[BLACK] += ROOK_SEMI_OPEN_FILE_BONUS;
					}
					if (ROW(i) == 6)
						score[BLACK] += ROOK_ON_SEVENTH_BONUS;
                }
                break;
            case KING:
                if (color == WHITE) {
                    if (g_ctx.eval.piece_mat[BLACK] <= 1200)
						score[WHITE] += king_endgame_pcsq[i];
					else
                        score[WHITE] += eval_white_king(i);
                }
                else {
                    if (g_ctx.eval.piece_mat[WHITE] <= 1200)
						score[BLACK] += king_endgame_pcsq[mirror[i]];
					else
                        score[BLACK] += eval_black_king(i);
                }
                break;
        } 
    }
    // Return the score relative to White
    return score[WHITE] - score[BLACK];
}

int eval_white_pawn(int sq)
{
	int r;  /* the value to return */
	int f;  /* the pawn's file */

	r = 0;
	f = COL(sq) + 1;

	r += pawn_pcsq[sq];

	/* if there's a pawn behind this one, it's doubled */
	if (g_ctx.eval.pawn_rank[WHITE][f] > ROW(sq))
		r -= DOUBLED_PAWN_PENALTY;

	/* if there aren't any friendly pawns on either side of
	   this one, it's isolated */
	if ((g_ctx.eval.pawn_rank[WHITE][f - 1] == 0) &&
			(g_ctx.eval.pawn_rank[WHITE][f + 1] == 0))
		r -= ISOLATED_PAWN_PENALTY;

	/* if it's not isolated, it might be backwards */
	else if ((g_ctx.eval.pawn_rank[WHITE][f - 1] < ROW(sq)) &&
			(g_ctx.eval.pawn_rank[WHITE][f + 1] < ROW(sq)))
		r -= BACKWARDS_PAWN_PENALTY;

	/* add a bonus if the pawn is passed */
	if ((g_ctx.eval.pawn_rank[BLACK][f - 1] >= ROW(sq)) &&
			(g_ctx.eval.pawn_rank[BLACK][f] >= ROW(sq)) &&
			(g_ctx.eval.pawn_rank[BLACK][f + 1] >= ROW(sq)))
		r += (7 - ROW(sq)) * PASSED_PAWN_BONUS;

	return r;
}

int eval_black_pawn(int sq)
{
	int r;  /* the value to return */
	int f;  /* the pawn's file */

	r = 0;
	f = COL(sq) + 1;

	r += pawn_pcsq[mirror[sq]];

	/* if there's a pawn behind this one, it's doubled */
	if (g_ctx.eval.pawn_rank[BLACK][f] < ROW(sq))
		r -= DOUBLED_PAWN_PENALTY;

	/* if there aren't any friendly pawns on either side of
	   this one, it's isolated */
	if ((g_ctx.eval.pawn_rank[BLACK][f - 1] == 7) &&
			(g_ctx.eval.pawn_rank[BLACK][f + 1] == 7))
		r -= ISOLATED_PAWN_PENALTY;

	/* if it's not isolated, it might be backwards */
	else if ((g_ctx.eval.pawn_rank[BLACK][f - 1] > ROW(sq)) &&
			(g_ctx.eval.pawn_rank[BLACK][f + 1] > ROW(sq)))
		r -= BACKWARDS_PAWN_PENALTY;

	/* add a bonus if the pawn is passed */
	if ((g_ctx.eval.pawn_rank[WHITE][f - 1] <= ROW(sq)) &&
			(g_ctx.eval.pawn_rank[WHITE][f] <= ROW(sq)) &&
			(g_ctx.eval.pawn_rank[WHITE][f + 1] <= ROW(sq)))
		r += ROW(sq) * PASSED_PAWN_BONUS;

	return r;
}

int eval_white_king(int sq) {
    int r = king_pcsq[sq];  // The value to return starts with piece square value.
    int i;
    // If the king is castled, use a special function to evaluate the pawns on the appropriate side.
    if (COL(sq) < 3) {
        // King is on the Left side
		r += eval_wkp(FILE_A);
		r += eval_wkp(FILE_B);
		r += eval_wkp(FILE_C) / 2; // problems with pawns on the c file are not as serious
	}
	else if (COL(sq) > 4) {
        // King is on the Right side
		r += eval_wkp(FILE_H);
		r += eval_wkp(FILE_G);
		r += eval_wkp(FILE_F) / 2; // problems with pawns on the f file are not as serious
	}
    else {
        // otherwise, just apply a penalty if there are open files near the king
		for (i = COL(sq); i <= COL(sq) + 2; ++i)
			if ((g_ctx.eval.pawn_rank[WHITE][i] == 0) &&
					(g_ctx.eval.pawn_rank[BLACK][i] == 7))
				r -= 10;
	}
    // Finally, scale the king safety value according to the opponent's material
    r *= g_ctx.eval.piece_mat[BLACK];
	r /= 3100;
    return r;
}

int eval_wkp(int f) {
    // Evaluate the White King Pawn on File f
	int r = 0;
    // Evaluate the White pawn on this file
	if (g_ctx.eval.pawn_rank[WHITE][f] == 6);  // pawn hasn't moved
	else if (g_ctx.eval.pawn_rank[WHITE][f] == 5)
		r -= 10;  // pawn moved one square
	else if (g_ctx.eval.pawn_rank[WHITE][f] != 0)
		r -= 20;  // pawn moved more than one square
	else
		r -= 25;  // no pawn on this file
    // Evaluate the Enemy pawn on this file
	if (g_ctx.eval.pawn_rank[BLACK][f] == 7)
		r -= 15;  // no enemy pawn
	else if (g_ctx.eval.pawn_rank[BLACK][f] == 5)
		r -= 10;  // enemy pawn on the 3rd rank
	else if (g_ctx.eval.pawn_rank[BLACK][f] == 4)
		r -= 5;   // enemy pawn on the 4th rank
	return r;
}

int eval_black_king(int sq) {
    int r = king_pcsq[mirror[sq]];  // The value to return starts with piece square value.
    int i;
    // If the king is castled, use a special function to evaluate the pawns on the appropriate side.
    if (COL(sq) < 3) {
        // King is on the Left side
		r += eval_bkp(FILE_A);
		r += eval_bkp(FILE_B);
		r += eval_bkp(FILE_C) / 2; // problems with pawns on the c file are not as serious
	}
	else if (COL(sq) > 4) {
        // King is on the Right side
		r += eval_bkp(FILE_H);
		r += eval_bkp(FILE_G);
		r += eval_bkp(FILE_F) / 2; // problems with pawns on the f file are not as serious
	}
    else {
        // otherwise, just apply a penalty if there are open files near the king
		for (i = COL(sq); i <= COL(sq) + 2; ++i)
			if ((g_ctx.eval.pawn_rank[WHITE][i] == 0) &&
					(g_ctx.eval.pawn_rank[BLACK][i] == 7))
				r -= 10;
	}
    // Finally, scale the king safety value according to the opponent's material
    r *= g_ctx.eval.piece_mat[WHITE];
	r /= 3100;
    return r;
}

int eval_bkp(int f) {
    // Evaluate the Black King Pawn on File f
	int r = 0;
    // Evaluate the Black pawn on this file
	if (g_ctx.eval.pawn_rank[BLACK][f] == 6);  // pawn hasn't moved
	else if (g_ctx.eval.pawn_rank[BLACK][f] == 5)
		r -= 10;  // pawn moved one square
	else if (g_ctx.eval.pawn_rank[BLACK][f] != 0)
		r -= 20;  // pawn moved more than one square
	else
		r -= 25;  // no pawn on this file
    // Evaluate the Enemy pawn on this file
	if (g_ctx.eval.pawn_rank[WHITE][f] == 7)
		r -= 15;  // no enemy pawn
	else if (g_ctx.eval.pawn_rank[WHITE][f] == 5)
		r -= 10;  // enemy pawn on the 3rd rank
	else if (g_ctx.eval.pawn_rank[WHITE][f] == 4)
		r -= 5;   // enemy pawn on the 4th rank
	return r;
}

bool isCheckMate(const BoardData& board, const Move& move) {
    // Returns true only if the given move has check mated the side to move on board
    int sideToMove = board.whiteToMove ? WHITE : BLACK;
    if (inCheck(board, sideToMove)) {
        // If the side to move is in check is there any legal escape move?
        std::vector<Move> Moves;
        // generate all pseudo-legal moves
        Moves = generateMoves(board);
        if (Moves.empty())
            return true; // No escape moves, checkmate
    }
    return false; // Escape moves exist, not checkmate
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

// Generate all pseudo legal moves for the current board state
// Returns a vector of Move objects containing all pseudo legal moves
std::vector<Move> generatePseudoLegalMoves(const BoardData& board) {
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
                    if (COL(i) != 0 && board.PieceColor(i - 9) == BLACK) {
                        // White pawn making a capture
                        if (ROW(i - 9) == 0) {
                            // White pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                score = (1000000 + (k * 10));
                                Moves.push_back({ROW(i), COL(i), ROW(i - 9), COL(i - 9), false, false, TYPEtoCHAR(k), score});
                            }
                        } else {
                            // If the move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i - 9) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i - 9), COL(i - 9), false, false, '\0', score});
                        }
                    }
                    if (COL(i) != 7 && board.PieceColor(i - 7) == BLACK) {
                        // White pawn making a capture
                        if (ROW(i - 7) == 0) {
                            // White pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                score = (1000000 + (k * 10));
                                Moves.push_back({ROW(i), COL(i), ROW(i - 7), COL(i - 7), false, false, TYPEtoCHAR(k), score});
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
                    if (COL(i) != 0 && board.PieceColor(i + 7) == WHITE) {
                        // Black pawn making a capture
                        if (ROW(i + 7) == 7) {
                            // Black pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                score =  (1000000 + (k * 10));
                                Moves.push_back({ROW(i), COL(i), ROW(i + 7), COL(i + 7), false, false, TYPEtoCHAR(k), score});
                            }
                        } else {
                            // If the move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i + 7) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i + 7), COL(i + 7), false, false, '\0', score});
                        }
                    }
                    if (COL(i) != 7 && board.PieceColor(i + 9) == WHITE) {
                        // Black pawn making a capture
                        if (ROW(i + 9) == 7) {
                            // Black pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                score = (1000000 + (k * 10));
                                Moves.push_back({ROW(i), COL(i), ROW(i + 9), COL(i + 9), false, false, TYPEtoCHAR(k), score});
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
        // White can castle kingside check all pieces are in the right place
        if (board.pieces[F1] == '.' && board.pieces[G1] == '.' && 
            board.pieces[E1] == 'K' && board.pieces[H1] == 'R') {
                // Confirm King is not in check & none of the squares between the King & rook are attacked
                if (!(attacked(board, E1, BLACK) || attacked(board, F1, BLACK) || attacked(board, G1, BLACK)))
                    Moves.push_back({ROW(E1), COL(E1), ROW(G1), COL(G1), false, true}); // Kingside castling
        }
    }
    if (board.canCastleQ && board.whiteToMove) {   
        // White can castle queenside
        if (board.pieces[D1] == '.' && board.pieces[C1] == '.' && 
            board.pieces[B1] == '.' && board.pieces[E1] == 'K' && 
            board.pieces[A1] == 'R') {
                // Confirm King is not in check & none of the squares between the King & rook are attacked
                if (!(attacked(board, E1, BLACK) || attacked(board, B1, BLACK) || attacked(board, C1, BLACK) || attacked(board, D1, BLACK)))
                    Moves.push_back({ROW(E1), COL(E1), ROW(C1), COL(C1), false, true}); // Queenside castling
        }
    }
    if (board.canCastlek && !board.whiteToMove) {
        // Black can castle kingside
        if (board.pieces[F8] == '.' && board.pieces[G8] == '.' && 
            board.pieces[E8] == 'k' && board.pieces[H8] == 'r') {
                // Confirm King is not in check & none of the squares between the King & rook are attacked
                if (!(attacked(board, E8, WHITE) || attacked(board, F8, WHITE) || attacked(board, G8, WHITE)))
                    Moves.push_back({ROW(E8), COL(E8), ROW(G8), COL(G8), false, true}); // Kingside castling
        }
    }
    if (board.canCastleq && !board.whiteToMove) {
        // Black can castle queenside
        if (board.pieces[D8] == '.' && board.pieces[C8] == '.' && 
            board.pieces[B8] == '.' && board.pieces[E8] == 'k' && 
            board.pieces[A8] == 'r') {
                // Confirm King is not in check & none of the squares between the King & rook are attacked
                if (!(attacked(board, E8, WHITE) || attacked(board, B8, BLACK) || attacked(board, C8, BLACK) || attacked(board, D8, BLACK)))
                    Moves.push_back({ROW(E8), COL(E8), ROW(C8), COL(C8), false, true}); // Queenside castling
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

std::vector<Move> generateMoves(const BoardData& board) {
    std::vector<Move> legalMoves;
    std::vector<Move> pseudoMoves = generatePseudoLegalMoves(board);
    int side = board.whiteToMove ? WHITE : BLACK;
    for (const auto& m : pseudoMoves) {
        BoardData next = applyMove(board, m);
        if (!inCheck(next, side)) {
            legalMoves.push_back(m);
        }
    }
    return legalMoves;
}

// Generate all pseudo legal capture and promote moves for the current position.
// This function is used by the quiescence search.
std::vector<Move> generatePseudoLegalCaptures(const BoardData& board) {
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
                    if (COL(i) != 0 && board.PieceColor(i - 9) == BLACK) {
                        // White pawn making a capture
                        if (ROW(i - 9) == 0) {
                            // White pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // The promotion is a capture so provide the promotion piece and a high score for promotion moves.
                                score = (1000000 + (k * 10));
                                Moves.push_back({ROW(i), COL(i), ROW(i - 9), COL(i - 9), false, false, TYPEtoCHAR(k), score});
                            }
                        } else {
                            // The move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i - 9) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i - 9), COL(i - 9), false, false, '\0', score});
                        }
                    }
                    if (COL(i) != 7 && board.PieceColor(i - 7) == BLACK) {
                        // White pawn making a capture
                        if (ROW(i - 7) == 0) {
                            // White pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                score = (1000000 + (k * 10));
                                // The promotion is a capture so provide the promotion piece and a high score for promotion moves
                                Moves.push_back({ROW(i), COL(i), ROW(i - 7), COL(i - 7), false, false, TYPEtoCHAR(k), score});
                            }
                        } else {
                            // If the move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i - 7) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i - 7), COL(i - 7), false, false, '\0', score});
                        }
                    }
                } else { // Black pawns move down the board in steps of +8 (or +16 if they are still on their starting rank)
                    // Black pawns start on rank 7 (row 1) squares 8 to 15
                    if (COL(i) != 0 && board.PieceColor(i + 7) == WHITE) {
                        // Black pawn making a capture
                        if (ROW(i + 7) == 7) {
                            // Black pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // The promotion is a capture so provide the promotion piece and a high score for promotion moves
                                score = (1000000 + (k * 10));
                                Moves.push_back({ROW(i), COL(i), ROW(i + 7), COL(i + 7), false, false, TYPEtoCHAR(k), score});
                            }
                        } else {
                            // If the move is a capture, calculate score using MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
                            score = (1000000 + (board.PieceType(i + 7) * 10) - board.PieceType(i));
                            Moves.push_back({ROW(i), COL(i), ROW(i + 7), COL(i + 7), false, false, '\0', score});
                        }
                    }
                    if (COL(i) != 7 && board.PieceColor(i + 9) == WHITE) {
                        // Black pawn making a capture
                        if (ROW(i + 9) == 7) {
                            // Black pawn moving to last rank so generate promotion moves
                            for (int k = KNIGHT; k <= QUEEN; ++k) {
                                // Provide the promotion piece and a high score for promotion moves
                                score = (1000000 + (k * 10));
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

std::vector<Move> generateCaptures(const BoardData& board) {
    std::vector<Move> legalMoves;
    std::vector<Move> pseudoMoves = generatePseudoLegalCaptures(board);
    int side = board.whiteToMove ? WHITE : BLACK;
    for (const auto& m : pseudoMoves) {
        BoardData next = applyMove(board, m);
        if (!inCheck(next, side)) {
            legalMoves.push_back(m);
        }
    }
    return legalMoves;
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
    g_nodes.fetch_add(1, std::memory_order_relaxed);          // <-- count this node
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
                   std::chrono::steady_clock::time_point deadline, std::atomic<bool>& stop, std::vector<Move>& pv) {
    // Alpha-Beta pruning is an optimization technique for the minimax algorithm.
    // It cuts off branches in the game tree which need not be searched because there already is a better move available.
    // To do this it passes 2 extra parameters into the minimax function, namely alpha and beta.
    // Alpha is the best value that the maximizer currently can guarantee at that level or above.
    // Beta is the best value that the minimizer currently can guarantee at that level or below.

    g_nodes.fetch_add(1, std::memory_order_relaxed);          // <-- count this node
    if (stop.load() || std::chrono::steady_clock::now() > deadline) return 0;
    if (board.halfmoveClock >= 100) return 0; // Draw by 50-move rule
    if (depth == 0) 
        // If we are at maximum depth, perform a quiescence search to get a reaonable score and return it.
        // This is to avoid the horizon effect and ensure we evaluate only stable positions.
        return quiescence(board, alpha, beta, maximizing, deadline, stop);

    auto moves = generateMoves(board); // Generate all possible moves (child nodes) for the current board state.
    if (moves.empty()) return evaluate(board);

    moves = sortMoves(moves, board); // Sort moves by score for better performance

    Move bestMove = moves[0];
    std::vector<Move> bestLine;

    if (maximizing) {
        int maxEval = -INF; // maximizer starts with a best value of -INFINITY
        // Iterate through all moves (child nodes) at this level and apply the alphabeta search recursively.
        // If a move leads to a better (higher) score, update the alpha value.
        // If beta is less than or equal to alpha, we can prune the search.
        for (const auto& m : moves) {
            BoardData newBoard = applyMove(board, m);
            std::vector<Move> childPV;
            int eval = alphabetaTimed(newBoard, depth - 1, alpha, beta, false, deadline, stop, childPV);
            if (eval > maxEval) {
                // maxEval = std::max(maxEval, eval);
                maxEval = eval; // Update the maximum evaluation found so far
                bestMove = m;
                bestLine = childPV;
            }
            alpha = std::max(alpha, maxEval); // Update alpha with the highest evaluation found so far
            if (beta <= alpha) break; // Beta cut-off
        }
        pv.clear();
        pv.push_back(bestMove);
        pv.insert(pv.end(), bestLine.begin(), bestLine.end());
        return maxEval;
    } else {
        int minEval = INF; // minimizer starts with a best value of +INFINITY
        // Iterate through all moves (child nodes) at this level and apply the alphabeta search recursively.
        // If a move leads to a better (lower) score, update the beta value.
        // If beta is less than or equal to alpha, we can prune the search.
        for (auto& m : moves) {
            BoardData newBoard = applyMove(board, m);
            std::vector<Move> childPV;
            int eval = alphabetaTimed(newBoard, depth - 1, alpha, beta, true, deadline, stop, childPV);
            if (eval < minEval) {
                // minEval = std::min(minEval, eval);
                minEval = eval; // Update the minimum evaluation found so far
                bestMove = m;
                bestLine = childPV;
            }
            beta = std::min(beta, minEval); // Update beta with the lowest evaluation found so far
            if (beta <= alpha) break; // Alpha cut-off
        }
        pv.clear();
        pv.push_back(bestMove);
        pv.insert(pv.end(), bestLine.begin(), bestLine.end());
        return minEval;
    }
}

// Find the best move using parallel search.
// This function uses a parallel search to find the best move for the current board state.
// It generates all possible moves, then uses a thread pool to evaluate each move in parallel.
// It returns the best move found within the given time limit.
// The depth parameter specifies how deep the search should go, and timeLimitMs specifies the maximum
// time in milliseconds to spend searching for the best move.
// If depth is 1, it simply evaluates the moves and returns the best one.
Move findBestMoveParallel(BoardData board, int depth, int timeLimitMs) {
    std::cout << "Started findBestMoveParallel depth: " << depth << std::endl;
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

    std::vector<Move> pvLine;

    // Create atomic flag to stop the search if time limit is reached.
    std::atomic<bool> localStop(false);
    // Set the deadline for the search based on the time limit.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeLimitMs);

    // Enqueue tasks for each move and collect futures.
    // This allows us to run the alphabeta search in parallel for each move.
    for (const auto& m : moves) {
        BoardData next = applyMove(board, m);
        futures.emplace_back(pool.enqueue([=, &localStop, &pvLine]() {
            return alphabetaTimed(next, depth - 1, -INF, INF, false, deadline, localStop, pvLine);
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
