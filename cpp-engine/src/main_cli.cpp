#include "Backtest.h"
#include <iostream>
#include <iomanip>

// ============================================================
//  main_cli.cpp — Quick command-line backtest runner.
//
//  Usage:
//    ./quant_cli <data_dir> <symbol> <strategy>
//  Example:
//    ./quant_cli ../data AAPL sma
// ============================================================

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <data_dir> <symbol> <strategy>\n"
                  << "  strategies: sma | rsi | marsi | bollinger\n";
        return 1;
    }

    quant::BacktestConfig cfg;
    cfg.run_id          = "cli-run-001";
    cfg.data_dir        = argv[1];
    cfg.symbols         = { argv[2] };
    cfg.strategy.name   = argv[3];
    cfg.initial_capital = 100'000.0;

    // Default strategy parameters
    cfg.strategy.params = {
        {"fast_period", 10}, {"slow_period", 30},  // SMA
        {"period",      14}, {"oversold",    30.0}, // RSI
        {"overbought",  70.0},
    };

    auto progress = [](double pct, const std::string& msg) {
        std::cout << "\r[" << std::fixed << std::setprecision(1)
                  << pct << "%] " << msg << std::flush;
    };

    quant::Backtest bt(cfg, progress);
    auto result = bt.run();
    std::cout << "\n";

    if (!result.success) {
        std::cerr << "Backtest failed: " << result.error_message << "\n";
        return 1;
    }

    const auto& m = result.metrics;
    std::cout << "\n══════ Performance Summary ══════\n";
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Total Return        : " << m.total_return * 100    << "%\n";
    std::cout << "Annualized Return   : " << m.annualized_return * 100 << "%\n";
    std::cout << "Sharpe Ratio        : " << m.sharpe_ratio         << "\n";
    std::cout << "Sortino Ratio       : " << m.sortino_ratio        << "\n";
    std::cout << "Max Drawdown        : " << m.max_drawdown * 100   << "%\n";
    std::cout << "Calmar Ratio        : " << m.calmar_ratio         << "\n";
    std::cout << "Win Rate            : " << m.win_rate * 100       << "%\n";
    std::cout << "Profit Factor       : " << m.profit_factor        << "\n";
    std::cout << "Total Trades        : " << m.total_trades          << "\n";
    std::cout << "Volatility (ann.)   : " << m.volatility * 100     << "%\n";
    std::cout << "═════════════════════════════════\n";

    return 0;
}
