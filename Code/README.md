# ZoneTherm firmware

Architecture notes for the firmware. For the project overview, hardware,
wiring, API and Home Assistant surface, see the [root README](../README.md).

ESP32-S3 firmware that emulates an OpenTherm **boiler** on 7 channels. Each
channel has a real room thermostat (OpenTherm master) plugged in; the
controller answers its requests, captures the room temperature and setpoint
it reports, and drives that thermostat's valve set through an MCP23017 I2C
expander. A thermostat can own several valves (a room with three underfloor
loops) or none, and a valve belongs to at most one thermostat.
One global season (heating/cooling) applies to all zones.

Integrations: MQTT with Home Assistant auto-discovery (one climate + two
sensors per zone), a LittleFS-served web UI with REST API, dual-image OTA
(firmware and filesystem), mDNS, SNTP.

## Building

PlatformIO (primary): `pio run`, web assets: `pio run -t buildfs`, flash
with `pio run -t upload` / `-t uploadfs`. Raw ESP-IDF (`idf.py build`) works
too.

## Testing

`pio test -e native` runs the host unit tests. The `native` env is plain g++
against libstdc++ - no board, no framework, no IDF - and `build_src_filter`
limits it to `src/domain/`, which is the tested boundary. Suites live in
`test/`:

| Suite | Covers |
|---|---|
| `test_ot_frame` | frame codec: parity, field extraction, f8.8 fixed point |
| `test_ot_responder` | every data ID the slave answers, and its state delta |
| `test_climate_logic` | hysteresis band, both seasons, the fail-safe paths |
| `test_zone_registry` | sparse-delta merge, dirty mask, setpoint clamp |
| `test_string_utils` | urlDecode/trim/parsing on the HTTP request path |
| `test_valve_plan` | valve-set aggregation, the demand latch, exclusivity, unassigned valves |

CI is where these normally run - `.github/workflows/ci.yml` runs them plus a
full firmware build on every push and PR, and `release.yml` runs them before
publishing OTA assets.

Running them locally needs `gcc`/`g++` on `PATH`. PlatformIO's `native`
platform shells out to those names specifically, so an MSVC install does not
satisfy it - on a stock Windows box every suite ERRORs with `'g++' is not
recognized` before compiling anything. Install MSYS2/MinGW to run them here,
or just let CI do it.

A few tests assert behavior worth questioning rather than behavior worth
keeping - the staleness hold in `test_climate_logic` and the 0 degC sentinel
in `test_ot_responder`. Each is flagged with a comment saying so.

## Source layout

`Main.cpp` is the composition root - it builds every object and wires them
by reference; there are no singletons.

```
src/
  app/        Version.h, Pins.h (board wiring)
  domain/     pure logic, no ESP-IDF includes, compiles off-device:
              OtFrame (frame codec), OtResponder (request -> response +
              state delta), ZoneRegistry (all zone state behind one lock),
              ClimateLogic (the per-room decision, with hysteresis),
              ValvePlan (valve sets -> the open-valve mask, plus the
              exclusivity rules the API enforces)
  drivers/    OtChannel (vendored OpenTherm wrapper), ValveBank (MCP23017)
  services/   ConfigStore (NVS), OtEngine (OT task, deadline-scheduled
              responses), ControlService (1 Hz valve tick), MqttService,
              HaDiscovery, TimeService, ZoneJson (the one zone serializer)
  net/        NetworkManager (state machine), EthDriver (W5500),
              WifiDriver (STA + provisioning AP), MdnsService
  web/        HttpServer (pages), ApiRoutes (REST), OtaRoutes (fw+fs OTA)
  vendor/     unmodified third-party: OpenTherm, MCP23017
```

Concurrency: a dedicated `ot_task` (core 1, prio 15) owns all OpenTherm
I/O; responses are transmitted 40 ms after the request by deadline instead
of sleeping. The main task ticks network/valves/MQTT at 1 Hz. esp-mqtt and
esp_http_server run their own tasks. All shared zone state lives in
`ZoneRegistry` behind one mutex; callers only ever get value snapshots.

## Configuration

Everything is runtime-configurable at **http://\<device\>/config** (or
`GET/POST /api/config`) and stored in NVS namespace `otcfg`: hostname, WiFi
credentials, MQTT broker/credentials/topics, season, valve hysteresis, and
per-zone name + enabled flag + valve set. No credentials live in the source
tree.

Valve sets are stored one byte per zone under `z<i>_vlv` (bit v = valve v,
V1 = bit 0). Stores written before schema v3 have no such key, and `readNvs()`
only overwrites keys that exist, so an upgraded device keeps the 1:1 mapping
`applyDefaults()` installs — zone *i* drives valve *i*, exactly as before.

Network bring-up: Ethernet (DHCP, zero config) is primary. Without a link,
the device joins the configured WiFi as a station; with no WiFi configured
(or when it never connects), it opens the open provisioning AP
`ZoneTherm-<ID>` at http://192.168.4.1/ where the config page is served.
Ethernet coming up at any point always wins.

Devices already provisioned keep their stored hostname and MQTT base topic;
only fresh or factory-reset devices pick up the `zonetherm-<ID>` /
`zonetherm/<ID>` defaults.

## MQTT surface

Base topic default `zonetherm/<ID>` (configurable). Per zone `N` (1-7):
state topics `thermostat/N/{temperature, setpoint/state, mode/state,
action, modulation, state}` and commands
`thermostat/N/{setpoint, mode}/command`. Zone state is republished every
30 s and on change. Plant-wide: `system/mode/{state,command}` (`heating`/`cooling`)
and retained availability on `status` (with LWT). A zone's HA climate mode
maps `off` = zone disabled, `heat`/`cool` = enabled under the current
season. Setpoint commands are pushed to the room thermostat via the
OpenTherm TrOverride mechanism.
