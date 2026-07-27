import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// Build straight into the LittleFS image source folder. Filenames are fixed
// (no content hashes) because the firmware registers explicit routes for
// /app.js and /style.css, and restart.html/404.html reference /style.css.
export default defineConfig({
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
    proxy: Object.fromEntries(
      ['/api', '/ota', '/update', '/restart'].map((p) => [
        p,
        { target: process.env.VITE_DEVICE || 'http://zonetherm.local', changeOrigin: true },
      ])
    ),
  },
});
