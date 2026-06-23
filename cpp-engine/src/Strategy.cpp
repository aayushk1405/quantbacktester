#include "Strategy.h"
#include <cmath>
#include <stdexcept>
#include <numeric>

// ============================================================
//  Strategy.cpp — Indicator math + strategy signal logic.
//
//  All indicator calculations operate on the DataHandler's
//  rolling window, so they never "see" future prices.
// ============================================================

namespace quant {

// ── Base helpers ──────────────────────────────────────────────

void Strategy::emit_signal(const std::string& symbol,
                           SignalDirection    direction,
                           double             strength,
                           Timestamp          ts) {
    Event e;
    e.type = EventType::SIGNAL;
    auto& sig = e.data.signal;

    std::size_t n = std::min(symbol.size(), std::size_t{15});
    std::memcpy(sig.symbol, symbol.c_str(), n);
    sig.symbol[n]   = '\0';
    sig.direction   = direction;
    sig.strength    = strength;
    sig.timestamp   = ts;

    queue_->push_back(e);
}

// Simple Moving Average over the last n closes.
double Strategy::sma(const std::string& symbol, int n) const {
    auto bars = data_->get_latest_bars(symbol, n);
    if (static_cast<int>(bars.size()) < n) return 0.0;
    double sum = 0.0;
    for (const auto& b : bars) sum += b.close;
    return sum / n;
}

// Exponential Moving Average (using Wilder's smoothing).
double Strategy::ema(const std::string& symbol, int n) const {
    auto bars = data_->get_latest_bars(symbol, n * 3);  // enough seed bars
    if (static_cast<int>(bars.size()) < n) return 0.0;

    double alpha = 2.0 / (n + 1.0);
    double ema_val = bars[0].close;
    for (std::size_t i = 1; i < bars.size(); ++i)
        ema_val = bars[i].close * alpha + ema_val * (1.0 - alpha);
    return ema_val;
}

// RSI using Wilder's smoothed average gain/loss.
double Strategy::rsi(const std::string& symbol, int n) const {
    int needed = n + 1;
    auto bars = data_->get_latest_bars(symbol, needed);
    if (static_cast<int>(bars.size()) < needed) return 50.0;  // neutral

    double avg_gain = 0.0, avg_loss = 0.0;
    for (int i = 1; i <= n; ++i) {
        double diff = bars[i].close - bars[i-1].close;
        if (diff > 0) avg_gain += diff;
        else          avg_loss -= diff;
    }
    avg_gain /= n;
    avg_loss /= n;

    if (avg_loss < 1e-10) return 100.0;
    double rs = avg_gain / avg_loss;
    return 100.0 - 100.0 / (1.0 + rs);
}

// Bollinger Bands.
Strategy::BBands Strategy::bollinger(const std::string& symbol,
                                     int n, double k) const {
    auto bars = data_->get_latest_bars(symbol, n);
    if (static_cast<int>(bars.size()) < n) return {0,0,0};

    double sum = 0.0;
    for (const auto& b : bars) sum += b.close;
    double mid = sum / n;

    double var = 0.0;
    for (const auto& b : bars) {
        double d = b.close - mid;
        var += d * d;
    }
    double std_dev = std::sqrt(var / n);

    return {mid - k * std_dev, mid, mid + k * std_dev};
}

// ── SmaStrategy ───────────────────────────────────────────────

SmaStrategy::SmaStrategy(DataHandler* data, std::deque<Event>* queue,
                         int fast, int slow)
    : Strategy(data, queue), fast_period_(fast), slow_period_(slow) {}

void SmaStrategy::calculate_signals(const MarketEvent& event) {
    std::string sym = event.symbol;
    double fast_ma = sma(sym, fast_period_);
    double slow_ma = sma(sym, slow_period_);

    if (fast_ma == 0.0 || slow_ma == 0.0) return;  // not enough history

    int8_t& pos = prev_position_[sym];
    int8_t new_pos = (fast_ma > slow_ma) ? 1 : -1;

    if (new_pos != pos) {
        // Crossover detected → emit signal
        SignalDirection dir = (new_pos == 1)
            ? SignalDirection::LONG
            : SignalDirection::SHORT;

        // Strength = normalized gap between the two MAs.
        double gap = std::abs(fast_ma - slow_ma) / slow_ma;
        double strength = std::min(1.0, gap * 100.0);

        emit_signal(sym, dir, strength, event.timestamp);
        pos = new_pos;
    }
}

// ── RsiStrategy ───────────────────────────────────────────────

RsiStrategy::RsiStrategy(DataHandler* data, std::deque<Event>* queue,
                         int period, double os, double ob)
    : Strategy(data, queue), period_(period),
      oversold_(os), overbought_(ob) {}

void RsiStrategy::calculate_signals(const MarketEvent& event) {
    std::string sym = event.symbol;
    double r = rsi(sym, period_);
    int8_t& pos = position_[sym];

    if (r < oversold_ && pos != 1) {
        double strength = 1.0 - r / oversold_;
        emit_signal(sym, SignalDirection::LONG, strength, event.timestamp);
        pos = 1;
    } else if (r > overbought_ && pos != -1) {
        double strength = (r - overbought_) / (100.0 - overbought_);
        emit_signal(sym, SignalDirection::SHORT, strength, event.timestamp);
        pos = -1;
    }
}

// ── MaRsiStrategy ────────────────────────────────────────────

MaRsiStrategy::MaRsiStrategy(DataHandler* data, std::deque<Event>* queue,
                             int sma_p, int rsi_p, double e_rsi, double x_rsi)
    : Strategy(data, queue), sma_period_(sma_p), rsi_period_(rsi_p),
      entry_rsi_(e_rsi), exit_rsi_(x_rsi) {}

void MaRsiStrategy::calculate_signals(const MarketEvent& event) {
    std::string sym = event.symbol;
    double trend_ma = sma(sym, sma_period_);
    double r = rsi(sym, rsi_period_);

    if (trend_ma == 0.0) return;

    int8_t& pos = position_[sym];
    bool above_trend = event.close > trend_ma;

    if (above_trend && r < entry_rsi_ && pos != 1) {
        emit_signal(sym, SignalDirection::LONG,
                    1.0 - r / entry_rsi_, event.timestamp);
        pos = 1;
    } else if ((!above_trend || r > exit_rsi_) && pos == 1) {
        emit_signal(sym, SignalDirection::FLAT, 1.0, event.timestamp);
        pos = 0;
    }
}

// ── BollingerStrategy ─────────────────────────────────────────

BollingerStrategy::BollingerStrategy(DataHandler* data,
                                     std::deque<Event>* queue,
                                     int period, double k)
    : Strategy(data, queue), period_(period), k_(k) {}

void BollingerStrategy::calculate_signals(const MarketEvent& event) {
    std::string sym = event.symbol;
    auto bb = bollinger(sym, period_, k_);

    if (bb.mid == 0.0) return;

    int8_t& pos = position_[sym];
    double c = event.close;

    if (c < bb.lower && pos != 1) {
        double strength = (bb.lower - c) / (bb.mid - bb.lower);
        emit_signal(sym, SignalDirection::LONG,
                    std::min(1.0, strength), event.timestamp);
        pos = 1;
    } else if (c > bb.upper && pos != -1) {
        double strength = (c - bb.upper) / (bb.upper - bb.mid);
        emit_signal(sym, SignalDirection::SHORT,
                    std::min(1.0, strength), event.timestamp);
        pos = -1;
    } else if (c > bb.mid && pos == 1) {
        emit_signal(sym, SignalDirection::FLAT, 1.0, event.timestamp);
        pos = 0;
    } else if (c < bb.mid && pos == -1) {
        emit_signal(sym, SignalDirection::FLAT, 1.0, event.timestamp);
        pos = 0;
    }
}

// ── Factory ───────────────────────────────────────────────────

std::unique_ptr<Strategy> make_strategy(const StrategyParams& cfg,
                                        DataHandler*          data,
                                        std::deque<Event>*    queue) {
    auto p = [&](const std::string& k, double def) -> double {
        auto it = cfg.params.find(k);
        return (it != cfg.params.end()) ? it->second : def;
    };

    if (cfg.name == "sma" || cfg.name == "SmaStrategy") {
        return std::make_unique<SmaStrategy>(data, queue,
            (int)p("fast_period", 10), (int)p("slow_period", 30));
    }
    if (cfg.name == "rsi" || cfg.name == "RsiStrategy") {
        return std::make_unique<RsiStrategy>(data, queue,
            (int)p("period", 14), p("oversold", 30.0), p("overbought", 70.0));
    }
    if (cfg.name == "marsi" || cfg.name == "MaRsiStrategy") {
        return std::make_unique<MaRsiStrategy>(data, queue,
            (int)p("sma_period", 200), (int)p("rsi_period", 14),
            p("entry_rsi", 35.0), p("exit_rsi", 65.0));
    }
    if (cfg.name == "bollinger" || cfg.name == "BollingerStrategy") {
        return std::make_unique<BollingerStrategy>(data, queue,
            (int)p("period", 20), p("k", 2.0));
    }
    throw std::invalid_argument("Unknown strategy: " + cfg.name);
}

} // namespace quant
