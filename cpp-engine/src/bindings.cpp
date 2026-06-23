#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "Backtest.h"
#include "Portfolio.h"

// ============================================================
//  bindings.cpp — pybind11 glue between C++ engine and Python.
//
//  This compiles to a shared library (quant_engine.so) that the
//  FastAPI backend imports directly.  All hot-path computation
//  stays in C++; Python only handles serialization and HTTP.
//
//  Usage in Python:
//    import quant_engine as qe
//    result = qe.run_backtest({...})
// ============================================================

namespace py = pybind11;
using namespace quant;

PYBIND11_MODULE(quant_engine, m) {
    m.doc() = "Quant Backtesting Engine — C++ core";

    // ── Enums ────────────────────────────────────────────────
    py::enum_<OrderSide>(m, "OrderSide")
        .value("BUY",  OrderSide::BUY)
        .value("SELL", OrderSide::SELL);

    // ── EquityPoint ──────────────────────────────────────────
    py::class_<EquityPoint>(m, "EquityPoint")
        .def_readonly("timestamp", &EquityPoint::timestamp)
        .def_readonly("equity",    &EquityPoint::equity)
        .def_readonly("cash",      &EquityPoint::cash);

    // ── PerformanceMetrics ───────────────────────────────────
    py::class_<PerformanceMetrics>(m, "PerformanceMetrics")
        .def_readonly("total_return",      &PerformanceMetrics::total_return)
        .def_readonly("annualized_return", &PerformanceMetrics::annualized_return)
        .def_readonly("sharpe_ratio",      &PerformanceMetrics::sharpe_ratio)
        .def_readonly("sortino_ratio",     &PerformanceMetrics::sortino_ratio)
        .def_readonly("max_drawdown",      &PerformanceMetrics::max_drawdown)
        .def_readonly("calmar_ratio",      &PerformanceMetrics::calmar_ratio)
        .def_readonly("win_rate",          &PerformanceMetrics::win_rate)
        .def_readonly("profit_factor",     &PerformanceMetrics::profit_factor)
        .def_readonly("total_trades",      &PerformanceMetrics::total_trades)
        .def_readonly("winning_trades",    &PerformanceMetrics::winning_trades)
        .def_readonly("losing_trades",     &PerformanceMetrics::losing_trades)
        .def_readonly("avg_win",           &PerformanceMetrics::avg_win)
        .def_readonly("avg_loss",          &PerformanceMetrics::avg_loss)
        .def_readonly("volatility",        &PerformanceMetrics::volatility);

    // ── BacktestResult ───────────────────────────────────────
    py::class_<BacktestResult>(m, "BacktestResult")
        .def_readonly("run_id",       &BacktestResult::run_id)
        .def_readonly("metrics",      &BacktestResult::metrics)
        .def_readonly("equity_curve", &BacktestResult::equity_curve)
        .def_readonly("success",      &BacktestResult::success)
        .def_readonly("error_message",&BacktestResult::error_message);

    // ── run_backtest ─────────────────────────────────────────
    //  Accepts a plain Python dict; maps to BacktestConfig.
    m.def("run_backtest", [](py::dict cfg_dict,
                             py::object progress_cb) -> BacktestResult {

        BacktestConfig cfg;
        cfg.run_id          = cfg_dict["run_id"].cast<std::string>();
        cfg.data_dir        = cfg_dict["data_dir"].cast<std::string>();
        cfg.initial_capital = cfg_dict.contains("initial_capital")
                            ? cfg_dict["initial_capital"].cast<double>()
                            : 100'000.0;

        for (auto sym : cfg_dict["symbols"].cast<py::list>())
            cfg.symbols.push_back(sym.cast<std::string>());

        // Strategy params
        auto strat = cfg_dict["strategy"].cast<py::dict>();
        cfg.strategy.name = strat["name"].cast<std::string>();
        if (strat.contains("params")) {
            for (auto [k, v] : strat["params"].cast<py::dict>())
                cfg.strategy.params[k.cast<std::string>()] = v.cast<double>();
        }

        // Optional execution config
        if (cfg_dict.contains("execution")) {
            auto exc = cfg_dict["execution"].cast<py::dict>();
            if (exc.contains("commission_flat"))
                cfg.execution.commission_flat = exc["commission_flat"].cast<double>();
            if (exc.contains("commission_bps"))
                cfg.execution.commission_bps  = exc["commission_bps"].cast<double>();
            if (exc.contains("slippage_bps"))
                cfg.execution.slippage_bps    = exc["slippage_bps"].cast<double>();
        }

        // Build progress callback wrapper
        Backtest::ProgressCb cb;
        if (!progress_cb.is_none()) {
            cb = [progress_cb](double pct, const std::string& msg) {
                py::gil_scoped_acquire gil;
                progress_cb(pct, msg);
            };
        }

        Backtest bt(cfg, cb);
        return bt.run();

    }, py::arg("config"), py::arg("progress_callback") = py::none(),
       "Run a complete backtest. Returns a BacktestResult object.");
}
