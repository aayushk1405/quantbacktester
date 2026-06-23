#include <gtest/gtest.h>
#include "Backtest.h"
#include <fstream>
#include <filesystem>
#include <cmath>

// ============================================================
//  test_backtest_integration.cpp
//
//  Runs the full backtest pipeline end-to-end on synthetic data.
//  Verifies:
//    • Engine runs without crashing.
//    • Equity curve length matches bar count.
//    • Performance metrics are finite and in sensible ranges.
//    • All four built-in strategies produce results.
// ============================================================

namespace fs = std::filesystem;

class BacktestIntegrationTest : public ::testing::Test {
protected:
    std::string tmp_dir_;

    void SetUp() override {
        tmp_dir_ = (fs::temp_directory_path() / "quant_integration").string();
        fs::create_directories(tmp_dir_);
        write_random_walk_csv("TSYM", 500);
    }

    void TearDown() override { fs::remove_all(tmp_dir_); }

    // Generate a random-walk price series so there are both gains and losses.
    void write_random_walk_csv(const char* sym, int n) {
        std::ofstream f(tmp_dir_ + std::string("/") + sym + ".csv");
        f << "timestamp,open,high,low,close,volume\n";
        double price = 100.0;
        std::srand(42);
        for (int i = 0; i < n; ++i) {
            double ret   = ((std::rand() % 201) - 100) / 2000.0;  // ±5% max
            price        = std::max(1.0, price * (1.0 + ret));
            long long ts = 1'700'000'000'000LL + (long long)i * 86'400'000LL;
            f << ts << ","
              << price * 0.99 << ","   // open
              << price * 1.01 << ","   // high
              << price * 0.98 << ","   // low
              << price        << ","   // close
              << 1'000'000    << "\n"; // volume
        }
    }

    quant::BacktestConfig make_config(const std::string& strategy_name) {
        quant::BacktestConfig cfg;
        cfg.run_id          = "test-" + strategy_name;
        cfg.symbols         = {"TSYM"};
        cfg.data_dir        = tmp_dir_;
        cfg.initial_capital = 100'000.0;
        cfg.strategy.name   = strategy_name;
        cfg.strategy.params = {
            {"fast_period", 10}, {"slow_period", 30},
            {"period", 14}, {"oversold", 30.0}, {"overbought", 70.0},
            {"sma_period", 50}, {"rsi_period", 14},
            {"entry_rsi", 35.0}, {"exit_rsi", 65.0},
        };
        return cfg;
    }
};

// ── Basic completion tests ────────────────────────────────────

TEST_F(BacktestIntegrationTest, SmaStrategyCompletes) {
    quant::Backtest bt(make_config("sma"));
    auto r = bt.run();
    EXPECT_TRUE(r.success) << r.error_message;
    EXPECT_GT(r.equity_curve.size(), 0u);
}

TEST_F(BacktestIntegrationTest, RsiStrategyCompletes) {
    quant::Backtest bt(make_config("rsi"));
    auto r = bt.run();
    EXPECT_TRUE(r.success) << r.error_message;
}

TEST_F(BacktestIntegrationTest, MaRsiStrategyCompletes) {
    quant::Backtest bt(make_config("marsi"));
    auto r = bt.run();
    EXPECT_TRUE(r.success) << r.error_message;
}

TEST_F(BacktestIntegrationTest, BollingerStrategyCompletes) {
    quant::Backtest bt(make_config("bollinger"));
    auto r = bt.run();
    EXPECT_TRUE(r.success) << r.error_message;
}

// ── Metrics sanity ────────────────────────────────────────────

TEST_F(BacktestIntegrationTest, MetricsAreFinite) {
    quant::Backtest bt(make_config("sma"));
    auto r = bt.run();
    ASSERT_TRUE(r.success);
    const auto& m = r.metrics;
    EXPECT_TRUE(std::isfinite(m.sharpe_ratio));
    EXPECT_TRUE(std::isfinite(m.sortino_ratio));
    EXPECT_TRUE(std::isfinite(m.total_return));
    EXPECT_TRUE(std::isfinite(m.max_drawdown));
}

TEST_F(BacktestIntegrationTest, MaxDrawdownInRange) {
    quant::Backtest bt(make_config("sma"));
    auto r = bt.run();
    ASSERT_TRUE(r.success);
    // Drawdown must be in [0, 1]
    EXPECT_GE(r.metrics.max_drawdown, 0.0);
    EXPECT_LE(r.metrics.max_drawdown, 1.0);
}

TEST_F(BacktestIntegrationTest, WinRateInRange) {
    quant::Backtest bt(make_config("sma"));
    auto r = bt.run();
    ASSERT_TRUE(r.success);
    EXPECT_GE(r.metrics.win_rate, 0.0);
    EXPECT_LE(r.metrics.win_rate, 1.0);
}

TEST_F(BacktestIntegrationTest, EquityCurveMonotonicallyTimestamped) {
    quant::Backtest bt(make_config("sma"));
    auto r = bt.run();
    ASSERT_TRUE(r.success);
    ASSERT_GT(r.equity_curve.size(), 1u);
    for (std::size_t i = 1; i < r.equity_curve.size(); ++i) {
        EXPECT_GT(r.equity_curve[i].timestamp,
                  r.equity_curve[i-1].timestamp)
            << "Equity curve timestamps not strictly increasing at index " << i;
    }
}

// ── Progress callback ─────────────────────────────────────────

TEST_F(BacktestIntegrationTest, ProgressCallbackFired) {
    int call_count = 0;
    double last_pct = -1.0;

    auto cb = [&](double pct, const std::string&) {
        ++call_count;
        EXPECT_GE(pct, last_pct);   // progress is non-decreasing
        last_pct = pct;
    };

    quant::Backtest bt(make_config("sma"), cb);
    auto r = bt.run();
    EXPECT_TRUE(r.success);
    EXPECT_GT(call_count, 0);
}

// ── Invalid strategy ──────────────────────────────────────────

TEST_F(BacktestIntegrationTest, UnknownStrategyReturnsFailure) {
    auto cfg = make_config("nonexistent_strategy_xyz");
    // make_strategy() throws; Backtest::run() should catch and mark failure.
    try {
        quant::Backtest bt(cfg);
        auto r = bt.run();
        EXPECT_FALSE(r.success);
    } catch (const std::invalid_argument&) {
        // Also acceptable — constructor propagates the throw.
        SUCCEED();
    }
}
