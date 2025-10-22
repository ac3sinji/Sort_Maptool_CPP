// ========================= src/core/Solver.hpp =========================
#pragma once
#include "State.hpp"
#include <optional>

namespace ws {

    struct SolveResult { bool solved{ false }; int minMoves{ -1 }; };

    class Solver {
    public:
        explicit Solver(int timeBudgetMs = 2000) :budgetMs(timeBudgetMs) {}
        SolveResult solve(const State& start);
        double estimateDifficulty(const State& s, int minMoves) const;
    private:
        int budgetMs{ 2000 };
    };

} // namespace ws