
#pragma once

#include "engine.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

struct PolyglotEntry {
    uint64_t key;
    uint16_t move;
    uint16_t weight;
    uint32_t learn;
};

class OpeningBook {
public:
    bool load(const std::string& filename);
    bool hasMove(const std::string& fen) const;
    Move getMove(const std::string& fen) const;

private:
    std::vector<PolyglotEntry> entries;
    std::unordered_map<uint64_t, std::vector<PolyglotEntry>> entryMap;
};

int pieceIndex(char piece);
int squareIndex(int row, int col);
uint64_t computePolyglotKeyFromFEN(const std::string& fen);