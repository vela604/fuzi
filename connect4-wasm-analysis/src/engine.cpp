#include "engine.h"
#include <algorithm>
#include <sstream>
#include <cstring>
#include <iostream>
#include <iomanip>

// ─────────────────────────────────────────────
//  SearchStats::summary
// ─────────────────────────────────────────────
std::string SearchStats::summary() const {
    std::ostringstream oss;
    uint64_t total = nodes_explored.load();
    uint64_t pruned = nodes_pruned.load();
    double pct_pruned = total > 0 ? 100.0 * pruned / (total + pruned) : 0.0;
    oss << "Nodes: " << total
        << "  Pruned: " << pruned
        << " (" << std::fixed << std::setprecision(1) << pct_pruned << "%)"
        << "  TT Hits: " << tt_hits.load()
        << "  TT Stores: " << tt_stores.load();
    return oss.str();
}

// ─────────────────────────────────────────────
//  Engine constructor
// ─────────────────────────────────────────────
Engine::Engine(size_t tt_size_mb)
    : tt_(tt_size_mb)
{
    clear_killers();
    clear_history();
}

void Engine::reset() {
    tt_.clear();
    stats_.reset();
    stop_flag_ = false;
    clear_killers();
    clear_history();
}

void Engine::stop() {
    stop_flag_ = true;
}

void Engine::clear_killers() {
    std::memset(killer_moves_, -1, sizeof(killer_moves_));
}

void Engine::clear_history() {
    std::memset(history_, 0, sizeof(history_));
}

void Engine::update_killer(int col, int depth) {
    if (depth < 0 || depth > CELLS) return;
    if (killer_moves_[depth][0] != col) {
        killer_moves_[depth][1] = killer_moves_[depth][0];
        killer_moves_[depth][0] = col;
    }
}

void Engine::update_history(int col, int depth) {
    if (col < 0 || col >= COLS) return;
    if (depth < 0 || depth > CELLS) return;
    history_[col][depth] += depth * depth;  // bonus proportional to depth squared
}

// ─────────────────────────────────────────────
//  Mate score helpers
// ─────────────────────────────────────────────
int Engine::mate_score(int moves_left) {
    return SCORE_WIN - moves_left;
}

bool Engine::is_mate_score(int score) {
    return std::abs(score) >= MATE_THRESHOLD;
}

int Engine::moves_to_mate(int score, int /*moves_played*/) {
    // Positive score = current player wins
    // SCORE_WIN - moves_left = score
    // moves_left = SCORE_WIN - score
    // total_moves = moves_played + moves_left
    if (score > 0) {
        int moves_left = SCORE_WIN - score;
        return moves_left;
    } else {
        int moves_left = SCORE_WIN + score;
        return -moves_left;
    }
}

// ─────────────────────────────────────────────
//  Move ordering
// ─────────────────────────────────────────────
bool Engine::is_winning_move(const Position& pos, int col) const {
    return pos.is_winning_move(col);
}

bool Engine::is_opponent_winning(const Position& pos, int col) const {
    // If we play col, does opponent win on the NEXT move somewhere?
    // Actually this checks: after playing col, does opponent have immediate win?
    // We use it to detect: if we DON'T play col, opponent wins here
    // i.e., is col a blocking move?
    // Better: check if the cell above col is an opponent winning square
    Position after = pos.after(col);
    // In 'after', it's opponent's turn (they became 'current')
    // Check if opponent (now 'current' in after) has immediate win
    for (int c = 0; c < COLS; ++c) {
        if (after.can_play(c) && after.is_winning_move(c)) return true;
    }
    return false;
}

std::vector<int> Engine::order_moves(const Position& pos, int tt_best_move) const {
    struct ScoredMove {
        int col;
        int priority;
    };

    std::vector<ScoredMove> moves;
    moves.reserve(COLS);

    for (int col = 0; col < COLS; ++col) {
        if (!pos.can_play(col)) continue;

        int priority = 0;

        // Priority 1: Immediate win (highest)
        if (pos.is_winning_move(col)) {
            priority = 100'000;
        }
        // Priority 2: TT best move
        else if (col == tt_best_move) {
            priority = 90'000;
        }
        // Priority 3: Block opponent win
        else {
            // Check if opponent would win here if we don't play
            // (i.e., this is a forced blocking move)
            // Check if opponent currently threatens a win in col
            bool is_block = false;
            for (int oc = 0; oc < COLS; ++oc) {
                // Would opponent win if they played oc right now (before our move)?
                if (pos.can_play(oc)) {
                    BB opp = pos.opponent();
                    int or_ = pos.height(oc);
                    BB bit = BB(1) << (oc + or_ * COLS);
                    if (is_win(opp | bit) && oc == col) {
                        is_block = true;
                        break;
                    }
                }
            }
            if (is_block) {
                priority = 80'000;
            } else {
                // Priority 4: Killer moves
                int depth_approx = CELLS - pos.moves;
                if (depth_approx >= 0 && depth_approx <= CELLS) {
                    if (killer_moves_[depth_approx][0] == col) priority += 5'000;
                    else if (killer_moves_[depth_approx][1] == col) priority += 4'000;
                }
                // Priority 5: History heuristic
                if (depth_approx >= 0 && depth_approx <= CELLS) {
                    priority += history_[col][depth_approx];
                }
                // Priority 6: Center preference
                priority += CENTER_COL_WEIGHT[col] * 100;

                // Priority 7: Don't play below opponent's winning square
                // (playing col would give opponent a win above us)
                int h = pos.height(col);
                if (h + 1 < ROWS) {
                    BB above_bit = BB(1) << (col + (h + 1) * COLS);
                    BB opp = pos.opponent();
                    if (is_win(opp | above_bit)) {
                        priority -= 50'000;  // very bad move — don't give opponent win
                    }
                }
            }
        }

        moves.push_back({col, priority});
    }

    std::stable_sort(moves.begin(), moves.end(),
        [](const ScoredMove& a, const ScoredMove& b) {
            return a.priority > b.priority;
        });

    std::vector<int> result;
    result.reserve(moves.size());
    for (auto& m : moves) result.push_back(m.col);
    return result;
}

// ─────────────────────────────────────────────
//  Principal variation extraction
// ─────────────────────────────────────────────
void Engine::extract_pv(Position pos, std::vector<int>& pv, int depth) {
    if (depth <= 0 || pos.is_full()) return;
    if (is_win(pos.current) || is_win(pos.opponent())) return;

    uint64_t key = pos.canonical_key();
    const TTEntry* entry = tt_.probe(key);
    if (!entry || entry->best_move < 0) return;

    int col = entry->best_move;
    if (!pos.can_play(col)) return;

    pv.push_back(col);
    pos = pos.after(col);
    extract_pv(pos, pv, depth - 1);
}

// ─────────────────────────────────────────────
//  Quiescence search
//  Only check immediate wins/losses
// ─────────────────────────────────────────────
int Engine::quiescence(Position pos, int alpha, int beta) {
    ++stats_.nodes_explored;

    // Check for immediate wins by current player
    for (int c = 0; c < COLS; ++c) {
        if (pos.can_play(c) && pos.is_winning_move(c)) {
            return mate_score(CELLS - pos.moves);
        }
    }

    // Static eval
    int stand_pat = evaluate(pos);
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    return alpha;
}

// ─────────────────────────────────────────────
//  Negamax with Alpha-Beta Pruning
//
//  Returns score from perspective of current player.
//  Positive = current player winning.
// ─────────────────────────────────────────────
int Engine::negamax(Position pos, int depth, int alpha, int beta,
                    std::vector<int>& pv, bool use_tt)
{
    if (stop_flag_) return 0;

    ++stats_.nodes_explored;

    // ── Terminal checks ───────────────────────
    // Current player already won? (shouldn't happen in normal flow
    // since we detect wins before recursing, but guard anyway)
    if (is_win(pos.current)) {
        return mate_score(CELLS - pos.moves + 1);
    }
    if (is_win(pos.opponent())) {
        return -mate_score(CELLS - pos.moves);
    }
    if (pos.is_full()) {
        return SCORE_DRAW;
    }

    // ── Depth limit ───────────────────────────
    if (depth <= 0) {
        return quiescence(pos, alpha, beta);
    }

    // ── Transposition Table probe ─────────────
    uint64_t key = pos.canonical_key();
    int tt_best_move = -1;

    if (use_tt) {
        const TTEntry* entry = tt_.probe(key);
        if (entry && entry->depth >= depth) {
            ++stats_.tt_hits;
            int tt_score = entry->score;
            if (entry->flag == TTFlag::EXACT) {
                pv.clear();
                if (entry->best_move >= 0) pv.push_back(entry->best_move);
                return tt_score;
            } else if (entry->flag == TTFlag::LOWER) {
                alpha = std::max(alpha, tt_score);
            } else if (entry->flag == TTFlag::UPPER) {
                beta = std::min(beta, tt_score);
            }
            if (alpha >= beta) {
                ++stats_.nodes_pruned;
                return tt_score;
            }
            tt_best_move = entry->best_move;
        } else if (entry) {
            tt_best_move = entry->best_move;  // use move hint even if depth insufficient
        }
    }

    // ── Win in 1 check (before full move ordering) ────────
    // Quick scan for immediate wins
    for (int c : COL_ORDER) {
        if (pos.can_play(c) && pos.is_winning_move(c)) {
            int score = mate_score(CELLS - pos.moves);
            // Store in TT
            if (use_tt) {
                tt_.store(key, score, depth, c, TTFlag::EXACT);
                ++stats_.tt_stores;
            }
            pv.clear();
            pv.push_back(c);
            return score;
        }
    }

    // ── Check if opponent has winning threats we must block ────
    // Count opponent winning moves
    std::vector<int> forced_blocks;
    for (int c = 0; c < COLS; ++c) {
        if (!pos.can_play(c)) continue;
        BB opp = pos.opponent();
        int r = pos.height(c);
        BB bit = BB(1) << (c + r * COLS);
        if (is_win(opp | bit)) forced_blocks.push_back(c);
    }

    // If opponent has 2+ threats we can't block all → we lose
    if (forced_blocks.size() >= 2) {
        int score = -mate_score(CELLS - pos.moves - 1);
        if (use_tt) {
            tt_.store(key, score, depth, forced_blocks[0], TTFlag::EXACT);
            ++stats_.tt_stores;
        }
        return score;
    }

    // ── Move ordering ─────────────────────────
    std::vector<int> ordered_moves;
    if (!forced_blocks.empty()) {
        // Must play the only blocking move
        ordered_moves = forced_blocks;
    } else {
        ordered_moves = order_moves(pos, tt_best_move);
    }

    // ── Alpha-Beta search ─────────────────────
    int  best_score = std::numeric_limits<int>::min() + 1;
    int  best_move  = -1;
    TTFlag flag     = TTFlag::UPPER;
    std::vector<int> best_child_pv;

    for (int col : ordered_moves) {
        if (stop_flag_) break;

        std::vector<int> child_pv;
        Position next = pos.after(col);

        // Negamax: negate opponent's score
        int score = -negamax(next, depth - 1, -beta, -alpha, child_pv, use_tt);

        if (score > best_score) {
            best_score    = score;
            best_move     = col;
            best_child_pv = child_pv;
        }

        if (score > alpha) {
            alpha = score;
            flag  = TTFlag::EXACT;
        }

        if (alpha >= beta) {
            // Beta cutoff — prune remaining
            ++stats_.nodes_pruned;
            flag = TTFlag::LOWER;
            update_killer(col, depth);
            update_history(col, depth);
            break;
        }
    }

    // ── Store in TT ───────────────────────────
    if (!stop_flag_ && use_tt && best_move >= 0) {
        tt_.store(key, best_score, depth, best_move, flag);
        ++stats_.tt_stores;
    }

    // ── Build PV ─────────────────────────────
    pv.clear();
    if (best_move >= 0) {
        pv.push_back(best_move);
        pv.insert(pv.end(), best_child_pv.begin(), best_child_pv.end());
    }

    return best_score;
}

// ─────────────────────────────────────────────
//  Main search entry
// ─────────────────────────────────────────────
SearchResult Engine::search(const Position& pos, int fixed_depth) {
    stop_flag_ = false;
    stats_.reset();
    clear_killers();
    clear_history();

    SearchResult best_result;
    best_result.best_move = -1;
    best_result.score     = 0;
    best_result.depth     = 0;

    // Quick sanity: is game already over?
    if (is_win(pos.current) || is_win(pos.opponent()) || pos.is_full()) {
        return best_result;
    }

    int max_depth = (fixed_depth > 0) ? fixed_depth : CELLS;

    // Iterative deepening
    for (int depth = 1; depth <= max_depth; ++depth) {
        if (stop_flag_) break;

        stats_.current_depth = depth;

        std::vector<int> pv;
        int score = negamax(pos, depth, SCORE_LOSS - 1, SCORE_WIN + 1, pv);

        if (stop_flag_) break;

        // Update result
        best_result.depth     = depth;
        best_result.score     = score;
        best_result.is_exact  = true;
        best_result.pv        = pv;
        best_result.best_move = pv.empty() ? -1 : pv[0];

        // Compute mate distance
        if (is_mate_score(score)) {
            best_result.mate_in = moves_to_mate(score, pos.moves);
        } else {
            best_result.mate_in = 0;
        }

        // Notify caller (live display)
        if (progress_cb_) progress_cb_(best_result, stats_);

        // If we found a forced win/loss, no need to go deeper
        if (is_mate_score(score)) {
            if (fixed_depth == 0) break;  // unlimited mode: stop on mate
        }

        // If fixed depth reached, stop
        if (fixed_depth > 0 && depth >= fixed_depth) break;
    }

    return best_result;
}
