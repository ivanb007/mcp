// san_pgn.h

#pragma once
#include <string>
#include <vector>
#include "engine.h"

std::vector<std::string> splitSANMoves(const std::string& pgn);
std::vector<BoardData> replayPGN(const std::string& pgnText);
