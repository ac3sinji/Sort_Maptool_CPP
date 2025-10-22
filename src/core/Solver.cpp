// ========================= src/core/Solver.cpp =========================
#include "Solver.hpp"
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <algorithm>

namespace ws {

    struct Node { State s; int g{ 0 }; };

    // Lightweight IDDFS with heuristic cutoff; transposition table prunes repeats.
    static int heuristic(const State& s) {
        // Heuristic: count bottles needing work + color fragmentation penalty
        int h = 0; int empty = 0;
        for (const auto& b : s.B) {
            if (b.slots.empty()) { ++empty; continue; }
            if (!b.isMonoFull()) {
                // number of color groups in bottle minus 1
                int groups = 0; Color prev = 0; for (auto& sl : b.slots) { if (sl.c != prev) { if (sl.c != 0) ++groups; prev = sl.c; } }
                h += std::max(1, groups - 1);
            }
        }
        h = std::max(0, h - std::min(2, empty));
        return h;
    }

    SolveResult Solver::solve(const State& start) {
        using clock = std::chrono::steady_clock;
        auto t0 = clock::now();

        if (start.isSolved()) return { true, 0 };

        int bound = heuristic(start);

        auto timeOk = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count() < budgetMs; };

        // IDA* search
        std::unordered_set<size_t> visited;
        std::vector<Move> moves;

        std::function<int(const State&, int, int)> dfs = [&](const State& s, int g, int bound) {
            int f = g + heuristic(s);
            if (f > bound) return f;
            if (s.isSolved()) return -g; // found, return negative depth
            if (!timeOk()) return std::numeric_limits<int>::max();

            size_t h = s.hash();
            if (visited.count(h)) return std::numeric_limits<int>::max();
            visited.insert(h);

            int minNext = std::numeric_limits<int>::max();
            // move ordering: try pours that match color first
            struct Cand { Move m; bool prefer; };
            std::vector<Cand> cand;
            for (int i = 0; i < (int)s.B.size(); ++i) {
                for (int j = 0; j < (int)s.B.size(); ++j) {
                    if (i == j) continue; int amt = 0; if (!s.canPour(i, j, &amt)) continue;
                    bool prefer = !s.B[j].isEmpty() && s.B[i].topColor() == s.B[j].topColor();
                    cand.push_back({ Move{i,j,amt},prefer });
                }
            }
            std::stable_sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) {return a.prefer > b.prefer; });

            for (const auto& c : cand) {
                State s2 = s; s2.apply(c.m);
                int t = dfs(s2, g + 1, bound);
                if (t < 0) return t; // solved at depth g'
                if (t < minNext) minNext = t;
                if (!timeOk()) break;
            }
            return minNext;
        };

        for (;;) {
            visited.clear();
            int t = dfs(start, 0, bound);
            if (t < 0) { return { true, -t }; }
            if (t == std::numeric_limits<int>::max() || !timeOk()) {
                // time budget exceeded; return best bound as estimate (not exact)
                return { false, bound };
            }
            bound = t;
        }
    }

    double Solver::estimateDifficulty(const State& s, int minMoves) const {
        // Compose from heuristic features
        int colors = s.p.numColors;
        int empties = 0; for (auto& b : s.B) if (b.isEmpty()) ++empties;
        int locks = 0; for (size_t i = 0; i < s.B.size(); ++i) { if (s.B[i].gimmick.kind != StackGimmickKind::None) ++locks; }
        int h0 = heuristic(s);

        double score = 0.0;
        score += minMoves * 1.6;
        score += h0 * 1.8;
        score += std::max(0, locks * 3 - empties * 1);
        score += std::max(0, colors - 5);

        // clamp to 0..100
        if (score < 0) score = 0; if (score > 100) score = 100;
        return score;
    }

} // namespace ws