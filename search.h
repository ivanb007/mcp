
#pragma once
#include "engine.h"
#include <vector>
#include <chrono>
#include <atomic>

int evaluate(const BoardData& state);
std::vector<Move> generateMoves(const BoardData& state);
int alphabetaTimed(BoardData board, int depth, int alpha, int beta, bool maximizing,
                   std::chrono::steady_clock::time_point deadline, std::atomic<bool>& stop);
Move findBestMoveParallel(BoardData state, int depth, int timeLimitMs);
