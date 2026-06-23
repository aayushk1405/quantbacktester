// frontend/vite.config.ts
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    // Proxy API calls to the FastAPI backend during development.
    // This avoids CORS issues without modifying backend config.
    proxy: {
      "/api":      { target: "http://localhost:8000", changeOrigin: true, rewrite: p => p.replace(/^\/api/, "") },
      "/ws":       { target: "ws://localhost:8000",   ws: true },
      "/backtests":{ target: "http://localhost:8000", changeOrigin: true },
      "/strategies":{ target: "http://localhost:8000", changeOrigin: true },
      "/data":     { target: "http://localhost:8000", changeOrigin: true },
      "/healthz":  { target: "http://localhost:8000", changeOrigin: true },
    },
  },
  build: {
    outDir:   "dist",
    sourcemap: true,
    rollupOptions: {
      output: {
        // Split recharts into its own chunk so the main bundle stays small.
        manualChunks: { recharts: ["recharts"] },
      },
    },
  },
});
