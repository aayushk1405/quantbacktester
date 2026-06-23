// src/App.tsx — Root component with page routing.
import { useState } from "react";
import Dashboard from "./pages/Dashboard";
import BacktestRunner from "./pages/BacktestRunner";
import ResultsPage from "./pages/ResultsPage";
import UploadData from "./pages/UploadData";

type Page = "dashboard" | "run" | "results" | "upload";

export default function App() {
  const [page, setPage] = useState<Page>("dashboard");
  const [activeRunId, setActiveRunId] = useState<string | null>(null);

  const navigate = (p: Page, runId?: string) => {
    setPage(p);
    if (runId) setActiveRunId(runId);
  };

  return (
    <div className="app-shell">
      <Sidebar currentPage={page} onNavigate={navigate} />
      <main className="main-content">
        {page === "dashboard"  && <Dashboard onNewRun={() => navigate("run")} onViewRun={(id) => navigate("results", id)} />}
        {page === "run"        && <BacktestRunner onRunStarted={(id) => navigate("results", id)} />}
        {page === "results"    && <ResultsPage runId={activeRunId} onBack={() => navigate("dashboard")} />}
        {page === "upload"     && <UploadData />}
      </main>
    </div>
  );
}

function Sidebar({ currentPage, onNavigate }: {
  currentPage: Page;
  onNavigate: (p: Page) => void;
}) {
  const links: { id: Page; label: string; icon: string }[] = [
    { id: "dashboard", label: "Dashboard",    icon: "ti-layout-dashboard" },
    { id: "run",       label: "Run Backtest", icon: "ti-player-play" },
    { id: "results",   label: "Results",      icon: "ti-chart-line" },
    { id: "upload",    label: "Upload Data",  icon: "ti-file-upload" },
  ];

  return (
    <nav className="sidebar">
      <div className="sidebar-brand">
        <span className="brand-mark">Q</span>
        <span className="brand-name">QuantEngine</span>
      </div>
      <ul className="sidebar-nav">
        {links.map((l) => (
          <li key={l.id}
              className={`nav-item ${currentPage === l.id ? "active" : ""}`}
              onClick={() => onNavigate(l.id)}>
            <i className={`ti ${l.icon}`} aria-hidden="true" />
            <span>{l.label}</span>
          </li>
        ))}
      </ul>
      <div className="sidebar-footer">
        <span className="version-tag">v1.0.0 · C++ core</span>
      </div>
    </nav>
  );
}
