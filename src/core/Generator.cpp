// ========================= src/core/Generator.cpp =========================
#include "Generator.hpp"
#include "Solver.hpp"
#include <algorithm>
#include <limits>
#include <numeric>

namespace ws {

    Generator::Generator(Params p_, GenOptions opt_) :p(p_), opt(opt_) { rng.s = opt.seed ? opt.seed : 0xBADC0FFEEULL; }

    void Generator::setBase(const State& b) { base = b; }

    std::optional<State> Generator::buildRandomTemplate(int clothCount, int vineCount, int bushCount,
        int questionCount, int questionMaxPerBottle, std::string* reason) {
        auto setReason = [&](const std::string& msg) {
            if (reason) *reason = msg;
        };
        if (reason) reason->clear();

        if (clothCount < 0 || vineCount < 0 || bushCount < 0 || questionCount < 0 || questionMaxPerBottle < 0) {
            setReason("Counts must be non-negative.");
            return std::nullopt;
        }

        const int requested = clothCount + vineCount + bushCount;
        if (requested > p.numBottles) {
            setReason("Requested gimmicks exceed number of bottles.");
            return std::nullopt;
        }

        auto heights = opt.randomizeHeights ? computeRandomizedHeights() : computeDefaultHeights();
        std::vector<int> candidates;
        candidates.reserve(p.numBottles);
        for (int i = 0; i < p.numBottles; ++i) {
            int h = (i < (int)heights.size()) ? heights[i] : 0;
            if (h > 0) candidates.push_back(i);
        }

        int usableSlots = (int)candidates.size();
        int limit = usableSlots; // 랜덤 높이 배분으로 reserveEmpty를 초과해 채울 수 있으므로 실제 채워진 병 수를 기준으로 제한
        if (requested > limit) {
            std::string heightNote = opt.randomizeHeights ? " after random height allocation" : "";
            setReason("Not enough fillable bottles" + heightNote + " to satisfy requested gimmick counts (" +
                std::to_string(requested) + " requested, " + std::to_string(limit) + " available).");
            return std::nullopt;
        }
        long long sumH = 0; for (int h : heights) sumH += h;
        long long expected = 1ll * p.numColors * p.capacity;
        if (sumH != expected) {
            setReason("Template height sum must equal Colors*Capacity.");
            return std::nullopt;
        }

        std::vector<Color> bag;
        bag.reserve((size_t)expected);
        for (Color c = 1; c <= p.numColors; ++c) {
            for (int k = 0; k < p.capacity; ++k) bag.push_back(c);
        }
        for (size_t i = 0; i < bag.size(); ++i) {
            size_t j = (size_t)rng.irange((int)i, (int)bag.size() - 1);
            std::swap(bag[i], bag[j]);
        }

        for (size_t i = 0; i < candidates.size(); ++i) {
            size_t j = (size_t)rng.irange((int)i, (int)candidates.size() - 1);
            std::swap(candidates[i], candidates[j]);
        }

        State tpl; tpl.p = p; tpl.B.resize(p.numBottles);
        size_t pos = 0;
        for (int i = 0; i < p.numBottles; ++i) {
            tpl.B[i].capacity = p.capacity;
            auto h = (size_t)heights[i];
            tpl.B[i].slots.reserve(h);
            for (size_t k = 0; k < h && pos < bag.size(); ++k, ++pos) {
                tpl.B[i].slots.push_back(Slot{ bag[pos],false });
            }
        }

        auto assignOne = [&](StackGimmickKind kind) {
            if (candidates.empty()) return false;
            int idx = candidates.back();
            candidates.pop_back();
            auto& g = tpl.B[idx].gimmick;
            g.kind = kind;
            if (kind == StackGimmickKind::Cloth) {
                int target = rng.irange(1, std::max(1, p.numColors));
                g.clothTarget = (Color)target;
            }
            return true;
            };

        for (int i = 0; i < clothCount; ++i) {
            if (!assignOne(StackGimmickKind::Cloth)) {
                setReason("Unable to place all Cloth gimmicks.");
                return std::nullopt;
            }
        }
        for (int i = 0; i < vineCount; ++i) {
            if (!assignOne(StackGimmickKind::Vine)) {
                setReason("Unable to place all Vine gimmicks.");
                return std::nullopt;
            }
        }
        for (int i = 0; i < bushCount; ++i) {
            if (!assignOne(StackGimmickKind::Bush)) {
                setReason("Unable to place all Bush gimmicks.");
                return std::nullopt;
            }
        }

        for (auto& bottle : tpl.B) {
            if (bottle.gimmick.kind != StackGimmickKind::Vine) continue;
            if (bottle.slots.size() <= 1) continue;
            Color seed = bottle.slots.front().c;
            bool mono = true;
            for (const auto& s : bottle.slots) {
                if (s.c != seed) { mono = false; break; }
            }
            if (!mono) {
                for (auto& s : bottle.slots) {
                    s.c = seed;
                }
            }
        }

        const bool excludeTopSlots = true;
        std::vector<std::pair<int, int>> hideCandidates;
        hideCandidates.reserve((size_t)expected);
        int totalQuestionCapacity = 0;
        for (int bi = 0; bi < p.numBottles; ++bi) {
            const auto& b = tpl.B[bi];
            int bottleCapacity = (int)b.slots.size();
            if (excludeTopSlots && bottleCapacity > 0) {
                --bottleCapacity;
            }
            if (questionMaxPerBottle > 0) {
                bottleCapacity = std::min(bottleCapacity, questionMaxPerBottle);
            }
            bottleCapacity = std::max(0, bottleCapacity);
            totalQuestionCapacity += bottleCapacity;
            for (int si = 0; si < bottleCapacity; ++si) {
                hideCandidates.emplace_back(bi, si);
            }
        }
        if (questionCount > totalQuestionCapacity) {
            std::string policyNote = excludeTopSlots ? " (top slots excluded)" : "";
            setReason("Question count exceeds allowed capacity" + policyNote +
                " and per-bottle limit (requested " + std::to_string(questionCount) +
                ", allowed " + std::to_string(totalQuestionCapacity) + ").");
            return std::nullopt;
        }
        for (size_t i = 0; i < hideCandidates.size(); ++i) {
            size_t j = (size_t)rng.irange((int)i, (int)hideCandidates.size() - 1);
            std::swap(hideCandidates[i], hideCandidates[j]);
        }
        for (int i = 0; i < questionCount; ++i) {
            auto [bi, si] = hideCandidates[i];
            tpl.B[bi].slots[si].hidden = true;
        }

        tpl.refreshLocks();
        return tpl;
    }

    State Generator::createStartFromInitial(const InitialDistribution* initial) {
        // 템플릿 + startMixed => 템플릿 높이/기믹을 존중해 랜덤 채움으로 시작
        if (base && opt.startMixed && !initial) {
            return createRandomMixedFromHeights(*base);
        }

        // 템플릿 + startMixed OFF => 시작은 정렬(goal) 상태로 고정한다.
        // 즉, 기본적으로 "병 번호 == 색 번호" 배치에서 시작하고
        // scramble step을 진행할수록 섞이는 과정을 재생한다.
        if (base && !opt.startMixed && !initial) {
            State st = State::goal(p);
            for (size_t i = 0; i < st.B.size() && i < base->B.size(); ++i) {
                st.B[i].gimmick = base->B[i].gimmick;
                const auto& src = base->B[i].slots;
                auto& dst = st.B[i].slots;
                for (size_t k = 0; k < dst.size() && k < src.size(); ++k) {
                    dst[k].hidden = src[k].hidden;
                }
            }

            st.refreshLocks();
            return st;
        }

        // startMixed가 꺼져 있으면 기본 정렬(goal) 상태에서 시작한다.
        // 템플릿을 사용하는 경우에도 색 배치는 goal(병 번호=색 번호)로 맞추고,
        // 템플릿의 기믹/숨김 정보만 가져와 scramble 과정을 보여준다.
        State st = State::goal(p);
        if (base) {
            for (size_t i = 0; i < st.B.size() && i < base->B.size(); ++i) {
                st.B[i].gimmick = base->B[i].gimmick;
                const auto& src = base->B[i].slots;
                auto& dst = st.B[i].slots;
                for (size_t k = 0; k < dst.size() && k < src.size(); ++k) {
                    dst[k].hidden = src[k].hidden;
                }
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

    bool Generator::canPourForGeneration(const State& s, int from, int to, int* outAmount) const {
        if (from == to || from < 0 || to < 0 || from >= (int)s.B.size() || to >= (int)s.B.size()) return false;

        const auto& bf = s.B[from];
        const auto& bt = s.B[to];

        // Keep structural constraints aligned with gameplay rules.
        if (bf.gimmick.kind == StackGimmickKind::Vine) return false;
        if ((bf.gimmick.kind == StackGimmickKind::Cloth && s.locks.clothLocked[from]) ||
            (bf.gimmick.kind == StackGimmickKind::Bush && s.locks.bushLocked[from])) return false;
        if ((bt.gimmick.kind == StackGimmickKind::Cloth && s.locks.clothLocked[to]) ||
            (bt.gimmick.kind == StackGimmickKind::Bush && s.locks.bushLocked[to])) return false;

        if (bf.slots.empty()) return false;
        if (bt.size() >= bt.capacity) return false;

        Color tcol = bf.topColor();
        if (tcol == 0) return false;

        // Generation-only relaxation: ignore gameplay color-match check.
        int mv = std::min(bf.topChunk(), bt.capacity - bt.size());
        if (mv <= 0) return false;
        if (outAmount) *outAmount = mv;
        return true;
    }

    void Generator::scramble(State& s, int& outMix, std::vector<Move>* outSteps) {
        // Reverse‑move scramble from goal‑like state.
        // Scramble uses generation-specific pour rules, while solver/play keeps State::canPour.
        int target = rng.irange(opt.mixMin, opt.mixMax);
        outMix = 0;
        Move last{ -1,-1,0 };
        if (outSteps) outSteps->clear();
        for (int step = 0; step < target; ++step) {
            std::vector<Move> mv;
            for (int i = 0; i < (int)s.B.size(); ++i) {
                for (int j = 0; j < (int)s.B.size(); ++j) {
                    if (i == j) continue;
                    if (last.from == j && last.to == i) continue; // avoid immediate undo
                    int amount = 0;
                    if (!canPourForGeneration(s, i, j, &amount)) continue;
                    mv.push_back(Move{ i, j, amount });
                }
            }
            if (mv.empty()) break;
            auto m = mv[rng.irange(0, (int)mv.size() - 1)];
            s.apply(m);
            if (outSteps) outSteps->push_back(m);
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
            State scrambleStart;
            int mix = 0;
            std::vector<Move> scrambleMoves;

            // startMixed OFF: 정렬 시작점에서 scramble 과정을 기록한 뒤 solve
            if (!opt.startMixed) {
                scrambleStart = s;
                scramble(s, mix, &scrambleMoves);
            }
            // startMixed ON: 이미 랜덤 섞임 시작점에서 바로 solve
            else {
                mix = s.p.numColors * s.p.capacity; // 대충 섞임 강도 표기로 사용
                scrambleMoves.clear();
                scrambleStart = State{}; // scramble playback 비활성화를 명시
            }

            if (!hasAnyMove(s)) {
                continue;
                
            }
            Solver solver(opt.solveTimeMs);
            auto res = solver.solve(s);
            if (res.solved) {
                Generated g; g.state = s; g.scrambleStart = scrambleStart; g.mixCount = mix; g.minMoves = res.minMoves;
                g.diffScore = solver.estimateDifficulty(s, res);
                g.diffLabel = labelForScore(g.diffScore);
                g.scrambleMoves = std::move(scrambleMoves);
                g.solutionMoves = std::move(res.solutionMoves);
                g.difficulty = res.difficulty;
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

    std::vector<int> Generator::computeRandomizedHeights() {
        std::vector<int> heights(p.numBottles, 0);
        if (p.numBottles <= 0) return heights;

        const long long totalCells = 1ll * p.numColors * p.capacity;
        if (totalCells <= 0) return heights;

        // 최소로 필요한 병 수(모두 가득 찼을 때)
        int minActive = (int)((totalCells + p.capacity - 1) / p.capacity);
        // 기본적으로 reservedEmpty를 존중하되, 변주를 위해 필요한 경우 더 많은 병을 사용
        int preferredActive = std::clamp(p.numBottles - std::max(0, opt.reservedEmpty), minActive, p.numBottles);
        int active = preferredActive;
        if (1ll * preferredActive * p.capacity == totalCells && preferredActive < p.numBottles) {
            // 모든 병이 만땅이 되어버리는 경우, 여유 병을 섞어 높이 다양화
            active = rng.irange(preferredActive + 1, p.numBottles);
        }
        else {
            active = rng.irange(minActive, preferredActive);
        }
        active = std::clamp(active, minActive, p.numBottles);

        std::vector<int> order(p.numBottles);
        std::iota(order.begin(), order.end(), 0);
        for (size_t i = 0; i < order.size(); ++i) {
            size_t j = (size_t)rng.irange((int)i, (int)order.size() - 1);
            std::swap(order[i], order[j]);
        }

        int remaining = (int)totalCells;
        for (int idx = 0; idx < active; ++idx) {
            int bottle = order[idx];
            int bottlesLeft = active - idx;
            int maxRemainingCapacity = (bottlesLeft - 1) * p.capacity;
            int minTake = std::max(0, remaining - maxRemainingCapacity);
            int maxTake = std::min(p.capacity, remaining);
            if (remaining >= bottlesLeft) {
                minTake = std::max(1, minTake);
            }
            if (minTake > maxTake) minTake = maxTake;
            int take = (idx == active - 1) ? remaining : rng.irange(minTake, maxTake);
            heights[bottle] = take;
            remaining -= take;
        }

        // 혹시라도 남는 경우 대비 (이론상 남지 않아야 함)
        if (remaining > 0) {
            for (int idx = active; idx < p.numBottles && remaining > 0; ++idx) {
                int bottle = order[idx];
                int take = std::min(p.capacity - heights[bottle], remaining);
                heights[bottle] += take;
                remaining -= take;
            }
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
            std::vector<Color> vineFixedColor(p.numBottles, 0);

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

            auto respectsVine = [&](int bi, Color c) -> bool {
                const auto& B = st.B[bi];
                if (B.gimmick.kind != StackGimmickKind::Vine) return true;
                Color fixed = vineFixedColor[bi];
                if (fixed == 0 && !B.slots.empty()) {
                    fixed = B.slots.front().c;
                    for (const auto& s : B.slots) {
                        if (s.c != fixed) return false;
                    }
                }
                return fixed == 0 || c == fixed;
                };

            auto allowed = [&](int bi, Color c)->bool {
                const auto& B = st.B[bi];
                if ((int)B.slots.size() >= heights[bi]) return false;
                if (B.gimmick.kind == StackGimmickKind::Cloth && B.gimmick.clothTarget == c) return false;
                if (reservedColor[bi] == c && reservedCount[bi] >= reservedLimit[bi]) return false;
                if (opt.maxRunPerBottle > 0 && runlen(B, c) >= opt.maxRunPerBottle) return false;
                if (!respectsVine(bi, c)) return false;
                return true;
                };

            auto placeColor = [&](int bi, Color c) {
                st.B[bi].slots.push_back(Slot{ c,false });
                if (reservedColor[bi] == c) ++reservedCount[bi];
                if (st.B[bi].gimmick.kind == StackGimmickKind::Vine) {
                    if (vineFixedColor[bi] == 0) vineFixedColor[bi] = c;
                }
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
                        if (!respectsVine(bi, c)) continue;
                        placeColor(bi, c);
                        placed = true;
                    }
                }
                if (!placed) {
                    for (int bi = 0; bi < p.numBottles; ++bi) {
                        if ((int)st.B[bi].slots.size() < heights[bi]) {
                            if (!respectsVine(bi, c)) continue;
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

            if (base) {
                for (size_t bi = 0; bi < st.B.size() && bi < base->B.size(); ++bi) {
                    const auto& tplBottle = base->B[bi];
                    auto& slots = st.B[bi].slots;
                    int limit = std::min((int)slots.size(), (int)tplBottle.slots.size());
                    for (int idx = 0; idx < limit; ++idx) {
                        slots[idx].hidden = tplBottle.slots[idx].hidden;
                    }
                }
            }
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
        auto heights = opt.randomizeHeights ? computeRandomizedHeights() : computeDefaultHeights();
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
                if (mono.gimmick.kind == StackGimmickKind::Vine) continue;
                if (!hasMonoFull(mono)) continue;
                Color monoColor = mono.slots.empty() ? 0 : mono.slots[0].c;
                bool swapped = false;

                for (size_t j = 0; j < st.B.size() && !swapped; ++j) {
                    if (j == i) continue;
                    auto& other = st.B[j];
                    if (other.gimmick.kind == StackGimmickKind::Vine) continue;
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
                        if (other.gimmick.kind == StackGimmickKind::Vine) continue;
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
