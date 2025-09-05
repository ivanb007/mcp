// heuristics.h
#pragma once
#include "engine.h"

#include <algorithm>
#include <cstdint>
#include <vector>

// ---- Evaluation Heuristics -------
struct EvalMatrix {
    // pawn_rank[x][y] is the rank of the least advanced pawn of color x on the file y - 1. 
    // There are "buffer files" on the left and right to avoid special-case logic later. 
    // If there's no pawn on a rank, we pretend the pawn is impossibly far advanced (0 for WHITE and 7 for BLACK).
    // This makes it easy to test for pawns on a rank and it simplifies some pawn evaluation code.
    int16_t pawn_rank[2][10]{};

    // piece_mat[x] is the total material value of the pieces (excluding pawns) belonging to color x
    int16_t piece_mat[2]{};

    // piece_mat[x] is the total material value of the pawns belonging to color x
    int16_t pawn_mat[2]{};

    inline void clear() {
        for (int i = 0; i < 10; ++i) {
            pawn_rank[WHITE][i] = 0;
            pawn_rank[BLACK][i] = 7;
        }
        piece_mat[WHITE] = 0;
        piece_mat[BLACK] = 0;
        pawn_mat[WHITE] = 0;
        pawn_mat[BLACK] = 0;
    }
};

// --- History Heuristic (side, from, to) -> score ---
struct HistoryTable {
    // Keep it small (int16_t) and clamp
    // Indices are [side][from][to] side: 0=white, 1=black
    int16_t h[2][64][64]{};

    inline void clear() {
        std::fill(&h[0][0][0], &h[0][0][0] + 2*64*64, 0);
    }
    inline void add(int side, int from, int to, int v) {
        int32_t x = (int32_t)h[side][from][to] + v;
        if (x > 32767) x = 32767;
        if (x < -32768) x = -32768;
        h[side][from][to] = (int16_t)x;
    }
    // Merge: sum with clamp
    inline void mergeFrom(const HistoryTable& other) {
        for (int s=0; s<2; ++s)
            for (int f=0; f<64; ++f)
                for (int t=0; t<64; ++t) {
                    int32_t x = (int32_t)h[s][f][t] + other.h[s][f][t];
                    if (x > 32767) x = 32767;
                    if (x < -32768) x = -32768;
                    h[s][f][t] = (int16_t)x;
                }
    }
};

// --- Killer moves: top-2 non-captures per ply ---
struct KillerTable {
    // Two killers per ply (tune MAX_PLY to your engine); store as ints or your Move
    static constexpr int MAX_PLY = 128;
    Move k1[MAX_PLY]{};
    Move k2[MAX_PLY]{};

    inline void clear() {
        for (int i=0; i<MAX_PLY; ++i) { k1[i] = Move{}; k2[i] = Move{}; }
    }
    inline void add(int ply, Move m) {
        if (m == k1[ply] || m == k2[ply]) return;
        k2[ply] = k1[ply];
        k1[ply] = m;
    }
    // Merge: keep the union best-2 by simple frequency preference
    inline void mergeFrom(const KillerTable& o) {
        for (int p=0; p<MAX_PLY; ++p) {
            Move cands[4] = {k1[p], k2[p], o.k1[p], o.k2[p]};
            // Dedup while preserving earlier entries
            Move out1{}, out2{};
            for (int i=0; i<4; ++i) {
                if (!(cands[i] == Move{})) {
                    if (out1 == Move{}) out1 = cands[i];
                    else if (!(cands[i] == out1) && out2 == Move{}) out2 = cands[i];
                }
            }
            k1[p] = out1; k2[p] = out2;
        }
    }
};

// --- Transposition Table (simple set/replace by depth) ---
struct TTEntry {
    uint64_t key=0;
    int16_t  score=0;
    uint8_t  depth=0;
    uint8_t  flag=0;   // 0 exact, 1 alpha, 2 beta, etc.
    Move     best{};
    uint16_t age=0;
};

class TransTable {
public:
    explicit TransTable(size_t sz = 1<<20) : table(sz) {}
    void clear() { std::fill(table.begin(), table.end(), TTEntry{}); }

    inline TTEntry* probePtr(uint64_t key) {
        return &table[key % table.size()];
    }
    inline bool probe(uint64_t key, TTEntry& out) const {
        const TTEntry& e = table[key % table.size()];
        if (e.key == key) { out = e; return true; }
        return false;
    }
    inline void store(uint64_t key, int score, uint8_t depth, uint8_t flag, Move best, uint16_t age) {
        TTEntry& e = table[key % table.size()];
        // Replace by depth or empty
        if (e.key == 0 || depth >= e.depth) {
            e.key = key; e.score = (int16_t)score; e.depth = depth; e.flag = flag; e.best = best; e.age = age;
        }
    }
    // Merge preferring deeper (or newer age if equal depth)
    inline void mergeFrom(const TransTable& other) {
        size_t n = table.size();
        for (size_t i=0; i<n; ++i) {
            const TTEntry& src = other.table[i];
            if (src.key == 0) continue;
            TTEntry& dst = table[i];
            if (dst.key == 0 || src.depth > dst.depth || (src.depth == dst.depth && src.age > dst.age)) {
                dst = src;
            }
        }
    }

private:
    std::vector<TTEntry> table;
};
