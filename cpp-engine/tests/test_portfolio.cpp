#include <gtest/gtest.h>
#include "Portfolio.h"
#include <cmath>

// ============================================================
//  test_portfolio.cpp
//
//  Validates:
//    • Cash accounting on buy/sell fills.
//    • Commission is deducted correctly.
//    • P&L is realized correctly on round-trip trades.
//    • Equity equals cash + market value at all times.
//    • Performance metrics: Sharpe denominator, max drawdown.
// ============================================================

using namespace quant;

// ── Helper: build a FillEvent ─────────────────────────────────
static FillEvent make_fill(const char* sym, OrderSide side,
                           double qty, double price,
                           double commission = 0.0,
                           Timestamp ts = 1'000'000LL) {
    FillEvent f{};
    std::strncpy(f.symbol, sym, 15);
    f.side       = side;
    f.quantity   = qty;
    f.fill_price = price;
    f.commission = commission;
    f.timestamp  = ts;
    return f;
}

static MarketEvent make_bar(const char* sym, double close,
                            Timestamp ts = 1'000'000LL) {
    MarketEvent b{};
    std::strncpy(b.symbol, sym, 15);
    b.open = b.high = b.low = b.close = close;
    b.volume    = 1'000'000;
    b.timestamp = ts;
    return b;
}

// ── Tests ─────────────────────────────────────────────────────

TEST(Portfolio, InitialEqualsCapital) {
    Portfolio p(50'000.0);
    EXPECT_DOUBLE_EQ(p.cash(),   50'000.0);
    EXPECT_DOUBLE_EQ(p.equity(), 50'000.0);
}

TEST(Portfolio, BuyDeductsCash) {
    Portfolio p(100'000.0);
    // Buy 100 shares @ $50 = $5000 notional + $5 commission
    p.on_fill(make_fill("AAPL", OrderSide::BUY, 100, 50.0, 5.0));
    EXPECT_DOUBLE_EQ(p.cash(), 100'000.0 - 5'000.0 - 5.0);
}

TEST(Portfolio, SellAddsCash) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill("AAPL", OrderSide::BUY,  100, 50.0, 0.0));
    p.on_fill(make_fill("AAPL", OrderSide::SELL, 100, 60.0, 0.0));
    // Cash: 100000 - 5000 + 6000 = 101000
    EXPECT_DOUBLE_EQ(p.cash(), 101'000.0);
}

TEST(Portfolio, RealizedPnLOnRoundTrip) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill("AAPL", OrderSide::BUY,  100, 50.0, 0.0));
    p.on_fill(make_fill("AAPL", OrderSide::SELL, 100, 55.0, 0.0));
    // Realized P&L = 100 * (55 - 50) = $500
    EXPECT_DOUBLE_EQ(p.positions().at("AAPL").realized_pnl, 500.0);
}

TEST(Portfolio, CommissionDeducted) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill("AAPL", OrderSide::BUY,  100, 50.0, 10.0));
    p.on_fill(make_fill("AAPL", OrderSide::SELL, 100, 50.0, 10.0));
    // Total commission = 20; equity = 100000 - 20
    EXPECT_DOUBLE_EQ(p.cash(), 100'000.0 - 20.0);
}

TEST(Portfolio, EquityIncludesUnrealizedPnL) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill("AAPL", OrderSide::BUY, 100, 50.0, 0.0));
    // Mark to market at $55
    p.on_market(make_bar("AAPL", 55.0));
    // Equity = (100000 - 5000) cash + 100*55 mkt_value = 100000 + 500 unrealized
    EXPECT_DOUBLE_EQ(p.equity(), 100'500.0);
}

TEST(Portfolio, MaxDrawdownCalculation) {
    Portfolio p(100'000.0);
    // Simulate equity curve: 100k → 110k → 90k → 95k
    p.record_equity(1000);
    p.on_fill(make_fill("AAPL", OrderSide::BUY, 100, 100.0, 0.0));
    p.on_market(make_bar("AAPL", 110.0));
    p.record_equity(2000);
    p.on_market(make_bar("AAPL",  90.0));
    p.record_equity(3000);
    p.on_market(make_bar("AAPL",  95.0));
    p.record_equity(4000);

    auto m = p.compute_metrics();
    // Peak equity ≈ 100000 + (110-100)*100 = 101000
    // Trough      ≈ 100000 + (90 -100)*100 =  99000
    // Max DD ≈ (101000 - 99000) / 101000 ≈ 1.98%
    EXPECT_GT(m.max_drawdown, 0.0);
    EXPECT_LT(m.max_drawdown, 0.05);  // less than 5%
}

TEST(Portfolio, ZeroTradesMetrics) {
    Portfolio p(100'000.0);
    p.record_equity(1000);
    p.record_equity(2000);
    auto m = p.compute_metrics();
    EXPECT_EQ(m.total_trades,    0);
    EXPECT_DOUBLE_EQ(m.max_drawdown, 0.0);
}

// ── RiskManager ───────────────────────────────────────────────

TEST(RiskManager, PassesThroughValidOrder) {
    RiskManager rm;
    Portfolio p(100'000.0);
    std::deque<Event> q;

    OrderEvent ord{};
    std::strncpy(ord.symbol, "AAPL", 15);
    ord.side     = OrderSide::BUY;
    ord.type     = OrderType::MARKET;
    ord.quantity = 10;

    bool approved = rm.evaluate(ord, p, &q);
    EXPECT_TRUE(approved);
    // An approved order should have been pushed back onto the queue.
    EXPECT_EQ(q.size(), 1u);
    EXPECT_EQ(q.front().type, EventType::ORDER);
}

// ── SimulatedExecution ────────────────────────────────────────

TEST(SimulatedExecution, FillPriceIncludesSlippage) {
    SimulatedExecution::Config cfg;
    cfg.slippage_bps    = 10.0;   // 0.10%
    cfg.commission_bps  = 0.0;
    cfg.commission_flat = 0.0;

    SimulatedExecution exec(cfg);
    std::deque<Event> q;

    OrderEvent ord{};
    std::strncpy(ord.symbol, "AAPL", 15);
    ord.side     = OrderSide::BUY;
    ord.type     = OrderType::MARKET;
    ord.quantity = 100;

    MarketEvent bar = make_bar("AAPL", 0.0);
    bar.open = 100.0;

    exec.execute_order(ord, bar, &q);
    ASSERT_EQ(q.size(), 1u);

    const auto& fill = q.front().data.fill;
    // BUY slippage: price goes UP by 0.10%  → 100.10
    EXPECT_NEAR(fill.fill_price, 100.10, 0.001);
}

TEST(SimulatedExecution, CommissionCalculated) {
    SimulatedExecution::Config cfg;
    cfg.slippage_bps    = 0.0;
    cfg.commission_flat = 1.0;    // $1 flat
    cfg.commission_bps  = 10.0;   // + 0.10% of notional

    SimulatedExecution exec(cfg);
    std::deque<Event> q;

    OrderEvent ord{};
    std::strncpy(ord.symbol, "AAPL", 15);
    ord.side = OrderSide::BUY; ord.quantity = 100;

    MarketEvent bar = make_bar("AAPL", 0.0);
    bar.open = 50.0;

    exec.execute_order(ord, bar, &q);
    const auto& fill = q.front().data.fill;
    // notional = 100 * 50 = 5000; commission = 1 + 5000*0.001 = $6
    EXPECT_NEAR(fill.commission, 6.0, 0.001);
}
