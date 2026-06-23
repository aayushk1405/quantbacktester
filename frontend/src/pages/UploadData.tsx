// frontend/src/pages/UploadData.tsx
// Page for uploading OHLCV CSV data files.
// Shows a drag-and-drop zone, a live preview of the first 5 rows,
// upload progress, and a list of all currently loaded symbols.

import { useEffect, useState } from "react";
import FileUpload from "../components/FileUpload";
import { apiClient } from "../utils/api";

interface SymbolMeta {
  name: string;
}

interface PreviewRow {
  timestamp: string;
  open: string;
  high: string;
  low: string;
  close: string;
  volume: string;
}

export default function UploadData() {
  const [symbols, setSymbols]       = useState<SymbolMeta[]>([]);
  const [file, setFile]             = useState<File | null>(null);
  const [preview, setPreview]       = useState<PreviewRow[]>([]);
  const [uploading, setUploading]   = useState(false);
  const [uploadMsg, setUploadMsg]   = useState<{ text: string; ok: boolean } | null>(null);

  // Load existing symbols on mount
  const refreshSymbols = () =>
    apiClient.get<{ symbols: string[] }>("/data/symbols")
      .then(r => setSymbols(r.symbols.map(s => ({ name: s }))));

  useEffect(() => { refreshSymbols(); }, []);

  // Parse the first 5 data rows for a preview
  const parsePreview = (f: File) => {
    const reader = new FileReader();
    reader.onload = (e) => {
      const text = e.target?.result as string;
      const lines = text.split("\n").filter(Boolean);
      if (lines.length < 2) return;

      const headers = lines[0].split(",");
      const rows: PreviewRow[] = [];
      for (let i = 1; i <= Math.min(5, lines.length - 1); i++) {
        const vals = lines[i].split(",");
        // Convert Unix ms timestamp to readable date if it looks numeric
        let ts = vals[0] ?? "";
        if (/^\d{13}$/.test(ts.trim()))
          ts = new Date(parseInt(ts)).toLocaleDateString();
        rows.push({
          timestamp: ts,
          open:      parseFloat(vals[1] ?? "0").toFixed(2),
          high:      parseFloat(vals[2] ?? "0").toFixed(2),
          low:       parseFloat(vals[3] ?? "0").toFixed(2),
          close:     parseFloat(vals[4] ?? "0").toFixed(2),
          volume:    parseInt(vals[5] ?? "0").toLocaleString(),
        });
      }
      setPreview(rows);
    };
    reader.readAsText(f);
  };

  const handleFileSelected = (f: File) => {
    setFile(f);
    setUploadMsg(null);
    parsePreview(f);
  };

  const handleUpload = async () => {
    if (!file) return;
    setUploading(true);
    setUploadMsg(null);
    try {
      const form = new FormData();
      form.append("file", file);
      const res = await fetch(
        `${import.meta.env.VITE_API_URL ?? "http://localhost:8000"}/data/upload`,
        { method: "POST", body: form }
      );
      if (!res.ok) {
        const err = await res.json();
        throw new Error(err.detail ?? "Upload failed");
      }
      const body = await res.json();
      setUploadMsg({ text: `✓ ${body.symbol} uploaded — ${body.rows} bars`, ok: true });
      setFile(null);
      setPreview([]);
      refreshSymbols();
    } catch (e: any) {
      setUploadMsg({ text: e.message, ok: false });
    } finally {
      setUploading(false);
    }
  };

  return (
    <div className="page">
      <header className="page-header">
        <div>
          <h1>Upload Market Data</h1>
          <p className="subtitle">
            Add OHLCV CSV files to make new symbols available for backtesting.
          </p>
        </div>
      </header>

      <div className="upload-grid">

        {/* ── Drop zone ── */}
        <section className="card">
          <h2>CSV File</h2>
          <p className="upload-format-note">
            Expected format:&nbsp;
            <code>timestamp, open, high, low, close, volume</code>
            <br />
            <span className="muted">
              Timestamp can be Unix milliseconds or YYYY-MM-DD. One file per symbol.
              The filename becomes the ticker (e.g. <code>AAPL.csv</code> → symbol <code>AAPL</code>).
            </span>
          </p>

          <FileUpload onFile={handleFileSelected} disabled={uploading} />

          {uploadMsg && (
            <p className={`upload-feedback ${uploadMsg.ok ? "ok" : "fail"}`}>
              {uploadMsg.text}
            </p>
          )}

          {file && (
            <button
              className="btn-primary upload-btn"
              onClick={handleUpload}
              disabled={uploading}
            >
              {uploading
                ? <><i className="ti ti-loader-2 spin" /> Uploading…</>
                : <><i className="ti ti-upload" /> Upload {file.name}</>}
            </button>
          )}
        </section>

        {/* ── Preview table ── */}
        {preview.length > 0 && (
          <section className="card">
            <h2>Preview — first {preview.length} rows</h2>
            <div className="preview-scroll">
              <table className="runs-table">
                <thead>
                  <tr>
                    <th>Date</th>
                    <th>Open</th>
                    <th>High</th>
                    <th>Low</th>
                    <th>Close</th>
                    <th>Volume</th>
                  </tr>
                </thead>
                <tbody>
                  {preview.map((row, i) => (
                    <tr key={i}>
                      <td><code className="run-id-sm">{row.timestamp}</code></td>
                      <td>{row.open}</td>
                      <td className="good-text">{row.high}</td>
                      <td className="bad-text">{row.low}</td>
                      <td><strong>{row.close}</strong></td>
                      <td className="muted">{row.volume}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </section>
        )}

        {/* ── Loaded symbols ── */}
        <section className="card">
          <h2>Loaded Symbols ({symbols.length})</h2>
          {symbols.length === 0
            ? <p className="muted">No symbols loaded yet. Upload a CSV above.</p>
            : (
              <div className="symbol-grid">
                {symbols.map(s => (
                  <div key={s.name} className="symbol-chip selected">
                    {s.name}
                  </div>
                ))}
              </div>
            )}
        </section>

      </div>
    </div>
  );
}
