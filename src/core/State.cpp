// ========================= src/core/State.cpp =========================
#include "State.hpp"
#include <random>
#include <numeric>

namespace ws {

    static uint64_t rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
    uint64_t RNG::next() { s ^= rotl(s, 7); s ^= (s >> 9); return s * 0x9E3779B97F4A7C15ULL; }
    int RNG::irange(int lo, int hi) { return lo + int(next() % uint64_t(hi - lo + 1)); }

    State State::goal(const Params& p) {
        State st; st.p = p; st.B.resize(p.numBottles);
        for (auto& b : st.B) { b.capacity = p.capacity; }
        // First numColors bottles are mono‑full 1..N; rest empty
        for (int c = 1; c <= p.numColors; ++c) {
            auto& b = st.B[c - 1];
            b.slots.resize(p.capacity);
            for (int i = 0; i < p.capacity; ++i) b.slots[i] = Slot{ (Color)c,false };
        }
        st.refreshLocks();
        return st;
    }

    void State::refreshLocks() {
        locks.bushLocked.assign(B.size(), false);
        locks.clothLocked.assign(B.size(), false);

        // Precompute which colors are already completed in some bottle
        std::array<bool, 21> colorCompleted{}; // colors 1..20
        colorCompleted.fill(false);
        for (size_t i = 0; i < B.size(); ++i) {
            if (B[i].isMonoFull()) colorCompleted[B[i].slots[0].c] = true;
        }

        for (size_t i = 0; i < B.size(); ++i) {
            const auto& g = B[i].gimmick;
            if (g.kind == StackGimmickKind::Cloth) {
                if (g.clothTarget >= 1 && g.clothTarget <= 20) {
                    locks.clothLocked[i] = !colorCompleted[g.clothTarget];
                }
            }
            else if (g.kind == StackGimmickKind::Bush) {
                bool leftOk = (i > 0 && B[i - 1].isMonoFull());
                bool rightOk = (i + 1 < B.size() && B[i + 1].isMonoFull());
                locks.bushLocked[i] = !(leftOk || rightOk);
            }
        }
    }

    bool State::canPour(int from, int to, int* outAmount) const {
        if (from == to || from < 0 || to < 0 || from >= (int)B.size() || to >= (int)B.size()) return false;
        const auto& bf = B[from];
        const auto& bt = B[to];

        // Vine: cannot pour OUT of a vine bottle
        if (bf.gimmick.kind == StackGimmickKind::Vine) return false;

        // Cloth / Bush: if locked, cannot use this bottle at all (no in/out)
        if ((B[from].gimmick.kind == StackGimmickKind::Cloth && locks.clothLocked[from]) ||
            (B[from].gimmick.kind == StackGimmickKind::Bush && locks.bushLocked[from])) return false;
        if ((B[to].gimmick.kind == StackGimmickKind::Cloth && locks.clothLocked[to]) ||
            (B[to].gimmick.kind == StackGimmickKind::Bush && locks.bushLocked[to])) return false;

        if (bf.slots.empty()) return false;
        if (bt.size() >= bt.capacity) return false;

        Color tcol = bf.topColor();
        if (tcol == 0) return false;

        // '?' reveal rule: color reveals when it becomes the top (already true here)
        // (No special restriction for moving; UI may hide it from the player.)

        Color destTop = bt.topColor();
        if (destTop != 0 && destTop != tcol) return false;

        int chunk = bf.topChunk();
        int free = bt.capacity - bt.size();
        int mv = std::min(chunk, free);
        if (mv <= 0) return false;
        if (outAmount) *outAmount = mv;
        return true;
    }

    void State::apply(const Move& m) {
        if (m.from < 0 || m.to < 0) return;
        auto& f = B[m.from];
        auto& t = B[m.to];
        int amount = m.amount;
        if (amount <= 0) {
            int calc = 0; if (!canPour(m.from, m.to, &calc)) return; amount = calc;
        }
        Color col = f.topColor();
        for (int i = 0; i < amount; ++i) {
            auto s = f.slots.back();
            s.hidden = false; // when leaving source top, it’s revealed already
            t.slots.push_back(s);
            f.slots.pop_back();
        }
        // After move, revealing rule: if new top of any bottle has hidden=true and is now at top, it becomes visible
        if (!f.slots.empty()) f.slots.back().hidden = false;
        if (!t.slots.empty()) t.slots.back().hidden = false;

        // update locks (mono full may have changed)
        refreshLocks();
    }

    bool State::isSolved() const {
        for (const auto& b : B) { if (!b.slots.empty() && !b.isMonoFull()) return false; }
        return true;
    }

    size_t State::hash() const {
        // Simple rolling hash; good enough for pruning
        uint64_t h = 1469598103934665603ull;
        for (const auto& b : B) {
            h ^= uint64_t(b.capacity) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            for (const auto& s : b.slots) {
                uint64_t v = (uint64_t(s.c) << 1) ^ (s.hidden ? 0xdeadbeef : 0x12345678);
                h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            }
            // gimmick
            h ^= (uint64_t)b.gimmick.kind;
            h ^= (uint64_t)(b.gimmick.clothTarget << 32);
        }
        return size_t(h);
    }

} // namespace ws