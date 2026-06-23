#include "DataHandler.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <iostream>

// ============================================================
//  DataHandler.cpp
//
//  CsvDataHandler:
//    • Loads ALL rows for each symbol into memory at construction
//      time (fast random-access later).
//    • update_bars() advances the cursor for each symbol by one
//      bar and pushes a MarketEvent onto the queue.
//    • Look-ahead prevention: the bar_window_ only ever contains
//      bars whose cursor has already been advanced.
// ============================================================

namespace quant {

// ── Helpers ───────────────────────────────────────────────────

static void copy_symbol(char (&dst)[16], const std::string& src) {
    std::size_t n = std::min(src.size(), std::size_t{15});
    std::memcpy(dst, src.c_str(), n);
    dst[n] = '\0';
}

// Parse ISO-8601 or Unix-ms timestamp string → int64_t ms.
static Timestamp parse_timestamp(const std::string& s) {
    // If it's a pure integer (Unix ms), use it directly.
    bool is_int = !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
    if (is_int) return std::stoll(s);

    // Otherwise attempt YYYY-MM-DD or YYYY-MM-DD HH:MM:SS
    std::tm t{};
    std::istringstream ss(s);
    ss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        std::istringstream ss2(s);
        ss2 >> std::get_time(&t, "%Y-%m-%d");
    }
    std::time_t time = std::mktime(&t);
    return static_cast<Timestamp>(time) * 1000LL;  // → ms
}

// ── CsvDataHandler ────────────────────────────────────────────

CsvDataHandler::CsvDataHandler(const std::vector<std::string>& symbols,
                               const std::string&               data_dir) {
    symbols_ = symbols;
    for (const auto& sym : symbols_) {
        std::string path = data_dir + "/" + sym + ".csv";
        load_csv(sym, path);
        cursors_[sym] = 0;
    }
}

void CsvDataHandler::load_csv(const std::string& symbol,
                              const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open data file: " + path);

    std::string line;
    std::getline(file, line);  // skip header

    std::vector<MarketEvent> rows;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        std::vector<std::string> fields;
        while (std::getline(ss, token, ','))
            fields.push_back(token);

        if (fields.size() < 6) continue;   // malformed row

        MarketEvent bar{};
        copy_symbol(bar.symbol, symbol);
        bar.timestamp = parse_timestamp(fields[0]);
        bar.open      = std::stod(fields[1]);
        bar.high      = std::stod(fields[2]);
        bar.low       = std::stod(fields[3]);
        bar.close     = std::stod(fields[4]);
        bar.volume    = std::stod(fields[5]);
        rows.push_back(bar);
    }

    // Sort ascending by timestamp (guard against unsorted CSVs).
    std::sort(rows.begin(), rows.end(),
              [](const MarketEvent& a, const MarketEvent& b) {
                  return a.timestamp < b.timestamp;
              });

    all_data_[symbol] = std::move(rows);
    std::cout << "[DataHandler] Loaded " << all_data_[symbol].size()
              << " bars for " << symbol << "\n";
}

bool CsvDataHandler::update_bars() {
    if (!queue_) return false;

    bool any_advanced = false;

    for (const auto& sym : symbols_) {
        auto& cursor = cursors_[sym];
        const auto& rows = all_data_.at(sym);

        if (cursor >= rows.size()) continue;

        const MarketEvent& bar = rows[cursor];
        ++cursor;

        // Append to rolling window
        auto& window = bar_window_[sym];
        window.push_back(bar);
        if (window.size() > static_cast<std::size_t>(kWindowSize))
            window.pop_front();

        // Push onto the shared event queue
        Event e;
        e.type        = EventType::MARKET;
        e.data.market = bar;
        queue_->push_back(e);

        any_advanced = true;
    }

    return any_advanced;
}

std::optional<MarketEvent>
CsvDataHandler::get_latest_bar(const std::string& symbol) const {
    auto it = bar_window_.find(symbol);
    if (it == bar_window_.end() || it->second.empty())
        return std::nullopt;
    return it->second.back();
}

std::vector<MarketEvent>
CsvDataHandler::get_latest_bars(const std::string& symbol, int n) const {
    auto it = bar_window_.find(symbol);
    if (it == bar_window_.end()) return {};

    const auto& window = it->second;
    int sz = static_cast<int>(window.size());
    int start = std::max(0, sz - n);

    return std::vector<MarketEvent>(
        window.begin() + start, window.end());
}

} // namespace quant
