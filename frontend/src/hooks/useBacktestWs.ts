// src/hooks/useBacktestWs.ts
// Custom hook that connects to the backend WebSocket endpoint
// and streams backtest progress in real time.

import { useEffect, useRef, useState } from "react";
import type { BacktestResult } from "../utils/types";

const WS_BASE = (import.meta.env.VITE_API_URL ?? "http://localhost:8000")
  .replace(/^http/, "ws");

interface WsState {
  status:  string;
  pct:     number;
  message: string;
  result:  BacktestResult | null;
  error:   string | null;
}

export function useBacktestWs(runId: string | null): WsState {
  const [state, setState] = useState<WsState>({
    status: "pending", pct: 0, message: "", result: null, error: null,
  });
  const wsRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    if (!runId) return;

    const ws = new WebSocket(`${WS_BASE}/ws/${runId}`);
    wsRef.current = ws;

    ws.onmessage = (evt) => {
      const data = JSON.parse(evt.data);
      switch (data.type) {
        case "status":
        case "progress":
          setState(s => ({
            ...s,
            status:  data.status ?? "running",
            pct:     data.pct    ?? s.pct,
            message: data.message ?? s.message,
          }));
          break;
        case "complete":
          setState(s => ({
            ...s,
            status: "complete",
            pct: 100,
            result: data.result,
          }));
          ws.close();
          break;
        case "error":
          setState(s => ({ ...s, status: "failed", error: data.message }));
          ws.close();
          break;
      }
    };

    ws.onerror = () =>
      setState(s => ({ ...s, error: "WebSocket connection error" }));

    return () => ws.close();
  }, [runId]);

  return state;
}
