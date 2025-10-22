// ========================= src/core/Generator.cpp =========================
#include "Generator.hpp"
#include "Solver.hpp"
#include <algorithm>

namespace ws {

    Generator::Generator(Params p_, GenOptions opt_) :p(p_), opt(opt_) { rng.s = opt.seed ? opt.seed : 0xBADC0FFEEULL; }

    void Generator::setBase(const State& b) { base = b; }

    State Generator::createStartFromInitial(const InitialDistribution* initial) {
        // 템플릿 + startMixed => 템플릿 높이/기믹을 존중해 랜덤 채움으로 시작
        if (base && opt.startMixed && !initial) {
            return createRandomMixedFromHeights(*base);
        }

        State st = State::goal(p);
        if (base) { st = *base; }
        else {
            for (auto& b : st.B) { b.gimmick = {}; b.slots.clear(); }
            for (int c = 1; c <= p.numColors && c <= p.numBottles; ++c) {
                auto& b = st.B[c - 1]; b.capacity = p.capacity; b.slots.resize(p.capacity);
                for (int i = 0; i < p.capacity; ++i) b.slots[i] = Slot{ (Color)c,false };
            }
        }

        if (initial) {
            for (size_t i = 0; i < st.B.size() && i < initial->size(); ++i) {
                st.B[i].slots.clear();
                st.B[i].capacity = p.capacity;
                for (Color c : initial->at(i)) st.B[i].slots.push_back(Slot{ c,false });
            }
        }

        st.refreshLocks();
        return st;
    }

    void Generator::scramble(State& s, int& outMix) {
        // Reverse‑move scramble from goal‑like state: apply legal moves that are NOT immediately undoing previous
        int target = rng.irange(opt.mixMin, opt.mixMax);
        outMix = 0;
        Move last{ -1,-1,0 };
        for (int step = 0; step < target; ++step) {
            // collect legal moves
            std::vector<Move> mv;
            for (int i = 0; i < (int)s.B.size(); ++i) {
                for (int j = 0; j < (int)s.B.size(); ++j) {
                    if (i == j) continue; int amount = 0; if (!s.canPour(i, j, &amount)) continue;
                    // avoid trivial undo: pouring back the same chunk immediately
                    if (last.from == j && last.to == i) continue;
                    // prefer moves that create mixed stacks
                    mv.push_back(Move{ i,j,amount });
                }
            }
            if (mv.empty()) break;
            auto m = mv[rng.irange(0, (int)mv.size() - 1)];
            s.apply(m);
            last = m; ++outMix;
        }
    }

    bool Generator::placeGimmicksRespecting(const State& sIn, State& out) {
        // If base exists, we already have gimmicks set. Otherwise we keep none here; GUI will set.
        out = sIn;
        out.refreshLocks();
        return true;
    }

    std::optional<Generated> Generator::makeOne(const InitialDistribution* initial) {
        for (int tries = 0; tries < opt.gimmickPlacementTries; ++tries) {
            State s = createStartFromInitial(initial);
            int mix = 0;

            // NEW: startMixed면 별도의 scramble 불필요
            if (!opt.startMixed) {
                scramble(s, mix);
            }
            else {
                mix = s.p.numColors * s.p.capacity; // 대충 섞임 강도 표기로 사용
            }

            Solver solver(opt.solveTimeMs);
            auto res = solver.solve(s);
            if (res.solved) {
                Generated g; g.state = s; g.mixCount = mix; g.minMoves = res.minMoves;
                g.diffScore = solver.estimateDifficulty(s, res.minMoves);
                g.diffLabel = labelForScore(g.diffScore);
                return g;
            }
            // 실패 시 다음 시도
        }
        return std::nullopt;
    }

    State Generator::createRandomMixed() {
        State st; st.p = p; st.B.resize(p.numBottles);
        for (auto& b : st.B) b.capacity = p.capacity;

        // 기믹 유지(있다면)
        if (base) {
            for (size_t i = 0; i < st.B.size() && i < base->B.size(); ++i)
                st.B[i].gimmick = base->B[i].gimmick;
        }

        // 채울 병 개수 = 전체 - 예약 비병 수 (최소 1 보장)
        int fillable = std::max(1, p.numBottles - std::max(0, opt.reservedEmpty));
        fillable = std::min(fillable, p.numBottles);

        // 색상 “봉투” 만들기 (각 색 p.capacity개씩)
        std::vector<Color> bag; bag.reserve(p.numColors * p.capacity);
        for (int c = 1; c <= p.numColors; ++c)
            for (int k = 0; k < p.capacity; ++k) bag.push_back((Color)c);

        // 랜덤 셔플
        for (size_t i = 0; i < bag.size(); ++i) {
            size_t j = size_t(rng.irange(0, (int)bag.size() - 1));
            std::swap(bag[i], bag[j]);
        }

        auto runlen = [](const Bottle& b, Color c) {
            int len = 0; for (int i = (int)b.slots.size() - 1; i >= 0; --i) { if (b.slots[i].c == c) ++len; else break; }
            return len;
            };

        // bag의 색들을 무작위 병에 배치 (같은색 연속 길이 제한으로 “섞임” 유지)
        for (Color c : bag) {
            bool placed = false;
            for (int tries = 0; tries < 64 && !placed; ++tries) {
                int bi = rng.irange(0, fillable - 1);
                auto& b = st.B[bi];
                if (b.size() < b.capacity) {
                    if (opt.maxRunPerBottle <= 0 || runlen(b, c) < opt.maxRunPerBottle) {
                        b.slots.push_back(Slot{ c,false });
                        placed = true;
                    }
                }
            }
            if (!placed) {
                // 후방대체: 아무 병에나 넣기
                for (int bi = 0; bi < fillable; ++bi) {
                    auto& b = st.B[bi];
                    if (b.size() < b.capacity) { b.slots.push_back(Slot{ c,false }); break; }
                }
            }
        }

        st.refreshLocks();
        return st;
    }

    State Generator::createRandomMixedFromHeights(const State& baseTpl) {
        State st; st.p = p; st.B.resize(p.numBottles);
        for (size_t i = 0; i < st.B.size(); ++i) {
            st.B[i].capacity = p.capacity;
            st.B[i].gimmick = (i < baseTpl.B.size() ? baseTpl.B[i].gimmick : StackGimmick{});
        }

        // 템플릿의 목표 높이 수집
        std::vector<int> heights(p.numBottles, 0); long long sum = 0;
        for (size_t i = 0; i < heights.size() && i < baseTpl.B.size(); ++i) {
            heights[i] = std::min((int)baseTpl.B[i].slots.size(), p.capacity);
            sum += heights[i];
        }
        long long expected = 1ll * p.numColors * p.capacity;
        if (sum != expected) {
            // 합이 안 맞으면 왼쪽부터 expected만큼 채우는 안전한 폴백
            long long need = expected;
            for (int i = 0; i < p.numBottles; ++i) {
                int take = (int)std::min<long long>(need, p.capacity);
                heights[i] = take; need -= take;
            }
        }

        // 색상 토큰 가방 만들기 (각 색 capacity개씩)
        std::vector<Color> bag; bag.reserve((size_t)expected);
        for (int c = 1; c <= p.numColors; ++c)
            for (int k = 0; k < p.capacity; ++k) bag.push_back((Color)c);

        // 셔플
        for (size_t i = 0; i < bag.size(); ++i) {
            size_t j = size_t(rng.irange(0, (int)bag.size() - 1));
            std::swap(bag[i], bag[j]);
        }

        auto runlen = [](const Bottle& b, Color c) {
            int len = 0; for (int i = (int)b.slots.size() - 1; i >= 0; --i) { if (b.slots[i].c == c) ++len; else break; }
            return len;
            };

        // 목표 높이만큼 각 병에 랜덤 채움(같은 색 연속 길이 제한으로 섞임 유지)
        for (Color c : bag) {
            bool placed = false;
            for (int tries = 0; tries < 64 && !placed; ++tries) {
                int bi = rng.irange(0, p.numBottles - 1);
                auto& b = st.B[bi];
                if (b.size() < heights[bi]) {
                    if (opt.maxRunPerBottle <= 0 || runlen(b, c) < opt.maxRunPerBottle) {
                        b.slots.push_back(Slot{ c,false }); placed = true;
                    }
                }
            }
            if (!placed) {
                for (int bi = 0; bi < p.numBottles; ++bi) {
                    auto& b = st.B[bi];
                    if (b.size() < heights[bi]) { b.slots.push_back(Slot{ c,false }); break; }
                }
            }
        }

        st.refreshLocks();
        return st;
    }

} // namespace ws