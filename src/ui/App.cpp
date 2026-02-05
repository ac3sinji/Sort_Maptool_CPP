// ========================= src/ui/App.cpp =========================
#include "App.hpp"
#include <SDL.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include <algorithm> // for std::clamp
#include <filesystem> // for font path existence check
#include <cstdint>
#include <string>

namespace ws {

    AppUI::AppUI() :p{ 6,8,4 }, opt{} {
        tpl.p = p;
        tpl.B.resize(p.numBottles);
        for (auto& b : tpl.B) b.capacity = p.capacity;
    }

    AppUI::~AppUI() {
        if (generationThread.joinable()) {
            generationThread.join();
        }
    }

    void AppUI::setStatus(const std::string& msg) {
        std::lock_guard<std::mutex> lock(statusMutex);
        statusMessage = msg;
    }

    std::string AppUI::getStatus() {
        std::lock_guard<std::mutex> lock(statusMutex);
        return statusMessage;
    }

    void AppUI::ensureIndex(int idx) {
        if (idx >= 0 && idx < (int)generated.size()) {
            currentIndex = idx;
            viewIndexInput = idx + 1;
            playbackStep = 0;
        }
    }

    void AppUI::collectGenerated() {
        if (!isGenerating.load() && generationThread.joinable()) {
            generationThread.join();
            generationTotal = 0;
            generationCompleted.store(0);
        }

        std::vector<Generated> newly;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (!pendingGenerated.empty()) {
                newly.swap(pendingGenerated);
            }
        }

        if (!newly.empty()) {
            bool hadAny = !generated.empty();
            for (auto& g : newly) {
                generated.push_back(std::move(g));
            }
            if (currentIndex < 0 && !generated.empty()) ensureIndex(0);
            else if (!hadAny && !generated.empty()) ensureIndex(0);
        }
    }

    static bool InputIntClamped(const char* label, int* value, int minValue, int maxValue, int step = 1, int stepFast = 5) {
        if (minValue > maxValue) std::swap(minValue, maxValue);
        int before = *value;
        bool interacted = ImGui::InputInt(label, value, step, stepFast);
        if (*value < minValue) *value = minValue;
        if (*value > maxValue) *value = maxValue;

        return interacted || *value != before;
    }

    void AppUI::drawTopBar() {
        collectGenerated();

        ImGui::Begin("Controls");
        ImGui::Text("Params");
        bool pChanged = false;
        pChanged |= InputIntClamped("Colors", &p.numColors, 1, 18);
        pChanged |= InputIntClamped("Bottles", &p.numBottles, 3, 30);
        pChanged |= InputIntClamped("Capacity", &p.capacity, 3, 50);
        ImGui::Separator();
        ImGui::Text("Generator");
        if (InputIntClamped("Mix min", &opt.mixMin, 10, 300, 5, 20)) {
            if (opt.mixMax < opt.mixMin) opt.mixMax = opt.mixMin;
        }
        InputIntClamped("Mix max", &opt.mixMax, opt.mixMin, 10000, 5, 20);
        InputIntClamped("Solve ms", &opt.solveTimeMs, 200, 100000, 10, 100);
        InputIntClamped("Count (N)", &NtoGenerate, 1, 50);
        InputIntClamped("Auto template maps", &autoCount, 1, 50);
        ImGui::Separator();
        ImGui::Text("Auto template gimmicks");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Cloth/Vine/Bush 개수는 자동 템플릿 생성 시 병에 배치되는 기믹 수입니다.");
        }
        InputIntClamped("Cloth count", &clothCount, 0, p.numBottles);
        InputIntClamped("Vine count", &vineCount, 0, p.numBottles);
        InputIntClamped("Bush count", &bushCount, 0, p.numBottles);
        int filledSlots = std::max(0, p.numColors * p.capacity);
        InputIntClamped("Question count", &questionCount, 0, filledSlots);
        ImGui::Checkbox("Randomize heights (auto template)", &opt.randomizeHeights);
        uint64_t seedValue = opt.seed;
        if (ImGui::InputScalar("Generator seed (random heights)", ImGuiDataType_U64, &seedValue)) {
            opt.seed = seedValue;
        }
        ImGui::Separator();
        ImGui::Text("Start State");
        ImGui::Checkbox("Start mixed (random deal)", &opt.startMixed);
        ImGui::BeginDisabled(!opt.startMixed);
        InputIntClamped("Reserved empty bottles", &opt.reservedEmpty, 0, std::max(0, p.numBottles - 1));
        InputIntClamped("Max same-color run", &opt.maxRunPerBottle, 0, p.capacity);
        ImGui::EndDisabled();
        if (pChanged) syncTemplateWithParams();

        ImGui::Checkbox("Use template on generate", &useTemplate);
        InputIntClamped("Max same-color run", &opt.maxRunPerBottle, 0, p.capacity);

        long long sumH = 0; for (const auto& b : tpl.B) sumH += (int)b.slots.size();
        long long expected = 1ll * p.numColors * p.capacity;
        if (useTemplate) {
            if (sumH != expected)
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Template sum %lld != Colors*Capacity %lld", sumH, expected);
            else
                ImGui::TextColored(ImVec4(0.6f, 1, 0.6f, 1), "Template OK (sum=%lld)", sumH);
        }

        bool currentlyGenerating = isGenerating.load();
        if (currentlyGenerating) ImGui::BeginDisabled();
        if (ImGui::Button("Generate N")) {
            bool canGenerate = true;
            if (useTemplate) {
                if (sumH != expected) canGenerate = false;
            }
            if (canGenerate) {
                Params pCopy = p;
                GenOptions optCopy = opt;
                State tplCopy = tpl;
                int count = NtoGenerate;
                bool useTemplateNow = useTemplate && sumH == expected;
                setStatus("");

                if (generationThread.joinable()) generationThread.join();
                generationTotal = count;
                generationCompleted.store(0);
                isGenerating.store(true);

                generationThread = std::thread([this, pCopy, optCopy, tplCopy, count, useTemplateNow]() mutable {
                    Generator localGen(pCopy, optCopy);
                    if (useTemplateNow) {
                        localGen.setBase(tplCopy);
                    }
                    std::vector<Generated> local;
                    local.reserve(count);
                    for (int i = 0; i < count; ++i) {
                        auto g = localGen.makeOne(nullptr);
                        if (g) {
                            local.push_back(std::move(*g));
                        }
                        generationCompleted.fetch_add(1);
                    }
                    {
                        std::lock_guard<std::mutex> lock(pendingMutex);
                        for (auto& item : local) {
                            pendingGenerated.push_back(std::move(item));
                        }
                    }
                    isGenerating.store(false);
                });
            }
            else {
                setStatus("Template height sum must match Colors*Capacity.");
                generationTotal = 0;
                generationCompleted.store(0);
            }
        }

        if (ImGui::Button("Generate with Auto Template")) {
            Params pCopy = p;
            GenOptions optCopy = opt;
            int cloth = clothCount;
            int vine = vineCount;
            int bush = bushCount;
            int questions = questionCount;
            int count = autoCount;

            Generator validator(pCopy, optCopy);
            std::string validationMsg;
            if (!validator.buildRandomTemplate(cloth, vine, bush, questions, &validationMsg)) {
                if (validationMsg.empty()) validationMsg = "Unable to build template with current settings.";
                setStatus(validationMsg);
                generationTotal = 0;
                generationCompleted.store(0);
            }
            else {
                setStatus("");
                if (generationThread.joinable()) generationThread.join();
                generationTotal = count;
                generationCompleted.store(0);
                isGenerating.store(true);

                generationThread = std::thread([this, pCopy, optCopy, cloth, vine, bush, questions, count]() mutable {
                    Generator localGen(pCopy, optCopy);
                    std::vector<Generated> local;
                    std::string status;
                    local.reserve(count);
                    for (int i = 0; i < count; ++i) {
                        std::string reason;
                        auto tplOpt = localGen.buildRandomTemplate(cloth, vine, bush, questions, &reason);
                        if (!tplOpt) {
                            status = reason.empty() ? "Failed to build template." : reason;
                            break;
                        }
                        localGen.setBase(*tplOpt);
                        auto g = localGen.makeOne(nullptr);
                        if (g) {
                            local.push_back(std::move(*g));
                        }
                        else {
                            status = "Generation failed for a map.";
                            break;
                        }
                        generationCompleted.fetch_add(1);
                    }
                    {
                        std::lock_guard<std::mutex> lock(pendingMutex);
                        for (auto& item : local) {
                            pendingGenerated.push_back(std::move(item));
                        }
                    }
                    if (status.empty()) {
                        status = std::string("Auto template generation complete (heights ") +
                            (optCopy.randomizeHeights ? "randomized" : "fixed") + ").";
                    }
                    setStatus(status);
                    isGenerating.store(false);
                    });
            }
        }
        if (currentlyGenerating) ImGui::EndDisabled();

        if (isGenerating.load()) {
            ImGui::SameLine();
            int total = generationTotal;
            int done = generationCompleted.load();
            if (total < 1) total = 1;
            if (done > total) done = total;
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "Generating Maps... %d/%d", done, total);
        }

        std::string status = getStatus();
        if (!status.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.5f, 1.0f), "%s", status.c_str());
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear Memory")) {
            generated.clear();
            currentIndex = -1;
            viewIndexInput = 1;
            playbackStep = 0;
        }

        ImGui::Separator();
        ImGui::InputText("Save CSV", savePath.data(), 256);
        if (ImGui::Button("Save")) {
            // append indices continuing from existing file if present
            auto rowsExisting = CsvIO::load(savePath);
            int startIdx = rowsExisting.empty() ? 0 : (rowsExisting.back().index + 1);
            std::vector<CsvRow> rows;
            for (size_t i = 0; i < generated.size(); ++i) {
                const auto& g = generated[i];
                rows.push_back(CsvIO::encode(startIdx + (int)i, g.state, g.mixCount, g.minMoves, g.diffScore, g.diffLabel));
            }
            CsvIO::save(savePath, rows, true);
        }

        ImGui::InputText("Load CSV", loadPath.data(), 256);
        if (ImGui::Button("Load")) {
            generated.clear(); currentIndex = -1; viewIndexInput = 1;
            auto rows = CsvIO::load(loadPath);
            for (const auto& r : rows) {
                State s; if (CsvIO::decode(r, s)) {
                    Generated g; g.state = std::move(s); g.mixCount = r.MixCount; g.minMoves = r.MinMoves; g.diffScore = r.DifficultyScore; g.diffLabel = r.DifficultyLabel; generated.push_back(std::move(g));
                }
            }
            if (!generated.empty()) ensureIndex(0);
        }

        ImGui::Separator();
        ImGui::Text("View by index");
        bool hasMaps = !generated.empty();
        int maxIndex = hasMaps ? (int)generated.size() : 1;
        viewIndexInput = std::clamp(viewIndexInput, 1, maxIndex);
        int inputValue = viewIndexInput;
        if (!hasMaps) ImGui::BeginDisabled();
        if (InputIntClamped("Map #", &inputValue, 1, maxIndex)) {
            viewIndexInput = inputValue;
            if (hasMaps) ensureIndex(viewIndexInput - 1);
        }
        if (!hasMaps) ImGui::EndDisabled();

        ImGui::End();
    }

    static ImU32 colorFor(Color c) {
        static ImU32 table[21] = {
            IM_COL32(40,40,40,255),
            IM_COL32(230, 80, 80,255), IM_COL32(80,180,250,255), IM_COL32(90,200,120,255), IM_COL32(240,210,70,255),
            IM_COL32(200,120,240,255), IM_COL32(255,160,120,255), IM_COL32(120,120,240,255), IM_COL32(90,160,160,255), IM_COL32(250,130,180,255),
            IM_COL32(150,100,80,255), IM_COL32(100,150,100,255), IM_COL32(80,160,200,255), IM_COL32(200,80,200,255), IM_COL32(100,100,220,255),
            IM_COL32(220,120,60,255), IM_COL32(160,220,60,255), IM_COL32(60,220,160,255), IM_COL32(60,160,220,255), IM_COL32(200,200,200,255),
            IM_COL32(30,30,30,255)
        };
        if (c > 20) c = 20; return table[c];
    }

    void AppUI::drawViewer() {
        ImGui::Begin("Viewer");
        if (currentIndex < 0 || currentIndex >= (int)generated.size()) { ImGui::Text("No map selected"); ImGui::End(); return; }
        const auto& g = generated[currentIndex];
        const auto& baseState = g.state;

        ImGui::Text("Mix=%d  MinMoves=%d  Diff=%.1f (%s)", g.mixCount, g.minMoves, g.diffScore, g.diffLabel.c_str());
        ImGui::Text("Difficulty breakdown:");
        ImGui::Text("  Move: %.1f  Heuristic: %.1f  Fragment: %.1f", g.difficulty.moveComponent, g.difficulty.heuristicComponent, g.difficulty.fragmentationComponent);
        ImGui::Text("  Hidden: %.1f  Gimmick: %.1f  Color: %.1f", g.difficulty.hiddenComponent, g.difficulty.gimmickComponent, g.difficulty.colorComponent);
        ImGui::Text("  Solution: %.1f  Total: %.1f", g.difficulty.solutionComponent, g.difficulty.totalScore);

        const auto& moves = g.solutionMoves;
        int maxStep = (int)moves.size();
        playbackStep = std::clamp(playbackStep, 0, maxStep);
        if (moves.empty()) {
            ImGui::TextDisabled("No solution path recorded.");
        }
        else {
            ImGui::Separator();
            ImGui::Text("Solution step: %d / %d", playbackStep, maxStep);
            bool canPrev = playbackStep > 0;
            bool canNext = playbackStep < maxStep;
            if (!canPrev) ImGui::BeginDisabled();
            if (ImGui::Button("Prev")) { --playbackStep; }
            if (!canPrev) ImGui::EndDisabled();
            ImGui::SameLine();
            if (!canNext) ImGui::BeginDisabled();
            if (ImGui::Button("Next")) { ++playbackStep; }
            if (!canNext) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Reset")) { playbackStep = 0; }
            int stepInput = playbackStep;
            if (InputIntClamped("Step", &stepInput, 0, maxStep)) {
                playbackStep = stepInput;
            }
            if (playbackStep > 0 && playbackStep <= maxStep) {
                const auto& lastMove = moves[playbackStep - 1];
                ImGui::Text("Move %d: %d -> %d (amount %d)", playbackStep, lastMove.from + 1, lastMove.to + 1, lastMove.amount);
            }
        }

        State viewState = baseState;
        for (int i = 0; i < playbackStep && i < maxStep; ++i) {
            viewState.apply(moves[i]);
        }
        const auto& s = viewState;

        const auto& moves = g.solutionMoves;
        int maxStep = (int)moves.size();
        playbackStep = std::clamp(playbackStep, 0, maxStep);
        if (moves.empty()) {
            ImGui::TextDisabled("No solution path recorded.");
        }
        else {
            ImGui::Separator();
            ImGui::Text("Solution step: %d / %d", playbackStep, maxStep);
            bool canPrev = playbackStep > 0;
            bool canNext = playbackStep < maxStep;
            if (!canPrev) ImGui::BeginDisabled();
            if (ImGui::Button("Prev")) { --playbackStep; }
            if (!canPrev) ImGui::EndDisabled();
            ImGui::SameLine();
            if (!canNext) ImGui::BeginDisabled();
            if (ImGui::Button("Next")) { ++playbackStep; }
            if (!canNext) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Reset")) { playbackStep = 0; }
            int stepInput = playbackStep;
            if (InputIntClamped("Step", &stepInput, 0, maxStep)) {
                playbackStep = stepInput;
            }
            if (playbackStep > 0 && playbackStep <= maxStep) {
                const auto& lastMove = moves[playbackStep - 1];
                ImGui::Text("Move %d: %d -> %d (amount %d)", playbackStep, lastMove.from + 1, lastMove.to + 1, lastMove.amount);
            }
        }

        State viewState = baseState;
        for (int i = 0; i < playbackStep && i < maxStep; ++i) {
            viewState.apply(moves[i]);
        }
        const auto& s = viewState;

        // draw bottles
        float cell = 18.0f; // cell height
        float bottleW = 28.0f; float gap = 12.0f; float baseY = 80.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();

        for (size_t i = 0; i < s.B.size(); ++i) {
            const auto& b = s.B[i];
            float x = origin.x + i * (bottleW + gap);
            float y = origin.y + baseY;
            // outline
            dl->AddRect(ImVec2(x, y - b.capacity * cell), ImVec2(x + bottleW, y), IM_COL32(200, 200, 200, 255));
            // slots bottom->top
            for (int k = 0; k < b.capacity; ++k) {
                float yTop = y - (k + 1) * cell;
                ImU32 col = IM_COL32(60, 60, 60, 255);
                if (k < (int)b.slots.size()) {
                    col = colorFor(b.slots[k].c);
                    if (b.slots[k].hidden) col = IM_COL32(90, 90, 90, 255);
                }
                dl->AddRectFilled(ImVec2(x + 2, yTop + 2), ImVec2(x + bottleW - 2, yTop + cell - 2), col, 3.0f);
                if (k < (int)b.slots.size() && b.slots[k].hidden) {
                    const char* hiddenMark = "?";
                    ImVec2 textSize = ImGui::CalcTextSize(hiddenMark);
                    ImVec2 textPos(
                        x + (bottleW - textSize.x) * 0.5f,
                        yTop + (cell - textSize.y) * 0.5f
                    );
                    dl->AddText(textPos, IM_COL32(255, 255, 255, 255), hiddenMark);
                }
            }
            // gimmick badge
            std::string badge = ""; auto kind = b.gimmick.kind;
            if (kind == StackGimmickKind::Cloth) badge = "C(" + std::to_string((int)b.gimmick.clothTarget) + ")";
            else if (kind == StackGimmickKind::Vine) badge = "V";
            else if (kind == StackGimmickKind::Bush) badge = "B";
            if (!badge.empty()) {
                dl->AddText(ImVec2(x, y - b.capacity * cell - 16), IM_COL32(250, 220, 120, 255), badge.c_str());
            }
            std::string bottleLabel = std::to_string(i + 1);
            dl->AddText(ImVec2(x, y + 6), IM_COL32(200, 200, 200, 255), bottleLabel.c_str());
        }

        ImGui::End();
    }

    void AppUI::drawEditor() {
        ImGui::Begin("Editor (per bottle)");
        if (currentIndex < 0 || currentIndex >= (int)generated.size()) { ImGui::Text("No map selected"); ImGui::End(); return; }
        auto& s = generated[currentIndex].state;

        static int selBottle = 0;
        selBottle = std::clamp(selBottle, 0, (int)s.B.size() - 1);
        int displayBottle = selBottle + 1;
        if (InputIntClamped("Bottle", &displayBottle, 1, (int)s.B.size())) {
            selBottle = displayBottle - 1;
        }
        ImGui::Text("Editing Bottle #%d", selBottle + 1);

        auto& b = s.B[selBottle];
        ImGui::Text("Capacity=%d  Size=%d", b.capacity, (int)b.slots.size());

        // Gimmicks
        int kind = (int)b.gimmick.kind;
        if (ImGui::RadioButton("None", kind == 0)) kind = 0; ImGui::SameLine();
        if (ImGui::RadioButton("Cloth", kind == 1)) kind = 1; ImGui::SameLine();
        if (ImGui::RadioButton("Vine", kind == 2)) kind = 2; ImGui::SameLine();
        if (ImGui::RadioButton("Bush", kind == 3)) kind = 3;
        b.gimmick.kind = (StackGimmickKind)kind;
        if (kind == 1) {
            int ct = b.gimmick.clothTarget; if (ct < 1) ct = 1; if (ct > p.numColors) ct = p.numColors;
            if (InputIntClamped("Cloth Target Color", &ct, 1, p.numColors)) {
                b.gimmick.clothTarget = (Color)ct;
            }
        }

        ImGui::Separator();
        ImGui::Text("Paint / Edit Slots");
        static int paintColor = 1; paintColor = std::clamp(paintColor, 1, p.numColors);
        if (InputIntClamped("Paint Color", &paintColor, 1, p.numColors)) {
            paintColor = std::clamp(paintColor, 1, p.numColors);
        }
        if (ImGui::Button("Push Top")) {
            if (b.size() < b.capacity) { b.slots.push_back(Slot{ (Color)paintColor,false }); s.refreshLocks(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Pop Top")) {
            if (b.size() > 0) { b.slots.pop_back(); s.refreshLocks(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Bottle")) {
            b.slots.clear(); s.refreshLocks();
        }

        static int editIndex = 1; editIndex = std::clamp(editIndex, 1, std::max(1, b.capacity));
        if (InputIntClamped("Edit Slot Index (1 = bottom)", &editIndex, 1, std::max(1, b.capacity))) {
            editIndex = std::clamp(editIndex, 1, std::max(1, b.capacity));
        }
        int slotIndex = editIndex - 1;
        if (slotIndex < (int)b.slots.size()) {
            int ec = b.slots[slotIndex].c; if (ec < 0) ec = 0; if (ec > p.numColors) ec = p.numColors;
            if (InputIntClamped("Edit Slot Color (0 = empty)", &ec, 0, p.numColors)) { b.slots[slotIndex].c = (Color)ec; s.refreshLocks(); }
            bool h = b.slots[slotIndex].hidden; if (ImGui::Checkbox("? Hidden", &h)) { b.slots[slotIndex].hidden = h; }
        }
        else {
            ImGui::TextDisabled("(Index beyond current height)");
        }

        ImGui::Separator();
        ImGui::Text("? toggles by slot (1 = bottom)");
        for (int k = 0; k < b.capacity; ++k) {
            bool h = (k < (int)b.slots.size()) ? b.slots[k].hidden : false;
            std::string lbl = "? slot " + std::to_string(k + 1);
            if (ImGui::Checkbox(lbl.c_str(), &h)) {
                if (k < (int)b.slots.size()) { b.slots[k].hidden = h; s.refreshLocks(); }
            }
        }

        ImGui::End();
    }

    void AppUI::syncTemplateWithParams() {
        tpl.p = p;
        if ((int)tpl.B.size() != p.numBottles) tpl.B.resize(p.numBottles);
        for (auto& b : tpl.B) {
            if (b.capacity != p.capacity) b.capacity = p.capacity;
            if ((int)b.slots.size() > p.capacity) b.slots.resize(p.capacity);
        }
    }

    void AppUI::drawTemplate() {
        ImGui::Begin("Template (pre-generate)");
        ImGui::Text("Set start 'Height' and 'Gimmick'");
        if ((int)tpl.B.size() != p.numBottles) syncTemplateWithParams();

        static int tb = 0; tb = std::clamp(tb, 0, (int)tpl.B.size() - 1);
        int displayTemplateBottle = tb + 1;
        if (InputIntClamped("Bottle", &displayTemplateBottle, 1, (int)tpl.B.size())) {
            tb = displayTemplateBottle - 1;
        }
        auto& b = tpl.B[tb];
        ImGui::Text("Editing Bottle #%d", tb + 1);
        ImGui::Text("Capacity=%d  Current height=%d", b.capacity, (int)b.slots.size());

        int h = (int)b.slots.size();
        if (InputIntClamped("Initial height", &h, 0, p.capacity)) {
            if (h < (int)b.slots.size()) b.slots.resize(h);
            else while ((int)b.slots.size() < h) b.slots.push_back(Slot{ 1,false }); // placeholder
        }

        int kind = (int)b.gimmick.kind;
        if (ImGui::RadioButton("None", kind == 0)) kind = 0; ImGui::SameLine();
        if (ImGui::RadioButton("Cloth", kind == 1)) kind = 1; ImGui::SameLine();
        if (ImGui::RadioButton("Vine", kind == 2)) kind = 2; ImGui::SameLine();
        if (ImGui::RadioButton("Bush", kind == 3)) kind = 3;
        b.gimmick.kind = (StackGimmickKind)kind;
        if (kind == 1) {
            int ct = b.gimmick.clothTarget; if (ct < 1) ct = 1; if (ct > p.numColors) ct = p.numColors;
            if (InputIntClamped("Cloth Target Color", &ct, 1, p.numColors)) { b.gimmick.clothTarget = (Color)ct; }
        }

        ImGui::Separator();
        ImGui::Text("? Hidden per slot (1 = bottom)");
        for (int k = 0; k < b.capacity; ++k) {
            bool enabled = k < (int)b.slots.size();
            bool h = enabled ? b.slots[k].hidden : false;
            if (!enabled) ImGui::BeginDisabled();
            std::string lbl = "? slot " + std::to_string(k + 1);
            if (ImGui::Checkbox(lbl.c_str(), &h) && enabled) {
                b.slots[k].hidden = h;
            }
            if (!enabled) ImGui::EndDisabled();
        }

        long long sumH = 0; for (const auto& bx : tpl.B) sumH += (int)bx.slots.size();
        long long expected = 1ll * p.numColors * p.capacity;
        ImGui::Text("Sum heights: %lld / expected %lld", sumH, expected);

        ImGui::End();
    }

    int AppUI::run() {
        // SDL2 init
        SDL_Init(SDL_INIT_VIDEO);
        SDL_Window* window = SDL_CreateWindow("WaterSort Map Tool", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1400, 900, SDL_WINDOW_SHOWN);
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO(); (void)io;
        ImGui::StyleColorsDark();

        // ▼▼ 한글 폰트 강제 로드: 실패하면 다음 후보로, 끝까지 실패하면 기본폰트
        const char* font_candidates[] = {
            "C:/Windows/Fonts/malgun.ttf",       // 맑은 고딕
            "C:/Windows/Fonts/malgunbd.ttf",
            "C:/Users/pivot/AppData/Local/Microsoft/Windows/Fonts/NanumGothic.ttf",  // 나눔고딕 (설치되어 있으면)
            "C:/Windows/Fonts/arialuni.ttf"      // Arial Unicode MS (있을 때만)
        };
        ImFont* korean = nullptr;
        for (const char* path : font_candidates) {
            // 존재 여부와 무관하게 바로 시도(일부 PC에서 exists()가 false를 반환하는 경우가 있어요)
            korean = io.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr, io.Fonts->GetGlyphRangesKorean());
            if (korean) { io.FontDefault = korean; break; }
        }
        if (!korean) {
            // 최후 수단: 기본 폰트(라틴 전용). 이 경우 한글은 깨져 보입니다.
            korean = io.Fonts->AddFontDefault();
            io.FontDefault = korean;
            // 디버그 확인용(콘솔에 출력)
            printf("[ImGui] Korean font NOT found, using default font. Install 'Malgun Gothic' or 'NanumGothic'.\n");
        }

        // (옵션) 즉시 폰트 아틀라스 빌드 — 백엔드 init 전에 하면 자동으로 반영됩니다.
        io.Fonts->Build();

        // 2) 백엔드 초기화
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);

        // 3) 폰트 텍스처를 GPU에 업로드 (백엔드 초기화 이후에 안전)
        ImGui_ImplSDLRenderer2_DestroyFontsTexture();
        ImGui_ImplSDLRenderer2_CreateFontsTexture();

        bool running = true; SDL_Event e;
        while (running) {
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL2_ProcessEvent(&e);
                if (e.type == SDL_QUIT) running = false;
            }
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            drawTopBar();
            drawTemplate();   // ← 추가
            drawViewer();
            drawEditor();

            ImGui::Render();
            SDL_SetRenderDrawColor(renderer, 20, 20, 24, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
        }

        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

} // namespace ws
