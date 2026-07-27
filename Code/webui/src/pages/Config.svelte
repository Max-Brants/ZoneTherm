<script lang="ts">
  import { onMount } from 'svelte';
  import { system } from '../lib/system';
  import { fetchJSON, post } from '../lib/api';
  import type { AppConfig, SaveConfigResponse, Season, ZoneConfig } from '../lib/types';

  let controllerId = $state('…');
  let hostname = $state('');
  let wifiSsid = $state('');
  let wifiPass = $state('');
  let mqttEnabled = $state('true');
  let mqttHost = $state('');
  let mqttPort = $state(1883);
  let mqttUser = $state('');
  let mqttPass = $state('');
  let mqttBase = $state('');
  let mqttDisc = $state('homeassistant');
  let mode: Season = $state('heating');
  let hysteresis = $state(0.3);
  let updEnabled = $state('true');
  let updAuto = $state('false');
  let updRepo = $state('');
  let updInterval = $state(24);
  let updFs = $state(true);
  let zones: ZoneConfig[] = $state([]);
  let note = $state('');

  async function load(): Promise<void> {
    try {
      const cfg = await fetchJSON<AppConfig>('/api/config');
      controllerId = cfg.controllerId;
      hostname = cfg.net.hostname;
      wifiSsid = cfg.net.wifiSsid;
      wifiPass = cfg.net.wifiPass;
      mqttEnabled = String(cfg.mqtt.enabled);
      mqttHost = cfg.mqtt.host;
      mqttPort = cfg.mqtt.port;
      mqttUser = cfg.mqtt.user;
      mqttPass = cfg.mqtt.pass;
      mqttBase = cfg.mqtt.baseTopic;
      mqttDisc = cfg.mqtt.discoveryPrefix;
      mode = cfg.control.mode;
      hysteresis = cfg.control.hysteresisK;
      updEnabled = String(cfg.update.enabled);
      updAuto = String(cfg.update.autoInstall);
      updRepo = cfg.update.repo;
      updInterval = cfg.update.checkIntervalH;
      updFs = cfg.update.includeFilesystem;
      zones = (cfg.zones ?? []).map((z) => ({ ...z }));
    } catch (err) {
      console.error('Failed to load config', err);
      note = 'Failed to load configuration.';
    }
  }

  async function save(): Promise<void> {
    const payload = {
      net: { hostname, wifiSsid, wifiPass },
      mqtt: {
        enabled: mqttEnabled === 'true',
        host: mqttHost,
        port: Math.trunc(mqttPort) || 1883,
        user: mqttUser,
        pass: mqttPass,
        baseTopic: mqttBase,
        discoveryPrefix: mqttDisc,
      },
      control: { mode, hysteresisK: hysteresis || 0.3 },
      update: {
        enabled: updEnabled === 'true',
        autoInstall: updAuto === 'true',
        repo: updRepo.trim(),
        checkIntervalH: Math.trunc(updInterval) || 24,
        includeFilesystem: updFs,
      },
      zones: zones.map((z) => ({ id: z.id, name: z.name, enabled: z.enabled })),
    };
    note = 'Saving…';
    try {
      const res = await fetchJSON<SaveConfigResponse>('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      if (res.rebootRequired) {
        note = 'Saved. Network settings changed - device is rebooting…';
      } else {
        note = 'Saved.';
        setTimeout(load, 500);
      }
    } catch (err) {
      console.error('Save failed', err);
      note = 'Save failed.';
    }
  }

  async function factoryReset(): Promise<void> {
    if (!confirm('Erase ALL stored settings and reboot?')) return;
    try {
      await post('/api/factory-reset');
      note = 'Factory reset - rebooting…';
    } catch (err) {
      console.error('Factory reset failed', err);
    }
  }

  onMount(load);
</script>

<main class="page page-narrow">
  <div class="page-hero">
    <div>
      <span class="eyebrow">Device Settings</span>
      <h1>Configuration</h1>
    </div>
    <div class="meta-inline">
      <span>Controller <strong>{controllerId}</strong></span>
      <span>Firmware <strong>{$system?.fwVersion ?? '…'}</strong></span>
    </div>
  </div>

  <section class="section">
    <h2>Network</h2>
    <p>Ethernet needs no configuration (DHCP). WiFi is the fallback; leave the SSID empty to get the provisioning access point <em>ZoneTherm-&lt;ID&gt;</em> when no Ethernet is present. Changing these reboots the device.</p>
    <div class="form-grid">
      <div class="field"><label for="hostname">Hostname (mDNS: &lt;hostname&gt;.local)</label><input type="text" id="hostname" maxlength="32" bind:value={hostname}></div>
      <div class="field"><label for="wifiSsid">WiFi SSID</label><input type="text" id="wifiSsid" maxlength="32" autocomplete="off" bind:value={wifiSsid}></div>
      <div class="field"><label for="wifiPass">WiFi Password</label><input type="password" id="wifiPass" maxlength="64" autocomplete="new-password" bind:value={wifiPass}></div>
    </div>
  </section>

  <section class="section">
    <h2>MQTT / Home Assistant</h2>
    <p>State publishing and Home Assistant discovery. Applied live, no reboot needed.</p>
    <div class="form-grid">
      <div class="field"><label for="mqttEnabled">MQTT</label>
        <select id="mqttEnabled" bind:value={mqttEnabled}><option value="true">Enabled</option><option value="false">Disabled</option></select>
      </div>
      <div class="field"><label for="mqttHost">Broker host</label><input type="text" id="mqttHost" maxlength="64" placeholder="192.168.1.10" bind:value={mqttHost}></div>
      <div class="field"><label for="mqttPort">Broker port</label><input type="number" id="mqttPort" min="1" max="65535" bind:value={mqttPort}></div>
      <div class="field"><label for="mqttUser">Username</label><input type="text" id="mqttUser" maxlength="32" autocomplete="off" bind:value={mqttUser}></div>
      <div class="field"><label for="mqttPass">Password</label><input type="password" id="mqttPass" maxlength="64" autocomplete="new-password" bind:value={mqttPass}></div>
      <div class="field"><label for="mqttBase">Base topic</label><input type="text" id="mqttBase" maxlength="64" bind:value={mqttBase}></div>
      <div class="field"><label for="mqttDisc">Discovery prefix</label><input type="text" id="mqttDisc" maxlength="32" bind:value={mqttDisc}></div>
    </div>
  </section>

  <section class="section">
    <h2>Control</h2>
    <div class="form-grid">
      <div class="field"><label for="mode">Season</label>
        <select id="mode" bind:value={mode}><option value="heating">Heating</option><option value="cooling">Cooling</option></select>
      </div>
      <div class="field"><label for="hysteresis">Valve hysteresis (K)</label><input type="number" id="hysteresis" min="0" max="5" step="0.1" bind:value={hysteresis}></div>
    </div>
  </section>

  <section class="section">
    <h2>Automatic Updates</h2>
    <p>The controller checks GitHub for a newer release and can flash it unattended. In
      <em>Notify only</em> it stops at the notification and waits for you to press Install on the
      <a href="/update">Update</a> page. A release that cannot get back on the network after
      flashing is rolled back automatically.</p>
    <div class="form-grid">
      <div class="field"><label for="updEnabled">Update checks</label>
        <select id="updEnabled" bind:value={updEnabled}><option value="true">Enabled</option><option value="false">Disabled</option></select>
      </div>
      <div class="field"><label for="updAuto">When an update is found</label>
        <select id="updAuto" bind:value={updAuto}><option value="false">Notify only</option><option value="true">Install automatically</option></select>
      </div>
      <div class="field"><label for="updRepo">GitHub repository (owner/name)</label><input type="text" id="updRepo" maxlength="100" placeholder="maxbrants/ZoneTherm" bind:value={updRepo}></div>
      <div class="field"><label for="updInterval">Check every (hours)</label><input type="number" id="updInterval" min="1" max="720" bind:value={updInterval}></div>
      <div class="field"><label class="switch" for="updFs"><input type="checkbox" id="updFs" bind:checked={updFs}><span class="track"></span>Also update the web UI</label></div>
    </div>
  </section>

  <section class="section">
    <h2>Zones</h2>
    <p>Disabled zones answer their thermostat but keep the valve closed.</p>
    <div>
      {#each zones as zone (zone.id)}
        <div class="zone-row">
          <span class="zone-id">{zone.id}</span>
          <input type="text" maxlength="32" aria-label="Zone {zone.id} name" bind:value={zone.name}>
          <label class="switch"><input type="checkbox" bind:checked={zone.enabled}><span class="track"></span>Enabled</label>
        </div>
      {/each}
    </div>
  </section>

  <div class="form-actions">
    <button type="button" class="btn btn-primary" onclick={save}>Save Configuration</button>
    <span class="save-note">{note}</span>
  </div>

  <section class="section danger">
    <h2>Factory Reset</h2>
    <p>Erases all stored settings (network, MQTT, zones) and reboots with defaults.</p>
    <button type="button" class="btn btn-danger" onclick={factoryReset}>Factory Reset</button>
  </section>

  <footer>© {new Date().getFullYear()} ZoneTherm · Version {$system?.fwVersion ?? '…'}</footer>
</main>

<style>
  .form-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:1rem}
  .field{display:flex;flex-direction:column;gap:.35rem}
  .field label{font-size:.85rem;opacity:.8}
  .field input,.field select{padding:.55rem .7rem;border-radius:8px;border:1px solid rgba(128,128,128,.35);background:transparent;color:inherit;font:inherit}
  .zone-row{display:flex;align-items:center;gap:1rem;padding:.4rem 0}
  .zone-row .zone-id{width:2rem;opacity:.7}
  .zone-row input[type=text]{flex:1;padding:.45rem .6rem;border-radius:8px;border:1px solid rgba(128,128,128,.35);background:transparent;color:inherit;font:inherit}
  .form-actions{display:flex;gap:1rem;align-items:center;margin-top:1rem;flex-wrap:wrap}
  .save-note{opacity:.8}
  .danger{border:1px solid rgba(220,60,60,.4);border-radius:12px;padding:1rem}
  .btn-danger{background:#c0392b;color:#fff}
</style>
