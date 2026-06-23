// src/pages/Dashboard.tsx — Overview: recent runs, quick stats.
import { useEffect, useState } from "react";
import { apiClient } from "../utils/api";
import type { RunSummary } from "../utils/types";

interface Props {
  onNewRun:  () => void;
  onViewRun: (id: string) => void;
}

const STATUS_COLOR: Record<string, string> = {
  complete: "badge-success",
  running:  "badge-running",
  pending:  "badge-pending",
  failed:   "badge-error",
};

export default function Dashboard({ onNewRun, onViewRun }: Props) {
  const [runs, setRuns] = useState<RunSummary[]>([]);
  const [loading, setLoading] = useState(true);

  const refresh = () => {
    apiClient.get<{ runs: RunSummary[] }>("/backtests")
      .then(r => setRuns(r.runs))
      .finally(() => setLoading(false));
  };

  useEffect(() => { refresh(); }, []);

  return (
    <div className="page">
      <header className="page-header">
        <div>
          <h1>Dashboard</h1>
          <p className="subtitle">Recent backtest runs and their outcomes.</p>
        </div>
        <button className="btn-primary" onClick={onNewRun}>
          <i className="ti ti-plus" aria-hidden="true" /> New Backtest
        </button>
      </header>

      {loading && <p className="muted">Loading…</p>}

      {!loading && runs.length === 0 && (
        <div className="empty-state">
          <i className="ti ti-chart-line empty-icon" aria-hidden="true" />
          <p>No runs yet. Start your first backtest!</p>
          <button className="btn-primary" onClick={onNewRun}>Run Backtest</button>
        </div>
      )}

      {runs.length > 0 && (
        <table className="runs-table">
          <thead>
            <tr>
              <th>Run ID</th>
              <th>Status</th>
              <th>Created</th>
              <th>Message</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            {runs.map(run => (
              <tr key={run.run_id} onClick={() => onViewRun(run.run_id)}
                  className="run-row">
                <td><code className="run-id-sm">{run.run_id.slice(0, 8)}…</code></td>
                <td>
                  <span className={`badge ${STATUS_COLOR[run.status] ?? "badge-pending"}`}>
                    {run.status}
                  </span>
                </td>
                <td>{run.created_at
                  ? new Date(parseFloat(run.created_at) * 1000).toLocaleString()
                  : "—"}</td>
                <td className="muted">{run.message}</td>
                <td>
                  <button className="btn-ghost"
                    onClick={e => { e.stopPropagation(); onViewRun(run.run_id); }}>
                    View →
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </div>
  );
}
