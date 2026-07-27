# ZoneTherm

**A 7-zone OpenTherm heating/cooling controller.** ESP32-S3 firmware, KiCad
hardware, and a Svelte web UI for a house where every room has its own
OpenTherm room thermostat but the plant has a single manifold of zone valves.

Version 1.0.0.

---

## The idea

A normal OpenTherm setup is one thermostat talking to one boiler. ZoneTherm sits
in the middle of seven of those conversations at once:

```
 Room thermostat 1  ──OpenTherm──┐
 Room thermostat 2  ──OpenTherm──┤
        ...                      ├──►  ZoneTherm  ──I²C──►  MCP23017  ──►  7 zone valves
 Room thermostat 7  ──OpenTherm──┘        │
                                          ├──► Ethernet / WiFi ──► MQTT ──► Home Assistant
                                          └──► Web UI + REST API
```

To each thermostat, ZoneTherm **pretends to be a boiler**. It answers every
OpenTherm request the thermostat makes, so the thermostat behaves exactly as it
would on a real appliance — it shows a flame, it runs its own schedule, it lets
you turn the dial. While answering, ZoneTherm captures the two numbers that
matter: the room temperature the thermostat measured, and the setpoint it is
asking for.

It then makes the only decision that is actually its own: for each zone,
should the valve be open? That is a bang-bang comparison with a symmetric
hysteresis band, and it lives in exactly one function
([`ClimateLogic::valveWanted`](Code/src/domain/ClimateLogic.h)).

One **season** — heating or cooling — applies to the whole plant, because
there is one set of pipes. Individual zones can be disabled, but they cannot
disagree about which direction the water is going.

Setpoints can also be pushed the other way. A setpoint sent from Home Assistant
or the web UI is written back to the room thermostat over OpenTherm's
`TrOverride` mechanism, so the wall unit shows the new target too.

### Things worth knowing before you build one

- ZoneTherm **is not a gateway.** There is no upstream boiler connection in the
  main firmware. It terminates all seven OpenTherm links itself and drives
  valves; your boiler is expected to be commanded by something else (a valve
  end-switch loop, or its own thermostat input).
- A zone whose thermostat has never reported a room temperature keeps its valve
  **closed**, as does a disabled zone. There is no fail-open.
- `boilerTemp`, `flameOn`, `fault` and friends exist in the zone state and are
  parsed off the wire, but only `outside_temperature` and `modulation` are
  currently exported as Home Assistant sensors.

---

## Repository layout

| Path | What it is |
|---|---|
| [Code/](Code/) | ESP32-S3 firmware (ESP-IDF via PlatformIO). See [Code/README.md](Code/README.md) for the architecture write-up. |
| [Code/webui/](Code/webui/) | Svelte 5 + Vite + TypeScript single-page app, built into `Code/data/` and flashed as a LittleFS image. |
| [pcb/](pcb/) | KiCad 8 project `ZoneTherm.kicad_pro` — seven OpenTherm slave interfaces, W5500 Ethernet, MCP23017 valve bank. Fabrication outputs in `GERBER/`, `production/`, `jlcpcb/`. |
| [enclosure/](enclosure/) | Placeholder — empty. |
| [.github/workflows/](.github/workflows/) | Release workflow that builds and publishes the OTA assets. |

---

## Hardware

**Board:** ESP32-S3-DevKitC-1 (16 MB flash, no PSRAM required).

**Pinout** — from [`Code/src/app/Pins.h`](Code/src/app/Pins.h), matching the
`Thermos_In_X` / `Thermos_Out_X` nets on the schematic:

| Zone | In (GPIO) | Out (GPIO) |
|---|---|---|
| 1 | 2 | 1 |
| 2 | 42 | 41 |
| 3 | 40 | 39 |
| 4 | 48 | 47 |
| 5 | 20 | 19 |
| 6 | 18 | 17 |
| 7 | 16 | 15 |

I²C to the MCP23017 valve expander: `SDA = 4`, `SCL = 5`.
Ethernet is a W5500 on SPI — see U3 on `ZoneTherm.kicad_pcb`.

**Flash layout** ([`Code/partitions.csv`](Code/partitions.csv)) — dual OTA app
slots plus a large filesystem, which is what makes both firmware *and* web UI
updatable over the air:

| Partition | Offset | Size |
|---|---|---|
| `nvs` | 0x9000 | 16 KB |
| `otadata` | 0xd000 | 8 KB |
| `ota_0` | 0x10000 | 3 MB |
| `ota_1` | 0x310000 | 3 MB |
| `littlefs` | 0x610000 | ~10 MB |

---

## Getting started

### 1. Build and flash

PlatformIO is the primary path. On Windows, `pio` may not be on `PATH` —
`~/.platformio/penv/Scripts/platformio.exe` is the full path.

```bash
cd Code
pio run                 # build firmware
pio run -t upload       # flash over USB
pio run -t buildfs      # build the web UI into data/ and pack a LittleFS image
pio run -t uploadfs     # flash the filesystem
pio run -t monitor      # serial console @ 115200
```

`buildfs`/`uploadfs` run [`webui_build.py`](Code/webui_build.py) first, so the
image always contains the current `webui/src` rather than a stale `data/`.

Raw ESP-IDF (`idf.py build`) also works.

**Flash both** the firmware and the filesystem on a new board. Firmware alone
boots but serves no web UI.

### 2. First boot

Network bring-up is automatic and layered:

1. **Ethernet** (DHCP, zero configuration) is tried first and always wins — if
   the link comes up at any later point, WiFi is shut down and Ethernet takes
   over.
2. No Ethernet link → joins the configured **WiFi** as a station.
3. No WiFi configured, or it never gets an IP within 60 s → opens an open
   provisioning access point **`ZoneTherm-<ID>`** at <http://192.168.4.1/>.

`<ID>` is the last three bytes of the MAC in hex, so each controller is
distinct on a shared network.

The device also advertises itself over mDNS as `zonetherm-<ID>.local`.

### 3. Configure

Everything is runtime-configurable — **no credentials live in the source
tree**. Open `http://<device>/config` (or `POST /api/config`) and set:

- hostname
- WiFi SSID / password
- MQTT broker host, port, username, password, base topic, discovery prefix
- season (heating / cooling)
- valve hysteresis
- per-zone name and enabled flag
- update repo, check interval, auto-install, include-filesystem

Settings persist in NVS namespace `otcfg`. `POST /api/factory-reset` erases it.

---

## Web UI and REST API

Four SPA routes are served from LittleFS: `/`, `/thermostats`, `/config`,
`/update`. `/restart` and a 404 page are standalone HTML so they still render
while the device is rebooting or when assets are missing.

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/system` | GET | hostname, ip, mac, network, heap, uptime, cpuFreq, flashSize, sdkVersion, fwVersion, mode |
| `/api/thermostats` | GET | all seven zones (see below) |
| `/api/config` | GET / POST | full runtime config; secrets are masked on read |
| `/api/system/mode` | POST | set the plant season |
| `/api/thermostat/<1-7>/settemp` | POST | `temp=` — pushes a setpoint to the thermostat via TrOverride |
| `/api/thermostat/<1-7>/setname` | POST | `name=` — 1–32 characters |
| `/api/thermostat/<1-7>/enable` | POST | `enabled=true\|false` |
| `/api/factory-reset` | POST | wipe NVS |
| `/api/update` | GET | updater status |
| `/api/update/check` | POST | check GitHub for a newer release now |
| `/api/update/install` | POST | install the discovered release |
| `/ota/start`, `/update` | GET / POST | manual firmware/filesystem image upload |

Each zone in `/api/thermostats` carries: `id`, `name`, `enabled`, `status`
(`Active`/`Inactive`), `currentTemp`, `setpoint`, `outsideTemp`, `modulation`,
`valveOpen`, `action`, `errorCode`, `totalRequests`, `failedRequests`.

---

## MQTT and Home Assistant

Set a broker on `/config` and ZoneTherm publishes **Home Assistant MQTT
auto-discovery** itself — no YAML. You get one `climate` entity plus two
sensors per zone.

Base topic defaults to `zonetherm/<ID>` and is configurable. Per zone `N`
(1–7):

**Published state**

| Topic | Payload |
|---|---|
| `thermostat/N/temperature` | current room temperature |
| `thermostat/N/setpoint/state` | active setpoint |
| `thermostat/N/mode/state` | `off` \| `heat` \| `cool` |
| `thermostat/N/action` | `off` \| `idle` \| `heating` \| `cooling` |
| `thermostat/N/outside_temperature` | °C |
| `thermostat/N/modulation` | % |
| `thermostat/N/state` | JSON attributes blob |

**Subscribed commands**

| Topic | Payload |
|---|---|
| `thermostat/N/setpoint/command` | target temperature |
| `thermostat/N/mode/command` | `off` disables the zone; `heat`/`cool` enables it |
| `system/mode/command` | `heating` \| `cooling` |

**Plant-wide:** `system/mode/state` and `status` are retained; `status` is also
the last-will topic, so an unplugged controller shows as unavailable in Home
Assistant rather than silently freezing at its last values.

Zone state is republished every 30 s, and immediately on a change.

A zone's HA climate mode maps onto the zone's enabled flag: `off` = disabled,
`heat`/`cool` = enabled under the current season. You cannot put one zone into
cooling while the rest heat — that is a plant-level setting.

---

## Over-the-air updates

ZoneTherm can pull its own releases from GitHub. Configure the repo
(`owner/name`) and interval on `/config`; the default is a 24 h check with
**notify-only** behaviour, so nothing installs without a click unless you
enable auto-install.

Version discovery deliberately **avoids the GitHub REST API**: this board has
no PSRAM, and a `/releases/latest` JSON body would have to be buffered and
parsed on a heap already shared with the HTTP server, MQTT and the OpenTherm
engine. Instead the updater requests `https://github.com/<repo>/releases/latest`
with redirects disabled and reads the tag out of the 302 `Location` header —
one request, zero body bytes, no rate limit, and it works for hand-made
releases. Assets then live at predictable
`.../releases/download/<tag>/{firmware.bin,littlefs.bin}` URLs.

Pushing a `v*` tag runs [`release.yml`](.github/workflows/release.yml), which
stamps the tag into `Version.h`, builds both images, trims the ~10 MB of erased
padding off `littlefs.bin`, and publishes them with a `SHA256SUMS.txt`.

Two constraints worth knowing:

- **The device only installs a strictly newer semver.** With 1.0.0 flashed, tag
  `v1.0.1` to actually exercise the updater.
- **Rollback needs one serial flash to arm.** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`
  is set, and an OTA image boots on probation — `UpdateService` only confirms it
  once the network is up, so a release that cannot get online reverts instead of
  stranding the controller. But the revert logic lives in the *bootloader*, which
  cannot be delivered over the air. Devices flashed before this option was added
  keep the old bootloader until reflashed over USB.
- Asset filenames are hard-coded device-side. Renaming one silently breaks every
  deployed controller.

---

## Architecture at a glance

Full write-up in [Code/README.md](Code/README.md). The short version:

- `Main.cpp` is the composition root — every object is built there and wired by
  reference. No singletons.
- `domain/` is pure logic with no ESP-IDF includes — the frame codec, the request
  responder, the zone registry, and the valve decision — so it can be compiled
  off-device.
- A dedicated `ot_task` (core 1, priority 15) owns all OpenTherm I/O. Responses
  are transmitted 40 ms after the request **by deadline** rather than by
  sleeping, so one channel cannot stall the other six.
- The main task ticks network, valves, MQTT and the updater at 1 Hz.
- All shared zone state lives in `ZoneRegistry` behind a single mutex; callers
  only ever receive value snapshots, never references into locked state.

---

## Credits and licensing

ZoneTherm is released under the MIT License — see [LICENSE](LICENSE).

Third-party code lives in `Code/src/vendor/`. Both libraries keep their upstream
register maps, frame logic and public APIs; only the hardware touchpoints were
rewritten for native ESP-IDF:

- **OpenTherm library** — [ihormelnyk/OpenTherm](https://github.com/ihormelnyk/OpenTherm),
  MIT, © 2023 Ihor Melnyk. Ported from Arduino to native ESP-IDF GPIO/timer APIs.
- **MCP23017 driver** — [blemasle/arduino-mcp23017](https://github.com/blemasle/arduino-mcp23017),
  MIT, © 2017 Bertrand Lemasle. Ported from Arduino `Wire` to `driver/i2c_master.h`.
