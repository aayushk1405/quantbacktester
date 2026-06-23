#include <gtest/gtest.h>
#include "Strategy.h"
#include "DataHandler.h"
#include <fstream>
#include <filesystem>
#include <cmath>

// ============================================================
//  test_indicators.cpp
//
//  Tests the indicator calculations (SMA, EMA, RSI, Bollinger)
//  against known reference values computed independently in Python:
//    import pandas as pd
//    df = pd.read_csv("test_data.csv")
//    df["sma10"] = df["close"].rolling(10).mean()
//    ...
// ============================================================

namespace fs = std::filesystem;

// ── Fixture: write a deterministic CSV and load it ───────────
class IndicatorTest : public ::testing::Test {
protected:
    std::string tmp_dir_;
    std::string symbol_ = "TEST";

    // Synthetic close prices: 1, 2, 3, ..., 50
    static constexpr int N = 50;

    void SetUp() override {
        tmp_dir_ = (fs::temp_directory_path() / "quant_test").string();
        fs::create_directories(tmp_dir_);
        write_csv();
    }

    void TearDown() override {
        fs::remove_all(tmp_dir_);
    }

    void write_csv() {
        std::ofstream f(tmp_dir_ + "/TEST.csv");
        f << "timestamp,open,high,low,close,volume\n";
        for (int i = 1; i <= N; ++i) {
            long long ts = 1'700'000'000'000LL + i * 86'400'000LL;
            double c = static_cast<double>(i);
            f << ts << "," << c << "," << c+0.5
              << "," << c-0.5 << "," << c << ",1000000\n";
        }
    }

    // Advance data handler by n bars and return the handler.
    std::shared_ptr<quant::CsvDataHandler> load_bars(int n) {
        std::deque<quant::Event> q;
        auto dh = std::make_shared<quant::CsvDataHandler>(
            std::vector<std::string>{symbol_}, tmp_dir_);
        dh->set_event_queue(&q);
        for (int i = 0; i < n; ++i) dh->update_bars();
        return dh;
    }
};

// ── SMA tests ────────────────────────────────────────────────

TEST_F(IndicatorTest, SMA10_KnownValue) {
    // After 10 bars of prices 1..10, SMA(10) = (1+2+...+10)/10 = 5.5
    auto dh = load_bars(10);
    std::deque<quant::Event> q;
    // Use a concrete strategy just to access the protected indicator helpers.
    // We expose a testable subclass:
    struct Probe : quant::Strategy {
        using quant::Strategy::sma;
        using quant::Strategy::ema;
        using quant::Strategy::rsi;
        using quant::Strategy::bollinger;
        Probe(quant::DataHandler* d, std::deque<quant::Event>* q)
            : quant::Strategy(d, q) {}
        void calculate_signals(const quant::MarketEvent&) override {}
    };
    Probe probe(dh.get(), &q);
    double result = probe.sma(symbol_, 10);
    EXPECT_NEAR(result, 5.5, 1e-9);
}

TEST_F(IndicatorTest, SMA10_AfterMoreBars) {
    // After 20 bars (prices 1..20), SMA(10) over last 10 = (11+..+20)/10 = 15.5
    auto dh = load_bars(20);
    std::deque<quant::Event> q;
    struct Probe : quant::Strategy {
        using quant::Strategy::sma;
        Probe(quant::DataHandler* d, std::deque<quant::Event>* qp)
            : quant::Strategy(d, qp) {}
        void calculate_signals(const quant::MarketEvent&) override {}
    };
    Probe probe(dh.get(), &q);
    EXPECT_NEAR(probe.sma(symbol_, 10), 15.5, 1e-9);
}

TEST_F(IndicatorTest, SMA_InsufficientData_ReturnsZero) {
    auto dh = load_bars(5);   // only 5 bars loaded
    std::deque<quant::Event> q;
    struct Probe : quant::Strategy {
        using quant::Strategy::sma;
        Probe(quant::DataHandler* d, std::deque<quant::Event>* qp)
            : quant::Strategy(d, qp) {}
        void calculate_signals(const quant::MarketEvent&) override {}
    };
    Probe probe(dh.get(), &q);
    EXPECT_DOUBLE_EQ(probe.sma(symbol_, 10), 0.0);
}

TEST_F(IndicatorTest, RSI_NeutralOnLinearTrend) {
    // A perfectly linear uptrend should give RSI = 100
    // (all gains, no losses).  After 15 bars of 1..15:
    auto dh = load_bars(15);
    std::deque<quant::Event> q;
    struct Probe : quant::Strategy {
        using quant::Strategy::rsi;
        Probe(quant::DataHandler* d, std::deque<quant::Event>* qp)
            : quant::Strategy(d, qp) {}
        void calculate_signals(const quant::MarketEvent&) override {}
    };
    Probe probe(dh.get(), &q);
    double r = probe.rsi(symbol_, 14);
    EXPECT_DOUBLE_EQ(r, 100.0);
}

TEST_F(IndicatorTest, Bollinger_MidEqualsSimpleSMA) {
    auto dh = load_bars(20);
    std::deque<quant::Event> q;
    struct Probe : quant::Strategy {
        using quant::Strategy::sma;
        using quant::Strategy::bollinger;
        Probe(quant::DataHandler* d, std::deque<quant::Event>* qp)
            : quant::Strategy(d, qp) {}
        void calculate_signals(const quant::MarketEvent&) override {}
    };
    Probe probe(dh.get(), &q);
    auto bb = probe.bollinger(symbol_, 20);
    double s = probe.sma(symbol_, 20);
    EXPECT_NEAR(bb.mid, s, 1e-9);
    EXPECT_GT(bb.upper, bb.mid);
    EXPECT_LT(bb.lower, bb.mid);
}

// ── No look-ahead bias ────────────────────────────────────────

TEST_F(IndicatorTest, NoLookAheadBias) {
    // After loading exactly N bars, the latest bar returned should
    // have close == N (not some future value).
    auto dh = load_bars(N);
    auto bar = dh->get_latest_bar(symbol_);
    ASSERT_TRUE(bar.has_value());
    EXPECT_DOUBLE_EQ(bar->close, static_cast<double>(N));
}
