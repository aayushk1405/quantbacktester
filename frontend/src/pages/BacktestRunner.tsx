// src/pages/BacktestRunner.tsx
// The main "run a backtest" form.  Fetches strategy catalogue from
// the API, renders a dynamic parameter form based on the selection,
// then submits and opens the results page with live WS streaming.

import { useEffect, useState } from "react";
import { apiClient } from "../utils/api";
import type { StrategyInfo } from "../utils/types";

interface Props {
  onRunStarted: (runId: string) => void;
}

export default function BacktestRunner({ onRunStarted }: Props) {
  const [strategies, setStrategies]     = useState<StrategyInfo[]>([]);
  const [symbols, setSymbols]           = useState<string[]>([]);
  const [selectedStrategy, setStrategy] = useState<string>("");
  const [selectedSymbols, setSelected]  = useState<Set<string>>(new Set());
  const [params, setParams]             = useState<Record<string, number>>({});
  const [capital, setCapital]           = useState(100_000);
  const [loading, setLoading]           = useState(false);
  const [error, setError]               = useState("");

  // Load strategy catalogue and available symbols on mount.
  useEffect(() => {
    apiClient.get<StrategyInfo[]>("/strategies").then(setStrategies);
    apiClient.get<{ symbols: string[] }>("/data/symbols")
      .then(r => setSymbols(r.symbols));
  }, []);

  // When strategy changes, populate default param values.
  useEffect(() => {
    const strat = strategies.find(s => s.name === selectedStrategy);
    if (!strat) return;
    const defaults: Record<string, number> = {};
    for (const [k, spec] of Object.entries(strat.params))
      defaults[k] = spec.default;
    setParams(defaults);
  }, [selectedStrategy, strategies]);

  const toggleSymbol = (sym: string) => {
    setSelected(prev => {
      const next = new Set(prev);
      next.has(sym) ? next.delete(sym) : next.add(sym);
      return next;
    });
  };

  const handleSubmit = async () => {
    if (!selectedStrategy) return setError("Select a strategy");
    if (selectedSymbols.size === 0) return setError("Select at least one symbol");
    setError("");
    setLoading(true);
    try {
      const res = await apiClient.post<{ run_id: string }>("/backtests", {
        symbols:         Array.from(selectedSymbols),
        strategy:        selectedStrategy,
        strategy_params: params,
        initial_capital: capital,
      });
      onRunStarted(res.run_id);
    } catch (e: any) {
      setError(e.message ?? "Failed to start backtest");
    } finally {
      setLoading(false);
    }
  };

  const currentStrategy = strategies.find(s => s.name === selectedStrategy);

  return (
    <div className="page">
      <header className="page-header">
        <h1>Configure Backtest</h1>
        <p className="subtitle">Select a strategy, symbols, and parameters then run.</p>
      </header>

      <div className="runner-grid">
        {/* ── Strategy picker ── */}
        <section className="card">
          <h2>Strategy</h2>
          <div className="strategy-list">
            {strategies.map(s => (
              <button
                key={s.name}
                className={`strategy-card ${selectedStrategy === s.name ? "selected" : ""}`}
                onClick={() => setStrategy(s.name)}
              >
                <span className="strategy-name">{s.display_name}</span>
                <span className="strategy-desc">{s.description}</span>
              </button>
            ))}
          </div>
        </section>

        {/* ── Symbol picker ── */}
        <section className="card">
          <h2>Symbols</h2>
          {symbols.length === 0
            ? <p className="muted">No data files found. Upload a CSV first.</p>
            : (
              <div className="symbol-grid">
                {symbols.map(sym => (
                  <button
                    key={sym}
                    className={`symbol-chip ${selectedSymbols.has(sym) ? "selected" : ""}`}
                    onClick={() => toggleSymbol(sym)}
                  >
                    {sym}
                  </button>
                ))}
              </div>
            )}
        </section>

        {/* ── Parameters ── */}
        {currentStrategy && (
          <section className="card">
            <h2>Parameters — {currentStrategy.display_name}</h2>
            <div className="param-grid">
              {Object.entries(currentStrategy.params).map(([key, spec]) => (
                <div className="param-row" key={key}>
                  <label htmlFor={key}>{key.replace(/_/g, " ")}</label>
                  <input
                    id={key}
                    type="number"
                    value={params[key] ?? spec.default}
                    min={spec.min}
                    max={spec.max}
                    step={spec.type === "int" ? 1 : 0.1}
                    onChange={e => setParams(p => ({
                      ...p, [key]: parseFloat(e.target.value)
                    }))}
                  />
                  <span className="param-hint">
                    {spec.min}–{spec.max}
                  </span>
                </div>
              ))}

              <div className="param-row">
                <label htmlFor="capital">Initial capital ($)</label>
                <input
                  id="capital"
                  type="number"
                  value={capital}
                  min={1000}
                  step={1000}
                  onChange={e => setCapital(parseFloat(e.target.value))}
                />
              </div>
            </div>
          </section>
        )}

        {/* ── Submit ── */}
        <section className="card submit-card">
          {error && <p className="error-msg">{error}</p>}
          <button
            className="btn-primary run-btn"
            onClick={handleSubmit}
            disabled={loading}
          >
            {loading
              ? <><i className="ti ti-loader-2 spin" /> Starting…</>
              : <><i className="ti ti-player-play" /> Run Backtest</>}
          </button>
          <p className="run-hint">
            Results stream live via WebSocket. The engine is written in C++
            and processes thousands of bars per second.
          </p>
        </section>
      </div>
    </div>
  );
}
