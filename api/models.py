"""
api/models.py — Pydantic request/response schemas.

Every model is fully typed and validated before it hits any
business logic.  The frontend TypeScript types are auto-generated
from these via `openapi-typescript` (see scripts/gen_types.sh).
"""

from __future__ import annotations
from enum import Enum
from typing import Any, Optional
from pydantic import BaseModel, Field, validator


class BacktestStatus(str, Enum):
    PENDING   = "pending"
    RUNNING   = "running"
    COMPLETE  = "complete"
    FAILED    = "failed"


# ── Strategy catalogue ────────────────────────────────────────

class ParamSpec(BaseModel):
    type:    str
    default: float
    min:     Optional[float] = None
    max:     Optional[float] = None

class StrategyInfo(BaseModel):
    name:         str
    display_name: str
    description:  str
    params:       dict[str, dict[str, Any]]


# ── Request schemas ───────────────────────────────────────────

class BacktestRequest(BaseModel):
    symbols:         list[str]  = Field(..., min_items=1, max_items=20)
    strategy:        str        = Field(..., description="Strategy key, e.g. 'sma'")
    strategy_params: Optional[dict[str, float]] = None
    initial_capital: float      = Field(default=100_000.0, gt=0)

    @validator("symbols", each_item=True)
    def symbol_uppercase(cls, v: str) -> str:
        return v.upper().strip()


# ── Performance metrics ───────────────────────────────────────

class PerformanceMetrics(BaseModel):
    total_return:       float
    annualized_return:  float
    sharpe_ratio:       float
    sortino_ratio:      float
    max_drawdown:       float
    calmar_ratio:       float
    win_rate:           float
    profit_factor:      float
    total_trades:       int
    winning_trades:     int
    losing_trades:      int
    avg_win:            float
    avg_loss:           float
    volatility:         float


class EquityPoint(BaseModel):
    timestamp: int     # Unix ms
    equity:    float
    cash:      float


class BacktestResult(BaseModel):
    metrics:      PerformanceMetrics
    equity_curve: list[EquityPoint]
    run_time_ms:  float


# ── Response schemas ──────────────────────────────────────────

class BacktestResponse(BaseModel):
    run_id:  str
    status:  BacktestStatus
    pct:     float               = 0.0
    message: str                 = ""
    result:  Optional[dict[str, Any]] = None
