# ZoneTherm Web UI

Svelte 5 + Vite + TypeScript single-page app for the controller's web interface.
API payload types (mirroring `src/web/ApiRoutes.cpp`) live in `src/lib/types.ts`.
Run `npm run check` (svelte-check) to type-check.

## Build

```
npm install
npm run build
```

The build writes straight into `../data/` (the LittleFS image source):

- `index.html`, `app.js`, `style.css` — the SPA, served for `/`, `/thermostats`, `/config`, `/update`
- `restart.html`, `404.html` — standalone pages (copied from `public/`); they must not depend
  on the SPA because they are shown while the device reboots or when assets are missing

Filenames are fixed (no content hashes) because the firmware registers explicit
routes for `/app.js` and `/style.css` (see `src/web/HttpServer.cpp`).

After building, flash the filesystem image (PlatformIO "Upload Filesystem Image",
or the web updater in Filesystem mode with a built LittleFS .bin).

## Development

```
npm run dev
```

Vite serves the app locally and proxies `/api`, `/ota`, `/update`, `/restart` to a
running controller (`http://zonetherm.local` by default; override with the
`VITE_DEVICE` environment variable).
