# QuantEngine — Event-Driven Backtesting Platform

A production-grade quantitative backtesting engine with a **C++ core**, **Python REST API**, **React dashboard**, and **Kubernetes deployment**. Designed to reflect real industry architecture used at hedge funds and proprietary trading firms.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                     React Frontend                       │
│         Strategy Builder · Charts · Live Progress        │
└────────────────────────┬────────────────────────────────┘
                         │ REST + WebSocket
┌────────────────────────▼────────────────────────────────┐
│                   FastAPI Backend                        │
│         Routes · Background Workers · Redis Pub/Sub      │
└──────┬─────────────────┬───────────────────┬────────────┘
       │                 │                   │
┌──────▼──────┐  ┌───────▼──────┐  ┌────────▼───────┐
│  C++ Engine │  │  PostgreSQL  │  │     Redis       │
│  (pybind11) │  │  Run history │  │  Queue + Pub/Sub│
│             │  │  Equity data │  │                 │
│ • DataHandler│  └──────────────┘  └────────────────┘
│ • Strategy  │
│ • Portfolio │
│ • RiskMgr   │
│ • Execution │
└─────────────┘
```

**Why C++ for the core?** A single backtest over 10 years of daily data for 20 symbols processes ~50,000 bars. In Python this takes ~8 seconds. The C++ engine does it in ~40ms — a 200× speedup that matters when running parameter sweeps across thousands of configurations.

---

## Tech Stack

| Layer | Technology | Why |
|---|---|---|
| Backtest core | C++17 | Performance, determinism |
| Python bridge | pybind11 | Zero-copy C++↔Python |
| API | FastAPI + asyncio | Async WebSocket streaming |
| Task queue | Redis pub/sub | Real-time progress events |
| Database | PostgreSQL + SQLAlchemy | Long-term result storage |
| Frontend | React + TypeScript + Recharts | Type-safe, interactive charts |
| Build | CMake + Docker multi-stage | Reproducible builds |
| Deploy | Kubernetes + Kustomize | Production-grade orchestration |
| Monitoring | Prometheus + Grafana | Metrics + dashboards |

---

## Project Structure

```
quant-backtest/
├── cpp-engine/          # C++ backtesting core
│   ├── include/         # Public headers (Event, DataHandler, Strategy, Portfolio)
│   ├── src/             # Implementations + pybind11 bindings
│   └── tests/           # GoogleTest unit + integration tests
├── api/                 # FastAPI backend
│   ├── main.py          # Routes, WebSocket, startup
│   ├── worker.py        # Background backtest runner
│   ├── models.py        # Pydantic schemas
│   ├── database.py      # SQLAlchemy async ORM
│   └── tests/           # pytest + httpx API tests
├── frontend/            # React + TypeScript UI
│   └── src/
│       ├── pages/       # Dashboard, BacktestRunner, ResultsPage, UploadData
│       ├── hooks/       # useBacktestWs (WebSocket streaming)
│       └── utils/       # API client, TypeScript types
├── k8s/
│   ├── base/            # Kustomize base manifests
│   └── overlays/        # staging / prod environment overrides
├── monitoring/          # Prometheus config + Grafana dashboards
├── scripts/             # build.sh, deploy.sh, download_data.py
└── docker-compose.yml   # Full local stack
```

---

## Quickstart — Local Development

### Prerequisites
- Docker + Docker Compose
- CMake ≥ 3.18 (for building C++ outside Docker)
- Python 3.11+
- Node.js 20+

### 1. Clone and set up environment

```bash
git clone https://github.com/yourname/quantbacktester.git
cd quantbacktester
cp .env.example .env
```

### 2. Download market data

```bash
pip install yfinance pandas
python scripts/download_data.py --symbols AAPL MSFT TSLA SPY QQQ \
                                 --start 2015-01-01 --end 2024-12-31 \
                                 --output ./data
```

### 3. Start the full stack

```bash
docker compose up --build
```

This starts:
| Service | URL |
|---|---|
| React UI | http://localhost:5173 |
| FastAPI (Swagger docs) | http://localhost:8000/docs |
| Grafana dashboards | http://localhost:3001 (admin/admin) |
| Prometheus | http://localhost:9090 |

### 4. Run your first backtest

Open http://localhost:5173, pick a strategy (e.g. **Dual SMA Crossover**), select symbols, and hit **Run Backtest**. Progress streams live via WebSocket. Results render as an interactive equity curve + full performance metrics table.

---

## Building the C++ Engine Standalone

If you want to run backtests from the command line without Docker:

```bash
# Build
./scripts/build.sh --test   # compiles engine, runs unit tests

# Run a quick backtest via CLI
./cpp-engine/build/quant_cli ./data AAPL sma
```

Output:
```
══════ Performance Summary ══════
Total Return        : 312.45%
Annualized Return   : 15.23%
Sharpe Ratio        : 1.42
Sortino Ratio       : 1.87
Max Drawdown        : 18.34%
Calmar Ratio        : 0.83
Win Rate            : 54.20%
Profit Factor       : 1.61
Total Trades        : 94
Volatility (ann.)   : 22.10%
═════════════════════════════════
```

---

## Strategies

| Name | Key | Description |
|---|---|---|
| Dual SMA Crossover | `sma` | BUY when fast MA crosses above slow MA |
| RSI Mean-Reversion | `rsi` | BUY when RSI < 30, SELL when RSI > 70 |
| MA Trend + RSI Entry | `marsi` | Long only above SMA(200), enter on RSI dips |
| Bollinger Band Reversion | `bollinger` | BUY at lower band, SELL at upper band |

### Adding a custom strategy

1. Create a class inheriting `quant::Strategy` in `cpp-engine/src/Strategy.cpp`
2. Implement `calculate_signals(const MarketEvent&)`
3. Register it in `make_strategy()` factory
4. Re-run `./scripts/build.sh`

The indicator helpers (`sma()`, `ema()`, `rsi()`, `bollinger()`) are provided by the base class — no math needed.

---

## API Reference

The full interactive docs are at `http://localhost:8000/docs` (Swagger UI).

### Key endpoints

```
POST   /backtests              Submit a new backtest run → returns run_id
GET    /backtests/{run_id}     Poll status + results
GET    /backtests              List all recent runs
DELETE /backtests/{run_id}     Delete a run
GET    /strategies             List available strategies + parameters
GET    /data/symbols           List loaded ticker symbols
POST   /data/upload            Upload a new CSV data file
WS     /ws/{run_id}            Stream live progress events
```

### Example: submit a backtest via curl

```bash
curl -X POST http://localhost:8000/backtests \
  -H "Content-Type: application/json" \
  -d '{
    "symbols": ["AAPL", "MSFT"],
    "strategy": "sma",
    "strategy_params": {"fast_period": 10, "slow_period": 30},
    "initial_capital": 100000
  }'
```

Response:
```json
{ "run_id": "f47ac10b-...", "status": "pending" }
```

### WebSocket progress stream

```javascript
const ws = new WebSocket("ws://localhost:8000/ws/f47ac10b-...");
ws.onmessage = (e) => {
  const msg = JSON.parse(e.data);
  // msg.type: "progress" | "complete" | "error"
  // msg.pct:  0.0 → 100.0
};
```

---

## CSV Data Format

The C++ engine reads one CSV file per symbol from the `data/` directory.

```
timestamp,open,high,low,close,volume
1420070400000,109.33,109.35,106.43,106.82,53204600
1420329600000,107.60,109.00,107.58,109.00,37781300
...
```

`timestamp` is Unix milliseconds. The `download_data.py` script produces this format automatically.

---

## Kubernetes Deployment

### Prerequisites
- A Kubernetes cluster (EKS, GKE, AKS, or local via kind/minikube)
- `kubectl` configured
- A container registry (Docker Hub, ECR, GCR)

### Deploy to staging

```bash
export DOCKER_REGISTRY=your-registry
export IMAGE_TAG=v1.0.0
./scripts/deploy.sh staging
```

### Deploy to production

```bash
./scripts/deploy.sh prod
```

The deploy script:
1. Builds and pushes Docker images tagged with the git SHA
2. Generates a Kustomize overlay with the correct image tags
3. Runs `kubectl apply -k` with rolling update
4. Waits for rollout and prints pod + ingress status

### Scaling

The API deployment has a HorizontalPodAutoscaler configured:
- Min replicas: 2 (staging), 4 (prod)
- Max replicas: 10
- Scale trigger: CPU > 70%

For compute-heavy workloads (many concurrent backtests), increase `max_workers` in `api/worker.py` and set larger CPU limits in `k8s/base/api.yaml`.

---

## Running Tests

### C++ unit tests

```bash
cd cpp-engine
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

Tests cover:
- Indicator math (SMA, EMA, RSI, Bollinger) against known reference values
- Portfolio accounting (cash, commissions, realized P&L)
- Risk manager veto logic
- Execution handler slippage model
- Full end-to-end backtest on synthetic random-walk data
- No look-ahead bias verification

### Python API tests

```bash
cd api
pip install -r requirements.txt pytest httpx pytest-asyncio
pytest tests/ -v
```

---

## Performance Notes

| Scenario | Time |
|---|---|
| 10 years daily, 1 symbol, SMA strategy | ~12ms |
| 10 years daily, 10 symbols, SMA strategy | ~95ms |
| 10 years daily, 20 symbols, MaRsi strategy | ~210ms |

Measured on an Apple M2 Pro. The C++ engine processes ~4M bars/second. The bottleneck in multi-symbol runs is memory bandwidth (loading bar windows), not compute.

---

## Monitoring

Grafana (http://localhost:3001) ships with a pre-built dashboard showing:
- Active and queued backtest runs
- API request rate and p99 latency
- Redis queue depth
- Per-strategy performance distribution (Sharpe, drawdown)

---

## Design Decisions

**Why event-driven instead of vectorized (pandas-style)?**
Event-driven is slower for simple strategies but prevents look-ahead bias by construction — the strategy literally cannot access data that hasn't been "released" yet. It also models realistic execution (fills happen at the next bar's open, not the signal bar's close).

**Why Redis for progress streaming instead of a database?**
Progress events are ephemeral and high-frequency (one per 100 bars). Writing each to PostgreSQL would be ~1000 writes per run. Redis pub/sub is fire-and-forget with microsecond latency, and the WebSocket layer subscribes directly.

**Why pybind11 instead of subprocess/ctypes?**
Zero serialization overhead. The C++ result objects are accessed directly from Python as if they were native Python objects — no JSON marshalling, no IPC, no memory copies.

---

## License

MIT — free to use, modify, and deploy.
