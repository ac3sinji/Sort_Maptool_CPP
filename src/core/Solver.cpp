// ========================= src/core/Solver.cpp =========================
#include "Solver.hpp"
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <cmath>

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
        // Compose from heuristic features with softer contribution from gimmicks.
        const int colors = s.p.numColors;
        const int bottles = static_cast<int>(s.B.size());

        // Base move pressure – emphasise longer optimal routes but with diminishing returns.
        const double moveDepth = std::max(0, minMoves);
        const double moveComponent = std::min(45.0, std::pow(moveDepth + 1.0, 1.1) * 1.35);

        // Structural complexity derived from the IDA* heuristic (fragmentation, blocking, etc.).
        const int h0 = heuristic(s);
        const double heuristicComponent = std::min(25.0, std::pow(static_cast<double>(std::max(0, h0)), 1.2) * 1.6);

        // Count fragmentation and hidden information.
        double fragmentation = 0.0;
        int hiddenSlots = 0;
        int emptyBottles = 0;
        for (const auto& b : s.B) {
            if (b.isEmpty()) { ++emptyBottles; continue; }
            Color prev = 0;
            int groups = 0;
            for (const auto& sl : b.slots) {
                if (sl.hidden) ++hiddenSlots;
                if (sl.c == 0) continue;
                if (sl.c != prev) {
                    ++groups;
                    prev = sl.c;
                }
            }
            if (groups > 1) fragmentation += static_cast<double>(groups - 1);
        }
        const double fragmentationComponent = std::min(15.0, fragmentation * 1.25);
        const double hiddenComponent = std::min(8.0, hiddenSlots * 0.7);

        // Evaluate gimmick intensity. Weight each gimmick by type and fill state, then saturate.
        double gimmickWeight = 0.0;
        for (const auto& b : s.B) {
            if (b.gimmick.kind == StackGimmickKind::None) continue;
            double weight = 1.0;
            switch (b.gimmick.kind) {
            case StackGimmickKind::Cloth: weight = 0.6; break; // light constraint
            case StackGimmickKind::Vine:  weight = 1.0; break; // medium constraint
            case StackGimmickKind::Bush:  weight = 1.3; break; // heavy constraint
            default: break;
            }
            const double fillRatio = b.capacity > 0 ? static_cast<double>(b.size()) / b.capacity : 0.0;
            // Gimmicks on mostly empty bottles contribute less to difficulty.
            weight *= 0.5 + std::min(1.0, fillRatio) * 0.5;
            gimmickWeight += weight;
        }
        const double normalizedGimmickPressure = bottles > 0 ? gimmickWeight / bottles : 0.0;
        double gimmickComponent = (1.0 - std::exp(-normalizedGimmickPressure * 2.8)) * 18.0;
        gimmickComponent -= std::min(5.0, static_cast<double>(emptyBottles)); // free space mitigates gimmicks
        if (gimmickComponent < 0.0) gimmickComponent = 0.0;

        // Additional subtle scaling by colour variety beyond the default palette.
        const double colorComponent = std::min(7.0, std::max(0, colors - 5) * 1.2);

        double score = moveComponent + heuristicComponent + fragmentationComponent + hiddenComponent + gimmickComponent + colorComponent;

        if (score < 0.0) score = 0.0;
        if (score > 100.0) score = 100.0;
        return score;
    }

} // namespace ws