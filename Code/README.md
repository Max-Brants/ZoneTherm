# ZoneTherm firmware

Architecture notes for the firmware. For the project overview, hardware,
wiring, API and Home Assistant surface, see the [root README](../README.md).

ESP32-S3 firmware that emulates an OpenTherm **boiler** on 7 channels. Each
channel has a real room thermostat (OpenTherm master) plugged in; the
controller answers its requests, captures the room temperature and setpoint
it reports, and drives that zone's valve through an MCP23017 I2C expander.
One global season (heating/cooling) applies to all zones.

Integrations: MQTT with Home Assistant auto-discovery (one climate + two
sensors per zone), a LittleFS-served web UI with REST API, dual-image OTA
(firmware and filesystem), mDNS, SNTP.

## Building

PlatformIO (primary): `pio run`, web assets: `pio run -t buildfs`, flash
with `pio run -t upload` / `-t uploadfs`. Raw ESP-IDF (`idf.py build`) works
too.

## Source layout

`Main.cpp` is the composition root - it builds every object and wires them
by reference; there are no singletons.

```
src/
  app/        Version.h, Pins.h (board wiring)
  domain/     pure logic, no ESP-IDF includes, compiles off-device:
              OtFrame (frame codec), OtResponder (request -> response +
              state delta), ZoneRegistry (all zone state behind one lock),
              ClimateLogic (the single valve decision, with hysteresis)
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
per-zone name + enabled flag. No credentials live in the source tree.

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
action, outside_temperature, modulation, state}` and commands
`thermostat/N/{setpoint, mode}/command`. Zone state is republished every
30 s and on change. Plant-wide: `system/mode/{state,command}` (`heating`/`cooling`)
and retained availability on `status` (with LWT). A zone's HA climate mode
maps `off` = zone disabled, `heat`/`cool` = enabled under the current
season. Setpoint commands are pushed to the room thermostat via the
OpenTherm TrOverride mechanism.
