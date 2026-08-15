import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";

// Build straight into ../dist, which is committed: end users run only the
// Python server, never node. `npm run dev` proxies /api to a locally running
// backend so frontend edits hot-reload against real data.
export default defineConfig({
  plugins: [react(), tailwindcss()],
  build: {
    outDir: "../dist",
    emptyOutDir: true,
  },
  server: {
    proxy: {
      "/api": "http://127.0.0.1:8790",
    },
  },
});
