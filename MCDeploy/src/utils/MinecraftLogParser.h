#pragma once

#include <string>
#include <vector>
#include <regex>

namespace MCDeploy {

struct ParsedServerStats {
    double tps_1m = 20.0;
    double tps_5m = 20.0;
    double tps_15m = 20.0;
    double mspt = 5.2;
    int player_count = 0;
    int max_players = 20;
    int entities = 0;
    int chunks = 0;
};

class MinecraftLogParser {
public:
    static bool parseLine(const std::string& line, ParsedServerStats& stats) {
        // 1. Parse TPS (e.g., "TPS from last 1m, 5m, 15m: 19.95, 19.98, 20.0")
        if (line.find("TPS from last") != std::string::npos) {
            std::regex tpsRegex("TPS from last 1m, 5m, 15m:\\s*([0-9\\.]+),\\s*([0-9\\.]+),\\s*([0-9\\.]+)");
            std::smatch matches;
            if (std::regex_search(line, matches, tpsRegex)) {
                try {
                    stats.tps_1m = std::stod(matches[1].str());
                    stats.tps_5m = std::stod(matches[2].str());
                    stats.tps_15m = std::stod(matches[3].str());
                    return true;
                } catch (...) {}
            }
        }

        // 2. Parse MSPT (e.g., "MSPT: 15.4" or "Average tick time: 12.34ms")
        if (line.find("MSPT:") != std::string::npos) {
            std::regex msptRegex("MSPT:\\s*([0-9\\.]+)");
            std::smatch matches;
            if (std::regex_search(line, matches, msptRegex)) {
                try {
                    stats.mspt = std::stod(matches[1].str());
                    return true;
                } catch (...) {}
            }
        } else if (line.find("Average tick time:") != std::string::npos) {
            std::regex msptRegex("Average tick time:\\s*([0-9\\.]+)ms");
            std::smatch matches;
            if (std::regex_search(line, matches, msptRegex)) {
                try {
                    stats.mspt = std::stod(matches[1].str());
                    return true;
                } catch (...) {}
            }
        }

        // 3. Parse Player Joins/Leaves (e.g., "odenj joined the game", "odenj left the game")
        if (line.find("joined the game") != std::string::npos) {
            stats.player_count++;
            return true;
        } else if (line.find("left the game") != std::string::npos) {
            if (stats.player_count > 0) stats.player_count--;
            return true;
        }

        // 4. Parse Chunk/Entity counts (e.g., "Chunks: 1024, Entities: 145")
        if (line.find("Chunks:") != std::string::npos && line.find("Entities:") != std::string::npos) {
            std::regex chunkEntityRegex("Chunks:\\s*([0-9]+),\\s*Entities:\\s*([0-9]+)");
            std::smatch matches;
            if (std::regex_search(line, matches, chunkEntityRegex)) {
                try {
                    stats.chunks = std::stoi(matches[1].str());
                    stats.entities = std::stoi(matches[2].str());
                    return true;
                } catch (...) {}
            }
        }

        return false;
    }
};

} // namespace MCDeploy
