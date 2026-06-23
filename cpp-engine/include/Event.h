#pragma once
#include <string>
#include <chrono>

// ============================================================
//  Event.h — The backbone of the event-driven backtest loop.
//
//  Every "thing that happens" in the simulation is an Event.
//  We use a tagged union approach (EventType enum + union data)
//  rather than virtual inheritance so events stay trivially
//  copyable and can be packed into a cache-friendly queue.
// ============================================================

namespace quant {

using Timestamp = std::int64_t;   // Unix ms since epoch

// ── Event taxonomy ──────────────────────────────────────────
enum class EventType : std::uint8_t {
    MARKET,   // new OHLCV bar arrived
    SIGNAL,   // strategy emitted a directional signal
    ORDER,    // order submitted to execution handler
    FILL,     // order has been (partially) filled
    RISK,     // risk-manager veto or size reduction
};

// ── Market (bar) data ────────────────────────────────────────
struct MarketEvent {
    char     symbol[16];   // e.g. "AAPL", null-terminated
    double   open;
    double   high;
    double   low;
    double   close;
    double   volume;
    Timestamp timestamp;   // bar close time in ms
};

// ── Directional signal ───────────────────────────────────────
enum class SignalDirection : std::int8_t {
    LONG  =  1,
    FLAT  =  0,
    SHORT = -1,
};

struct SignalEvent {
    char            symbol[16];
    SignalDirection direction;
    double          strength;    // 0.0–1.0, used for position sizing
    Timestamp       timestamp;
};

// ── Order ────────────────────────────────────────────────────
enum class OrderType : std::uint8_t {
    MARKET,
    LIMIT,
    STOP,
};

enum class OrderSide : std::uint8_t {
    BUY,
    SELL,
};

struct OrderEvent {
    char      symbol[16];
    OrderSide side;
    OrderType type;
    double    quantity;
    double    limit_price;   // only used when type == LIMIT
    Timestamp timestamp;
};

// ── Fill ─────────────────────────────────────────────────────
struct FillEvent {
    char      symbol[16];
    OrderSide side;
    double    quantity;
    double    fill_price;
    double    commission;    // flat + proportional model
    Timestamp timestamp;
};

// ── Risk veto ────────────────────────────────────────────────
struct RiskEvent {
    char   symbol[16];
    char   reason[64];
    double requested_qty;
    double approved_qty;
};

// ── Tagged union wrapper ─────────────────────────────────────
//  Keeping this as a plain struct (not std::variant) means
//  sizeof(Event) is fixed and the queue stays contiguous.
struct Event {
    EventType type;

    union {
        MarketEvent  market;
        SignalEvent  signal;
        OrderEvent   order;
        FillEvent    fill;
        RiskEvent    risk;
    } data;
};

} // namespace quant
