#!/usr/bin/env python3
"""
scripts/download_data.py — Fetch historical OHLCV data from Yahoo Finance.

Usage:
    python scripts/download_data.py --symbols AAPL MSFT TSLA NVDA SPY QQQ \
                                    --start 2015-01-01 --end 2024-12-31 \
                                    --output ./data

Requirements:
    pip install yfinance pandas

The output is one CSV per symbol: data/AAPL.csv, data/MSFT.csv, etc.
Format matches what the C++ CsvDataHandler expects:
    timestamp,open,high,low,close,volume
"""

import argparse
import os
import sys
from datetime import datetime

try:
    import yfinance as yf
    import pandas as pd
except ImportError:
    print("Missing dependencies. Run: pip install yfinance pandas")
    sys.exit(1)


DEFAULT_SYMBOLS = [
    "AAPL", "MSFT", "GOOGL", "AMZN", "NVDA",
    "TSLA", "META",  "SPY",   "QQQ",  "BRK-B",
]


def download_symbol(symbol: str, start: str, end: str, out_dir: str) -> bool:
    """Download one symbol and save to CSV. Returns True on success."""
    print(f"  Downloading {symbol}...", end=" ", flush=True)
    try:
        df = yf.download(
            symbol,
            start=start,
            end=end,
            auto_adjust=True,   # adjusts for splits + dividends
            progress=False,
        )

        if df.empty:
            print("⚠ No data returned")
            return False

        # Flatten multi-level columns if present (yfinance 0.2.x quirk)
        if isinstance(df.columns, pd.MultiIndex):
            df.columns = df.columns.get_level_values(0)

        # Rename and reorder columns
        df = df[["Open", "High", "Low", "Close", "Volume"]]
        df.columns = ["open", "high", "low", "close", "volume"]

        # Convert index (DatetimeIndex) to Unix milliseconds
        df.index.name = "timestamp"
        df.index = df.index.astype("int64") // 1_000_000   # ns → ms

        # Drop rows with any NaN
        df = df.dropna()

        out_path = os.path.join(out_dir, f"{symbol.replace('-', '_')}.csv")
        df.to_csv(out_path)

        print(f"✓  {len(df)} bars → {out_path}")
        return True

    except Exception as e:
        print(f"✗  Error: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="Download OHLCV data")
    parser.add_argument(
        "--symbols", nargs="+", default=DEFAULT_SYMBOLS,
        help="Ticker symbols to download (default: 10 large-caps + ETFs)"
    )
    parser.add_argument("--start",  default="2015-01-01", help="Start date YYYY-MM-DD")
    parser.add_argument("--end",    default=datetime.today().strftime("%Y-%m-%d"),
                        help="End date YYYY-MM-DD")
    parser.add_argument("--output", default="./data", help="Output directory")
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)
    print(f"\nDownloading {len(args.symbols)} symbols: {args.start} → {args.end}")
    print(f"Output directory: {os.path.abspath(args.output)}\n")

    success, failed = [], []
    for sym in args.symbols:
        ok = download_symbol(sym, args.start, args.end, args.output)
        (success if ok else failed).append(sym)

    print(f"\n{'='*50}")
    print(f"✓ Downloaded:  {len(success)} symbols: {', '.join(success)}")
    if failed:
        print(f"✗ Failed:      {len(failed)} symbols: {', '.join(failed)}")
    print(f"\nData ready in: {os.path.abspath(args.output)}/")


if __name__ == "__main__":
    main()
