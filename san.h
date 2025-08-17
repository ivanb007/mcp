// san.h

#pragma once

#include <string>
#include "engine.h"

Move parseSAN(const std::string& san, const BoardData& board);
std::string sanFromMove(const Move& move, const BoardData& board);