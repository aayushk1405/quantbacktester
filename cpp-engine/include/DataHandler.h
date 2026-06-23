#pragma once
#include "Event.h"
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

// ============================================================
//  DataHandler.h — Abstracts all market-data access.
//
//  The backtest engine only ever calls DataHandler methods; it
//  never touches files or databases directly.  This makes it
//  trivial to swap CSV replay for a live-data WebSocket feed
//  without touching strategy code.
//
//  Design:
//    • StreamingDataHandler   → iterates through a CSV file
//      bar-by-bar, injecting MarketEvents into the queue.
//    • We deliberately replay bars one at a time so that the
//      strategy cannot "peek" into the future (look-ahead bias).
// ============================================================

namespace quant {

// ── Abstract interface ────────────────────────────────────────
class DataHandler {
public:
    virtual ~DataHandler() = default;

    // Returns false when all data is exhausted.
    virtual bool update_bars() = 0;

    // Latest complete bar for a symbol (returns nullopt if not yet available).
    virtual std::optional<MarketEvent> get_latest_bar(const std::string& symbol) const = 0;

    // Last N bars (oldest first).  Useful for rolling-window indicators.
    virtual std::vector<MarketEvent> get_latest_bars(const std::string& symbol, int n) const = 0;

    // Feed the event queue pointer so the handler can push MarketEvents.
    virtual void set_event_queue(std::deque<Event>* q) { queue_ = q; }

    const std::vector<std::string>& symbols() const { return symbols_; }

protected:
    std::deque<Event>*       queue_   = nullptr;
    std::vector<std::string> symbols_;
};

// ── CSV replay handler ────────────────────────────────────────
//  Expects a header row: timestamp,open,high,low,close,volume
//  Rows must be sorted by timestamp ascending.
class CsvDataHandler final : public DataHandler {
public:
    // symbols : list of tickers to load (one CSV per ticker in data_dir).
    // data_dir: directory containing <SYMBOL>.csv files.
    CsvDataHandler(const std::vector<std::string>& symbols,
                   const std::string&               data_dir);

    bool                      update_bars()                                          override;
    std::optional<MarketEvent> get_latest_bar(const std::string& symbol) const      override;
    std::vector<MarketEvent>   get_latest_bars(const std::string& symbol, int n) const override;

private:
    void load_csv(const std::string& symbol, const std::string& path);

    // symbol → all rows (sorted asc)
    std::unordered_map<std::string, std::vector<MarketEvent>> all_data_;

    // symbol → cursor (how many rows have been replayed so far)
    std::unordered_map<std::string, std::size_t> cursors_;

    // Rolling window of the last 500 bars per symbol
    static constexpr int kWindowSize = 500;
    std::unordered_map<std::string, std::deque<MarketEvent>> bar_window_;
};

} // namespace quant
