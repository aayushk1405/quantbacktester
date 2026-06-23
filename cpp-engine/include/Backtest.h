#pragma once
#include "DataHandler.h"
#include "Strategy.h"
#include "Portfolio.h"
#include <functional>
#include <memory>
#include <string>

// ============================================================
//  Backtest.h — The main event loop.
//
//  The Backtest class wires all components together and drives
//  the simulation.  The loop:
//
//    while data.update_bars():          ← advance one bar
//      while queue not empty:           ← drain events
//        e = queue.pop_front()
//        if MARKET  → strategy.calculate_signals(e)
//        if SIGNAL  → portfolio.on_signal(e)
//        if ORDER   → risk_manager.evaluate(e) → maybe pushes ORDER
//        if ORDER   → execution.execute_order(e) → pushes FILL
//        if FILL    → portfolio.on_fill(e)
//      portfolio.record_equity(ts)
//
//  All components are injected via constructor (DI / testability).
//
//  Progress callback: called after each bar with % complete.
//  This feeds the WebSocket streaming in the API layer.
// ============================================================

namespace quant {

struct BacktestConfig {
    std::string              run_id;         // UUID from the API
    std::vector<std::string> symbols;
    std::string              data_dir;
    double                   initial_capital = 100'000.0;
    StrategyParams           strategy;
    SimulatedExecution::Config execution;
    RiskManager::Config        risk;
};

struct BacktestResult {
    std::string         run_id;
    PerformanceMetrics  metrics;
    std::vector<EquityPoint> equity_curve;
    std::vector<FillEvent>   trades;
    bool   success = false;
    std::string error_message;
};

class Backtest {
public:
    using ProgressCb = std::function<void(double pct, const std::string& msg)>;

    explicit Backtest(const BacktestConfig& cfg,
                      ProgressCb           on_progress = nullptr);

    // Runs synchronously; returns when complete.
    BacktestResult run();

private:
    BacktestConfig cfg_;
    ProgressCb     on_progress_;

    std::deque<Event>                event_queue_;
    std::unique_ptr<DataHandler>     data_;
    std::unique_ptr<Strategy>        strategy_;
    std::unique_ptr<Portfolio>       portfolio_;
    std::unique_ptr<RiskManager>     risk_mgr_;
    std::unique_ptr<ExecutionHandler> exec_;

    void process_market_event(const MarketEvent& e);
    void process_signal_event(const SignalEvent& e);
    void process_order_event(const OrderEvent& e);
    void process_fill_event(const FillEvent& e);
};

} // namespace quant
