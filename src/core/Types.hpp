// ========================= src/core/Types.hpp =========================
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <array>
#include <limits>

namespace ws {

    using Color = uint8_t;          // 0 = empty, 1..20 colors (we allow up to 20 here)

    struct Slot {
        Color c{ 0 };
        bool hidden{ false }; // true = '?' (question mark); reveals when this slot is at top
    };

    enum class StackGimmickKind : uint8_t { None = 0, Cloth = 1, Vine = 2, Bush = 3 };

    struct StackGimmick {
        StackGimmickKind kind{ StackGimmickKind::None };
        Color clothTarget{ 0 }; // only used when kind==Cloth (1..N)
    };

    struct Bottle {
        std::vector<Slot> slots;     // bottom -> top order
        int capacity{ 4 };             // 3..50
        StackGimmick gimmick{};

        int size() const { return static_cast<int>(slots.size()); }
        bool isFull() const { return size() >= capacity; }
        bool isEmpty() const { return slots.empty(); }

        Color topColor() const { return slots.empty() ? 0 : slots.back().c; }
        bool topHidden() const { return slots.empty() ? false : slots.back().hidden; }

        // count of contiguous same-color from top
        int topChunk() const {
            if (slots.empty()) return 0;
            Color t = slots.back().c;
            if (t == 0 || slots.back().hidden) return 0;
            int cnt = 0;
            for (int i = static_cast<int>(slots.size()) - 1; i >= 0; --i) {
                if (slots[i].hidden) break;
                if (slots[i].c == t) ++cnt; else break;
            }
            return cnt;
        }

        bool isMonoFull() const {
            if (size() != capacity || slots.empty()) return false;
            Color t = slots[0].c; if (t == 0) return false;
            for (auto& s : slots) if (s.c != t) return false; return true;
        }
    };

    struct Move { int from{ -1 }; int to{ -1 }; int amount{ 0 }; }; // amount = cells moved

    struct Params {
        int numColors{ 6 };     // 1..18 (your request: up to 18)
        int numBottles{ 8 };    // total stacks
        int capacity{ 4 };      // 3..50
    };

    // Difficulty label bands
    inline std::string labelForScore(double s) {
        if (s < 10) return "Very Easy";
        if (s < 25) return "Easy";
        if (s < 60) return "Normal";
        if (s < 72) return "Hard";
        return "Very Hard";
    }

} // namespace ws
