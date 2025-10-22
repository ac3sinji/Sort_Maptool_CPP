// ========================= src/core/Generator.hpp =========================
#pragma once
#include "State.hpp"
#include <optional>

namespace ws {

    struct GenOptions {
        int mixMin{ 60 };
        int mixMax{ 180 };
        uint64_t seed{ 0xA17C3B5ECAFEBEEFULL };
        int gimmickPlacementTries{ 30 };
        int solveTimeMs{ 2500 }; // validation solver budget per attempt

        // NEW: start mixed initial state
        bool startMixed{ true };      // 섞인 상태로 시작(기본 true)
        int  reservedEmpty{ 2 };      // 초기 상태에서 비워둘 병 개수(일반적으로 2)
        int  maxRunPerBottle{ 2 };    // 한 병 안에서 같은 색이 연속으로 허용되는 최대 길이(섞임 유지)
    };

    struct Generated { State state; int mixCount{ 0 }; int minMoves{ -1 }; double diffScore{ 0.0 }; std::string diffLabel; };

    // If initialDistribution is provided, it overrides the default goal distribution.
    // The counts MUST sum to numColors*capacity, and each bottle vector has bottom->top colors (0 means empty cell at bottom is not stored; provide exact heights).
    using InitialDistribution = std::vector<std::vector<Color>>; // size=bottles, each is a stack bottom->top

    class Generator {
    public:
        Generator(Params p, GenOptions opt);

        // Generate one solvable map honoring existing bottle gimmicks in p/B (if provided via setBase)
        std::optional<Generated> makeOne(const InitialDistribution* initial = nullptr);

        // Attach current base state (with bottle gimmicks already set from UI). If not set, defaults used.
        void setBase(const State& base);

    private:
        Params p; GenOptions opt; RNG rng; std::optional<State> base;

        State createStartFromInitial(const InitialDistribution* initial);
        void scramble(State& s, int& outMix);
        bool placeGimmicksRespecting(const State& sIn, State& out);
        State createRandomMixed();  // NEW
        State createRandomMixedFromHeights(const State& baseTpl); // NEW
        struct SupportSpec { int bottle{ -1 }; Color color{ 0 }; };
        std::vector<int> computeDefaultHeights() const;
        std::vector<int> computeHeightsFromTemplate(const State& baseTpl) const;
        std::vector<SupportSpec> buildSupportPlan(const std::vector<int>& heights) const;
        State createRandomMixedWithHeights(const std::vector<int>& heights);
        bool hasAnyMove(const State& s) const;
        void fixClothStart(State& st);                  // Cloth 병 안의 타깃색을 마지막에 제거(스왑)하는 안전망
    };

} // namespace ws