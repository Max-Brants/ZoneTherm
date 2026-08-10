// Shapes of the JSON payloads produced by src/web/ApiRoutes.cpp.

export type Season = 'heating' | 'cooling';
export type ThermostatAction = 'heating' | 'cooling' | 'idle' | 'off';

export interface SystemInfo {
  hostname: string;
  ip: string;
  mac: string;
  network: string;
  heap: number;
  uptime: string;
  cpuFreq: number;
  flashSize: number;
  sdkVersion: string;
  fwVersion: string;
  mode: Season;
}

export interface Thermostat {
  id: number;
  name: string;
  status: string;
  action: ThermostatAction;
  currentTemp: number;
  setpoint: number;
  enabled: boolean;
  valves: number[]; // the valves this thermostat drives; empty = drives nothing
  valveOpen: boolean; // at least one valve in `valves` is open
  totalRequests: number;
  failedRequests: number;
  errorCode?: number;
}

export interface ThermostatsResponse {
  hostname: string;
  fwVersion: string;
  mode: Season;
  thermostats: Thermostat[];
}

export interface ZoneConfig {
  id: number;
  name: string;
  enabled: boolean;
  valves: number[]; // 1-based valve numbers V1..V7; exclusive across zones
}

export interface AppConfig {
  controllerId: string;
  net: {
    hostname: string;
    wifiSsid: string;
    wifiPass: string;
  };
  mqtt: {
    enabled: boolean;
    host: string;
    port: number;
    user: string;
    pass: string;
    baseTopic: string;
    discoveryPrefix: string;
  };
  control: {
    mode: Season;
    hysteresisK: number;
  };
  update: UpdateConfig;
  zones: ZoneConfig[];
}

export interface UpdateConfig {
  enabled: boolean;
  autoInstall: boolean;
  repo: string;
  checkIntervalH: number;
  includeFilesystem: boolean;
}

export type UpdateState =
  | 'idle'
  | 'checking'
  | 'available'
  | 'downloading'
  | 'failed'
  | 'installed';

// GET /api/update
export interface UpdateStatus {
  state: UpdateState;
  installedVersion: string;
  latestVersion: string;
  releaseUrl: string;
  message: string;
  progress: number; // 0-100 while downloading, -1 otherwise
  updateAvailable: boolean;
  autoInstall: boolean;
  pendingVerify: boolean;
  busy: boolean;
  checked: boolean;
}

export interface SaveConfigResponse {
  status: string;
  rebootRequired: boolean;
}
