#include <gtest/gtest.h>
#include "DataHandler.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

class DataHandlerTest : public ::testing::Test {
protected:
    std::string tmp_;

    void SetUp() override {
        tmp_ = (fs::temp_directory_path() / "quant_dh_test").string();
        fs::create_directories(tmp_);
        std::ofstream f(tmp_ + "/SPY.csv");
        f << "timestamp,open,high,low,close,volume\n";
        for (int i = 1; i <= 20; ++i) {
            long long ts = 1'700'000'000'000LL + (long long)i * 86'400'000LL;
            f << ts << "," << i << "," << i+1 << ","
              << i-1 << "," << i << ",500000\n";
        }
    }
    void TearDown() override { fs::remove_all(tmp_); }
};

TEST_F(DataHandlerTest, LoadsCorrectBarCount) {
    std::deque<quant::Event> q;
    quant::CsvDataHandler dh({"SPY"}, tmp_);
    dh.set_event_queue(&q);
    int bars = 0;
    while (dh.update_bars()) ++bars;
    EXPECT_EQ(bars, 20);
}

TEST_F(DataHandlerTest, FirstBarIsEarliest) {
    std::deque<quant::Event> q;
    quant::CsvDataHandler dh({"SPY"}, tmp_);
    dh.set_event_queue(&q);
    dh.update_bars();
    ASSERT_FALSE(q.empty());
    EXPECT_DOUBLE_EQ(q.front().data.market.close, 1.0);
}

TEST_F(DataHandlerTest, GetLatestBarNullBeforeAnyUpdate) {
    quant::CsvDataHandler dh({"SPY"}, tmp_);
    EXPECT_FALSE(dh.get_latest_bar("SPY").has_value());
}

TEST_F(DataHandlerTest, GetLatestBarsReturnsCorrectWindow) {
    std::deque<quant::Event> q;
    quant::CsvDataHandler dh({"SPY"}, tmp_);
    dh.set_event_queue(&q);
    for (int i = 0; i < 10; ++i) dh.update_bars();
    auto bars = dh.get_latest_bars("SPY", 5);
    ASSERT_EQ(bars.size(), 5u);
    EXPECT_DOUBLE_EQ(bars.back().close, 10.0);  // last loaded = bar #10
    EXPECT_DOUBLE_EQ(bars.front().close, 6.0);  // oldest in window of 5
}

TEST_F(DataHandlerTest, MissingCsvThrows) {
    EXPECT_THROW(
        quant::CsvDataHandler({"MISSING"}, tmp_),
        std::runtime_error
    );
}

TEST_F(DataHandlerTest, UpdateBarsReturnsFalseWhenExhausted) {
    std::deque<quant::Event> q;
    quant::CsvDataHandler dh({"SPY"}, tmp_);
    dh.set_event_queue(&q);
    for (int i = 0; i < 20; ++i) dh.update_bars();
    EXPECT_FALSE(dh.update_bars());   // 21st call: no more data
}
