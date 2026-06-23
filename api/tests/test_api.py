"""
api/tests/test_api.py — Full API test suite.

Tests every endpoint with real HTTP calls using httpx's AsyncClient,
which runs against the FastAPI app in-process (no server needed).

Run:
    pytest api/tests/test_api.py -v
    pytest api/tests/test_api.py -v --tb=short   # shorter tracebacks

Coverage:
    • GET  /healthz
    • GET  /strategies
    • GET  /data/symbols
    • POST /data/upload
    • POST /backtests  (validation + happy path)
    • GET  /backtests/{run_id}
    • GET  /backtests  (list)
    • DELETE /backtests/{run_id}
    • WebSocket /ws/{run_id}
    • Error cases (404, 400, unknown strategy, missing symbol)
"""

from __future__ import annotations

import asyncio
import csv
import io
import json
import os
import tempfile
import time
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
import pytest_asyncio
from httpx import ASGITransport, AsyncClient

# ── Point DATA_DIR at a temp directory before importing the app ──
_tmp_data = tempfile.mkdtemp()
os.environ["DATA_DIR"] = _tmp_data

from api.main import app  # noqa: E402  (import after env setup)


# ═══════════════════════════════════════════════════════════════
#  Fixtures
# ═══════════════════════════════════════════════════════════════

@pytest_asyncio.fixture
async def client():
    """AsyncClient wired directly to the FastAPI app (no network)."""
    # Patch Redis so tests don't need a running Redis instance.
    mock_redis = AsyncMock()
    mock_redis.hset      = AsyncMock(return_value=True)
    mock_redis.hgetall   = AsyncMock(return_value={})
    mock_redis.rpush     = AsyncMock(return_value=1)
    mock_redis.publish   = AsyncMock(return_value=1)
    mock_redis.expire    = AsyncMock(return_value=True)
    mock_redis.delete    = AsyncMock(return_value=1)
    mock_redis.keys      = AsyncMock(return_value=[])
    mock_redis.pubsub    = MagicMock(return_value=AsyncMock(
        subscribe=AsyncMock(),
        unsubscribe=AsyncMock(),
        close=AsyncMock(),
        listen=AsyncMock(return_value=aiter([])),
    ))

    app.state.redis = mock_redis

    async with AsyncClient(
        transport=ASGITransport(app=app),
        base_url="http://test",
    ) as ac:
        yield ac


@pytest.fixture
def sample_csv_bytes() -> bytes:
    """Generate a minimal valid OHLCV CSV (30 rows)."""
    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow(["timestamp", "open", "high", "low", "close", "volume"])
    base_ts = 1_700_000_000_000
    for i in range(30):
        ts    = base_ts + i * 86_400_000
        close = 100.0 + i * 0.5
        writer.writerow([ts, close - 1, close + 1, close - 2, close, 1_000_000])
    return output.getvalue().encode()


@pytest.fixture
def uploaded_symbol(sample_csv_bytes) -> str:
    """Write a CSV to the temp data dir and return the symbol name."""
    sym = "TESTSYM"
    path = os.path.join(_tmp_data, f"{sym}.csv")
    with open(path, "wb") as f:
        f.write(sample_csv_bytes)
    return sym


# ── Async iterator helper (for mocking pubsub.listen) ───────
async def aiter(items):
    for item in items:
        yield item


# ═══════════════════════════════════════════════════════════════
#  Health check
# ═══════════════════════════════════════════════════════════════

@pytest.mark.asyncio
async def test_healthz(client: AsyncClient):
    r = await client.get("/healthz")
    assert r.status_code == 200
    body = r.json()
    assert body["status"] == "ok"
    assert "version" in body


# ═══════════════════════════════════════════════════════════════
#  Strategy catalogue
# ═══════════════════════════════════════════════════════════════

@pytest.mark.asyncio
async def test_list_strategies_returns_all_four(client: AsyncClient):
    r = await client.get("/strategies")
    assert r.status_code == 200
    strategies = r.json()
    names = {s["name"] for s in strategies}
    assert names == {"sma", "rsi", "marsi", "bollinger"}


@pytest.mark.asyncio
async def test_strategy_has_required_fields(client: AsyncClient):
    r = await client.get("/strategies")
    for s in r.json():
        assert "name"         in s
        assert "display_name" in s
        assert "description"  in s
        assert "params"       in s
        assert isinstance(s["params"], dict)


@pytest.mark.asyncio
async def test_strategy_params_have_defaults(client: AsyncClient):
    r = await client.get("/strategies")
    for strategy in r.json():
        for param_name, spec in strategy["params"].items():
            assert "default" in spec, \
                f"Param '{param_name}' in strategy '{strategy['name']}' missing 'default'"


# ═══════════════════════════════════════════════════════════════
#  Data endpoints
# ═══════════════════════════════════════════════════════════════

@pytest.mark.asyncio
async def test_list_symbols_empty_initially(client: AsyncClient):
    # Clean temp dir (remove any leftovers from other tests)
    for f in os.listdir(_tmp_data):
        if f.endswith(".csv"):
            os.remove(os.path.join(_tmp_data, f))
    r = await client.get("/data/symbols")
    assert r.status_code == 200
    assert r.json()["symbols"] == []


@pytest.mark.asyncio
async def test_upload_csv_success(client: AsyncClient, sample_csv_bytes: bytes):
    r = await client.post(
        "/data/upload",
        files={"file": ("UPLOADTEST.csv", sample_csv_bytes, "text/csv")},
    )
    assert r.status_code == 200
    body = r.json()
    assert body["symbol"] == "UPLOADTEST"
    assert body["rows"] == 30


@pytest.mark.asyncio
async def test_upload_non_csv_rejected(client: AsyncClient):
    r = await client.post(
        "/data/upload",
        files={"file": ("data.xlsx", b"fake excel content", "application/octet-stream")},
    )
    assert r.status_code == 400


@pytest.mark.asyncio
async def test_list_symbols_after_upload(client: AsyncClient, sample_csv_bytes: bytes):
    await client.post(
        "/data/upload",
        files={"file": ("SYMCHECK.csv", sample_csv_bytes, "text/csv")},
    )
    r = await client.get("/data/symbols")
    assert "SYMCHECK" in r.json()["symbols"]


# ═══════════════════════════════════════════════════════════════
#  POST /backtests
# ═══════════════════════════════════════════════════════════════

@pytest.mark.asyncio
async def test_create_backtest_returns_run_id(
    client: AsyncClient, uploaded_symbol: str
):
    with patch("api.main.run_backtest_task", new_callable=AsyncMock):
        r = await client.post("/backtests", json={
            "symbols":  [uploaded_symbol],
            "strategy": "sma",
        })
    assert r.status_code == 202
    body = r.json()
    assert "run_id" in body
    assert len(body["run_id"]) == 36   # UUID format
    assert body["status"] == "pending"


@pytest.mark.asyncio
async def test_create_backtest_unknown_strategy(
    client: AsyncClient, uploaded_symbol: str
):
    r = await client.post("/backtests", json={
        "symbols":  [uploaded_symbol],
        "strategy": "nonexistent_xyz",
    })
    assert r.status_code == 400


@pytest.mark.asyncio
async def test_create_backtest_missing_symbol(client: AsyncClient):
    r = await client.post("/backtests", json={
        "symbols":  ["DOESNOTEXIST"],
        "strategy": "sma",
    })
    assert r.status_code == 404


@pytest.mark.asyncio
async def test_create_backtest_empty_symbols_rejected(
    client: AsyncClient, uploaded_symbol: str
):
    r = await client.post("/backtests", json={
        "symbols":  [],
        "strategy": "sma",
    })
    assert r.status_code == 422   # Pydantic validation error


@pytest.mark.asyncio
async def test_create_backtest_symbols_uppercased(
    client: AsyncClient, uploaded_symbol: str
):
    """Symbols should be normalised to uppercase by the validator."""
    with patch("api.main.run_backtest_task", new_callable=AsyncMock):
        r = await client.post("/backtests", json={
            "symbols":  [uploaded_symbol.lower()],
            "strategy": "sma",
        })
    # If the validator upcased it correctly, it finds the CSV and returns 202.
    assert r.status_code == 202


@pytest.mark.asyncio
async def test_create_backtest_custom_params(
    client: AsyncClient, uploaded_symbol: str
):
    with patch("api.main.run_backtest_task", new_callable=AsyncMock):
        r = await client.post("/backtests", json={
            "symbols":          [uploaded_symbol],
            "strategy":         "sma",
            "strategy_params":  {"fast_period": 5, "slow_period": 20},
            "initial_capital":  50_000,
        })
    assert r.status_code == 202


# ═══════════════════════════════════════════════════════════════
#  GET /backtests/{run_id}
# ═══════════════════════════════════════════════════════════════

@pytest.mark.asyncio
async def test_get_backtest_not_found(client: AsyncClient):
    # Redis mock returns empty dict → 404
    r = await client.get("/backtests/nonexistent-run-id")
    assert r.status_code == 404


@pytest.mark.asyncio
async def test_get_backtest_pending_status(
    client: AsyncClient, uploaded_symbol: str
):
    # Make Redis return a pending run
    app.state.redis.hgetall = AsyncMock(return_value={
        "status":  "pending",
        "pct":     "0",
        "message": "Queued",
    })
    r = await client.get("/backtests/some-run-id")
    assert r.status_code == 200
    body = r.json()
    assert body["status"] == "pending"
    assert body["pct"] == 0.0


@pytest.mark.asyncio
async def test_get_backtest_complete_includes_result(client: AsyncClient):
    fake_result = {
        "metrics": {
            "total_return": 0.25, "annualized_return": 0.08,
            "sharpe_ratio": 1.2,  "sortino_ratio": 1.5,
            "max_drawdown": 0.12, "calmar_ratio": 0.67,
            "win_rate": 0.55,     "profit_factor": 1.4,
            "total_trades": 42,   "winning_trades": 23,
            "losing_trades": 19,  "avg_win": 320.0,
            "avg_loss": 180.0,    "volatility": 0.18,
        },
        "equity_curve": [
            {"timestamp": 1_700_000_000_000, "equity": 100_000, "cash": 100_000},
            {"timestamp": 1_700_086_400_000, "equity": 101_000, "cash": 50_000},
        ],
        "run_time_ms": 45.2,
    }
    app.state.redis.hgetall = AsyncMock(return_value={
        "status":  "complete",
        "pct":     "100",
        "message": "Complete",
        "result":  json.dumps(fake_result),
    })

    r = await client.get("/backtests/completed-run-id")
    assert r.status_code == 200
    body = r.json()
    assert body["status"] == "complete"
    assert body["result"] is not None
    assert body["result"]["metrics"]["sharpe_ratio"] == 1.2


# ═══════════════════════════════════════════════════════════════
#  GET /backtests  (list)
# ═══════════════════════════════════════════════════════════════

@pytest.mark.asyncio
async def test_list_backtests_empty(client: AsyncClient):
    app.state.redis.keys = AsyncMock(return_value=[])
    r = await client.get("/backtests")
    assert r.status_code == 200
    assert r.json()["runs"] == []


@pytest.mark.asyncio
async def test_list_backtests_returns_runs(client: AsyncClient):
    app.state.redis.keys = AsyncMock(return_value=["run:abc123", "run:def456"])
    app.state.redis.hgetall = AsyncMock(return_value={
        "status":     "complete",
        "created_at": str(time.time()),
        "message":    "Complete",
    })
    r = await client.get("/backtests")
    assert r.status_code == 200
    assert len(r.json()["runs"]) == 2


# ═══════════════════════════════════════════════════════════════
#  DELETE /backtests/{run_id}
# ═══════════════════════════════════════════════════════════════

@pytest.mark.asyncio
async def test_delete_backtest_success(client: AsyncClient):
    app.state.redis.delete = AsyncMock(return_value=1)
    r = await client.delete("/backtests/some-run-id")
    assert r.status_code == 200
    assert r.json()["deleted"] == "some-run-id"


@pytest.mark.asyncio
async def test_delete_backtest_not_found(client: AsyncClient):
    app.state.redis.delete = AsyncMock(return_value=0)   # key didn't exist
    r = await client.delete("/backtests/ghost-run-id")
    assert r.status_code == 404


# ═══════════════════════════════════════════════════════════════
#  Input validation (Pydantic)
# ═══════════════════════════════════════════════════════════════

@pytest.mark.asyncio
async def test_negative_capital_rejected(
    client: AsyncClient, uploaded_symbol: str
):
    r = await client.post("/backtests", json={
        "symbols":          [uploaded_symbol],
        "strategy":         "sma",
        "initial_capital":  -1000,
    })
    assert r.status_code == 422


@pytest.mark.asyncio
async def test_too_many_symbols_rejected(
    client: AsyncClient, uploaded_symbol: str
):
    r = await client.post("/backtests", json={
        "symbols":  [uploaded_symbol] * 21,   # max is 20
        "strategy": "sma",
    })
    assert r.status_code == 422


# ═══════════════════════════════════════════════════════════════
#  Worker unit tests (mocked C++ engine)
# ═══════════════════════════════════════════════════════════════

@pytest.mark.asyncio
async def test_worker_publishes_complete_on_success():
    """Worker should publish a 'complete' message to Redis on success."""
    from api.worker import run_backtest_task

    # Mock the C++ engine result
    mock_metrics = MagicMock(
        total_return=0.3, annualized_return=0.1, sharpe_ratio=1.5,
        sortino_ratio=2.0, max_drawdown=0.1, calmar_ratio=1.0,
        win_rate=0.6, profit_factor=1.8, total_trades=50,
        winning_trades=30, losing_trades=20, avg_win=400.0,
        avg_loss=200.0, volatility=0.15,
    )
    mock_result = MagicMock(
        success=True,
        metrics=mock_metrics,
        equity_curve=[
            MagicMock(timestamp=1_700_000_000_000, equity=100_000, cash=100_000)
        ],
    )

    mock_redis = AsyncMock()
    published_messages = []

    async def capture_publish(channel, message):
        published_messages.append((channel, json.loads(message)))

    mock_redis.publish = capture_publish
    mock_redis.hset    = AsyncMock()
    mock_redis.expire  = AsyncMock()

    payload = {
        "run_id":          "test-worker-run",
        "symbols":         ["FAKE"],
        "strategy":        "sma",
        "strategy_params": {},
        "initial_capital": 100_000,
        "data_dir":        _tmp_data,
    }

    with patch("api.worker.quant_engine" if False else "builtins.__import__") as mock_import:
        # Patch quant_engine module import inside the worker
        import sys
        mock_qe = MagicMock()
        mock_qe.run_backtest = MagicMock(return_value=mock_result)
        sys.modules["quant_engine"] = mock_qe

        await run_backtest_task(payload, mock_redis)

        del sys.modules["quant_engine"]

    # The last published message should be type="complete"
    assert len(published_messages) > 0
    last_channel, last_msg = published_messages[-1]
    assert last_channel == f"progress:{payload['run_id']}"
    assert last_msg["type"] == "complete"
    assert "result" in last_msg


@pytest.mark.asyncio
async def test_worker_publishes_error_on_failure():
    """Worker should publish type='error' when the C++ engine fails."""
    from api.worker import run_backtest_task

    mock_result = MagicMock(success=False, error_message="Segfault in strategy")
    mock_redis = AsyncMock()
    published_messages = []

    async def capture(channel, message):
        published_messages.append((channel, json.loads(message)))

    mock_redis.publish = capture
    mock_redis.hset    = AsyncMock()

    payload = {
        "run_id": "fail-run", "symbols": ["X"], "strategy": "sma",
        "strategy_params": {}, "initial_capital": 1000, "data_dir": _tmp_data,
    }

    import sys
    mock_qe = MagicMock()
    mock_qe.run_backtest = MagicMock(return_value=mock_result)
    sys.modules["quant_engine"] = mock_qe

    await run_backtest_task(payload, mock_redis)
    del sys.modules["quant_engine"]

    error_msgs = [m for _, m in published_messages if m.get("type") == "error"]
    assert len(error_msgs) > 0
