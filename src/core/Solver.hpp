// ========================= src/core/Solver.hpp =========================
#pragma once
#include "State.hpp"
#include <optional>

namespace ws {

    struct SolveResult {
        bool solved{ false };
        bool timedOut{ false };
        int minMoves{ -1 };              // best-known optimal move count (exact when solved==true)
        int distinctSolutions{ 0 };      // number of distinct optimal solutions discovered (capped)
        bool solutionCountExhaustive{ false }; // true if the optimal-solution count search finished exhaustively
        bool solutionCountLimited{ false };    // true if counting stopped after hitting the sampling cap
    };

    class Solver {
    public:
        explicit Solver(int timeBudgetMs = 2000) :budgetMs(timeBudgetMs) {}
        SolveResult solve(const State& start);
        double estimateDifficulty(const State& s, const SolveResult& solveStats) const;
    private:
        int budgetMs{ 2000 };
    };

} // namespace ws