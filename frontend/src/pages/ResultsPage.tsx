// src/pages/ResultsPage.tsx
// Shows live WS-streamed progress, then the full performance dashboard
// (equity curve chart, metrics table, drawdown chart).

import { useEffect, useRef, useState } from "react";
import {
  LineChart, Line, XAxis, YAxis, Tooltip,
  ResponsiveContainer, ReferenceLine, Area, AreaChart,
} from "recharts";
import { useBacktestWs } from "../hooks/useBacktestWs";
import type { BacktestResult, EquityPoint } from "../utils/types";

interface Props {
  runId: string | null;
  onBack: () => void;
}

export default function ResultsPage({ runId, onBack }: Props) {
  const { status, pct, message, result, error } = useBacktestWs(runId);

  if (!runId) {
    return (
      <div className="page">
        <p className="muted">No run selected. Go to Run Backtest to start one.</p>
        <button className="btn-secondary" onClick={onBack}>← Back</button>
      </div>
    );
  }

  return (
    <div className="page">
      <header className="page-header">
        <div>
          <h1>Backtest Results</h1>
          <code className="run-id">{runId}</code>
        </div>
        <button className="btn-secondary" onClick={onBack}>← Dashboard</button>
      </header>

      {/* ── Progress bar ── */}
      {status !== "complete" && status !== "failed" && (
        <section className="card progress-card">
          <div className="progress-header">
            <span className="status-badge running">Running</span>
            <span className="progress-msg">{message || "Initialising…"}</span>
            <span className="progress-pct">{pct.toFixed(1)}%</span>
          </div>
          <div className="progress-bar-track">
            <div className="progress-bar-fill" style={{ width: `${pct}%` }} />
          </div>
        </section>
      )}

      {error && (
        <section className="card error-card">
          <h2>Run failed</h2>
          <pre className="error-detail">{error}</pre>
        </section>
      )}

      {result && <ResultsDashboard result={result} />}
    </div>
  );
}

// ─────────────────────────────────────────────────────────────

function ResultsDashboard({ result }: { result: BacktestResult }) {
  const { metrics: m, equity_curve } = result;

  // Derive drawdown series from equity curve
  const ddSeries = deriveDrawdown(equity_curve);

  const metricRows = [
    { label: "Total Return",       value: pct(m.total_return),        good: m.total_return > 0 },
    { label: "Annualized Return",  value: pct(m.annualized_return),   good: m.annualized_return > 0 },
    { label: "Sharpe Ratio",       value: m.sharpe_ratio.toFixed(2),  good: m.sharpe_ratio > 1 },
    { label: "Sortino Ratio",      value: m.sortino_ratio.toFixed(2), good: m.sortino_ratio > 1 },
    { label: "Max Drawdown",       value: pct(m.max_drawdown),        good: m.max_drawdown < 0.15 },
    { label: "Calmar Ratio",       value: m.calmar_ratio.toFixed(2),  good: m.calmar_ratio > 1 },
    { label: "Win Rate",           value: pct(m.win_rate),            good: m.win_rate > 0.5 },
    { label: "Profit Factor",      value: m.profit_factor.toFixed(2), good: m.profit_factor > 1.5 },
    { label: "Total Trades",       value: String(m.total_trades),     good: true },
    { label: "Annual Volatility",  value: pct(m.volatility),          good: m.volatility < 0.25 },
    { label: "Avg Win",            value: "$" + m.avg_win.toFixed(2), good: true },
    { label: "Avg Loss",           value: "$" + m.avg_loss.toFixed(2),good: true },
  ];

  // Downsample equity curve for chart (max 500 points)
  const chartData = downsample(equity_curve, 500);

  return (
    <>
      {/* ── Equity curve ── */}
      <section className="card chart-card">
        <h2>Equity Curve</h2>
        <ResponsiveContainer width="100%" height={300}>
          <AreaChart data={chartData}
            margin={{ top: 10, right: 20, left: 10, bottom: 0 }}>
            <defs>
              <linearGradient id="equityGrad" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%"  stopColor="#6366f1" stopOpacity={0.3} />
                <stop offset="95%" stopColor="#6366f1" stopOpacity={0.0} />
              </linearGradient>
            </defs>
            <XAxis dataKey="timestamp"
              tickFormatter={ts => new Date(ts).toLocaleDateString()}
              tick={{ fontSize: 11 }} />
            <YAxis tickFormatter={v => `$${(v/1000).toFixed(0)}k`}
              tick={{ fontSize: 11 }} />
            <Tooltip
              formatter={(v: number) => [`$${v.toLocaleString()}`, "Equity"]}
              labelFormatter={ts => new Date(ts).toLocaleDateString()} />
            <Area type="monotone" dataKey="equity"
              stroke="#6366f1" fill="url(#equityGrad)"
              strokeWidth={1.5} dot={false} />
          </AreaChart>
        </ResponsiveContainer>
      </section>

      {/* ── Drawdown ── */}
      <section className="card chart-card">
        <h2>Drawdown</h2>
        <ResponsiveContainer width="100%" height={180}>
          <AreaChart data={ddSeries}
            margin={{ top: 5, right: 20, left: 10, bottom: 0 }}>
            <defs>
              <linearGradient id="ddGrad" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%"  stopColor="#ef4444" stopOpacity={0.4} />
                <stop offset="95%" stopColor="#ef4444" stopOpacity={0.0} />
              </linearGradient>
            </defs>
            <XAxis dataKey="timestamp"
              tickFormatter={ts => new Date(ts).toLocaleDateString()}
              tick={{ fontSize: 11 }} />
            <YAxis tickFormatter={v => `${(v * 100).toFixed(1)}%`}
              tick={{ fontSize: 11 }} />
            <Tooltip
              formatter={(v: number) => [`${(v * 100).toFixed(2)}%`, "Drawdown"]}
              labelFormatter={ts => new Date(ts).toLocaleDateString()} />
            <Area type="monotone" dataKey="drawdown"
              stroke="#ef4444" fill="url(#ddGrad)"
              strokeWidth={1} dot={false} />
          </AreaChart>
        </ResponsiveContainer>
      </section>

      {/* ── Metrics table ── */}
      <section className="card">
        <h2>Performance Metrics</h2>
        <div className="metrics-grid">
          {metricRows.map(row => (
            <div key={row.label} className="metric-tile">
              <span className="metric-label">{row.label}</span>
              <span className={`metric-value ${row.good ? "good" : "bad"}`}>
                {row.value}
              </span>
            </div>
          ))}
        </div>
      </section>
    </>
  );
}

// ─── Helpers ─────────────────────────────────────────────────

function pct(v: number) { return `${(v * 100).toFixed(2)}%`; }

function downsample(pts: EquityPoint[], max: number): EquityPoint[] {
  if (pts.length <= max) return pts;
  const step = Math.ceil(pts.length / max);
  return pts.filter((_, i) => i % step === 0);
}

function deriveDrawdown(pts: EquityPoint[]) {
  let peak = 0;
  return pts.map(pt => {
    peak = Math.max(peak, pt.equity);
    return { timestamp: pt.timestamp, drawdown: (peak - pt.equity) / peak };
  });
}
