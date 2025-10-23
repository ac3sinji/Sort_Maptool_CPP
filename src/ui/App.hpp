// ========================= src/ui/App.hpp =========================
#pragma once
#include "../core/Generator.hpp"
#include "../io/Csv.hpp"
#include <string>

namespace ws {

    class AppUI {
    public:
        AppUI();
        int run(); // SDL2 + ImGui main loop

    private:
        Params p; GenOptions opt; Generator gen; int NtoGenerate{ 5 };
        std::vector<Generated> generated; // in‑memory pool
        int currentIndex{ -1 };
        int viewIndexInput{ 1 };
        std::string savePath{ "maps.csv" };
        std::string loadPath{ "maps.csv" };
        State tpl;                 // 생성용 템플릿(병별 초기 높이 + 기믹)
        bool useTemplate{ true };    // Generate 시 템플릿 사용 여부

        // UI helpers
        void drawTopBar();
        void drawEditor();
        void drawViewer();
        void drawTemplate();           // 템플릿 편집창
        void syncTemplateWithParams(); // Colors/Bottles/Capacity 바뀔 때 템플릿 맞춰주기

        void ensureIndex(int idx);
    };

} // namespace ws