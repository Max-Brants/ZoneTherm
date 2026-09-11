import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// Build straight into the LittleFS image source folder. Filenames are fixed
// (no content hashes) because the firmware registers explicit routes for
// /app.js and /style.css, and restart.html/404.html reference /style.css.
export default defineConfig(({ mode }) => ({
  plugins: [svelte()],
  build: {
    outDir: '../data',
    emptyOutDir: true,
    rollupOptions: {
      output: {
        entryFileNames: 'app.js',
        chunkFileNames: '[name].js',
        assetFileNames: (asset) =>
          asset.names?.some((n) => n.endsWith('.css')) ? 'style.css' : '[name][extname]',
      },
    },
  },
  server: {
    // During `npm run dev`, proxy API/OTA calls to a running controller.
    // Override the target with e.g.: set VITE_DEVICE=http://192.168.1.50
    // `npm run dev:mock` (vite --mode mock) targets the in-memory mock
    // controller from `npm run mock` instead, for UI work without hardware.
    proxy: Object.fromEntries(
      ['/api', '/ota', '/update', '/restart'].map((p) => [
        p,
        {
          target:
            process.env.VITE_DEVICE || (mode === 'mock' ? 'http://127.0.0.1:8787' : 'http://zonetherm.local'),
          changeOrigin: true,
          // /update is both the SPA page (GET, HTML) and the OTA upload
          // (POST, octet-stream). Only the upload belongs to the controller.
          ...(p === '/update'
            ? { bypass: (req) => (req.headers.accept?.includes('text/html') ? '/index.html' : undefined) }
            : {}),
        },
      ])
    ),
  },
}));
