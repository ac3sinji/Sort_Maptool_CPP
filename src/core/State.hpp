// ========================= src/core/State.hpp =========================
#pragma once
#include "Types.hpp"
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <chrono>

namespace ws {

    struct Locks {
        // dynamic locks derived from gimmicks
        // bushActive[i] = true when bush on bottle i is still locking usage
        std::vector<bool> bushLocked; // true until neighbor complete
        std::vector<bool> clothLocked; // true until target color completed elsewhere
    };

    struct State {
        Params p;
        std::vector<Bottle> B; // size = p.numBottles

        // Derived status for gimmicks runtime
        Locks locks;

        // initialization helpers
        static State goal(const Params& p);

        // move legality considering gimmicks
        bool canPour(int from, int to, int* outAmount = nullptr) const;
        void apply(const Move& m);

        bool isSolved() const;

        // recompute locks (cloth/bush)
        void refreshLocks();

        // util
        size_t hash() const; // Zobrist‑style cheap hash
    };

    struct RNG { uint64_t s = 0x9E3779B97F4A7C15ULL; uint64_t next(); int irange(int lo, int hi); };

} // namespace ws