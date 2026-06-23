#pragma once
#include "Event.h"
#include "DataHandler.h"
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

// ============================================================
//  Strategy.h — Strategy interface + built-in implementations.
//
//  A Strategy:
//   1. Receives a MarketEvent (new bar arrived).
//   2. Computes indicator values from history.
//   3. Pushes a SignalEvent onto the shared event queue.
//
//  Built-in strategies shipped with the engine:
//    • SmaStrategy   — dual SMA crossover (fast / slow)
//    • RsiStrategy   — RSI overbought / oversold mean-reversion
//    • MaRsiStrategy — SMA trend filter + RSI entry trigger
//
//  How to add your own:
//    1. Inherit Strategy.
//    2. Implement calculate_signals(const MarketEvent&).
//    3. Register in StrategyFactory (see bottom of this file).
// ============================================================

namespace quant {

// ── Abstract base ─────────────────────────────────────────────
class Strategy {
public:
    explicit Strategy(DataHandler* data, std::deque<Event>* queue)
        : data_(data), queue_(queue) {}

    virtual ~Strategy() = default;

    // Called each time a new bar is processed.
    virtual void calculate_signals(const MarketEvent& event) = 0;

    // Optional: called once before the first bar (parameter setup).
    virtual void on_start() {}

protected:
    void emit_signal(const std::string& symbol,
                     SignalDirection    direction,
                     double             strength = 1.0,
                     Timestamp          ts       = 0);

    // Utility: compute simple moving average over last n closes.
    double sma(const std::string& symbol, int n) const;

    // Utility: compute EMA.
    double ema(const std::string& symbol, int n) const;

    // Utility: compute RSI(n).
    double rsi(const std::string& symbol, int n) const;

    // Utility: compute Bollinger Bands (returns {lower, mid, upper}).
    struct BBands { double lower, mid, upper; };
    BBands bollinger(const std::string& symbol, int n, double k = 2.0) const;

    DataHandler*       data_;
    std::deque<Event>* queue_;
};

// ── Dual SMA crossover ────────────────────────────────────────
//  BUY  when fast SMA crosses above slow SMA.
//  SELL when fast SMA crosses below slow SMA.
class SmaStrategy final : public Strategy {
public:
    SmaStrategy(DataHandler*       data,
                std::deque<Event>* queue,
                int                fast_period = 10,
                int                slow_period = 30);

    void calculate_signals(const MarketEvent& event) override;

private:
    int  fast_period_;
    int  slow_period_;

    // Track previous cross state per symbol to avoid duplicate signals.
    std::unordered_map<std::string, int8_t> prev_position_;
};

// ── RSI mean-reversion ────────────────────────────────────────
//  BUY  when RSI < oversold threshold (default 30).
//  SELL when RSI > overbought threshold (default 70).
//  FLAT when RSI is between thresholds.
class RsiStrategy final : public Strategy {
public:
    RsiStrategy(DataHandler*       data,
                std::deque<Event>* queue,
                int                period         = 14,
                double             oversold_level  = 30.0,
                double             overbought_level = 70.0);

    void calculate_signals(const MarketEvent& event) override;

private:
    int    period_;
    double oversold_;
    double overbought_;
    std::unordered_map<std::string, int8_t> position_;
};

// ── SMA trend filter + RSI entry ─────────────────────────────
//  Only enter long when price > SMA(trend_period) AND RSI < entry_rsi.
//  Exit when price < SMA(trend_period) or RSI > exit_rsi.
class MaRsiStrategy final : public Strategy {
public:
    MaRsiStrategy(DataHandler*       data,
                  std::deque<Event>* queue,
                  int                sma_period   = 200,
                  int                rsi_period   = 14,
                  double             entry_rsi    = 35.0,
                  double             exit_rsi     = 65.0);

    void calculate_signals(const MarketEvent& event) override;

private:
    int    sma_period_;
    int    rsi_period_;
    double entry_rsi_;
    double exit_rsi_;
    std::unordered_map<std::string, int8_t> position_;
};

// ── Bollinger Band mean-reversion ────────────────────────────
class BollingerStrategy final : public Strategy {
public:
    BollingerStrategy(DataHandler*       data,
                      std::deque<Event>* queue,
                      int                period = 20,
                      double             k      = 2.0);

    void calculate_signals(const MarketEvent& event) override;

private:
    int    period_;
    double k_;
    std::unordered_map<std::string, int8_t> position_;
};

// ── Factory ───────────────────────────────────────────────────
//  Used by the Python API to instantiate strategies by name with
//  arbitrary JSON params.
struct StrategyParams {
    std::string name;
    std::unordered_map<std::string, double> params;
};

std::unique_ptr<Strategy> make_strategy(const StrategyParams& cfg,
                                        DataHandler*          data,
                                        std::deque<Event>*    queue);

} // namespace quant
