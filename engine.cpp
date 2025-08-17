// engine.cpp

#include "engine.h"
#include "fen.h"
#include "search.h"

#include <sstream>
#include <random>
#include <chrono>
#include <vector>

BoardData getInitialBoard() {
    BoardData board;
    const char* init =
        "rnbqkbnr"
        "pppppppp"
        "........"
        "........"
        "........"
        "........"
        "PPPPPPPP"
        "RNBQKBNR";

    for (int i = 0; i < 64; ++i)
        board.pieces[i] = init[i];
    board.whiteToMove = true;
    board.canCastleK = board.canCastleQ = board.canCastlek = board.canCastleq = true;
    board.enPassantTarget = -1;
    return board;
}

BoardData applyMove(BoardData board, Move move) {
    int side = board.whiteToMove ? WHITE : BLACK;
    // If castling do legality checks
    if (move.isCastling) {
        if (side == WHITE) {
            if ((move.toCol == 6 && !board.canCastleK) || (move.toCol == 2 && !board.canCastleQ)) {
                throw std::invalid_argument("Illegal move attempted"); // Cannot castle if rights are not available
            }
            // Check if the squares between the king and rook are empty and not attacked by enemy side
            if (move.toCol == 6) { // If White is castling kingside
                if (board.PieceColor(F1) != EMPTY || board.PieceColor(G1) != EMPTY) {
                    throw std::invalid_argument("Illegal move attempted"); // Cannot castle if squares are not empty
                }
                if (attacked(board, F1, BLACK) || attacked(board, G1, BLACK)) {
                    throw std::invalid_argument("Illegal move attempted"); // Cannot castle if squares are attacked
                }
            } else if (move.toCol == 2) { // If White is castling queenside
                if (board.PieceColor(D1) != EMPTY || board.PieceColor(C1) != EMPTY || board.PieceColor(B1) != EMPTY) {
                    throw std::invalid_argument("Illegal move attempted"); // Cannot castle if squares are not empty
                }
                if (attacked(board, D1, BLACK) || attacked(board, C1, BLACK) || attacked(board, B1, BLACK)) {
                    throw std::invalid_argument("Illegal move attempted"); // Cannot castle if squares are attacked
                }
            }
        } else { // If Black is castling
            if ((move.toCol == 6 && !board.canCastlek) || (move.toCol == 2 && !board.canCastleq)) {
                throw std::invalid_argument("Illegal move attempted"); // Cannot castle if rights are not available
            }
            // Check if the squares between the king and rook are empty and not attacked by enemy side
            if (move.toCol == 6) { // If Black is castling kingside
                if (board.PieceColor(F8) != EMPTY || board.PieceColor(G8) != EMPTY) {
                    throw std::invalid_argument("Illegal move attempted"); // Cannot castle if squares are not empty
                }
                if (attacked(board, F8, WHITE) || attacked(board, G8, WHITE)) {
                    throw std::invalid_argument("Illegal move attempted"); // Cannot castle if squares are attacked
                }
            } else if (move.toCol == 2) { // If Black is castling queenside
                if (board.PieceColor(D8) != EMPTY || board.PieceColor(C8) != EMPTY || board.PieceColor(B8) != EMPTY) {
                    throw std::invalid_argument("Illegal move attempted"); // Cannot castle if squares are not empty
                }
                if (attacked(board, D8, WHITE) || attacked(board, C8, WHITE) || attacked(board, B8, WHITE)) {
                    throw std::invalid_argument("Illegal move attempted"); // Cannot castle if squares are attacked
                }
            }
        }
    }

    // Decode the given move
    int from = SQUARE(move.fromRow, move.fromCol);
    int to = SQUARE(move.toRow, move.toCol);
    char movingPiece = board.pieces[from];
    bool isCapture = board.pieces[to] != '.';

    // Handle en passant captures
    if (move.isEnPassant) {
        if (board.PieceColor(to) != EMPTY) 
            throw std::invalid_argument("Illegal en passant move attempted");
        // If the move is an en passant capture, then the move to square
        // is actually the square behind the pawn being captured and must be empty.
        // Regardless which colour is moving the captured pawn is on the same row as the from square
        // so remove the captured pawn from the board
        board.pieces[SQUARE(move.fromRow, move.toCol)] = '.';
        board.enPassantTarget = -1; // Clear en passant target after capture
        isCapture = true;
        // Piece move is done by normal piece move logic below
    }

    // Do the Piece move
    if (move.promotion != '\0') {
        // Handle promotion move
        if (board.whiteToMove) board.pieces[to] = toupper(move.promotion);
        else board.pieces[to] = tolower(move.promotion);
    } else {
        // Normal move, including en passant, just move the piece
        board.pieces[to] = movingPiece;
    }
    // Clear the piece from the original square
    board.pieces[from] = '.';

    if (move.isCastling) {
        // If castling, move the rook (the king is moved by the normal piece move logic above)
        if (to == G1) { board.pieces[H1] = '.'; board.pieces[F1] = 'R'; }
        else if (to == C1) { board.pieces[A1] = '.'; board.pieces[D1] = 'R'; }
        else if (to == G8) { board.pieces[H8] = '.'; board.pieces[F8] = 'r'; }
        else if (to == C8) { board.pieces[A8] = '.'; board.pieces[D8] = 'r'; }
    }

    // Update castling rights when necessary
    if (from == E1) board.canCastleK = board.canCastleQ = false; // If White king moved then both castling rights are lost
    if (from == E8) board.canCastlek = board.canCastleq = false; // If Black king moved then both castling rights are lost
    if (from == H1 || to == H1) board.canCastleK = false; // If White rook on H1 moved or castled then White cannot castle kingside
    if (from == A1 || to == A1) board.canCastleQ = false; // If White rook on A1 moved or castled then White cannot castle queenside
    if (from == H8 || to == H8) board.canCastlek = false; // If Black rook on H8 moved or castled then Black cannot castle kingside
    if (from == A8 || to == A8) board.canCastleq = false; // If Black rook on A8 moved or castled then Black cannot castle queenside

    board.enPassantTarget = 0;
    // If the moving piece is a pawn and it moved two squares forward, set the en passant target
    if (tolower(movingPiece) == 'p' && abs(from - to) == 16)
        board.enPassantTarget = (from + to) / 2;

    // Reset halfmove clock if a pawn move or capture was made
    // Otherwise increment it to count the number of halfmoves since the last capture or pawn move
    if (tolower(movingPiece) == 'p' || isCapture)
        board.halfmoveClock = 0;
    else
        board.halfmoveClock++;

    // Update side to move.
    board.whiteToMove = !board.whiteToMove;

    // Increment fullmove number if Black just moved.
    if (board.whiteToMove) board.fullmoveNumber++;

    // Check if the move is legal
    // if (!isLegalMove(board, m)) throw std::invalid_argument("Illegal move attempted");

    return board;
}

void parsePosition(const std::string& input, BoardData& board) {
    std::istringstream iss(input);
    std::string token;
    iss >> token;  // 'position'
    if (!(iss >> token)) return;

    if (token == "startpos") {
        board = getInitialBoard();
        if (iss >> token && token == "moves") {
            while (iss >> token) {
                int fromCol = token[0] - 'a';
                int fromRow = 8 - (token[1] - '0');
                int toCol = token[2] - 'a';
                int toRow = 8 - (token[3] - '0');
                char promo = (token.length() == 5) ? token[4] : '\0';
                board = applyMove(board, {fromRow, fromCol, toRow, toCol, false, false, promo});
            }
        }
    } else if (token == "fen") {
        std::string fen;
        std::string word;
        int count = 0;
        while (count < 6 && iss >> word) {
            fen += word + ' ';
            count++;
        }
        board = loadFEN(fen);

        if (iss >> token && token == "moves") {
            while (iss >> token) {
                int fromCol = token[0] - 'a';
                int fromRow = 8 - (token[1] - '0');
                int toCol = token[2] - 'a';
                int toRow = 8 - (token[3] - '0');
                char promo = (token.length() == 5) ? token[4] : '\0';
                board = applyMove(board, {fromRow, fromCol, toRow, toCol, false, false, promo});
            }
        }
    }
}

std::string moveToUci(const Move& m) {
    std::string uci;
    uci += 'a' + m.fromCol;
    uci += '8' - m.fromRow;
    uci += 'a' + m.toCol;
    uci += '8' - m.toRow;
    if (m.promotion) uci += tolower(m.promotion);
    return uci;
}

Move decodeUciMove(const std::string& uci) {
    if (uci.length() < 4 || uci.length() > 5) throw std::invalid_argument("Invalid UCI move format");
    int fromCol = uci[0] - 'a';
    int fromRow = 8 - (uci[1] - '0');
    int toCol = uci[2] - 'a';
    int toRow = 8 - (uci[3] - '0');
    char promotion = (uci.length() == 5) ? uci[4] : '\0';
    return {fromRow, fromCol, toRow, toCol, false, false, promotion};
}

namespace {
    int pieceToIndex(char p) {
        switch (p) {
            case 'P': return 0; case 'N': return 1; case 'B': return 2;
            case 'R': return 3; case 'Q': return 4; case 'K': return 5;
            case 'p': return 6; case 'n': return 7; case 'b': return 8;
            case 'r': return 9; case 'q': return 10; case 'k': return 11;
            default: return -1;
        }
    }
}

Zobrist::Zobrist() {
    std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    for (int i = 0; i < 12; ++i)
        for (int j = 0; j < 64; ++j)
            pieceHash[i][j] = rng();
    whiteToMoveHash = rng();
    for (int i = 0; i < 4; ++i) castlingHash[i] = rng();
    for (int i = 0; i < 8; ++i) enPassantFileHash[i] = rng();
}

uint64_t Zobrist::computeHash(const BoardData& board) {
    uint64_t h = 0;
    for (int sq = 0; sq < 64; ++sq) {
        int idx = pieceToIndex(board.pieces[sq]);
        if (idx != -1) h ^= pieceHash[idx][sq];
    }
    if (board.whiteToMove) h ^= whiteToMoveHash;
    if (board.canCastleK) h ^= castlingHash[0];
    if (board.canCastleQ) h ^= castlingHash[1];
    if (board.canCastlek) h ^= castlingHash[2];
    if (board.canCastleq) h ^= castlingHash[3];
    if (board.enPassantTarget != -1)
        h ^= enPassantFileHash[board.enPassantTarget % 8];
    return h;
}
