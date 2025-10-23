// ========================= src/io/Csv.hpp =========================
#pragma once
#include "../core/State.hpp"
#include <string>
#include <vector>

namespace ws {

    struct CsvRow {
        int index;              // map number
        std::string map;        // e.g. 7_7_2_4#6_2_3_0#1_8_2_0#...
        std::string slot_gimmick;   // e.g. 0110#0100#...
        std::string stack_gimmick;  // e.g. 0_0#1_5#2_0#3_0#...
        int NumberOfItem;
        int NumberOfSlot;
        int NumberOfStack;
        int MixCount;
        int MinMoves;
        double DifficultyScore;
        std::string DifficultyLabel;
    };

    // Encode/Decode according to your exact spec
    struct CsvIO {
        static CsvRow encode(int index, const State& s, int mix, int minMoves, double diffScore, const std::string& diffLabel);
        static bool decode(const CsvRow& row, State& outState);

        static bool save(const std::string& path, const std::vector<CsvRow>& rows, bool appendIfExists = true);
        static std::vector<CsvRow> load(const std::string& path);
    };

} // namespace ws