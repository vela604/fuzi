#pragma once
#include "bitboard.h"
#include <vector>
#include <random>
#include <atomic>
#include <cstring>

// ─────────────────────────────────────────────
//  Zobrist Hashing
// ─────────────────────────────────────────────
struct ZobristTable {
    // [player 0/1][col 0-6][row 0-5]
    uint64_t table[2][COLS][ROWS];

    ZobristTable() {
        std::mt19937_64 rng(0xDEADBEEFCAFEBABEULL);
        for (auto& p : table)
            for (auto& c : p)
                for (auto& r : c)
                    r = rng();
    }

    uint64_t hash(const Position& pos) const {
        uint64_t h = 0;
        // P1 pieces: in mask but not in (current if P2 to move) ...
        // Simpler: enumerate all cells
        for (int c = 0; c < COLS; ++c) {
            for (int r = 0; r < ROWS; ++r) {
                BB bit = BB(1) << (c + r * COLS);
                if (pos.mask & bit) {
                    // current = player-to-move's pieces
                    // Piece placed by P1 (even turn piece)?
                    // We can't determine player from Position alone without
                    // knowing which pieces belong to which player purely by bitboard.
                    // Use: pos.current = current mover's pieces
                    //      pos.opponent() = current ^ mask
                    int player = (pos.current & bit) ? pos.side_to_move() : (1 - pos.side_to_move());
                    h ^= table[player][c][r];
                }
            }
        }
        return h;
    }
};

// Global Zobrist table
inline ZobristTable ZOBRIST;

// ─────────────────────────────────────────────
//  Transposition Table Entry
// ─────────────────────────────────────────────
enum class TTFlag : uint8_t {
    EXACT = 0,   // exact score
    LOWER = 1,   // alpha bound (score >= value)
    UPPER = 2,   // beta  bound (score <= value)
    EMPTY = 3
};

struct TTEntry {
    uint64_t key;    // full key for verification
    int32_t  score;
    int8_t   depth;
    int8_t   best_move;  // column 0-6, or -1
    TTFlag   flag;

    TTEntry() : key(0), score(0), depth(-1), best_move(-1), flag(TTFlag::EMPTY) {}
};

// ─────────────────────────────────────────────
//  Transposition Table
// ─────────────────────────────────────────────
class TranspositionTable {
public:
    static constexpr size_t DEFAULT_SIZE_MB = 256;

    explicit TranspositionTable(size_t size_mb = DEFAULT_SIZE_MB) {
        resize(size_mb);
    }

    void resize(size_t size_mb) {
        size_t bytes   = size_mb * 1024 * 1024;
        num_entries_   = bytes / sizeof(TTEntry);
        // Round down to power of 2 for fast modulo
        size_t pow2 = 1;
        while (pow2 * 2 <= num_entries_) pow2 *= 2;
        num_entries_ = pow2;
        mask_        = num_entries_ - 1;
        table_.assign(num_entries_, TTEntry{});
    }

    void clear() {
        std::fill(table_.begin(), table_.end(), TTEntry{});
        hits_    = 0;
        misses_  = 0;
        stores_  = 0;
    }

    // Store entry
    void store(uint64_t key, int score, int depth, int best_move, TTFlag flag) {
        size_t idx = key & mask_;
        TTEntry& e = table_[idx];
        // Always-replace strategy (simple, effective)
        e.key       = key;
        e.score     = static_cast<int32_t>(score);
        e.depth     = static_cast<int8_t>(depth);
        e.best_move = static_cast<int8_t>(best_move);
        e.flag      = flag;
        ++stores_;
    }

    // Probe entry — returns nullptr on miss
    const TTEntry* probe(uint64_t key) const {
        size_t idx = key & mask_;
        const TTEntry& e = table_[idx];
        if (e.flag != TTFlag::EMPTY && e.key == key) {
            ++hits_;
            return &e;
        }
        ++misses_;
        return nullptr;
    }

    // Stats
    uint64_t hits()   const { return hits_;   }
    uint64_t misses() const { return misses_; }
    uint64_t stores() const { return stores_; }
    double   usage()  const {
        size_t used = 0;
        for (auto& e : table_) if (e.flag != TTFlag::EMPTY) ++used;
        return 100.0 * used / num_entries_;
    }
    size_t   size()   const { return num_entries_; }

private:
    std::vector<TTEntry>  table_;
    size_t                num_entries_ = 0;
    size_t                mask_        = 0;
    mutable uint64_t      hits_        = 0;
    mutable uint64_t      misses_      = 0;
    uint64_t              stores_      = 0;
};
