#include "Portfolio.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <cstring>

// ============================================================
//  Portfolio.cpp — Position bookkeeping + performance metrics.
// ============================================================

namespace quant {

static void copy_symbol(char (&dst)[16], const std::string& src) {
    std::size_t n = std::min(src.size(), std::size_t{15});
    std::memcpy(dst, src.c_str(), n);
    dst[n] = '\0';
}

// ── Portfolio ────────────────────────────────────────────────

Portfolio::Portfolio(double initial_capital)
    : initial_capital_(initial_capital), cash_(initial_capital) {}

double Portfolio::equity() const {
    double mkt_value = 0.0;
    for (const auto& [sym, pos] : positions_) {
        auto it = last_price_.find(sym);
        if (it != last_price_.end())
            mkt_value += pos.quantity * it->second;
    }
    return cash_ + mkt_value;
}

void Portfolio::on_market(const MarketEvent& event) {
    std::string sym = event.symbol;
    last_price_[sym] = event.close;

    if (positions_.count(sym)) {
        auto& pos = positions_[sym];
        pos.unrealized_pnl = pos.quantity * (event.close - pos.avg_price);
    }
}

double Portfolio::compute_position_size(const SignalEvent& signal) const {
    // Fixed-fractional: allocate strength × 5% of equity per trade.
    double target_pct  = signal.strength * 0.05;
    double target_value = equity() * target_pct;
    auto it = last_price_.find(signal.symbol);
    if (it == last_price_.end() || it->second <= 0) return 0.0;
    return std::floor(target_value / it->second);
}

void Portfolio::on_signal(const SignalEvent& signal, std::deque<Event>* queue) {
    std::string sym = signal.symbol;
    double qty = compute_position_size(signal);
    if (qty <= 0) return;

    Event e;
    e.type = EventType::ORDER;
    auto& ord = e.data.order;
    copy_symbol(ord.symbol, sym);
    ord.type      = OrderType::MARKET;
    ord.timestamp = signal.timestamp;

    if (signal.direction == SignalDirection::LONG) {
        ord.side     = OrderSide::BUY;
        ord.quantity = qty;
    } else if (signal.direction == SignalDirection::SHORT) {
        ord.side     = OrderSide::SELL;
        ord.quantity = qty;
    } else {
        // FLAT: close the position.
        auto pit = positions_.find(sym);
        if (pit == positions_.end()) return;
        ord.side     = (pit->second.quantity > 0) ? OrderSide::SELL : OrderSide::BUY;
        ord.quantity = std::abs(pit->second.quantity);
    }

    queue->push_back(e);
}

void Portfolio::on_fill(const FillEvent& fill) {
    std::string sym = fill.symbol;
    auto& pos = positions_[sym];
    pos.symbol     = sym;
    pos.commission += fill.commission;

    double fill_value = fill.quantity * fill.fill_price;

    if (fill.side == OrderSide::BUY) {
        double old_value = pos.quantity * pos.avg_price;
        pos.quantity  += fill.quantity;
        if (pos.quantity > 0)
            pos.avg_price = (old_value + fill_value) / pos.quantity;
        cash_ -= fill_value + fill.commission;
    } else {
        // SELL: realize gain/loss on the closed portion.
        double closed_qty = std::min(fill.quantity, std::abs(pos.quantity));
        double pnl = closed_qty * (fill.fill_price - pos.avg_price);
        pos.realized_pnl += pnl;
        pos.quantity     -= fill.quantity;
        cash_            += fill_value - fill.commission;
    }

    fill_history_.push_back(fill);
}

void Portfolio::record_equity(Timestamp ts) {
    equity_curve_.push_back({ts, equity(), cash_});
}

// ── Performance metrics ───────────────────────────────────────

PerformanceMetrics Portfolio::compute_metrics() const {
    PerformanceMetrics m;
    if (equity_curve_.size() < 2) return m;

    // Total return
    double start_eq = equity_curve_.front().equity;
    double end_eq   = equity_curve_.back().equity;
    m.total_return  = (end_eq - start_eq) / start_eq;

    // Daily returns vector
    std::vector<double> returns;
    returns.reserve(equity_curve_.size() - 1);
    for (std::size_t i = 1; i < equity_curve_.size(); ++i) {
        double prev = equity_curve_[i-1].equity;
        if (prev > 0)
            returns.push_back((equity_curve_[i].equity - prev) / prev);
    }

    // Annualized return (assumes 252 trading days).
    double n_years = returns.size() / 252.0;
    if (n_years > 0)
        m.annualized_return = std::pow(1.0 + m.total_return, 1.0 / n_years) - 1.0;

    // Volatility
    double sum  = std::accumulate(returns.begin(), returns.end(), 0.0);
    double mean = sum / returns.size();
    double var  = 0.0;
    for (double r : returns) var += (r - mean) * (r - mean);
    var /= returns.size();
    m.volatility = std::sqrt(var) * std::sqrt(252.0);

    // Sharpe (risk-free = 0 for simplicity; easy to parameterize)
    if (m.volatility > 1e-10)
        m.sharpe_ratio = m.annualized_return / m.volatility;

    // Sortino (downside deviation only)
    double down_var = 0.0; int down_n = 0;
    for (double r : returns) if (r < 0) { down_var += r*r; down_n++; }
    if (down_n > 0) {
        double downside_vol = std::sqrt(down_var / down_n) * std::sqrt(252.0);
        if (downside_vol > 1e-10)
            m.sortino_ratio = m.annualized_return / downside_vol;
    }

    // Max drawdown
    double peak = equity_curve_[0].equity, max_dd = 0.0;
    for (const auto& pt : equity_curve_) {
        peak = std::max(peak, pt.equity);
        double dd = (peak - pt.equity) / peak;
        max_dd = std::max(max_dd, dd);
    }
    m.max_drawdown = max_dd;

    // Calmar
    if (m.max_drawdown > 1e-10)
        m.calmar_ratio = m.annualized_return / m.max_drawdown;

    // Trade stats
    m.total_trades = static_cast<int>(fill_history_.size());
    double total_win = 0.0, total_loss = 0.0;
    // Reconstruct per-trade P&L pairs (buy then sell).
    // Simplified: use position realized P&L from fill stream.
    for (const auto& [sym, pos] : positions_) {
        if (pos.realized_pnl > 0) {
            m.winning_trades++;
            total_win += pos.realized_pnl;
        } else if (pos.realized_pnl < 0) {
            m.losing_trades++;
            total_loss += std::abs(pos.realized_pnl);
        }
    }
    int t = m.winning_trades + m.losing_trades;
    if (t > 0) {
        m.win_rate = static_cast<double>(m.winning_trades) / t;
        if (m.winning_trades > 0) m.avg_win  = total_win  / m.winning_trades;
        if (m.losing_trades  > 0) m.avg_loss = total_loss / m.losing_trades;
    }
    if (total_loss > 1e-10) m.profit_factor = total_win / total_loss;

    return m;
}

// ── RiskManager ───────────────────────────────────────────────

RiskManager::RiskManager(const Config& cfg) : cfg_(cfg) {}

bool RiskManager::evaluate(const OrderEvent& order,
                           const Portfolio&  portfolio,
                           std::deque<Event>* queue) {
    std::string sym = order.symbol;

    // Daily loss halt
    double current_equity = portfolio.equity();
    if (day_start_equity_ == 0.0) day_start_equity_ = current_equity;
    double daily_loss = (day_start_equity_ - current_equity) / day_start_equity_;
    if (daily_loss > cfg_.daily_loss_limit) {
        Event e;
        e.type = EventType::RISK;
        auto& r = e.data.risk;
        std::memcpy(r.symbol, order.symbol, 16);
        std::snprintf(r.reason, sizeof(r.reason),
                      "Daily loss limit %.1f%% breached", cfg_.daily_loss_limit * 100);
        r.requested_qty = order.quantity;
        r.approved_qty  = 0.0;
        queue->push_back(e);
        return false;
    }

    // Max open positions
    int open_positions = 0;
    for (const auto& [s, p] : portfolio.positions())
        if (std::abs(p.quantity) > 0) open_positions++;
    if (order.side == OrderSide::BUY && open_positions >= cfg_.max_positions)
        return false;

    // Size check: don't let any single position exceed max_position_pct.
    auto it = portfolio.positions().find(sym);
    double existing_qty = (it != portfolio.positions().end())
                        ? std::abs(it->second.quantity) : 0.0;

    // Pass through (simplified — full implementation would query last price).
    Event passthrough;
    passthrough.type       = EventType::ORDER;
    passthrough.data.order = order;
    queue->push_back(passthrough);
    return true;
}

// ── SimulatedExecution ────────────────────────────────────────

SimulatedExecution::SimulatedExecution(const Config& cfg) : cfg_(cfg) {}

void SimulatedExecution::execute_order(const OrderEvent& order,
                                       const MarketEvent& bar,
                                       std::deque<Event>* queue) {
    // MARKET orders fill at next open + slippage.
    double fill_price = bar.open;

    // Slippage model: half-spread impact.
    double slippage = fill_price * (cfg_.slippage_bps / 10000.0);
    if (order.side == OrderSide::BUY)
        fill_price += slippage;
    else
        fill_price -= slippage;

    // Commission: flat + proportional.
    double notional   = order.quantity * fill_price;
    double commission = cfg_.commission_flat
                      + notional * (cfg_.commission_bps / 10000.0);

    Event e;
    e.type = EventType::FILL;
    auto& f = e.data.fill;
    std::memcpy(f.symbol, order.symbol, 16);
    f.side       = order.side;
    f.quantity   = order.quantity;
    f.fill_price = fill_price;
    f.commission = commission;
    f.timestamp  = bar.timestamp;

    queue->push_back(e);
}

} // namespace quant
