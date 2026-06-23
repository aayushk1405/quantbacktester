"""
api/database.py — Async SQLAlchemy models + session factory.

Tables:
  • backtest_runs  — one row per run (metadata, status, metrics)
  • equity_points  — equity curve stored for long-term archival
    (Redis only keeps 7 days; DB keeps forever)

Alembic is used for schema migrations (see alembic/ directory).
Run migrations:
    alembic upgrade head
"""

from __future__ import annotations

import datetime
import json
from typing import AsyncGenerator

from sqlalchemy import (
    BigInteger, Boolean, Column, DateTime, Float,
    ForeignKey, Integer, String, Text,
)
from sqlalchemy.ext.asyncio import (
    AsyncSession,
    async_sessionmaker,
    create_async_engine,
)
from sqlalchemy.orm import DeclarativeBase, relationship

from .config import settings

# ── Engine + session factory ──────────────────────────────────

engine = create_async_engine(
    settings.database_url,
    pool_pre_ping=True,
    pool_size=5,
    max_overflow=10,
    echo=False,
)

AsyncSessionLocal = async_sessionmaker(
    engine,
    class_=AsyncSession,
    expire_on_commit=False,
)

async def get_db() -> AsyncGenerator[AsyncSession, None]:
    async with AsyncSessionLocal() as session:
        try:
            yield session
            await session.commit()
        except Exception:
            await session.rollback()
            raise


# ── ORM models ────────────────────────────────────────────────

class Base(DeclarativeBase):
    pass


class BacktestRun(Base):
    """
    Stores metadata and aggregated results for one backtest run.
    The full equity curve lives in EquityPoint rows (linked by run_id).
    """
    __tablename__ = "backtest_runs"

    id               = Column(String(36), primary_key=True)   # UUID
    status           = Column(String(16), nullable=False, default="pending")
    symbols          = Column(String(256), nullable=False)     # comma-separated
    strategy         = Column(String(64),  nullable=False)
    strategy_params  = Column(Text, nullable=True)             # JSON
    initial_capital  = Column(Float, nullable=False)
    created_at       = Column(DateTime, default=datetime.datetime.utcnow)
    completed_at     = Column(DateTime, nullable=True)

    # Aggregated metrics (denormalized for fast listing)
    total_return      = Column(Float, nullable=True)
    annualized_return = Column(Float, nullable=True)
    sharpe_ratio      = Column(Float, nullable=True)
    sortino_ratio     = Column(Float, nullable=True)
    max_drawdown      = Column(Float, nullable=True)
    calmar_ratio      = Column(Float, nullable=True)
    win_rate          = Column(Float, nullable=True)
    profit_factor     = Column(Float, nullable=True)
    total_trades      = Column(Integer, nullable=True)
    volatility        = Column(Float, nullable=True)
    run_time_ms       = Column(Float, nullable=True)

    error_message    = Column(Text, nullable=True)

    equity_points = relationship(
        "EquityPoint",
        back_populates="run",
        cascade="all, delete-orphan",
        lazy="dynamic",
    )

    def set_metrics(self, metrics: dict, run_time_ms: float) -> None:
        self.total_return      = metrics.get("total_return")
        self.annualized_return = metrics.get("annualized_return")
        self.sharpe_ratio      = metrics.get("sharpe_ratio")
        self.sortino_ratio     = metrics.get("sortino_ratio")
        self.max_drawdown      = metrics.get("max_drawdown")
        self.calmar_ratio      = metrics.get("calmar_ratio")
        self.win_rate          = metrics.get("win_rate")
        self.profit_factor     = metrics.get("profit_factor")
        self.total_trades      = metrics.get("total_trades")
        self.volatility        = metrics.get("volatility")
        self.run_time_ms       = run_time_ms
        self.completed_at      = datetime.datetime.utcnow()
        self.status            = "complete"


class EquityPoint(Base):
    """
    Time-series storage for the equity curve of a run.
    Stored as individual rows so it can be queried with SQL date ranges.
    For very long backtests (10+ years daily), consider TimescaleDB.
    """
    __tablename__ = "equity_points"

    id         = Column(BigInteger, primary_key=True, autoincrement=True)
    run_id     = Column(String(36), ForeignKey("backtest_runs.id",
                        ondelete="CASCADE"), nullable=False, index=True)
    timestamp  = Column(BigInteger, nullable=False)   # Unix ms
    equity     = Column(Float, nullable=False)
    cash       = Column(Float, nullable=False)

    run = relationship("BacktestRun", back_populates="equity_points")


# ── Database persistence helper ───────────────────────────────

async def persist_result(
    db: AsyncSession,
    run_id: str,
    payload: dict,
    symbols: list[str],
    strategy: str,
    strategy_params: dict,
    initial_capital: float,
) -> None:
    """
    Called after a successful backtest to write results to PostgreSQL.
    This is separate from Redis (which serves the live API) and
    provides long-term persistence and advanced querying.
    """
    run = BacktestRun(
        id              = run_id,
        symbols         = ",".join(symbols),
        strategy        = strategy,
        strategy_params = json.dumps(strategy_params),
        initial_capital = initial_capital,
    )
    run.set_metrics(payload["metrics"], payload.get("run_time_ms", 0))
    db.add(run)

    # Bulk-insert equity curve (downsample to at most 2000 points for DB)
    curve = payload.get("equity_curve", [])
    step  = max(1, len(curve) // 2000)
    for pt in curve[::step]:
        db.add(EquityPoint(
            run_id    = run_id,
            timestamp = pt["timestamp"],
            equity    = pt["equity"],
            cash      = pt["cash"],
        ))

    await db.commit()


async def init_db() -> None:
    """Create all tables (use Alembic migrations in production)."""
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
