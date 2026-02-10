// ========================= src/ui/App.hpp =========================
#pragma once
#include "../core/Generator.hpp"
#include "../io/Csv.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace ws {

    class AppUI {
    public:
        AppUI();
        ~AppUI();
        int run(); // SDL2 + ImGui main loop

    private:
        Params p; GenOptions opt; int NtoGenerate{ 5 };
        int autoCount{ 5 }; // maps to generate with auto template per request
        int clothCount{ 0 };
        int vineCount{ 0 };
        int bushCount{ 0 };
        int questionCount{ 0 };
        int questionMaxPerBottle{ 0 };
        int workerThreads{ 1 };
        int workerThreadMax{ 8 };
        std::vector<Generated> generated; // in‑memory pool
        int currentIndex{ -1 };
        int viewIndexInput{ 1 };
        int playbackStep{ 0 };
        bool playbackScramble{ false };
        std::string savePath{ "maps.csv" };
        std::string loadPath{ "maps.csv" };
        State tpl;                 // 생성용 템플릿(병별 초기 높이 + 기믹)
        bool useTemplate{ true };    // Generate 시 템플릿 사용 여부
        std::string statusMessage;  // last user‑visible status/error
        std::mutex statusMutex;

        std::atomic<bool> isGenerating{ false };
        std::atomic<int> generationCompleted{ 0 };
        int generationTotal{ 0 };
        std::mutex pendingMutex;
        std::vector<Generated> pendingGenerated;
        std::thread generationThread;

        // UI helpers
        void drawTopBar();
        void drawEditor();
        void drawViewer();
        void drawTemplate();           // 템플릿 편집창
        void syncTemplateWithParams(); // Colors/Bottles/Capacity 바뀔 때 템플릿 맞춰주기
        void collectGenerated();
        void setStatus(const std::string& msg);
        std::string getStatus();

        void ensureIndex(int idx);
    };

} // namespace ws
