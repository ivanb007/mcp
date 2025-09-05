// search.h

#pragma once
#include "engine.h"

#include <vector>
#include <chrono>
#include <atomic>
#include <cstdint>

extern std::atomic<uint64_t> g_nodes;

#define DOUBLED_PAWN_PENALTY		10
#define ISOLATED_PAWN_PENALTY		20
#define BACKWARDS_PAWN_PENALTY		8
#define PASSED_PAWN_BONUS			20
#define ROOK_SEMI_OPEN_FILE_BONUS	10
#define ROOK_OPEN_FILE_BONUS		15
#define ROOK_ON_SEVENTH_BONUS		20

// The value of each of the piece types used in evaluation
const int piece_value[PIECE_NB] = {
	0, 100, 320, 330, 500, 900, 0
};

// The "pcsq" arrays are piece/square tables.
// They are bonuses added to the material value of the piece based on location.
const int pawn_pcsq[64] = {
	  0,   0,   0,   0,   0,   0,   0,   0,
	  5,  10,  15,  20,  20,  15,  10,   5,
	  4,   8,  12,  16,  16,  12,   8,   4,
	  3,   6,   9,  12,  12,   9,   6,   3,
	  2,   4,   6,   8,   8,   6,   4,   2,
	  1,   2,   3, -10, -10,   3,   2,   1,
	  0,   0,   0, -40, -40,   0,   0,   0,
	  0,   0,   0,   0,   0,   0,   0,   0
};

const int knight_pcsq[64] = {
	-10, -10, -10, -10, -10, -10, -10, -10,
	-10,   0,   0,   0,   0,   0,   0, -10,
	-10,   0,   5,   5,   5,   5,   0, -10,
	-10,   0,   5,  10,  10,   5,   0, -10,
	-10,   0,   5,  10,  10,   5,   0, -10,
	-10,   0,   5,   5,   5,   5,   0, -10,
	-10,   0,   0,   0,   0,   0,   0, -10,
	-10, -30, -10, -10, -10, -10, -30, -10
};

const int bishop_pcsq[64] = {
	-10, -10, -10, -10, -10, -10, -10, -10,
	-10,   0,   0,   0,   0,   0,   0, -10,
	-10,   0,   5,   5,   5,   5,   0, -10,
	-10,   0,   5,  10,  10,   5,   0, -10,
	-10,   0,   5,  10,  10,   5,   0, -10,
	-10,   0,   5,   5,   5,   5,   0, -10,
	-10,   0,   0,   0,   0,   0,   0, -10,
	-10, -10, -20, -10, -10, -20, -10, -10
};

// Don't use piece squares for rook or queen

const int king_pcsq[64] = {
	-40, -40, -40, -40, -40, -40, -40, -40,
	-40, -40, -40, -40, -40, -40, -40, -40,
	-40, -40, -40, -40, -40, -40, -40, -40,
	-40, -40, -40, -40, -40, -40, -40, -40,
	-40, -40, -40, -40, -40, -40, -40, -40,
	-40, -40, -40, -40, -40, -40, -40, -40,
	-20, -20, -20, -20, -20, -20, -20, -20,
	  0,  20,  40, -20,   0, -20,  40,  20
};

const int king_endgame_pcsq[64] = {
	  0,  10,  20,  30,  30,  20,  10,   0,
	 10,  20,  30,  40,  40,  30,  20,  10,
	 20,  30,  40,  50,  50,  40,  30,  20,
	 30,  40,  50,  60,  60,  50,  40,  30,
	 30,  40,  50,  60,  60,  50,  40,  30,
	 20,  30,  40,  50,  50,  40,  30,  20,
	 10,  20,  30,  40,  40,  30,  20,  10,
	  0,  10,  20,  30,  30,  20,  10,   0
};

// All the "pcsq" arrays above are valued from the WHITE perspective.
// This mirror array is used to calculate the piece/square values for BLACK pieces.
// The piece/square value of a WHITE pawn is pawn_pcsq[sq] and the value of a BLACK
// pawn is pawn_pcsq[mirror[sq]].
const int mirror[64] = {
	 56,  57,  58,  59,  60,  61,  62,  63,
	 48,  49,  50,  51,  52,  53,  54,  55,
	 40,  41,  42,  43,  44,  45,  46,  47,
	 32,  33,  34,  35,  36,  37,  38,  39,
	 24,  25,  26,  27,  28,  29,  30,  31,
	 16,  17,  18,  19,  20,  21,  22,  23,
	  8,   9,  10,  11,  12,  13,  14,  15,
	  0,   1,   2,   3,   4,   5,   6,   7
};

int evaluate(const BoardData& state);
int eval_white_pawn(int sq);
int eval_black_pawn(int sq);
int eval_white_king(int sq);
int eval_wkp(int f);
int eval_black_king(int sq);
int eval_bkp(int f);
bool isCheckMate(const BoardData& board, const Move& move);
bool inCheck(const BoardData& board, int side);
bool attacked(const BoardData& board, int sq, int side);
bool pawn_attack(const BoardData& board, int sq, int side);
std::vector<Move> generateMoves(const BoardData& state);
std::vector<Move> generateCaptures(const BoardData& board);
std::vector<Move> sortMoves(std::vector<Move>& moves, const BoardData& board);
// Timed alpha-beta (implemented via negamax internally)
int alphabetaTimed(BoardData board, int depth, int alpha, int beta, bool /* maximizing ignored */,
                   std::chrono::steady_clock::time_point deadline, std::atomic<bool>& stop, std::vector<Move>& pv);
// Timed negamax quiescence (captures only), with PV
int quiescenceTimed(BoardData& board, int alpha, int beta, int qdepth,
                    std::chrono::steady_clock::time_point deadline,
                    std::atomic<bool>& stop, std::vector<Move>& pv);
Move findBestMoveParallel(BoardData state, int depth, int timeLimitMs);
