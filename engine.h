// engine.h

#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Constants used to indicate the colour of a piece at square sq
// 1 for white, -1 for black, 0 for empty
#define WHITE			1
#define BLACK			0
#define EMPTY			-1

// Constants used to indicate square is occupied by one of the 7 piece types:
// 0 for empty, 1 for pawn, 2 for knight, 3 for bishop, 4 for rook, 5 for queen, 6 for king
#define NO_PIECE		0
#define PAWN		    1
#define KNIGHT	        2
#define BISHOP	        3
#define ROOK		    4
#define QUEEN		    5
#define KING		    6
#define PIECE_NB        7 // Total number of piece types

// Function to convert integer piece type to a char
constexpr char TYPEtoCHAR(int p) {
    char c = '.'; // must initialise in constexpr function
    switch (p) {
        case EMPTY:  c = '.'; break;
        case PAWN:   c = 'p'; break;
        case KNIGHT: c = 'n'; break;
        case BISHOP: c = 'b'; break;
        case ROOK:   c = 'r'; break;
        case QUEEN:  c = 'q'; break;
        case KING:   c = 'k'; break;
        default: c = 'e'; break;
    }
    return c;
}

// Constants representing useful squares
#define A1				56
#define B1				57
#define C1				58
#define D1				59
#define E1				60
#define F1				61
#define G1				62
#define H1				63
#define A8				0
#define B8				1
#define C8				2
#define D8				3
#define E8				4
#define F8				5
#define G8				6
#define H8				7
#define SQUARE_NB       64 // Total number of squares on the board

// Constants for Files
#define FILE_A      1
#define FILE_B      2
#define FILE_C      3
#define FILE_D      4
#define FILE_E      5
#define FILE_F      6
#define FILE_G      7
#define FILE_H      8

// Constants for Ranks
#define RANK_1      1
#define RANK_2      2
#define RANK_3      3
#define RANK_4      4
#define RANK_5      5
#define RANK_6      6
#define RANK_7      7
#define RANK_8      8

// Returns the row index of the square sq
// Rows are numbered from 0 (top) to 7 (bottom).
constexpr int ROW(int sq) {
    return sq >> 3;
}

// Returns the column index of the square sq.
// Columns are numbered from 0 (left) to 7 (right).
constexpr int COL(int sq) {
    return sq & 7;
}

constexpr int SQUARE(int row, int col) {
        return row * 8 + col;
}

// Define the mailbox array used to map between the 8x8 board representation and
// the 10x12 board representation (called mailbox because it looks like a mailbox).

// The 10x12 representation is a 120 element array that embeds the 8x8 board array,
// surrounded by sentinel files and ranks. When generating moves using offsets per
// piece and direction to determine target squares these sentinel files and ranks
// are used to prevent out-of-bounds errors.

// For example, suppose we have a rook on square a4 (32 on the 8x8 board) and want
// to know if that rook can move one square to the left. Subtract 1 and get 31 (h5).
// The rook can't move to h5, but the program won't know that without doing a lot more work.
// Instead, it looks up a4 (32) in mailbox64 to find 61 (i.e. mailbox64[32] = 61) then 
// subtracts 1 from 61 to get 60. Now it looks up 60 in mailbox to see if it is a valid square.
// But mailbox[60] = -1, so the target square is invalid, which means the rook can't move there.
//
// Suppose we have a bishop on square c4 (34 on the 8x8 board) and want to know if that bishop
// can move three squares diagonally up the board to the left. Add the normal offset -9 once and 
// get 25 (b5) then again to get 16 (a6) and again to get 7 (h8) which is an invalid move.
// But the program won't know that because 7 is a valid square index.
// To get round this the program looks up 34 in mailbox64 to get 63 (i.e. mailbox64[34] = 63)
// then adds the special mailbox bishop offset of -11 three times to get 52, 41 and finally 30.
// When it looks up 30 in the mailbox array mailbox[30] = -1, so the target square is invalid.
// 

const int mailbox[120] = {
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1,  0,  1,  2,  3,  4,  5,  6,  7, -1,
	 -1,  8,  9, 10, 11, 12, 13, 14, 15, -1,
	 -1, 16, 17, 18, 19, 20, 21, 22, 23, -1,
	 -1, 24, 25, 26, 27, 28, 29, 30, 31, -1,
	 -1, 32, 33, 34, 35, 36, 37, 38, 39, -1,
	 -1, 40, 41, 42, 43, 44, 45, 46, 47, -1,
	 -1, 48, 49, 50, 51, 52, 53, 54, 55, -1,
	 -1, 56, 57, 58, 59, 60, 61, 62, 63, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

const int mailbox64[64] = {
	21, 22, 23, 24, 25, 26, 27, 28,
	31, 32, 33, 34, 35, 36, 37, 38,
	41, 42, 43, 44, 45, 46, 47, 48,
	51, 52, 53, 54, 55, 56, 57, 58,
	61, 62, 63, 64, 65, 66, 67, 68,
	71, 72, 73, 74, 75, 76, 77, 78,
	81, 82, 83, 84, 85, 86, 87, 88,
	91, 92, 93, 94, 95, 96, 97, 98
};

struct Move {
    int fromRow, fromCol, toRow, toCol;
    bool isEnPassant = false;
    bool isCastling = false;
    char promotion = '\0';  // Set to 'q', 'r', 'b', 'n' for promotions.
    int score = 0; // The score of the move, used for sorting moves.

    // Constructor to initialize all fields
    Move() : fromRow(0), fromCol(0), toRow(0), toCol(0), isEnPassant(false), isCastling(false), promotion('\0'), score(0) {}

    Move(int fr, int fc, int tr, int tc, bool ep = false, bool castling = false, char promo = '\0', int sc = 0)
        : fromRow(fr), fromCol(fc), toRow(tr), toCol(tc), isEnPassant(ep), isCastling(castling), promotion(promo), score(sc) {}
    
    // Define the operator == for the structure Move
    bool operator==(const Move& other) const {
        // Compare members of Move for equality
        // Example: return this->field == other.field;
        if ( (this->fromRow == other.fromRow) && (this->fromCol == other.fromCol) 
              && (this->toRow == other.toRow) && (this->toCol == other.toCol) ) return true;
        return false;
    }
};

// We use an 8x8 square-centric board representation comprising a 64 element char array:
// char pieces[64] contains:
// P, N, B, R, Q, K for White pieces or
// p, n, b, r, q, k for Black pieces or
// '.' for Empty squares.
//
//  0  1  2  3  4  5  6  7  - This is row 0 (Rank 8 the top). Rows are numbered from 0 (top) to 7 (bottom).
//  8  9 10 11 12 13 14 15  - This is row 1.
// 16 17 18 19 20 21 22 23  - This is row 2.
// 24 25 26 27 28 29 30 31  - This is row 3.
// 32 33 34 35 36 37 38 39  - This is row 4.
// 40 41 42 43 44 45 46 47  - This is row 5.
// 48 49 50 51 52 53 54 55  - This is row 6.
// 56 57 58 59 60 61 62 63  - This is row 7 (Rank 1 the bottom).
//  A  B  C  D  E  F  G  H  - These are Files (A to H) from left to right numbered from 1 to 8.
//  0  1  2  3  4  5  6  7  - Columns are numbered from 0 (left) to 7 (right).
//
//  A8 B8 C8 D8 E8 F8 G8 H8
//  A7 B7 C7 D7 E7 F7 G7 H7
//  A6 B6 C6 D6 E6 F6 G6 H6
//  A5 B5 C5 D5 E5 F5 G5 H5
//  A4 B4 C4 D4 E4 F4 G4 H4
//  A3 B3 C3 D3 E3 F3 G3 H3
//  A2 B2 C2 D2 E2 F2 G2 H2
//  A1 B1 C1 D1 E1 F1 G1 H1
//
struct BoardData {
    char pieces[64];           // Flat 8x8 board: row * 8 + col
    bool whiteToMove = true;   // Whose turn is it

    // Castling rights: white king/queen side, black king/queen side
    bool canCastleK = true;
    bool canCastleQ = true;
    bool canCastlek = true;
    bool canCastleq = true;

    int enPassantTarget = -1; // Square index (0-63) or -1 for none
    int halfmoveClock = 0; // halfmove count since last capture or pawn move
    int fullmoveNumber = 1; // fullmove number (starts at 1, incremented after Black move)

    // Member functions

    bool isValidSquare(int sq) const {
        return sq >= 0 && sq < 64;
    }   

    // Returns the color of the piece at square sq
    int PieceColor(int sq) const {
        if (sq < 0 || sq >= 64)
            return SQUARE_NB; // Invalid square
        char p = pieces[sq];
        if (p == '.') return EMPTY;
        return isupper(p) ? WHITE : BLACK;
    }

    // Returns the piece type at square sq
    int PieceType(int sq) const {
        if (sq < 0 || sq >= 64)
            return SQUARE_NB; // Invalid square
        char p = pieces[sq];
        if (p == '.') return EMPTY;
        switch (tolower(p)) {   
            case 'p': return PAWN;
            case 'n': return KNIGHT;
            case 'b': return BISHOP;
            case 'r': return ROOK;
            case 'q': return QUEEN;
            case 'k': return KING;
            default: return PIECE_NB; // Invalid piece
        }
    }

    // Convert square (row, col) to SAN-style string (e.g., "e4")
    // Rows are numbered from 7 (top) to 0 (bottom).
    // Columns are numbered from 0 (left) to 7 (right).
    std::string SquareToString(int row, int col) const {
        std::string str;
        str += static_cast<char>('a' + col + 1);
        str += static_cast<char>('1' + row);
        return str;
    }

    int SquareIndex(int row, int col) const {
        return (7 - row) * 8 + col;
    }
};

// Engine API
BoardData getInitialBoard();
void parsePosition(const std::string& input, BoardData& board);
BoardData applyMove(BoardData board, Move m);
std::string moveToUci(const Move& m);
Move bookMoveToFullMove(const Move& m, BoardData board);

// Zobrist hashing support
struct Zobrist {
    uint64_t pieceHash[12][64];
    uint64_t whiteToMoveHash;
    uint64_t castlingHash[4];
    uint64_t enPassantFileHash[8];

    Zobrist();
    uint64_t computeHash(const BoardData& board);
};
