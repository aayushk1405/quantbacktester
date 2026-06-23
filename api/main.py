"""
api/main.py — FastAPI backend for the Quant Backtesting Engine.

Architecture:
  • POST /backtests     → enqueue a run in Redis, return run_id immediately
  • GET  /backtests/{id} → poll status + results
  • WS   /ws/{run_id}   → stream real-time progress during execution
  • GET  /strategies    → list available strategies + parameters
  • GET  /data/symbols  → list loaded symbols
  • POST /data/upload   → upload a new CSV data file

The heavy lifting (C++ engine) runs in a background thread pool to
avoid blocking the async event loop.  Redis is used as a task queue
so multiple worker pods can pick up jobs in Kubernetes.
"""

from __future__ import annotations

import asyncio
import json
import os
import time
import uuid
from contextlib import asynccontextmanager
from typing import Any, AsyncGenerator

import redis.asyncio as aioredis
from fastapi import (
    BackgroundTasks,
    Depends,
    FastAPI,
    File,
    HTTPException,
    UploadFile,
    WebSocket,
    WebSocketDisconnect,
)
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse

from .models import (
    BacktestRequest,
    BacktestResponse,
    BacktestStatus,
    StrategyInfo,
)
from .worker import run_backtest_task
from .database import get_db, AsyncSession
from .config import settings

# ── Lifespan (startup / shutdown) ────────────────────────────

@asynccontextmanager
async def lifespan(app: FastAPI) -> AsyncGenerator[None, None]:
    """Initialise Redis connection pool on startup."""
    app.state.redis = await aioredis.from_url(
        settings.redis_url,
        encoding="utf-8",
        decode_responses=True,
    )
    yield
    await app.state.redis.close()

# ── App setup ────────────────────────────────────────────────

app = FastAPI(
    title="Quant Backtest API",
    version="1.0.0",
    description="Event-driven C++ backtesting engine with REST + WebSocket API",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.allowed_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── Dependency helpers ────────────────────────────────────────

def get_redis(ws_or_req=None) -> aioredis.Redis:
    return app.state.redis


# ═══════════════════════════════════════════════════════════════
#  Strategy catalogue
# ═══════════════════════════════════════════════════════════════

STRATEGY_CATALOGUE: dict[str, StrategyInfo] = {
    "sma": StrategyInfo(
        name="sma",
        display_name="Dual SMA Crossover",
        description="BUY when fast MA crosses above slow MA; SELL on the reverse.",
        params={
            "fast_period": {"type": "int",   "default": 10,  "min": 2,  "max": 100},
            "slow_period": {"type": "int",   "default": 30,  "min": 10, "max": 500},
        },
    ),
    "rsi": StrategyInfo(
        name="rsi",
        display_name="RSI Mean-Reversion",
        description="BUY when RSI < oversold; SELL when RSI > overbought.",
        params={
            "period":     {"type": "int",   "default": 14,  "min": 2,  "max": 100},
            "oversold":   {"type": "float", "default": 30.0,"min": 5.0,"max": 49.0},
            "overbought": {"type": "float", "default": 70.0,"min": 51.0,"max": 95.0},
        },
    ),
    "marsi": StrategyInfo(
        name="marsi",
        display_name="MA Trend + RSI Entry",
        description="Only enter long when price is above SMA(trend) and RSI is oversold.",
        params={
            "sma_period": {"type": "int",   "default": 200, "min": 50, "max": 500},
            "rsi_period": {"type": "int",   "default": 14,  "min": 2,  "max": 50},
            "entry_rsi":  {"type": "float", "default": 35.0,"min": 10.0,"max": 50.0},
            "exit_rsi":   {"type": "float", "default": 65.0,"min": 50.0,"max": 90.0},
        },
    ),
    "bollinger": StrategyInfo(
        name="bollinger",
        display_name="Bollinger Band Reversion",
        description="BUY at lower band; SELL at upper band; exit at midline.",
        params={
            "period": {"type": "int",   "default": 20, "min": 5,  "max": 200},
            "k":      {"type": "float", "default": 2.0,"min": 0.5,"max": 4.0},
        },
    ),
}


# ═══════════════════════════════════════════════════════════════
#  Routes
# ═══════════════════════════════════════════════════════════════

@app.get("/healthz")
async def health_check():
    return {"status": "ok", "version": "1.0.0"}


@app.get("/strategies", response_model=list[StrategyInfo])
async def list_strategies():
    """Return all available strategy definitions with their parameters."""
    return list(STRATEGY_CATALOGUE.values())


@app.get("/data/symbols")
async def list_symbols():
    """Scan the data directory and return available ticker symbols."""
    data_dir = settings.data_dir
    if not os.path.isdir(data_dir):
        return {"symbols": []}
    symbols = [
        f[:-4] for f in os.listdir(data_dir) if f.endswith(".csv")
    ]
    return {"symbols": sorted(symbols)}


@app.post("/data/upload")
async def upload_data(file: UploadFile = File(...)):
    """
    Upload a CSV file for a new symbol.
    Expected format: timestamp,open,high,low,close,volume
    """
    if not file.filename.endswith(".csv"):
        raise HTTPException(400, "Only CSV files are accepted")

    symbol = file.filename[:-4].upper()
    dest = os.path.join(settings.data_dir, f"{symbol}.csv")
    os.makedirs(settings.data_dir, exist_ok=True)

    content = await file.read()
    with open(dest, "wb") as f:
        f.write(content)

    lines = content.decode().count("\n")
    return {"symbol": symbol, "rows": lines - 1, "path": dest}


@app.post("/backtests", response_model=BacktestResponse, status_code=202)
async def create_backtest(
    req: BacktestRequest,
    background_tasks: BackgroundTasks,
    redis: aioredis.Redis = Depends(get_redis),
):
    """
    Enqueue a new backtest run.  Returns immediately with a run_id.
    Poll GET /backtests/{run_id} or connect to WS /ws/{run_id} for progress.
    """
    if req.strategy not in STRATEGY_CATALOGUE:
        raise HTTPException(400, f"Unknown strategy '{req.strategy}'")

    for sym in req.symbols:
        csv_path = os.path.join(settings.data_dir, f"{sym}.csv")
        if not os.path.exists(csv_path):
            raise HTTPException(404, f"No data found for symbol '{sym}'")

    run_id = str(uuid.uuid4())
    payload = {
        "run_id":          run_id,
        "symbols":         req.symbols,
        "strategy":        req.strategy,
        "strategy_params": req.strategy_params or {},
        "initial_capital": req.initial_capital,
        "data_dir":        settings.data_dir,
    }

    # Store initial status in Redis
    await redis.hset(f"run:{run_id}", mapping={
        "status":    BacktestStatus.PENDING,
        "pct":       "0",
        "message":   "Queued",
        "created_at": str(time.time()),
    })

    # Enqueue the job (worker pods listen on this list)
    await redis.rpush("backtest_queue", json.dumps(payload))

    # Also kick off in-process for single-pod deployments
    background_tasks.add_task(run_backtest_task, payload, redis)

    return BacktestResponse(run_id=run_id, status=BacktestStatus.PENDING)


@app.get("/backtests/{run_id}", response_model=BacktestResponse)
async def get_backtest(
    run_id: str,
    redis: aioredis.Redis = Depends(get_redis),
):
    """Fetch current status and results of a backtest run."""
    data = await redis.hgetall(f"run:{run_id}")
    if not data:
        raise HTTPException(404, f"Run '{run_id}' not found")

    result = None
    if data.get("result"):
        result = json.loads(data["result"])

    return BacktestResponse(
        run_id=run_id,
        status=data.get("status", BacktestStatus.PENDING),
        pct=float(data.get("pct", 0)),
        message=data.get("message", ""),
        result=result,
    )


@app.get("/backtests")
async def list_backtests(
    redis: aioredis.Redis = Depends(get_redis),
    limit: int = 20,
):
    """List recent backtest run IDs and their statuses."""
    keys = await redis.keys("run:*")
    runs = []
    for key in keys[-limit:]:
        data = await redis.hgetall(key)
        run_id = key.split(":", 1)[1]
        runs.append({
            "run_id":     run_id,
            "status":     data.get("status"),
            "created_at": data.get("created_at"),
            "message":    data.get("message"),
        })
    return {"runs": sorted(runs, key=lambda r: r.get("created_at",""), reverse=True)}


@app.delete("/backtests/{run_id}")
async def delete_backtest(
    run_id: str,
    redis: aioredis.Redis = Depends(get_redis),
):
    deleted = await redis.delete(f"run:{run_id}")
    if not deleted:
        raise HTTPException(404, f"Run '{run_id}' not found")
    return {"deleted": run_id}


# ═══════════════════════════════════════════════════════════════
#  WebSocket — real-time progress streaming
# ═══════════════════════════════════════════════════════════════

@app.websocket("/ws/{run_id}")
async def ws_progress(
    websocket: WebSocket,
    run_id: str,
    redis: aioredis.Redis = Depends(get_redis),
):
    """
    Stream backtest progress events to the browser.
    The worker publishes to Redis channel "progress:{run_id}";
    this endpoint subscribes and forwards to the WebSocket.

    Message format (JSON):
      {"type": "progress", "pct": 45.2, "message": "Processing bar 4520"}
      {"type": "complete", "result": {...}}
      {"type": "error",    "message": "..."}
    """
    await websocket.accept()

    # Subscribe to the run's progress channel
    pubsub = redis.pubsub()
    await pubsub.subscribe(f"progress:{run_id}")

    try:
        # First, send the current status (in case client connects late)
        current = await redis.hgetall(f"run:{run_id}")
        if current:
            await websocket.send_json({
                "type":    "status",
                "status":  current.get("status"),
                "pct":     float(current.get("pct", 0)),
                "message": current.get("message", ""),
            })

        # Stream incoming messages until completion or disconnect
        async for message in pubsub.listen():
            if message["type"] != "message":
                continue

            data = json.loads(message["data"])
            await websocket.send_json(data)

            # Stop streaming on terminal states
            if data.get("type") in ("complete", "error"):
                break

    except WebSocketDisconnect:
        pass
    finally:
        await pubsub.unsubscribe(f"progress:{run_id}")
        await pubsub.close()
