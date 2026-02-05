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
        std::vector<Move> solutionMoves; // one optimal solution path (may be empty if unsolved)
        struct DifficultyBreakdown {
            double moveComponent{ 0.0 };
            double heuristicComponent{ 0.0 };
            double fragmentationComponent{ 0.0 };
            double hiddenComponent{ 0.0 };
            double gimmickComponent{ 0.0 };
            double colorComponent{ 0.0 };
            double solutionComponent{ 0.0 };
            double totalScore{ 0.0 };
        } difficulty;
    };

    class Solver {
    public:
        explicit Solver(int timeBudgetMs = 2000) :budgetMs(timeBudgetMs) {}
        SolveResult solve(const State& start);
        double estimateDifficulty(const State& s, SolveResult& solveStats) const;
    private:
        int budgetMs{ 2000 };
    };

} // namespace ws
