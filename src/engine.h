#pragma once
#include "bitboard.h"
#include "tt.h"
#include "evaluator.h"
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <functional>

// ─────────────────────────────────────────────
//  Search statistics (live, printed during search)
// ─────────────────────────────────────────────
struct SearchStats {
    std::atomic<uint64_t> nodes_explored{0};
    std::atomic<uint64_t> nodes_pruned{0};
    std::atomic<uint64_t> tt_hits{0};
    std::atomic<uint64_t> tt_stores{0};
    uint64_t              nodes_at_depth[CELLS + 1]{};
    int                   current_depth{0};

    void reset() {
        nodes_explored = 0;
        nodes_pruned   = 0;
        tt_hits        = 0;
        tt_stores      = 0;
        current_depth  = 0;
        std::fill(std::begin(nodes_at_depth), std::end(nodes_at_depth), 0);
    }

    std::string summary() const;
};

// ─────────────────────────────────────────────
//  Search result for one depth
// ─────────────────────────────────────────────
struct SearchResult {
    int  best_move    = -1;
    int  score        = 0;
    int  depth        = 0;
    bool is_exact     = false;   // score is exact (not bound)
    int  mate_in      = 0;       // 0 = no forced mate, >0 = win in N, <0 = lose in N

    // Principal variation (sequence of best moves)
    std::vector<int> pv;
};

// ─────────────────────────────────────────────
//  Engine
// ─────────────────────────────────────────────
class Engine {
public:
    explicit Engine(size_t tt_size_mb = 256);
    ~Engine() = default;

    // ── Main search entry ────────────────────
    // fixed_depth == 0 → unlimited (search until stop())
    // fixed_depth >  0 → search exactly that depth
    SearchResult search(const Position& pos, int fixed_depth = 0);

    // Stop ongoing search (call from another thread or signal)
    void stop();

    // Reset engine state (clear TT, stats)
    void reset();

    // Access stats
    const SearchStats& stats() const { return stats_; }

    // Callback invoked after each depth level completes (for live display)
    // Arguments: (result_so_far, stats)
    using ProgressCallback = std::function<void(const SearchResult&, const SearchStats&)>;
    void set_progress_callback(ProgressCallback cb) { progress_cb_ = std::move(cb); }

    // TT info
    double tt_usage_pct() const { return tt_.usage(); }

private:
    // ── Negamax with Alpha-Beta ──────────────
    int negamax(Position pos, int depth, int alpha, int beta,
                std::vector<int>& pv, bool use_tt = true);

    // ── Quiescence search for tactical stability ──
    int quiescence(Position pos, int alpha, int beta);

    // ── Move ordering ────────────────────────
    // Returns ordered list: winning moves → threatening moves → center-first → rest
    std::vector<int> order_moves(const Position& pos, int tt_best_move) const;

    // Check if move creates an immediate win
    bool is_winning_move(const Position& pos, int col) const;

    // Check if opponent wins if we play col (i.e., we must block)
    bool is_opponent_winning(const Position& pos, int col) const;

    // ── Principal variation extraction ───────
    void extract_pv(Position pos, std::vector<int>& pv, int depth);

    // ── Mate score helpers ───────────────────
    static int  mate_score(int moves_left);
    static bool is_mate_score(int score);
    static int  moves_to_mate(int score, int moves_played);

    TranspositionTable   tt_;
    SearchStats          stats_;
    std::atomic<bool>    stop_flag_{false};
    ProgressCallback     progress_cb_;

    // Killer moves [depth][2 slots]
    int killer_moves_[CELLS + 1][2];

    // History heuristic table [col][depth] 
    int history_[COLS][CELLS + 1];

    void clear_killers();
    void clear_history();
    void update_killer(int col, int depth);
    void update_history(int col, int depth);
};
