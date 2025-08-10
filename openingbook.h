
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
    bool hasMove(const BoardData& board) const;
    Move getMove(const BoardData& board) const;

private:
    std::vector<PolyglotEntry> entries;
    std::unordered_map<uint64_t, std::vector<PolyglotEntry>> entryMap;
};
