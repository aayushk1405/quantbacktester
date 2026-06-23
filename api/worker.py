"""
api/worker.py — Executes the C++ backtest engine in a background thread.

Why a thread (not a process)?
  • pybind11-compiled .so shares the Python interpreter; forking
    causes issues with open file descriptors in asyncio.
  • The GIL is released inside the C++ engine for compute-heavy
    sections (pybind11 calls py::gil_scoped_release automatically
    on functions annotated as such).
  • For large-scale deployments, replace background_tasks with
    Celery + Redis or a proper worker queue.

Progress publishing:
  The C++ engine calls the progress callback with (pct, message).
  We publish JSON to Redis channel "progress:{run_id}" so that
  any WS connection can receive it in real time.
"""

from __future__ import annotations

import asyncio
import json
import time
import traceback
from concurrent.futures import ThreadPoolExecutor

import redis.asyncio as aioredis

from .models import BacktestStatus

# Thread pool for C++ computation (CPU-bound, not I/O-bound).
_executor = ThreadPoolExecutor(max_workers=4, thread_name_prefix="backtest")


async def run_backtest_task(
    payload: dict,
    redis: aioredis.Redis,
) -> None:
    """
    Called as a FastAPI BackgroundTask.  Runs the C++ engine in
    a thread and publishes progress to Redis pub/sub.
    """
    run_id = payload["run_id"]

    async def publish(msg: dict) -> None:
        await redis.publish(f"progress:{run_id}", json.dumps(msg))
        # Also update the hash for late-joining clients.
        if "pct" in msg:
            await redis.hset(f"run:{run_id}", mapping={
                "pct":     str(msg["pct"]),
                "message": msg.get("message", ""),
                "status":  BacktestStatus.RUNNING,
            })

    await redis.hset(f"run:{run_id}", mapping={
        "status":  BacktestStatus.RUNNING,
        "message": "Starting engine…",
    })
    await publish({"type": "status", "status": BacktestStatus.RUNNING,
                   "pct": 0, "message": "Starting engine…"})

    t_start = time.perf_counter()

    try:
        # Import the C++ module lazily so startup isn't blocked.
        import quant_engine as qe  # type: ignore  (compiled .so)

        loop = asyncio.get_event_loop()

        # Wrap the synchronous progress callback to publish asynchronously.
        def sync_progress(pct: float, message: str) -> None:
            asyncio.run_coroutine_threadsafe(
                publish({"type": "progress", "pct": round(pct, 1),
                         "message": message}),
                loop,
            )

        # Build the config dict the C++ binding expects.
        cfg = {
            "run_id":          run_id,
            "symbols":         payload["symbols"],
            "data_dir":        payload["data_dir"],
            "initial_capital": payload.get("initial_capital", 100_000.0),
            "strategy": {
                "name":   payload["strategy"],
                "params": payload.get("strategy_params", {}),
            },
        }

        # Run synchronously in a thread (releases GIL).
        result = await loop.run_in_executor(
            _executor,
            lambda: qe.run_backtest(cfg, sync_progress),
        )

        elapsed = (time.perf_counter() - t_start) * 1000

        if not result.success:
            raise RuntimeError(result.error_message)

        # Serialize result
        m = result.metrics
        metrics_dict = {
            "total_return":       m.total_return,
            "annualized_return":  m.annualized_return,
            "sharpe_ratio":       m.sharpe_ratio,
            "sortino_ratio":      m.sortino_ratio,
            "max_drawdown":       m.max_drawdown,
            "calmar_ratio":       m.calmar_ratio,
            "win_rate":           m.win_rate,
            "profit_factor":      m.profit_factor,
            "total_trades":       m.total_trades,
            "winning_trades":     m.winning_trades,
            "losing_trades":      m.losing_trades,
            "avg_win":            m.avg_win,
            "avg_loss":           m.avg_loss,
            "volatility":         m.volatility,
        }

        equity_curve = [
            {"timestamp": pt.timestamp, "equity": pt.equity, "cash": pt.cash}
            for pt in result.equity_curve
        ]

        result_payload = {
            "metrics":      metrics_dict,
            "equity_curve": equity_curve,
            "run_time_ms":  elapsed,
        }

        # Store in Redis (TTL = 7 days)
        await redis.hset(f"run:{run_id}", mapping={
            "status":  BacktestStatus.COMPLETE,
            "pct":     "100",
            "message": "Complete",
            "result":  json.dumps(result_payload),
        })
        await redis.expire(f"run:{run_id}", 60 * 60 * 24 * 7)

        await publish({
            "type":   "complete",
            "pct":    100,
            "result": result_payload,
        })

    except Exception as exc:
        tb = traceback.format_exc()
        await redis.hset(f"run:{run_id}", mapping={
            "status":  BacktestStatus.FAILED,
            "message": str(exc),
        })
        await publish({
            "type":    "error",
            "message": str(exc),
            "detail":  tb,
        })
