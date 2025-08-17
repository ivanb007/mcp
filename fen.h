// fen.h

#pragma once

#include <string>
#include "engine.h"

std::string boardToFEN(const BoardData& board);
BoardData loadFEN(const std::string& fen);
void printFENBoard(const std::string& fen);
