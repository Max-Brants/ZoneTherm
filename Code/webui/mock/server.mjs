// A stand-in controller for working on the UI without hardware.
//
//   npm run mock       -> this server on http://127.0.0.1:8787
//   npm run dev:mock   -> vite, proxying /api, /ota, /update, /restart here
//
// It mimics the JSON shapes of src/web/ApiRoutes.cpp closely enough for every
// page, keeps state in memory, drifts room temperatures towards or away from
// their setpoints depending on the valve, and walks the GitHub updater through
// checking -> available -> downloading -> installed. Nothing here is flashed.

import http from 'node:http';

const PORT = Number(process.env.PORT) || 8787;
const FW_VERSION = '1.0.2';
const LATEST = '1.0.3';
const MASK = '•••';

const config = {
  controllerId: '3F9A1C',
  net: { hostname: 'zonetherm-3f9a1c', wifiSsid: 'Home', wifiPass: 'secret' },
  mqtt: {
    enabled: true,
    host: '192.168.1.10',
    port: 1883,
    user: 'zonetherm',
    pass: 'secret',
    baseTopic: 'zonetherm/3F9A1C',
    discoveryPrefix: 'homeassistant',
  },
  control: { mode: 'heating', hysteresisK: 0.3 },
  update: { enabled: true, autoInstall: false, repo: 'Max-Brants/ZoneTherm', checkIntervalH: 24, includeFilesystem: true },
  zones: [
    { id: 1, name: 'Living room', enabled: true, valves: [1, 2] },
    { id: 2, name: 'Kitchen', enabled: true, valves: [3] },
    { id: 3, name: 'Study', enabled: true, valves: [4] },
    { id: 4, name: 'Bedroom', enabled: true, valves: [5] },
    { id: 5, name: 'Bathroom', enabled: true, valves: [6] },
    { id: 6, name: 'Guest room', enabled: false, valves: [] },
    { id: 7, name: 'Hall', enabled: true, valves: [7] },
  ],
};

// Live state per zone; zone 7 has no thermostat plugged in.
const live = config.zones.map((z, i) => ({
  roomTemp: [20.9, 19.4, 21.8, 17.6, 22.4, 16.2, 0][i],
  setpoint: [21.0, 20.0, 21.0, 18.0, 22.0, 17.0, 20.0][i],
  valveOpen: false,
  totalRequests: 100000 + i * 7311,
  failedRequests: i === 2 ? 4 : 0,
  errorCode: 0,
  lastSeen: i === 6 ? 0 : Date.now(),
}));

const update = {
  state: 'idle',
  latestVersion: '',
  releaseUrl: '',
  message: '',
  progress: -1,
  checked: false,
  pendingVerify: false,
};

const bootedAt = Date.now();

function tick() {
  const heating = config.control.mode === 'heating';
  const half = config.control.hysteresisK / 2;
  live.forEach((z, i) => {
    const cfg = config.zones[i];
    if (z.lastSeen) {
      z.lastSeen = Date.now();
      z.totalRequests += 1;
    }
    if (!cfg.enabled || !z.lastSeen || cfg.valves.length === 0) {
      z.valveOpen = false;
    } else if (heating) {
      if (z.roomTemp < z.setpoint - half) z.valveOpen = true;
      if (z.roomTemp > z.setpoint + half) z.valveOpen = false;
    } else {
      if (z.roomTemp > z.setpoint + half) z.valveOpen = true;
      if (z.roomTemp < z.setpoint - half) z.valveOpen = false;
    }
    if (z.lastSeen) {
      const towards = heating ? (z.valveOpen ? 0.02 : -0.01) : z.valveOpen ? -0.02 : 0.01;
      z.roomTemp = Math.round((z.roomTemp + towards + (Math.random() - 0.5) * 0.01) * 100) / 100;
    }
  });
}
setInterval(tick, 1000);

const round1 = (v) => Math.round(v * 10) / 10;
const seasonName = () => config.control.mode;

function thermostatJson(i) {
  const cfg = config.zones[i];
  const z = live[i];
  const active = z.lastSeen !== 0 && Date.now() - z.lastSeen < 60000;
  const action = !cfg.enabled ? 'off' : z.valveOpen ? seasonName() : 'idle';
  return {
    id: cfg.id,
    name: cfg.name,
    enabled: cfg.enabled,
    currentTemp: round1(z.roomTemp),
    setpoint: round1(z.setpoint),
    action,
    status: active ? 'Active' : 'Inactive',
    valveOpen: z.valveOpen,
    valves: cfg.valves,
    totalRequests: z.totalRequests,
    failedRequests: z.failedRequests,
    errorCode: z.errorCode,
    modulation: z.valveOpen ? 100 : 0,
  };
}

function systemJson() {
  const up = Math.floor((Date.now() - bootedAt) / 1000) + 3 * 86400 + 4 * 3600 + 17 * 60;
  return {
    hostname: config.net.hostname,
    ip: '192.168.1.42',
    mac: '7C:DF:A1:3F:9A:1C',
    network: 'Ethernet',
    heap: 187_432 + Math.round(Math.random() * 2000),
    uptime: `${Math.floor(up / 86400)} days, ${Math.floor(up / 3600) % 24}h ${Math.floor(up / 60) % 60}m ${up % 60}s`,
    cpuFreq: 240,
    flashSize: 16 * 1024 * 1024,
    sdkVersion: 'v5.5.4',
    fwVersion: FW_VERSION,
    mode: seasonName(),
  };
}

function configJson() {
  return {
    ...config,
    net: { ...config.net, wifiPass: config.net.wifiPass ? MASK : '' },
    mqtt: { ...config.mqtt, pass: config.mqtt.pass ? MASK : '' },
  };
}

function updateJson() {
  return {
    state: update.state,
    installedVersion: FW_VERSION,
    latestVersion: update.latestVersion,
    releaseUrl: update.releaseUrl,
    message: update.message,
    progress: update.progress,
    updateAvailable: update.state === 'available',
    autoInstall: config.update.autoInstall,
    pendingVerify: update.pendingVerify,
    busy: update.state === 'checking' || update.state === 'downloading',
    checked: update.checked,
  };
}

function startCheck() {
  update.state = 'checking';
  update.message = 'Asking GitHub for the latest release';
  setTimeout(() => {
    update.checked = true;
    update.latestVersion = LATEST;
    update.releaseUrl = `https://github.com/${config.update.repo}/releases/tag/v${LATEST}`;
    update.state = 'available';
    update.message = `Release ${LATEST} is newer than ${FW_VERSION}`;
  }, 2000);
}

function startInstall() {
  update.state = 'downloading';
  update.progress = 0;
  update.message = 'Downloading firmware.bin';
  const step = setInterval(() => {
    update.progress = Math.min(100, update.progress + 7);
    if (update.progress >= 100) {
      clearInterval(step);
      update.state = 'installed';
      update.message = 'Installed. Rebooting into the new firmware';
      update.progress = -1;
    }
  }, 400);
}

function readBody(req) {
  return new Promise((resolve) => {
    const chunks = [];
    req.on('data', (c) => chunks.push(c));
    req.on('end', () => resolve(Buffer.concat(chunks)));
  });
}

function send(res, status, body, type = 'application/json') {
  const payload = typeof body === 'string' ? body : JSON.stringify(body);
  res.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store' });
  res.end(payload);
}
const ok = (res) => send(res, 200, { status: 'success' });
const fail = (res, error, status = 400) => send(res, status, { error });

function applyConfig(body, res) {
  const zones = config.zones.map((z) => ({ ...z, valves: [...z.valves] }));
  for (const z of body.zones ?? []) {
    const target = zones.find((c) => c.id === z.id);
    if (!target) continue;
    if (Array.isArray(z.valves)) {
      if (z.valves.some((v) => v < 1 || v > 7)) return fail(res, 'Valve numbers must be 1-7');
      target.valves = [...new Set(z.valves)].sort((a, b) => a - b);
    }
    if (typeof z.name === 'string' && z.name.trim()) target.name = z.name.trim().slice(0, 32);
    if (typeof z.enabled === 'boolean') target.enabled = z.enabled;
  }
  const seen = new Set();
  for (const z of zones) for (const v of z.valves) {
    if (seen.has(v)) return fail(res, 'Each valve can belong to only one thermostat');
    seen.add(v);
  }
  config.zones = zones;

  let rebootRequired = false;
  if (body.net) {
    const pass = body.net.wifiPass === MASK || body.net.wifiPass === undefined ? config.net.wifiPass : body.net.wifiPass;
    rebootRequired =
      (body.net.hostname ?? config.net.hostname) !== config.net.hostname ||
      (body.net.wifiSsid ?? config.net.wifiSsid) !== config.net.wifiSsid ||
      pass !== config.net.wifiPass;
    config.net = { hostname: body.net.hostname ?? config.net.hostname, wifiSsid: body.net.wifiSsid ?? config.net.wifiSsid, wifiPass: pass };
  }
  if (body.mqtt) {
    const pass = body.mqtt.pass === MASK || body.mqtt.pass === undefined ? config.mqtt.pass : body.mqtt.pass;
    config.mqtt = { ...config.mqtt, ...body.mqtt, pass };
  }
  if (body.control) {
    if (body.control.mode) config.control.mode = body.control.mode;
    if (typeof body.control.hysteresisK === 'number') config.control.hysteresisK = body.control.hysteresisK;
  }
  if (body.update) config.update = { ...config.update, ...body.update };
  send(res, 200, { status: 'success', rebootRequired });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const path = url.pathname;
  const q = url.searchParams;
  const method = req.method;

  if (method === 'GET' && path === '/api/system') return send(res, 200, systemJson());
  if (method === 'GET' && path === '/api/thermostats') {
    return send(res, 200, {
      hostname: config.net.hostname,
      fwVersion: FW_VERSION,
      mode: seasonName(),
      thermostats: config.zones.map((_, i) => thermostatJson(i)),
    });
  }
  if (method === 'POST' && path === '/api/system/mode') {
    const mode = q.get('mode');
    if (mode !== 'heating' && mode !== 'cooling') return fail(res, 'Invalid mode');
    config.control.mode = mode;
    return ok(res);
  }
  const thermo = path.match(/^\/api\/thermostat\/(\d+)\/(\w+)$/);
  if (method === 'POST' && thermo) {
    const id = Number(thermo[1]);
    if (id < 1 || id > 7) return fail(res, 'Invalid thermostat ID');
    const cfg = config.zones[id - 1];
    const z = live[id - 1];
    switch (thermo[2]) {
      case 'settemp': {
        const temp = Number(q.get('temp'));
        if (!Number.isFinite(temp)) return fail(res, 'Missing temp parameter');
        // The wall unit takes a poll or two to echo the override back.
        setTimeout(() => (z.setpoint = Math.min(30, Math.max(5, temp))), 1500);
        return ok(res);
      }
      case 'setname': {
        const name = (q.get('name') ?? '').trim();
        if (!name || name.length > 32) return fail(res, 'Name must be 1-32 characters');
        cfg.name = name;
        return ok(res);
      }
      case 'enable':
        cfg.enabled = q.get('enabled') === 'true';
        return ok(res);
      default:
        return fail(res, 'Unknown action');
    }
  }
  if (method === 'GET' && path === '/api/config') return send(res, 200, configJson());
  if (method === 'POST' && path === '/api/config') {
    try {
      return applyConfig(JSON.parse((await readBody(req)).toString('utf8')), res);
    } catch {
      return fail(res, 'Invalid JSON');
    }
  }
  if (method === 'POST' && path === '/api/factory-reset') return ok(res);
  if (method === 'GET' && path === '/api/update') return send(res, 200, updateJson());
  if (method === 'POST' && path === '/api/update/check') {
    if (update.state === 'checking' || update.state === 'downloading') return fail(res, 'An update operation is already running', 409);
    startCheck();
    return ok(res);
  }
  if (method === 'POST' && path === '/api/update/install') {
    if (update.state !== 'available') return fail(res, 'Nothing staged to install - run a check first', 409);
    startInstall();
    return ok(res);
  }
  if (method === 'GET' && path === '/ota/start') return send(res, 200, 'OK', 'text/plain');
  if (method === 'POST' && path === '/update') {
    await readBody(req);
    return send(res, 200, 'OK', 'text/plain');
  }
  if (method === 'GET' && path === '/restart') {
    return send(res, 200, '<!doctype html><title>Restarting</title><p>Mock controller: pretending to restart.', 'text/html');
  }
  return fail(res, `No mock for ${method} ${path}`, 404);
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`Mock ZoneTherm controller on http://127.0.0.1:${PORT}`);
});
