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

    static State normalizeForSolve(const State& input) {
        State normalized = input;
        for (auto& bottle : normalized.B) {
            for (auto& slot : bottle.slots) {
                slot.hidden = false;
            }
        }
        normalized.refreshLocks();
        return normalized;
    }

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

    struct SolutionCountResult {
        int count{ 0 };
        bool exhaustive{ false };
        bool timedOut{ false };
        bool limitHit{ false };
    };

    static SolutionCountResult countMinimalSolutions(const State& start, int depthLimit, int maxCount, const std::function<bool()>& timeOk) {
        SolutionCountResult result;
        if (depthLimit < 0) {
            result.exhaustive = true;
            return result;
        }

        std::unordered_map<size_t, int> bestDepth;
        bestDepth.reserve(4096);
        bestDepth[start.hash()] = 0;

        std::function<void(const State&, int)> dfs = [&](const State& cur, int depth) {
            if (result.timedOut || result.limitHit) return;
            if (!timeOk()) { result.timedOut = true; return; }

            if (cur.isSolved()) {
                if (depth <= depthLimit) {
                    ++result.count;
                    if (result.count >= maxCount) {
                        result.limitHit = true;
                    }
                }
                return;
            }

            if (depth >= depthLimit) return;

            struct Candidate { Move m; bool prefer; };
            std::vector<Candidate> cand;
            cand.reserve(cur.B.size() * cur.B.size());

            for (int i = 0; i < (int)cur.B.size(); ++i) {
                for (int j = 0; j < (int)cur.B.size(); ++j) {
                    if (i == j) continue;
                    int amt = 0;
                    if (!cur.canPour(i, j, &amt)) continue;
                    bool prefer = !cur.B[j].isEmpty() && cur.B[i].topColor() == cur.B[j].topColor();
                    cand.push_back({ Move{i,j,amt}, prefer });
                }
            }

            std::stable_sort(cand.begin(), cand.end(), [](const Candidate& a, const Candidate& b) {
                return a.prefer > b.prefer;
                });

            for (const auto& c : cand) {
                State next = cur;
                next.apply(c.m);
                size_t h = next.hash();
                auto it = bestDepth.find(h);
                if (it != bestDepth.end() && it->second <= depth + 1) continue;
                bestDepth[h] = depth + 1;
                dfs(next, depth + 1);
                if (result.timedOut || result.limitHit) return;
            }
            };

        dfs(start, 0);
        result.exhaustive = !result.timedOut && !result.limitHit;
        return result;
    }

    SolveResult Solver::solve(const State& start) {
        using clock = std::chrono::steady_clock;
        auto t0 = clock::now();
        const State solveStart = normalizeForSolve(start);

        SolveResult result;
        std::vector<Move> path;
        std::vector<Move> solutionMoves;
        bool foundPath = false;

        if (solveStart.isSolved()) {
            result.solved = true;
            result.minMoves = 0;
            result.distinctSolutions = 1;
            result.solutionCountExhaustive = true;
            return result;
        }

        int bound = heuristic(solveStart);

        auto timeOk = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count() < budgetMs; };

        // IDA* search
        std::unordered_set<size_t> visited;
        bool searchTimedOut = false;
        int solvedDepth = -1;

        std::function<int(const State&, int, int)> dfs = [&](const State& s, int g, int boundVal) {
            if (!timeOk()) { searchTimedOut = true; return std::numeric_limits<int>::max(); }

            int f = g + heuristic(s);
            if (f > boundVal) return f;
            if (s.isSolved()) {
                if (!foundPath) {
                    solutionMoves = path;
                    foundPath = true;
                }
                return -g; // found, return negative depth
            }

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
                path.push_back(c.m);
                int t = dfs(s2, g + 1, boundVal);
                if (!path.empty()) path.pop_back();
                if (t < 0) return t; // solved at depth g'
                if (t < minNext) minNext = t;
                if (searchTimedOut) break;
            }
            return minNext;
        };

        while (true) {
            if (!timeOk()) { searchTimedOut = true; break; }
            visited.clear();
            int t = dfs(solveStart, 0, bound);
            if (t < 0) {
                solvedDepth = -t;
                result.solved = true;
                break;
            }
            if (searchTimedOut || t == std::numeric_limits<int>::max()) {
                searchTimedOut = true;
                break;
            }
            bound = t;
        }

        if (!result.solved) {
            result.timedOut = searchTimedOut;
            result.minMoves = bound;
            return result;
        }

        result.minMoves = solvedDepth;
        result.solutionMoves = std::move(solutionMoves);
        result.distinctSolutions = 1;

        if (!timeOk()) {
            result.timedOut = true;
            return result;
        }

        const int solutionSampleLimit = 4;
        auto countStats = countMinimalSolutions(solveStart, solvedDepth, solutionSampleLimit, timeOk);
        if (countStats.timedOut) {
            result.timedOut = true;
        }
        if (countStats.count > 0) {
            result.distinctSolutions = countStats.count;
        }
        result.solutionCountExhaustive = countStats.exhaustive;
        result.solutionCountLimited = countStats.limitHit;
        if (!result.solutionCountExhaustive) {
            // ensure we report at least one known optimal route
            result.distinctSolutions = std::max(1, result.distinctSolutions);
        }
        if (!timeOk()) {
            result.timedOut = true;
        }

        return result;
    }

    double Solver::estimateDifficulty(const State& s, SolveResult& solveStats) const {
        const int minMoves = solveStats.minMoves;
        // Compose from heuristic features with softer contribution from gimmicks.
        const int colors = s.p.numColors;
        const int bottles = static_cast<int>(s.B.size());

        // Base move pressure: compare against puzzle scale so short solutions stay low.
        const double moveDepth = std::max(0, minMoves);
        const double totalCells = static_cast<double>(colors * s.p.capacity);
        const double expectedMoves = std::max(1.0, totalCells * 1.1);
        const double moveRatio = moveDepth / expectedMoves;
        const double moveComponent = std::clamp(std::pow(std::max(0.0, moveRatio), 1.35) * 40.0, 0.0, 45.0);

        // Structural complexity derived from the IDA* heuristic (fragmentation, blocking, etc.).
        const int h0 = heuristic(s);
        const double heuristicComponent = std::min(18.0, std::pow(static_cast<double>(std::max(0, h0)), 1.12) * 1.15);

        // Count fragmentation and hidden information.
        double fragmentation = 0.0;
        int hiddenSlots = 0;
        int hiddenBottles = 0;
        int emptyBottles = 0;
        int monoFullBottles = 0;
        double effectiveHiddenGroups = 0.0;
        for (const auto& b : s.B) {
            if (b.isEmpty()) { ++emptyBottles; continue; }
            if (b.isMonoFull()) { ++monoFullBottles; }
            Color prev = 0;
            int groups = 0;
            bool hasHidden = false;
            int bottleHiddenCount = 0;
            bool hasKnownColor = false;
            bool monoKnownColor = true;
            Color firstKnownColor = 0;
            for (const auto& sl : b.slots) {
                if (sl.hidden) {
                    ++hiddenSlots;
                    hasHidden = true;
                    ++bottleHiddenCount;
                }
                if (sl.c == 0) continue;
                if (!hasKnownColor) {
                    hasKnownColor = true;
                    firstKnownColor = sl.c;
                }
                else if (sl.c != firstKnownColor) {
                    monoKnownColor = false;
                }
                if (sl.c != prev) {
                    ++groups;
                    prev = sl.c;
                }
            }
            if (hasHidden) {
                ++hiddenBottles;
                // C-model approximation: hidden slots in a mono-color bottle are partially redundant.
                if (bottleHiddenCount == 1) {
                    effectiveHiddenGroups += 1.0;
                }
                else {
                    const double extraWeight = (hasKnownColor && monoKnownColor) ? 0.35 : 0.6;
                    effectiveHiddenGroups += 1.0 + static_cast<double>(bottleHiddenCount - 1) * extraWeight;
                }
            }
            if (groups > 1) fragmentation += static_cast<double>(groups - 1);
        }
        const double fragmentationComponent = std::min(10.0, fragmentation * 0.9);

        // Hidden C-model: score by effective information groups instead of raw slot count.
        const double hiddenFree = 1.5;
        const double hiddenCap = 6.5;
        const double hiddenMaxScore = 8.0;
        double hiddenComponent = 0.0;
        if (effectiveHiddenGroups > hiddenFree) {
            if (effectiveHiddenGroups >= hiddenCap) {
                hiddenComponent = hiddenMaxScore;
            }
            else {
                double t = (effectiveHiddenGroups - hiddenFree) / (hiddenCap - hiddenFree);
                hiddenComponent = hiddenMaxScore * t;
            }
        }
        if (hiddenBottles >= 2) {
            const double hiddenBottlePressure = static_cast<double>(hiddenBottles - 1);
            const double hiddenBottleComponent = (std::exp(hiddenBottlePressure * 0.50) - 1.0) * 1.9;
            hiddenComponent = std::min(14.0, hiddenComponent + hiddenBottleComponent);
        }

        // Evaluate gimmick intensity. Weight each gimmick by type and fill state, then saturate.
        double gimmickWeight = 0.0;
        int gimmickCount = 0;
        for (const auto& b : s.B) {
            if (b.gimmick.kind == StackGimmickKind::None) continue;
            ++gimmickCount;
            double weight = 1.0;
            switch (b.gimmick.kind) {
            case StackGimmickKind::Cloth: weight = 0.70; break; // medium-light constraint
            case StackGimmickKind::Vine:  weight = 1.00; break; // baseline constraint
            case StackGimmickKind::Bush:  weight = 0.85; break; // medium constraint
            default: break;
            }
            const double fillRatio = b.capacity > 0 ? static_cast<double>(b.size()) / b.capacity : 0.0;
            // Gimmicks on mostly empty bottles contribute less to difficulty.
            weight *= 0.5 + std::min(1.0, fillRatio) * 0.5;
            gimmickWeight += weight;
        }
        const double normalizedGimmickPressure = bottles > 0 ? gimmickWeight / bottles : 0.0;
        const double adjustedGimmickPressure = std::pow(normalizedGimmickPressure, 1.12);
        // Keep gimmicks meaningful, but avoid over-labeling into Very Hard on otherwise manageable maps.
        double gimmickComponent = (1.0 - std::exp(-adjustedGimmickPressure * 3.4)) * 22.0;
        if (gimmickCount >= 1) gimmickComponent += 4.0;
        if (gimmickCount >= 2) gimmickComponent += 3.0;
        if (gimmickCount >= 3) gimmickComponent += 2.0;
        gimmickComponent -= std::min(1.5, static_cast<double>(emptyBottles) * 0.5); // free space mitigates gimmicks
        gimmickComponent = std::min(30.0, gimmickComponent);
        if (gimmickComponent < 0.0) gimmickComponent = 0.0;

        // Hidden+gimmick overlap correction: avoid over-scoring when both describe the same pressure.
        const double hiddenGimmickInteractionComponent = -0.45 * std::min(hiddenComponent, gimmickComponent);

        // Additional subtle scaling by colour variety beyond the default palette.
        const double colorComponent = std::min(7.0, std::max(0, colors - 5) * 1.2);

        // Extra relief for puzzles with more empty bottles (player flexibility).
        double emptyBottleComponent = 0.0;
        if (emptyBottles == 1) {
            emptyBottleComponent = -5.0;
        }
        else if (emptyBottles == 2) {
            emptyBottleComponent = -12.0;
        }
        else if (emptyBottles >= 3) {
            emptyBottleComponent = -22.0;
        }

        // Reward already-solved bottles to reflect player-perceived progress.
        const double solvedBottleComponent = -std::min(8.0, static_cast<double>(monoFullBottles) * 1.5);

        // Reward/punish based on how many optimal answers the puzzle offers.
        double solutionComponent = 0.0;
        if (solveStats.solved) {
            const int solutionCount = std::max(1, solveStats.distinctSolutions);
            if (solveStats.solutionCountExhaustive) {
                if (solutionCount == 1) {
                    solutionComponent = 6.0; // single-path puzzles feel tighter
                }
                else if (solutionCount == 2) {
                    solutionComponent = 2.5; // a couple of options still require planning
                }
                else {
                    solutionComponent = -4.0; // many optimal lines make the stage feel forgiving
                }
            }
            else {
                if (!solveStats.timedOut && solutionCount == 1 && !solveStats.solutionCountLimited) {
                    solutionComponent = 3.0; // likely unique but not fully proven
                }
                else if (solveStats.solutionCountLimited || solutionCount >= 3) {
                    solutionComponent = -3.0; // early saturation indicates abundance of answers
                }
            }
        }

        double score = moveComponent + heuristicComponent + fragmentationComponent + hiddenComponent
            + emptyBottleComponent + solvedBottleComponent + gimmickComponent + hiddenGimmickInteractionComponent + colorComponent + solutionComponent;

        if (score < 0.0) score = 0.0;
        if (score > 100.0) score = 100.0;
        if (emptyBottles >= 3 && score >= 25.0) {
            score = 24.9;
        }
        solveStats.difficulty.moveComponent = moveComponent;
        solveStats.difficulty.heuristicComponent = heuristicComponent;
        solveStats.difficulty.fragmentationComponent = fragmentationComponent;
        solveStats.difficulty.hiddenComponent = hiddenComponent;
        solveStats.difficulty.emptyBottleComponent = emptyBottleComponent;
        solveStats.difficulty.solvedBottleComponent = solvedBottleComponent;
        solveStats.difficulty.gimmickComponent = gimmickComponent;
        solveStats.difficulty.hiddenGimmickInteractionComponent = hiddenGimmickInteractionComponent;
        solveStats.difficulty.colorComponent = colorComponent;
        solveStats.difficulty.solutionComponent = solutionComponent;
        solveStats.difficulty.totalScore = score;
        return score;
    }

} // namespace ws
