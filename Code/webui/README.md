# ZoneTherm Web UI

Svelte 5 + Vite + TypeScript single-page app for the controller's web interface.
API payload types (mirroring `src/web/ApiRoutes.cpp`) live in `src/lib/types.ts`.
Run `npm run check` (svelte-check) to type-check.

## Pages

| Route | Page | What it is for |
|---|---|---|
| `/` | Zones | The control surface: every room's temperature, setpoint and whether it is heating, plus the plant season. |
| `/diagnostics` (also `/thermostats`) | Diagnostics | OpenTherm link health per channel, valve state, and the controller's network and memory figures. Restart lives here. |
| `/config` | Settings | Everything stored in NVS, with a save bar that appears only when something changed. |
| `/update` | Update | GitHub release status and the manual image upload. |

`restart.html` and `404.html` are standalone pages in `public/`; they load only
`style.css` and must not depend on the SPA, because they are shown while the
device reboots or when assets are missing.

## Design

Warm-neutral paper, ink for every control, and colour only where it carries
meaning: ember for a zone that is heating, glacier blue for one that is cooling,
amber for a thermostat with no signal. Tokens are at the top of `src/style.css`;
the mark and wordmark are `src/components/Brand.svelte`, mirrored by
`public/favicon.svg` and the assets in `../../brand/`. System fonts only, since
the device serves everything itself from LittleFS.

## Build

```
npm install
npm run build
```

The build writes straight into `../data/` (the LittleFS image source):

- `index.html`, `app.js`, `style.css` — the SPA, served for `/`, `/diagnostics`, `/thermostats`, `/config`, `/update`
- `restart.html`, `404.html`, `favicon.svg` — copied from `public/`

Filenames are fixed (no content hashes) because the firmware registers explicit
routes for `/app.js`, `/style.css` and `/favicon.svg` (see `src/web/HttpServer.cpp`).

After building, flash the filesystem image (PlatformIO "Upload Filesystem Image",
or the web updater in Filesystem mode with a built LittleFS .bin).

## Development

Against a real controller:

```
npm run dev
```

Vite serves the app locally and proxies `/api`, `/ota`, `/update`, `/restart` to a
running controller (`http://zonetherm.local` by default; override with the
`VITE_DEVICE` environment variable).

Without hardware, run the in-memory mock controller in one terminal and point
Vite at it in another:

```
npm run mock
npm run dev:mock
```

`mock/server.mjs` answers every endpoint the UI uses, drifts room temperatures
towards their setpoints, and walks the updater through a check and an install.
