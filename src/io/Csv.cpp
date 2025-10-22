// ========================= src/io/Csv.cpp =========================
#include "Csv.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace ws {

    static std::string encodeMap(const State& s) {
        std::ostringstream oss;
        for (size_t i = 0; i < s.B.size(); ++i) {
            const auto& b = s.B[i];
            if (!b.slots.empty()) {
                // write bottom->top colors and pad remaining capacity with explicit zeros (top-most positions)
                for (int k = 0; k < b.capacity; ++k) {
                    if (k < (int)b.slots.size()) oss << int(b.slots[k].c);
                    else oss << '0';
                }
            }
            if (i + 1 < s.B.size()) oss << '#'; else oss << "";
        }
        return oss.str();
    }

    static std::string encodeSlotGimmick(const State& s) {
        std::ostringstream oss;
        for (size_t i = 0; i < s.B.size(); ++i) {
            const auto& b = s.B[i];
            for (int k = 0; k < b.capacity; ++k) {
                int bit = (k < (int)b.slots.size() && b.slots[k].hidden) ? 1 : 0;
                oss << bit;
            }
            if (i + 1 < s.B.size()) oss << '#';
        }
        return oss.str();
    }

    static std::string encodeStackGimmick(const State& s) {
        std::ostringstream oss;
        for (size_t i = 0; i < s.B.size(); ++i) {
            const auto& g = s.B[i].gimmick;
            int kind = (int)g.kind;
            int param = (g.kind == StackGimmickKind::Cloth) ? int(g.clothTarget) : 0;
            oss << kind << '_' << param;
            if (i + 1 < s.B.size()) oss << '#';
        }
        return oss.str();
    }

    CsvRow CsvIO::encode(int index, const State& s, int mix, int minMoves, double diffScore, const std::string& diffLabel) {
        CsvRow row;
        row.index = index;
        row.map = encodeMap(s);
        row.slot_gimmick = encodeSlotGimmick(s);
        row.stack_gimmick = encodeStackGimmick(s);
        row.NumberOfItem = s.p.numColors;
        row.NumberOfSlot = s.p.capacity;
        row.NumberOfStack = s.p.numBottles;
        row.MixCount = mix;
        row.MinMoves = minMoves;
        row.DifficultyScore = diffScore;
        row.DifficultyLabel = diffLabel;
        return row;
    }

    static std::vector<std::string> split(const std::string& s, char sep) {
        std::vector<std::string> out; std::string cur; std::istringstream iss(s);
        while (std::getline(iss, cur, sep)) out.push_back(cur);
        return out;
    }

    bool CsvIO::decode(const CsvRow& row, State& outState) {
        Params p; p.numColors = row.NumberOfItem; p.capacity = row.NumberOfSlot; p.numBottles = row.NumberOfStack;
        State s; s.p = p; s.B.resize(p.numBottles); for (auto& b : s.B) b.capacity = p.capacity;

        // map
        auto cols = split(row.map, '#');
        for (size_t i = 0; i < cols.size() && i < s.B.size(); ++i) {
            auto& b = s.B[i];
            const auto& token = cols[i];
            b.slots.clear();
            for (char ch : token) {
                if (ch < '0' || ch > '9') continue;
                int v = ch - '0';
                if (v == 0) continue; // padded empty cell
                if ((int)b.slots.size() >= b.capacity) break;
                b.slots.push_back(Slot{ (Color)v,false });
            }
        }

        // slot_gimmick
        auto sg = split(row.slot_gimmick, '#');
        for (size_t i = 0; i < sg.size() && i < s.B.size(); ++i) {
            const auto& mask = sg[i];
            auto& b = s.B[i];
            // ensure we have exactly capacity digits
            for (int k = 0; k < b.capacity && k < (int)mask.size(); ++k) {
                if (k < (int)b.slots.size()) b.slots[k].hidden = (mask[k] == '1');
            }
        }

        // stack_gimmick
        auto gg = split(row.stack_gimmick, '#');
        for (size_t i = 0; i < gg.size() && i < s.B.size(); ++i) {
            auto parts = split(gg[i], '_');
            if (parts.size() == 2) {
                int kind = std::stoi(parts[0]); int param = std::stoi(parts[1]);
                s.B[i].gimmick.kind = (StackGimmickKind)kind;
                s.B[i].gimmick.clothTarget = (Color)param;
            }
        }

        s.refreshLocks();
        outState = std::move(s);
        return true;
    }

    static std::string esc(const std::string& s) {
        std::string out; out.reserve(s.size() + 2);
        for (char c : s) { if (c == '"' || c == ',') { out.push_back('"'); out.push_back(c); out.push_back('"'); } else out.push_back(c); } return s;
    }

    bool CsvIO::save(const std::string& path, const std::vector<CsvRow>& rows, bool appendIfExists) {
        namespace fs = std::filesystem;
        bool exists = fs::exists(path);
        std::ofstream f(path, std::ios::out | (appendIfExists ? std::ios::app : std::ios::trunc));
        if (!f) return false;
        if (!exists || !appendIfExists) {
            f << "index,map,slot_gimmick,stack_gimmick,NumberOfItem,NumberOfSlot,NumberOfStack,MixCount,MinMoves,DifficultyScore,DifficultyLabel\n";
        }
        for (const auto& r : rows) {
            f << r.index << ',' << r.map << ',' << r.slot_gimmick << ',' << r.stack_gimmick << ','
                << r.NumberOfItem << ',' << r.NumberOfSlot << ',' << r.NumberOfStack << ',' << r.MixCount << ','
                << r.MinMoves << ',' << r.DifficultyScore << ',' << r.DifficultyLabel << "\n";
        }
        return true;
    }

    std::vector<CsvRow> CsvIO::load(const std::string& path) {
        std::vector<CsvRow> out; std::ifstream f(path);
        if (!f) return out;
        std::string line; bool first = true;
        while (std::getline(f, line)) {
            if (first) { first = false; continue; }
            if (line.empty()) continue;
            auto cells = split(line, ',');
            if (cells.size() < 11) continue;
            CsvRow r; int i = 0;
            r.index = std::stoi(cells[i++]);
            r.map = cells[i++];
            r.slot_gimmick = cells[i++];
            r.stack_gimmick = cells[i++];
            r.NumberOfItem = std::stoi(cells[i++]);
            r.NumberOfSlot = std::stoi(cells[i++]);
            r.NumberOfStack = std::stoi(cells[i++]);
            r.MixCount = std::stoi(cells[i++]);
            r.MinMoves = std::stoi(cells[i++]);
            r.DifficultyScore = std::stod(cells[i++]);
            r.DifficultyLabel = cells[i++];
            out.push_back(std::move(r));
        }
        return out;
    }

} // namespace ws