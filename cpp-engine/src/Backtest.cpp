#include "Backtest.h"
#include <stdexcept>
#include <iostream>
#include <chrono>

// ============================================================
//  Backtest.cpp — The event loop that wires everything together.
//
//  The loop is intentionally simple and linear.  Every bar:
//    1.  Data handler pushes N MarketEvents (one per symbol).
//    2.  We drain the queue, routing each event to the right handler.
//    3.  Each handler may push more events (signal → order → fill).
//    4.  Portfolio snapshot is taken.
//
//  The progress callback is fired ~every 1% of total bars to
//  allow the Python API layer to stream status updates over WS.
// ============================================================

namespace quant {

Backtest::Backtest(const BacktestConfig& cfg, ProgressCb on_progress)
    : cfg_(cfg), on_progress_(std::move(on_progress)) {

    // Wire up all components.
    data_     = std::make_unique<CsvDataHandler>(cfg_.symbols, cfg_.data_dir);
    data_->set_event_queue(&event_queue_);

    portfolio_ = std::make_unique<Portfolio>(cfg_.initial_capital);
    risk_mgr_  = std::make_unique<RiskManager>(cfg_.risk);
    exec_      = std::make_unique<SimulatedExecution>(cfg_.execution);
    strategy_  = make_strategy(cfg_.strategy, data_.get(), &event_queue_);
}

BacktestResult Backtest::run() {
    BacktestResult result;
    result.run_id = cfg_.run_id;

    auto t_start = std::chrono::steady_clock::now();

    try {
        strategy_->on_start();

        std::size_t bar_count = 0;

        while (data_->update_bars()) {
            ++bar_count;

            // Drain everything placed on the queue this bar.
            while (!event_queue_.empty()) {
                Event e = event_queue_.front();
                event_queue_.pop_front();

                switch (e.type) {
                    case EventType::MARKET:
                        process_market_event(e.data.market);
                        break;
                    case EventType::SIGNAL:
                        process_signal_event(e.data.signal);
                        break;
                    case EventType::ORDER:
                        process_order_event(e.data.order);
                        break;
                    case EventType::FILL:
                        process_fill_event(e.data.fill);
                        break;
                    case EventType::RISK:
                        // Log risk veto, do nothing else.
                        std::cout << "[Risk] Veto on " << e.data.risk.symbol
                                  << ": " << e.data.risk.reason << "\n";
                        break;
                }
            }

            // Snapshot equity after the full bar is processed.
            auto latest_ts = data_->get_latest_bar(cfg_.symbols[0]);
            if (latest_ts)
                portfolio_->record_equity(latest_ts->timestamp);

            // Progress callback every 1%.
            if (on_progress_ && (bar_count % 100 == 0)) {
                // We don't know total bars ahead of time; use approximate pct.
                on_progress_(static_cast<double>(bar_count % 10000) / 100.0,
                             "Processing bar " + std::to_string(bar_count));
            }
        }

        on_progress_ && on_progress_(100.0, "Computing metrics…");

        result.metrics     = portfolio_->compute_metrics();
        result.equity_curve = portfolio_->equity_curve();
        result.success     = true;

        auto t_end = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(
            t_end - t_start).count();
        std::cout << "[Backtest] Completed in " << elapsed_ms << " ms, "
                  << bar_count << " bars processed.\n";

    } catch (const std::exception& ex) {
        result.success       = false;
        result.error_message = ex.what();
        std::cerr << "[Backtest] Error: " << ex.what() << "\n";
    }

    return result;
}

void Backtest::process_market_event(const MarketEvent& e) {
    portfolio_->on_market(e);
    strategy_->calculate_signals(e);
}

void Backtest::process_signal_event(const SignalEvent& e) {
    portfolio_->on_signal(e, &event_queue_);
}

void Backtest::process_order_event(const OrderEvent& e) {
    // First pass through risk manager; it will push a new ORDER if approved.
    risk_mgr_->evaluate(e, *portfolio_, &event_queue_);
}

void Backtest::process_fill_event(const FillEvent& e) {
    portfolio_->on_fill(e);
}

} // namespace quant
