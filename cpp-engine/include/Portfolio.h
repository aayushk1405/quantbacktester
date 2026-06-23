#pragma once
#include "Event.h"
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
//  Portfolio.h — Position tracking, P&L, and performance stats.
//
//  The Portfolio processes FillEvents and maintains:
//    • Current open positions per symbol.
//    • Realized and unrealized P&L.
//    • Equity curve sampled every bar.
//    • Performance metrics (Sharpe, Sortino, max drawdown, etc.)
//
//  Accounting model:
//    • All positions tracked in units of the base currency.
//    • Short selling is allowed (negative quantity).
//    • Commission is recorded from the FillEvent.
// ============================================================

namespace quant {

struct Position {
    std::string symbol;
    double      quantity      = 0.0;  // positive = long, negative = short
    double      avg_price     = 0.0;
    double      realized_pnl  = 0.0;
    double      unrealized_pnl = 0.0;
    double      commission    = 0.0;
};

struct EquityPoint {
    Timestamp timestamp;
    double    equity;       // cash + market value of all positions
    double    cash;
};

struct PerformanceMetrics {
    double total_return     = 0.0;
    double annualized_return = 0.0;
    double sharpe_ratio     = 0.0;
    double sortino_ratio    = 0.0;
    double max_drawdown     = 0.0;
    double calmar_ratio     = 0.0;
    double win_rate         = 0.0;
    double profit_factor    = 0.0;
    int    total_trades     = 0;
    int    winning_trades   = 0;
    int    losing_trades    = 0;
    double avg_win          = 0.0;
    double avg_loss         = 0.0;
    double volatility       = 0.0;
    double beta             = 0.0;    // vs buy-and-hold
};

class Portfolio {
public:
    explicit Portfolio(double initial_capital = 100'000.0);

    // Called on every new bar to update unrealized P&L.
    void on_market(const MarketEvent& event);

    // Called when an order should be generated from a signal.
    // Pushes an OrderEvent onto the queue.
    void on_signal(const SignalEvent& event, std::deque<Event>* queue);

    // Called when a fill is confirmed.
    void on_fill(const FillEvent& event);

    // Snapshot equity after each bar.
    void record_equity(Timestamp ts);

    PerformanceMetrics compute_metrics() const;

    const std::vector<EquityPoint>&               equity_curve()  const { return equity_curve_; }
    const std::unordered_map<std::string, Position>& positions()  const { return positions_; }
    double cash()    const { return cash_; }
    double equity()  const;

private:
    double initial_capital_;
    double cash_;

    std::unordered_map<std::string, Position> positions_;
    std::vector<EquityPoint>                  equity_curve_;
    std::vector<FillEvent>                    fill_history_;

    // Last known prices (for unrealized P&L).
    std::unordered_map<std::string, double>   last_price_;

    // Position sizing: fixed fractional (Kelly-inspired, capped at 5%).
    double compute_position_size(const SignalEvent& signal) const;
};

// ============================================================
//  RiskManager.h — Pre-trade risk filters.
//
//  Intercepts OrderEvents and either passes them through (emitting
//  another OrderEvent for the execution handler) or vetos / resizes
//  them (emitting a RiskEvent instead).
//
//  Rules implemented:
//    • Max single-position weight (default 10% of equity).
//    • Max total notional exposure (default 100% of equity).
//    • Max open positions count.
//    • Daily loss limit (halt all trading if exceeded).
// ============================================================

class RiskManager {
public:
    struct Config {
        double max_position_pct  = 0.10;   // max 10% per position
        double max_exposure_pct  = 1.00;   // max 100% gross exposure
        int    max_positions     = 20;
        double daily_loss_limit  = 0.02;   // halt if daily loss > 2%
    };

    explicit RiskManager(const Config& cfg = {});

    // Returns true if the order is approved (possibly resized).
    // Pushes modified OrderEvent or RiskEvent onto queue.
    bool evaluate(const OrderEvent& order,
                  const Portfolio&  portfolio,
                  std::deque<Event>* queue);

private:
    Config cfg_;
    double daily_pnl_  = 0.0;
    double day_start_equity_ = 0.0;
};

// ============================================================
//  ExecutionHandler.h — Simulated order execution.
//
//  Turns OrderEvents into FillEvents.  The SimulatedExecution
//  model uses:
//    • MARKET orders fill at the next bar's open price.
//    • LIMIT orders fill if the limit price is touched intrabar.
//    • Slippage: linear impact model (slippage_bps × sqrt(qty/volume)).
//    • Commission: flat fee + proportional (basis points).
// ============================================================

class ExecutionHandler {
public:
    virtual ~ExecutionHandler() = default;
    virtual void execute_order(const OrderEvent& order,
                               const MarketEvent& bar,
                               std::deque<Event>* queue) = 0;
};

class SimulatedExecution final : public ExecutionHandler {
public:
    struct Config {
        double commission_flat = 0.0;       // per trade USD
        double commission_bps  = 1.0;       // 1 bp = 0.01% of notional
        double slippage_bps    = 0.5;       // half-spread
        bool   fill_on_open    = true;      // false → fill on close
    };

    explicit SimulatedExecution(const Config& cfg = {});

    void execute_order(const OrderEvent& order,
                       const MarketEvent& bar,
                       std::deque<Event>* queue) override;

private:
    Config cfg_;
};

} // namespace quant
