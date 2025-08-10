
#include "openingbook.h"
#include "engine.h"
#include <fstream>
#include <random>
#include <algorithm>
#include <cstring>

namespace {
    uint64_t flip_bytes(uint64_t x) {
        uint64_t y = 0;
        for (int i = 0; i < 8; ++i)
            y = (y << 8) | ((x >> (i * 8)) & 0xFF);
        return y;
    }

    uint16_t flip_bytes16(uint16_t x) {
        return (x << 8) | (x >> 8);
    }

    uint32_t flip_bytes32(uint32_t x) {
        return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
               ((x & 0xFF0000) >> 8) | ((x & 0xFF000000) >> 24);
    }

    Move decode_polyglot_move(uint16_t m) {
        int from = ((m >> 6) & 0x3F);
        int to = (m & 0x3F);
        return { from / 8, from % 8, to / 8, to % 8 };
    }
}

bool OpeningBook::load(const std::string& filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) return false;
    while (!in.eof()) {
        PolyglotEntry entry;
        in.read(reinterpret_cast<char*>(&entry), sizeof(PolyglotEntry));
        if (in.gcount() != sizeof(PolyglotEntry)) break;
        entry.key = flip_bytes(entry.key);
        entry.move = flip_bytes16(entry.move);
        entry.weight = flip_bytes16(entry.weight);
        entry.learn = flip_bytes32(entry.learn);
        entries.push_back(entry);
        entryMap[entry.key].push_back(entry);
    }
    return true;
}

bool OpeningBook::hasMove(const BoardData& board) const {
    Zobrist zobrist;
    uint64_t key = zobrist.computeHash(board);
    return entryMap.find(key) != entryMap.end();
}

Move OpeningBook::getMove(const BoardData& board) const {
    Zobrist zobrist;
    uint64_t key = zobrist.computeHash(board);
    auto it = entryMap.find(key);
    if (it == entryMap.end()) return {0,0,0,0};

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1, 10000);
    int total = 0;
    for (auto& e : it->second) total += e.weight;
    int r = dist(gen) % total;
    int sum = 0;
    for (auto& e : it->second) {
        sum += e.weight;
        if (r < sum) return decode_polyglot_move(e.move);
    }
    return decode_polyglot_move(it->second[0].move);
}
