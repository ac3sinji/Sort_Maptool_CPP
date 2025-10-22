// ========================= src/core/Generator.cpp =========================
#include "Generator.hpp"
#include "Solver.hpp"
#include <algorithm>
#include <limits>

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

            if (!hasAnyMove(s)) {
                continue;
                
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

    std::vector<int> Generator::computeDefaultHeights() const {
        std::vector<int> heights(p.numBottles, 0);
        int fillable = std::clamp(p.numBottles - std::max(0, opt.reservedEmpty), 1, p.numBottles);
        long long cells = 1ll * p.numColors * p.capacity;

        int idx = 0;
        for (; idx < fillable && cells > 0; ++idx) {
            int take = (int)std::min<long long>(p.capacity, cells);
            heights[idx] = take;
            cells -= take;
        }
        for (; idx < p.numBottles && cells > 0; ++idx) {
            int take = (int)std::min<long long>(p.capacity, cells);
            heights[idx] = take;
            cells -= take;
        }
        return heights;
    }

    std::vector<int> Generator::computeHeightsFromTemplate(const State& baseTpl) const {
        std::vector<int> heights(p.numBottles, 0);
        long long sum = 0;
        for (size_t i = 0; i < heights.size() && i < baseTpl.B.size(); ++i) {
            heights[i] = std::min((int)baseTpl.B[i].slots.size(), p.capacity);
            sum += heights[i];
        }
        long long expected = 1ll * p.numColors * p.capacity;
        if (sum != expected) {
            long long need = expected;
            for (int i = 0; i < p.numBottles && need > 0; ++i) {
                int take = (int)std::min<long long>(need, p.capacity);
                heights[i] = take;
                need -= take;
            }
        }
        return heights;
    }

    std::vector<Generator::SupportSpec> Generator::buildSupportPlan(const std::vector<int>& heights) const {
        std::vector<SupportSpec> plan;
        if (!base) return plan;
        std::vector<int> colorOwner(p.numColors + 1, -1);
        std::vector<bool> bottleReserved(p.numBottles, false);

        auto ensureColor = [&](Color col, int preferIndex, bool strict) -> int {
            if (col < 1 || col > p.numColors) return -1;
            if (colorOwner[col] >= 0) return colorOwner[col];
            auto tryAssign = [&](int idx) -> bool {
                if (idx < 0 || idx >= p.numBottles) return false;
                if (heights[idx] != p.capacity) return false;
                if (bottleReserved[idx]) return false;
                bottleReserved[idx] = true;
                colorOwner[col] = idx;
                plan.push_back({ idx, col });
                return true;
            };
            if (tryAssign(preferIndex)) return colorOwner[col];
            if (strict) return -1;
            if (tryAssign((int)col - 1)) return colorOwner[col];
            for (int idx = 0; idx < p.numBottles; ++idx) {
                if (tryAssign(idx)) return colorOwner[col];
            }
            return -1;
        };

        auto pickUnusedColor = [&]() -> Color {
            for (Color c = 1; c <= p.numColors; ++c) {
                if (colorOwner[c] == -1) return c;
            }
            return 0;
            };

        for (size_t i = 0; i < base->B.size() && i < heights.size(); ++i) {
            const auto& gimmick = base->B[i].gimmick;
            if (gimmick.kind == StackGimmickKind::Cloth) {
                ensureColor(gimmick.clothTarget, (int)i, false);
            }
        }
        auto satisfyBush = [&](int idx) -> bool {
            if (idx < 0 || idx >= p.numBottles) return false;
            if (heights[idx] != p.capacity) return false;
            if (bottleReserved[idx]) return true;
            Color col = pickUnusedColor();
            if (col == 0) return false;
            return ensureColor(col, idx, true) >= 0;
            };

        for (size_t i = 0; i < base->B.size() && i < heights.size(); ++i) {
            const auto& gimmick = base->B[i].gimmick;
            if (gimmick.kind == StackGimmickKind::Bush) {
                bool ok = false;
                if (i > 0) ok = satisfyBush((int)i - 1);
                if (!ok && i + 1 < base->B.size()) satisfyBush((int)i + 1);
            }
        }

        return plan;
    }

    State Generator::createRandomMixedWithHeights(const std::vector<int>& heights) {
        auto attemptBuild = [&](State& st) {
            st = State{}; st.p = p; st.B.resize(p.numBottles);
            for (size_t i = 0; i < st.B.size(); ++i) {
                st.B[i].capacity = p.capacity;
                if (base && i < base->B.size()) st.B[i].gimmick = base->B[i].gimmick;
            }

            auto plan = buildSupportPlan(heights);
            std::vector<int> remaining(p.numColors + 1, p.capacity);
            std::vector<Color> reservedColor(p.numBottles, 0);
            std::vector<int> reservedCount(p.numBottles, 0);
            std::vector<int> reservedLimit(p.numBottles, std::numeric_limits<int>::max());

            for (const auto& spec : plan) {
                if (spec.bottle < 0 || spec.bottle >= p.numBottles) continue;
                if (spec.color < 1 || spec.color > p.numColors) continue;
                int target = heights[spec.bottle];
                if (target <= 0) continue;
                int available = remaining[spec.color];
                if (available <= 0) continue;

                auto& b = st.B[spec.bottle];
                b.slots.clear();

                int maxAssignable = (target > 0) ? 1 : 0;
                int assign = std::min(maxAssignable, available);
                if (assign <= 0 && maxAssignable > 0 && available > 0) assign = 1;

                for (int i = 0; i < assign; ++i) b.slots.push_back(Slot{ spec.color,false });
                remaining[spec.color] -= assign;
                reservedColor[spec.bottle] = spec.color;
                reservedCount[spec.bottle] = assign;
                reservedLimit[spec.bottle] = maxAssignable;
            }

            std::vector<Color> bag;
            long long expected = 1ll * p.numColors * p.capacity;
            bag.reserve((size_t)expected);
            for (Color c = 1; c <= p.numColors; ++c) {
                for (int k = 0; k < remaining[c]; ++k) bag.push_back(c);
            }

            for (size_t i = 0; i < bag.size(); ++i) {
                size_t j = size_t(rng.irange(0, bag.empty() ? 0 : (int)bag.size() - 1));
                std::swap(bag[i], bag[j]);
            }

            auto runlen = [](const Bottle& b, Color c) {
                int len = 0; for (int i = (int)b.slots.size() - 1; i >= 0; --i) { if (b.slots[i].c == c) ++len; else break; }
                return len;
            };

            auto allowed = [&](int bi, Color c)->bool {
                const auto& B = st.B[bi];
                if ((int)B.slots.size() >= heights[bi]) return false;
                if (B.gimmick.kind == StackGimmickKind::Cloth && B.gimmick.clothTarget == c) return false;
                if (reservedColor[bi] == c && reservedCount[bi] >= reservedLimit[bi]) return false;
                if (opt.maxRunPerBottle > 0 && runlen(B, c) >= opt.maxRunPerBottle) return false;
                return true;
                };

            auto placeColor = [&](int bi, Color c) {
                st.B[bi].slots.push_back(Slot{ c,false });
                if (reservedColor[bi] == c) ++reservedCount[bi];
                };

            for (Color c : bag) {
                bool placed = false;
                for (int tries = 0; tries < 64 && !placed; ++tries) {
                    int bi = rng.irange(0, p.numBottles - 1);
                    if (allowed(bi, c)) {
                        placeColor(bi, c);
                        placed = true;
                    }
                }
                if (!placed) {
                    for (int bi = 0; bi < p.numBottles; ++bi) {
                        if (allowed(bi, c)) {
                            placeColor(bi, c);
                            placed = true;
                            break;
                        }
                    }
                }
                if (!placed) {
                    for (int bi = 0; bi < p.numBottles && !placed; ++bi) {
                        if ((int)st.B[bi].slots.size() >= heights[bi]) continue;
                        if (reservedColor[bi] == c && reservedCount[bi] >= reservedLimit[bi]) continue;
                        placeColor(bi, c);
                        placed = true;
                    }
                }
                if (!placed) {
                    for (int bi = 0; bi < p.numBottles; ++bi) {
                        if ((int)st.B[bi].slots.size() < heights[bi]) {
                            placeColor(bi, c);
                            break;
                        }
                    }
                }
            }
            for (size_t bi = 0; bi < st.B.size(); ++bi) {
                if (reservedColor[bi] == 0 || reservedCount[bi] == 0) continue;
                auto& slots = st.B[bi].slots;
                if (slots.empty()) continue;
                int fromIdx = -1;
                for (int idx = 0; idx < (int)slots.size(); ++idx) {
                    if (slots[idx].c == reservedColor[bi]) { fromIdx = idx; break; }
                }
                if (fromIdx >= 0 && slots.size() > 1) {
                    int toIdx = rng.irange(0, (int)slots.size() - 1);
                    if (toIdx != fromIdx) {
                        std::swap(slots[fromIdx], slots[toIdx]);
                    }
                }
            }
            fixClothStart(st);
        };

        auto hasMonoFull = [](const State& st) {
            for (const auto& b : st.B) {
                if (!b.isEmpty() && b.isMonoFull()) return true;
            }
            return false;
            };

        State candidate;
        const int maxAttempts = 64;
        for (int attempt = 0; attempt < maxAttempts; ++attempt) {
            attemptBuild(candidate);
            if (!hasMonoFull(candidate)) {
                candidate.refreshLocks();
                return candidate;
            }
        }
            // Fallback: perturb the last candidate to break pre-solved stacks.
            for (int iter = 0; iter < 3; ++iter) {
                if (!hasMonoFull(candidate)) break;
                breakPreSolvedStacks(candidate);
                fixClothStart(candidate);
            }
            candidate.refreshLocks();
            return candidate;
    }

    State Generator::createRandomMixed() {
        auto heights = computeDefaultHeights();
        return createRandomMixedWithHeights(heights);
    }

    State Generator::createRandomMixedFromHeights(const State& baseTpl) {
        auto heights = computeHeightsFromTemplate(baseTpl);
        return createRandomMixedWithHeights(heights);
    }

    bool Generator::hasAnyMove(const State& s) const {
        for (int i = 0; i < (int)s.B.size(); ++i) {
            for (int j = 0; j < (int)s.B.size(); ++j) {
                if (i == j) continue;
                if (s.canPour(i, j, nullptr)) return true;
            }
        }
        return false;
    }

    void Generator::breakPreSolvedStacks(State& st) {
        auto hasMonoFull = [](const Bottle& b) {
            return !b.isEmpty() && b.isMonoFull();
            };

        for (int iter = 0; iter < 8; ++iter) {
            bool changed = false;
            for (size_t i = 0; i < st.B.size(); ++i) {
                auto& mono = st.B[i];
                if (!hasMonoFull(mono)) continue;
                Color monoColor = mono.slots.empty() ? 0 : mono.slots[0].c;
                bool swapped = false;

                for (size_t j = 0; j < st.B.size() && !swapped; ++j) {
                    if (j == i) continue;
                    auto& other = st.B[j];
                    for (size_t idx = 0; idx < other.slots.size(); ++idx) {
                        if (other.slots[idx].c == monoColor) continue;
                        size_t monoIdx = (size_t)rng.irange(0, mono.size() - 1);
                        std::swap(mono.slots[monoIdx], other.slots[idx]);
                        if (!mono.isMonoFull() && !other.isMonoFull()) {
                            swapped = true;
                            changed = true;
                            break;
                        }
                        std::swap(mono.slots[monoIdx], other.slots[idx]);
                    }
                }

                if (!swapped) {
                    for (size_t j = 0; j < st.B.size(); ++j) {
                        if (j == i) continue;
                        auto& other = st.B[j];
                        if (other.slots.empty()) continue;
                        std::swap(mono.slots.back(), other.slots.back());
                        if (!mono.isMonoFull()) {
                            changed = true;
                            break;
                        }
                        std::swap(mono.slots.back(), other.slots.back());
                    }
                }
            }
            if (!changed) break;
        }
    }

    void Generator::fixClothStart(State& st) {
        for (auto& b : st.B) {
            if (b.gimmick.kind != StackGimmickKind::Cloth) continue;
            Color t = b.gimmick.clothTarget;
            if (t <= 0) continue;

            // Cloth 병 안에 타깃색이 있으면, 다른 병의 '타깃색이 아닌' 칸과 1회 스왑
            for (int i = 0; i < (int)b.slots.size(); ++i) {
                if (b.slots[i].c == t) {
                    bool swapped = false;
                    for (auto& d : st.B) {
                        if (&d == &b) continue;
                        for (int k = 0; k < (int)d.slots.size(); ++k) {
                            if (d.slots[k].c != t) {
                                std::swap(b.slots[i].c, d.slots[k].c);
                                swapped = true;
                                break;
                            }
                        }
                        if (swapped) break;
                    }
                }
            }
        }
    }

} // namespace ws