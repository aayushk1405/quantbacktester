-- scripts/init.sql
-- Run automatically by the postgres Docker container on first start.
-- In production, use Alembic migrations (alembic upgrade head) instead.

-- ── Extensions ───────────────────────────────────────────────
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- ── backtest_runs ─────────────────────────────────────────────
-- One row per backtest run. Stores metadata + aggregated metrics.
CREATE TABLE IF NOT EXISTS backtest_runs (
    id                VARCHAR(36)  PRIMARY KEY,
    status            VARCHAR(16)  NOT NULL DEFAULT 'pending',
    symbols           VARCHAR(256) NOT NULL,
    strategy          VARCHAR(64)  NOT NULL,
    strategy_params   TEXT,
    initial_capital   DOUBLE PRECISION NOT NULL,
    created_at        TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
    completed_at      TIMESTAMPTZ,

    -- Aggregated performance metrics (denormalized for fast listing)
    total_return      DOUBLE PRECISION,
    annualized_return DOUBLE PRECISION,
    sharpe_ratio      DOUBLE PRECISION,
    sortino_ratio     DOUBLE PRECISION,
    max_drawdown      DOUBLE PRECISION,
    calmar_ratio      DOUBLE PRECISION,
    win_rate          DOUBLE PRECISION,
    profit_factor     DOUBLE PRECISION,
    total_trades      INTEGER,
    volatility        DOUBLE PRECISION,
    run_time_ms       DOUBLE PRECISION,

    error_message     TEXT
);

CREATE INDEX IF NOT EXISTS idx_runs_created  ON backtest_runs(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_runs_strategy ON backtest_runs(strategy);
CREATE INDEX IF NOT EXISTS idx_runs_status   ON backtest_runs(status);

-- ── equity_points ─────────────────────────────────────────────
-- Time-series equity curve for each run.
-- For very large datasets, consider TimescaleDB (just enable the
-- extension and call create_hypertable).
CREATE TABLE IF NOT EXISTS equity_points (
    id        BIGSERIAL         PRIMARY KEY,
    run_id    VARCHAR(36)       NOT NULL REFERENCES backtest_runs(id) ON DELETE CASCADE,
    timestamp BIGINT            NOT NULL,  -- Unix ms
    equity    DOUBLE PRECISION  NOT NULL,
    cash      DOUBLE PRECISION  NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_equity_run_ts ON equity_points(run_id, timestamp);

-- ── Useful views ──────────────────────────────────────────────

-- Leaderboard: top runs by Sharpe ratio
CREATE OR REPLACE VIEW leaderboard AS
SELECT
    id,
    strategy,
    symbols,
    ROUND(total_return::numeric    * 100, 2) AS total_return_pct,
    ROUND(annualized_return::numeric * 100, 2) AS ann_return_pct,
    ROUND(sharpe_ratio::numeric,     2) AS sharpe,
    ROUND(max_drawdown::numeric    * 100, 2) AS max_dd_pct,
    total_trades,
    created_at
FROM backtest_runs
WHERE status = 'complete'
ORDER BY sharpe_ratio DESC NULLS LAST;

-- Quick summary counts
CREATE OR REPLACE VIEW run_summary AS
SELECT
    COUNT(*)                                   AS total_runs,
    COUNT(*) FILTER (WHERE status='complete')  AS completed,
    COUNT(*) FILTER (WHERE status='running')   AS running,
    COUNT(*) FILTER (WHERE status='failed')    AS failed,
    AVG(run_time_ms)                           AS avg_run_time_ms
FROM backtest_runs;
