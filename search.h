
#pragma once
#include "engine.h"
#include <vector>
#include <chrono>
#include <atomic>

int evaluate(const BoardData& state);
bool isCheckMate(const BoardData& board, const Move& move);
bool isLegalMove(const BoardData& board, const Move& move);
bool inCheck(const BoardData& board, int side);
bool attacked(const BoardData& board, int sq, int side);
bool pawn_attack(const BoardData& board, int sq, int side);
std::vector<Move> generateMoves(const BoardData& state);
std::vector<Move> generateCaptures(const BoardData& board);
std::vector<Move> sortMoves(std::vector<Move>& moves, const BoardData& board);
int alphabetaTimed(BoardData board, int depth, int alpha, int beta, bool maximizing,
                   std::chrono::steady_clock::time_point deadline, std::atomic<bool>& stop);
Move findBestMoveParallel(BoardData state, int depth, int timeLimitMs);
