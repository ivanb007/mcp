// openingbook.cpp

#include "openingbook.h"
#include "search.h"

#include <fstream>
#include <random>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <map>
#include <vector>

// Polyglot hash random values (defined in external table)
extern uint64_t polyglotRandom[781]; // should be initialized with standard Polyglot values

int pieceIndex(char piece) {
    switch (piece) {
        case 'p': return 0;   // black pawn
        case 'P': return 1;   // white pawn
        case 'n': return 2;   // black knight
        case 'N': return 3;   // white knight
        case 'b': return 4;   // black bishop
        case 'B': return 5;   // white bishop
        case 'r': return 6;   // black rook
        case 'R': return 7;   // white rook
        case 'q': return 8;   // black queen
        case 'Q': return 9;   // white queen
        case 'k': return 10;  // black king
        case 'K': return 11;  // white king
        default:  return -1;  // not a valid piece
    }
}

int squareIndex(int row, int col) {
    // Convert row and column to reflect Polyglot's square indexing which is:
    // a1 = 0 (i.e. row=0 & col=0) up to h8 = 63 (i.e. row=7 & col=7)
    // This means rank 1 is row 0, and rank 8 is row 7 in the array.
    return row * 8 + col;
}

uint64_t computePolyglotKeyFromFEN(const std::string& fen) {
    std::istringstream iss(fen);
    std::string piecePlacement, activeColor, castling, ep;
    iss >> piecePlacement >> activeColor >> castling >> ep;

    uint64_t key = 0;

    // Process piece placement
    int row = 7, col = 0;
    for (char c : piecePlacement) {
        // If we hit a delimiter, move to the start of the next row
        if (c == '/') { row--; col = 0; continue; }
        // If we hit a digit, it means we need to skip that many squares
        if (isdigit(c)) {
            col += c - '0';
        } else {
            // Otherwise, we have a piece character
            int index = pieceIndex(c);
            int sq = squareIndex(row, col);
            if (index >= 0)
                key ^= polyglotRandom[64 * index + sq];
            // Increment column for the next piece
            col++;
        }
    }

    // Process castling rights
    if (castling.find('K') != std::string::npos)
        key ^= polyglotRandom[768 + 0];  // white short

    if (castling.find('Q') != std::string::npos)
        key ^= polyglotRandom[768 + 1];  // white long

    if (castling.find('k') != std::string::npos)
        key ^= polyglotRandom[768 + 2];  // black short

    if (castling.find('q') != std::string::npos)
        key ^= polyglotRandom[768 + 3];  // black long
    
    // Process en passant target square
    if (ep != "-") {
        char file = ep[0];
        int epFile = file - 'a';
        // We only include en passant file hash if a pawn is in position to capture
        std::string rankLine;
        // Determine the rank of the en passant target
        int epRank = (activeColor == "w") ? 4 : 3;
        std::istringstream pieceStream(fen);
        pieceStream >> piecePlacement;
        int curRow = 7, curCol = 0;
        for (char c : piecePlacement) {
            // If we hit a delimiter, move to the start of the next row
            if (c == '/') { curRow--; curCol = 0; continue; }
            // If we hit a digit, it means we need to skip that many squares
            if (isdigit(c)) {
                curCol += c - '0';
            } else {
                // Check if this piece is a pawn that can capture en passant
                if (curRow == epRank &&
                    ((activeColor == "w" && c == 'P') || (activeColor == "b" && c == 'p')) &&
                    (curCol == epFile - 1 || curCol == epFile + 1)) {
                    key ^= polyglotRandom[772 + epFile];
                    break;
                }
                curCol++;
            }
        }
    }

    // Process active color only if it is white to move
    if (activeColor == "w")
        key ^= polyglotRandom[780];

    return key;
}

Move decode_polyglot_move(uint16_t m) {
    //
    // The "polyglot move" is a bit field with the following meaning (bit 0 is the least significant bit)
    // bits                meaning
    // ===================================
    // 0,1,2               to file
    // 3,4,5               to row
    // 6,7,8               from file
    // 9,10,11             from row
    // 12,13,14            promotion piece
    int from = mirror[((m >> 6) & 0x3F)];
    int to = mirror[(m & 0x3F)];
    // Convert row and column to reflect Polyglot's square indexing which is:
    // a1 = 0 (i.e. row=0 & col=0) up to h8 = 63 (i.e. row=7 & col=7)
    // This means rank 1 is row 0, and rank 8 is row 7 in the array.

    // "promotion piece" is encoded as follows
    // none       0
    // knight     1
    // bishop     2
    // rook       3
    // queen      4
    int promo_piece = (m >> 12) & 0x3F;
    char promotion = '\0';
    switch (promo_piece) {
        case 1: promotion = 'n'; break;
        case 2: promotion = 'b'; break;
        case 3: promotion = 'r'; break;
        case 4: promotion = 'q'; break;
    }

    // Castling moves are represented somewhat unconventially as follows:
    // white short      e1h1
    // white long       e1a1
    // black short      e8h8
    // black long       e8a8
    // It is technically possible that these moves are legal non-castling moves.
    // So before deciding that these are really castling moves the engine must for example verify there is a king present on e1/e8.
    // This is done by the follow up function bookMoveToFullMove in engine.cpp.

    return { from / 8, from % 8, to / 8, to % 8, false, false, promotion };
}

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

bool OpeningBook::hasMove(const std::string& fen) const {
    uint64_t key = computePolyglotKeyFromFEN(fen);
    return entryMap.find(key) != entryMap.end();
}

Move OpeningBook::getMove(const std::string& fen) const {
    uint64_t key = computePolyglotKeyFromFEN(fen);
    auto it = entryMap.find(key);
    if (it == entryMap.end()) return {0,0,0,0, false, false, '\0'}; // No move found

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
