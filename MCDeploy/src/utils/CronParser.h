#pragma once

// ============================================================
// MCDeploy - Cron Parser
// ------------------------------------------------------------
// Minimal 5-field POSIX-style cron parser used by the scheduled
// tasks feature. Supports:
//   *          — any value
//   N          — a specific number
//   a,b,c      — a list of numbers
//   a-b        — inclusive range
//   * / n      — step (every n across the field)
//   a-b/n      — stepped range
//   */n        — every n starting at field minimum
//
// Not supported (intentionally kept small): named months/days,
// L/W/# specifiers, seconds, timezones. Everything is evaluated
// in the local timezone via std::localtime.
// ============================================================

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace MCDeploy {

class CronExpression {
public:
    static std::optional<CronExpression> parse(const std::string& expr) {
        CronExpression c;
        std::vector<std::string> parts;
        std::stringstream ss(expr);
        std::string tok;
        while (ss >> tok) parts.push_back(tok);
        if (parts.size() != 5) return std::nullopt;
        if (!parseField(parts[0], 0, 59, c.m_minute))     return std::nullopt;
        if (!parseField(parts[1], 0, 23, c.m_hour))       return std::nullopt;
        if (!parseField(parts[2], 1, 31, c.m_dom))        return std::nullopt;
        if (!parseField(parts[3], 1, 12, c.m_month))      return std::nullopt;
        // day-of-week: 0..6 with 0 = Sunday. Allow 7 as an alias for Sunday.
        if (!parseField(parts[4], 0,  7, c.m_dow))        return std::nullopt;
        for (auto& v : c.m_dow) if (v == 7) v = 0;
        std::sort(c.m_dow.begin(), c.m_dow.end());
        c.m_dow.erase(std::unique(c.m_dow.begin(), c.m_dow.end()), c.m_dow.end());
        return c;
    }

    // Given a "current" time_t, return the next matching time_t strictly
    // greater than `from`. If nothing matches within one year we give up
    // (indicates an unsatisfiable schedule like "Feb 31") and return 0.
    std::time_t nextAfter(std::time_t from) const {
        std::tm t{};
#ifdef _WIN32
        localtime_s(&t, &from);
#else
        localtime_r(&from, &t);
#endif
        // Start from the next minute — cron granularity is 1 minute.
        t.tm_sec = 0;
        t.tm_min += 1;
        std::mktime(&t);

        for (int guard = 0; guard < 366 * 24 * 60; guard++) {
            int mm = t.tm_min;
            int hh = t.tm_hour;
            int dom = t.tm_mday;
            int mon = t.tm_mon + 1;
            int dow = t.tm_wday;

            if (!contains(m_minute, mm) || !contains(m_hour, hh) ||
                !contains(m_month, mon) ||
                (!contains(m_dom, dom) && !contains(m_dow, dow))) {
                // Standard cron OR-semantics for dom/dow when either is
                // restricted. If both are '*' we fall through since both
                // vectors will contain the value.
                // Increment by 1 minute and retry.
                t.tm_min += 1;
                std::mktime(&t);
                continue;
            }
            return std::mktime(&t);
        }
        return 0;
    }

private:
    std::vector<int> m_minute, m_hour, m_dom, m_month, m_dow;

    static bool contains(const std::vector<int>& v, int x) {
        return std::find(v.begin(), v.end(), x) != v.end();
    }

    static bool parseNumber(const std::string& s, int& out) {
        if (s.empty()) return false;
        for (char c : s) if (c < '0' || c > '9') return false;
        try { out = std::stoi(s); } catch (...) { return false; }
        return true;
    }

    // Parses one field into a set of accepted values.
    static bool parseField(const std::string& field, int lo, int hi, std::vector<int>& out) {
        out.clear();
        std::stringstream ss(field);
        std::string chunk;
        while (std::getline(ss, chunk, ',')) {
            if (!parseChunk(chunk, lo, hi, out)) return false;
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return !out.empty();
    }

    static bool parseChunk(const std::string& chunk, int lo, int hi, std::vector<int>& out) {
        // Split on '/'
        std::string base = chunk;
        int step = 1;
        auto slash = chunk.find('/');
        if (slash != std::string::npos) {
            base = chunk.substr(0, slash);
            std::string stepStr = chunk.substr(slash + 1);
            if (!parseNumber(stepStr, step) || step <= 0) return false;
        }

        int rangeStart = lo, rangeEnd = hi;
        if (base == "*") {
            // range = [lo, hi]
        } else {
            auto dash = base.find('-');
            if (dash != std::string::npos) {
                int a, b;
                if (!parseNumber(base.substr(0, dash), a)) return false;
                if (!parseNumber(base.substr(dash + 1), b)) return false;
                if (a > b || a < lo || b > hi) return false;
                rangeStart = a; rangeEnd = b;
            } else {
                int v;
                if (!parseNumber(base, v)) return false;
                if (v < lo || v > hi) return false;
                rangeStart = v; rangeEnd = v;
            }
        }
        for (int v = rangeStart; v <= rangeEnd; v += step) out.push_back(v);
        return true;
    }
};

} // namespace MCDeploy
