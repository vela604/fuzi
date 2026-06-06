#include <string>
#include <sstream>
#include "engine.h"
#include "bitboard.h"

static Engine g_engine(64);
static Position g_pos;
static std::string g_pv;
static int g_last_score = 0;
static int g_last_best = -1;
static int g_last_depth = 0;

extern "C" {

void reset_engine() {
    g_engine.reset();
    g_pos = Position{};
    g_pv.clear();
    g_last_score = 0;
    g_last_best = -1;
    g_last_depth = 0;
}

void set_position_from_moves(const char* moves) {
    g_pos = Position{};
    std::istringstream iss(moves ? moves : "");
    int col;
    while (iss >> col) {
        if (col >= 0 && col < COLS && g_pos.can_play(col)) {
            g_pos = g_pos.after(col);
        }
    }
    g_pv.clear();
    g_last_score = 0;
    g_last_best = -1;
    g_last_depth = 0;
}

int search_bestmove(int depth) {
    SearchResult r = g_engine.search(g_pos, depth);
    g_last_score = r.score;
    g_last_best = r.best_move;
    g_last_depth = r.depth;
    g_pv.clear();
    for (size_t i = 0; i < r.pv.size(); ++i) {
        if (i) g_pv += ' ';
        g_pv += std::to_string(r.pv[i]);
    }
    return g_last_best;
}

int search_score() {
    return g_last_score;
}

int search_depth() {
    return g_last_depth;
}

const char* get_pv_line() {
    return g_pv.c_str();
}

int side_to_move() {
    return g_pos.side_to_move();
}

}
