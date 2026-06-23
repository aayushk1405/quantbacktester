// src/utils/types.ts — Mirrors the Pydantic schemas from the API.
// In a production build, run `scripts/gen_types.sh` to regenerate
// these automatically from the OpenAPI spec.

export interface StrategyInfo {
  name:         string;
  display_name: string;
  description:  string;
  params: Record<string, {
    type:    "int" | "float";
    default: number;
    min?:    number;
    max?:    number;
  }>;
}

export interface EquityPoint {
  timestamp: number;   // Unix ms
  equity:    number;
  cash:      number;
}

export interface PerformanceMetrics {
  total_return:       number;
  annualized_return:  number;
  sharpe_ratio:       number;
  sortino_ratio:      number;
  max_drawdown:       number;
  calmar_ratio:       number;
  win_rate:           number;
  profit_factor:      number;
  total_trades:       number;
  winning_trades:     number;
  losing_trades:      number;
  avg_win:            number;
  avg_loss:           number;
  volatility:         number;
}

export interface BacktestResult {
  metrics:      PerformanceMetrics;
  equity_curve: EquityPoint[];
  run_time_ms:  number;
}

export interface RunSummary {
  run_id:     string;
  status:     string;
  created_at: string;
  message:    string;
}


// ── src/utils/api.ts ─────────────────────────────────────────

const BASE = import.meta.env.VITE_API_URL ?? "http://localhost:8000";

async function request<T>(
  method: string,
  path: string,
  body?: unknown,
): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    method,
    headers: body ? { "Content-Type": "application/json" } : {},
    body: body ? JSON.stringify(body) : undefined,
  });

  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error(err.detail ?? "API error");
  }

  return res.json() as Promise<T>;
}

export const apiClient = {
  get:    <T>(path: string)                  => request<T>("GET",    path),
  post:   <T>(path: string, body: unknown)   => request<T>("POST",   path, body),
  delete: <T>(path: string)                  => request<T>("DELETE", path),
};
