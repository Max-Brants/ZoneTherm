<script lang="ts">
  import { onMount } from 'svelte';
  import { fetchJSON, post } from '../lib/api';
  import type { AppConfig, SaveConfigResponse, Season, ZoneConfig } from '../lib/types';
  import PageHeader from '../components/PageHeader.svelte';
  import Field from '../components/Field.svelte';
  import Switch from '../components/Switch.svelte';
  import Segmented from '../components/Segmented.svelte';

  // ApiRoutes echoes this for a stored secret and treats it as "unchanged" on POST.
  const MASK = '•••';
  const VALVES = [1, 2, 3, 4, 5, 6, 7];

  interface Model {
    hostname: string;
    wifiSsid: string;
    wifiPass: string; // '' = keep the stored one
    mqtt: {
      enabled: boolean;
      host: string;
      port: number;
      user: string;
      pass: string; // '' = keep the stored one
      baseTopic: string;
      discoveryPrefix: string;
    };
    mode: Season;
    hysteresisK: number;
    update: {
      enabled: boolean;
      autoInstall: boolean;
      repo: string;
      checkIntervalH: number;
      includeFilesystem: boolean;
    };
    zones: ZoneConfig[];
  }

  const empty = (): Model => ({
    hostname: '',
    wifiSsid: '',
    wifiPass: '',
    mqtt: { enabled: false, host: '', port: 1883, user: '', pass: '', baseTopic: '', discoveryPrefix: 'homeassistant' },
    mode: 'heating',
    hysteresisK: 0.3,
    update: { enabled: true, autoInstall: false, repo: '', checkIntervalH: 24, includeFilesystem: true },
    zones: [],
  });

  let model: Model = $state(empty());
  let baseline = $state('');
  let loaded = $state(false);
  let controllerId = $state('');
  let hasWifiPass = $state(false);
  let hasMqttPass = $state(false);
  let loadError = $state('');
  let saving = $state(false);
  let saved = $state(false);
  let saveError = $state('');
  let rebooting = $state(false);

  const dirty = $derived(loaded && JSON.stringify(model) !== baseline);

  function fromConfig(cfg: AppConfig): Model {
    return {
      hostname: cfg.net.hostname,
      wifiSsid: cfg.net.wifiSsid,
      wifiPass: '',
      mqtt: {
        enabled: cfg.mqtt.enabled,
        host: cfg.mqtt.host,
        port: cfg.mqtt.port,
        user: cfg.mqtt.user,
        pass: '',
        baseTopic: cfg.mqtt.baseTopic,
        discoveryPrefix: cfg.mqtt.discoveryPrefix,
      },
      mode: cfg.control.mode,
      hysteresisK: cfg.control.hysteresisK,
      update: { ...cfg.update },
      zones: (cfg.zones ?? []).map((z) => ({
        id: z.id,
        name: z.name,
        enabled: z.enabled,
        valves: [...(z.valves ?? [])].sort((a, b) => a - b),
      })),
    };
  }

  async function load(): Promise<void> {
    try {
      const cfg = await fetchJSON<AppConfig>('/api/config');
      controllerId = cfg.controllerId;
      hasWifiPass = cfg.net.wifiPass !== '';
      hasMqttPass = cfg.mqtt.pass !== '';
      const next = fromConfig(cfg);
      baseline = JSON.stringify(next);
      model = next;
      loaded = true;
      loadError = '';
    } catch (err) {
      console.error('Failed to load config', err);
      loadError = 'Could not read the configuration from the controller.';
    }
  }

  function discard(): void {
    if (baseline) model = JSON.parse(baseline) as Model;
    saveError = '';
  }

  async function save(): Promise<void> {
    const m = $state.snapshot(model);
    const payload = {
      net: {
        hostname: m.hostname.trim(),
        wifiSsid: m.wifiSsid,
        wifiPass: m.wifiPass || (hasWifiPass ? MASK : ''),
      },
      mqtt: {
        ...m.mqtt,
        port: Math.trunc(m.mqtt.port) || 1883,
        pass: m.mqtt.pass || (hasMqttPass ? MASK : ''),
      },
      control: { mode: m.mode, hysteresisK: m.hysteresisK || 0.3 },
      update: {
        ...m.update,
        repo: m.update.repo.trim(),
        checkIntervalH: Math.trunc(m.update.checkIntervalH) || 24,
      },
      zones: m.zones,
    };
    saving = true;
    saveError = '';
    try {
      const res = await fetchJSON<SaveConfigResponse>('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      if (res.rebootRequired) {
        // Nothing more to save: the device restarts in two seconds.
        baseline = JSON.stringify(model);
        rebooting = true;
        return;
      }
      await load();
      saved = true;
      setTimeout(() => (saved = false), 2500);
    } catch (err) {
      console.error('Save failed', err);
      saveError = err instanceof Error ? err.message : 'Save failed.';
    } finally {
      saving = false;
    }
  }

  async function factoryReset(): Promise<void> {
    if (!confirm('Erase every stored setting (network, MQTT, zones, updates) and restart with defaults?')) return;
    try {
      await post('/api/factory-reset');
      rebooting = true;
    } catch (err) {
      console.error('Factory reset failed', err);
      saveError = err instanceof Error ? err.message : 'Factory reset failed.';
    }
  }

  // ---- Valve sets -----------------------------------------------------------
  // valve -> owning zone id, recomputed from local state so unticking a valve
  // in one row immediately frees it in the others. This is what keeps the
  // exclusivity rule from ever reaching the API as a 400.
  const owner = $derived.by(() => {
    const map = new Map<number, number>();
    for (const z of model.zones) for (const v of z.valves) map.set(v, z.id);
    return map;
  });
  const unassigned = $derived(VALVES.filter((v) => !owner.has(v)));

  function toggleValve(zone: ZoneConfig, valve: number): void {
    const set = new Set(zone.valves);
    if (set.has(valve)) set.delete(valve);
    else set.add(valve);
    zone.valves = [...set].sort((a, b) => a - b);
  }

  // ---- Section navigation ---------------------------------------------------
  const sections = [
    { id: 'device', label: 'Device' },
    { id: 'network', label: 'Network' },
    { id: 'mqtt', label: 'MQTT' },
    { id: 'control', label: 'Control' },
    { id: 'updates', label: 'Updates' },
    { id: 'zones', label: 'Zones' },
    { id: 'reset', label: 'Reset' },
  ];
  let current = $state('device');

  $effect(() => {
    if (!loaded) return;
    const observer = new IntersectionObserver(
      (entries) => {
        for (const entry of entries) if (entry.isIntersecting) current = entry.target.id;
      },
      { rootMargin: '-25% 0px -65% 0px' }
    );
    for (const s of sections) {
      const el = document.getElementById(s.id);
      if (el) observer.observe(el);
    }
    return () => observer.disconnect();
  });

  onMount(load);
</script>

<main class="page" class:with-bar={dirty || saving || saved || saveError}>
  <PageHeader
    title="Settings"
    lede={loaded ? `Controller ${controllerId}. Changes apply when you save; network changes restart the device.` : undefined} />

  {#if rebooting}
    <p class="notice">Saved. The controller is restarting; reload this page in a moment, at the new hostname if you changed it.</p>
  {:else if loadError}
    <p class="notice danger">{loadError}</p>
  {:else if !loaded}
    <p class="muted">Loading…</p>
  {:else}
    <div class="layout">
      <nav class="toc" aria-label="Sections">
        {#each sections as s (s.id)}
          <a href="#{s.id}" class:on={current === s.id}>{s.label}</a>
        {/each}
      </nav>

      <div class="sections">
        <section class="section" id="device">
          <div class="section-head">
            <h2>Device</h2>
            <p>Controller {controllerId}. The hostname is also the mDNS name.</p>
          </div>
          <div class="form-grid">
            <Field label="Hostname" id="hostname" hint="Reachable as {model.hostname || 'zonetherm'}.local">
              <input id="hostname" type="text" maxlength="32" autocomplete="off" bind:value={model.hostname} />
            </Field>
          </div>
        </section>

        <section class="section" id="network">
          <div class="section-head">
            <h2>Network</h2>
            <p>
              Ethernet needs no setup and always wins when a cable is plugged in. WiFi is the fallback.
              With no SSID and no Ethernet, the controller opens the access point ZoneTherm-{controllerId} instead.
            </p>
          </div>
          <div class="form-grid">
            <Field label="WiFi network" id="wifiSsid">
              <input id="wifiSsid" type="text" maxlength="32" autocomplete="off" bind:value={model.wifiSsid} />
            </Field>
            <Field label="WiFi password" id="wifiPass" hint={hasWifiPass ? 'Leave empty to keep the stored password.' : undefined}>
              <input
                id="wifiPass"
                type="password"
                maxlength="64"
                autocomplete="new-password"
                placeholder={hasWifiPass ? 'Unchanged' : 'None'}
                bind:value={model.wifiPass} />
            </Field>
          </div>
        </section>

        <section class="section" id="mqtt">
          <div class="section-head">
            <h2>MQTT</h2>
            <p>Publishes zone state and announces one climate entity per zone to Home Assistant. Applied live.</p>
          </div>
          <div class="check-row">
            <div class="check-text">Publish over MQTT</div>
            <Switch checked={model.mqtt.enabled} onchange={(v) => (model.mqtt.enabled = v)} label="Publish over MQTT" />
          </div>
          <div class="form-grid">
            <Field label="Broker host" id="mqttHost">
              <input id="mqttHost" type="text" maxlength="64" placeholder="192.168.1.10" bind:value={model.mqtt.host} />
            </Field>
            <Field label="Broker port" id="mqttPort">
              <input id="mqttPort" type="number" min="1" max="65535" bind:value={model.mqtt.port} />
            </Field>
            <Field label="Username" id="mqttUser">
              <input id="mqttUser" type="text" maxlength="32" autocomplete="off" bind:value={model.mqtt.user} />
            </Field>
            <Field label="Password" id="mqttPass" hint={hasMqttPass ? 'Leave empty to keep the stored password.' : undefined}>
              <input
                id="mqttPass"
                type="password"
                maxlength="64"
                autocomplete="new-password"
                placeholder={hasMqttPass ? 'Unchanged' : 'None'}
                bind:value={model.mqtt.pass} />
            </Field>
            <Field label="Base topic" id="mqttBase">
              <input id="mqttBase" type="text" maxlength="64" bind:value={model.mqtt.baseTopic} />
            </Field>
            <Field label="Discovery prefix" id="mqttDisc">
              <input id="mqttDisc" type="text" maxlength="32" bind:value={model.mqtt.discoveryPrefix} />
            </Field>
          </div>
        </section>

        <section class="section" id="control">
          <div class="section-head">
            <h2>Control</h2>
            <p>One season for the whole plant. The hysteresis is the band around each setpoint within which a valve holds its state.</p>
          </div>
          <div class="form-grid">
            <Field label="Season" id="season">
              <div id="season">
                <Segmented
                  label="Season"
                  value={model.mode}
                  options={[
                    { value: 'heating', label: 'Heating', icon: 'flame' },
                    { value: 'cooling', label: 'Cooling', icon: 'snowflake' },
                  ]}
                  onchange={(v) => (model.mode = v)} />
              </div>
            </Field>
            <Field label="Valve hysteresis" id="hysteresis">
              <div class="with-unit">
                <input id="hysteresis" type="number" min="0" max="5" step="0.1" bind:value={model.hysteresisK} />
                <span class="unit">K</span>
              </div>
            </Field>
          </div>
        </section>

        <section class="section" id="updates">
          <div class="section-head">
            <h2>Updates</h2>
            <p>
              The controller looks for newer releases on GitHub. With notify only, it waits for you to press
              Install on the <a href="/update">Update</a> page. A release that cannot get back online is rolled back.
            </p>
          </div>
          <div class="check-row">
            <div class="check-text">Check for updates</div>
            <Switch checked={model.update.enabled} onchange={(v) => (model.update.enabled = v)} label="Check for updates" />
          </div>
          <div class="form-grid">
            <Field label="When a release is found" id="updAuto">
              <div id="updAuto">
                <Segmented
                  label="When a release is found"
                  value={model.update.autoInstall ? 'auto' : 'notify'}
                  options={[
                    { value: 'notify', label: 'Notify only' },
                    { value: 'auto', label: 'Install automatically' },
                  ]}
                  onchange={(v) => (model.update.autoInstall = v === 'auto')} />
              </div>
            </Field>
            <Field label="Check every" id="updInterval">
              <div class="with-unit">
                <input id="updInterval" type="number" min="1" max="720" bind:value={model.update.checkIntervalH} />
                <span class="unit">hours</span>
              </div>
            </Field>
            <Field label="GitHub repository" id="updRepo" hint="owner/name" span>
              <input id="updRepo" type="text" maxlength="100" placeholder="Max-Brants/ZoneTherm" bind:value={model.update.repo} />
            </Field>
          </div>
          <div class="check-row">
            <div class="check-text">Include the web interface<span>Also flashes the filesystem image of a release.</span></div>
            <Switch
              checked={model.update.includeFilesystem}
              onchange={(v) => (model.update.includeFilesystem = v)}
              label="Include the web interface in updates" />
          </div>
        </section>

        <section class="section" id="zones">
          <div class="section-head">
            <h2>Zones</h2>
            <p>
              Each zone is one thermostat driving a set of valves, V1 to V7 on the terminal row. A valve belongs
              to one zone only, so one already taken is greyed out. A zone that is off answers its thermostat but
              keeps its valves closed.
            </p>
          </div>
          <div class="zone-list">
            {#each model.zones as zone (zone.id)}
              <div class="zone-row">
                <span class="zone-id">{zone.id}</span>
                <input
                  class="input"
                  type="text"
                  maxlength="32"
                  aria-label="Zone {zone.id} name"
                  bind:value={zone.name} />
                <Switch checked={zone.enabled} onchange={(v) => (zone.enabled = v)} label="Zone {zone.id} on" />
                <div class="valves" role="group" aria-label="Zone {zone.id} valves">
                  {#each VALVES as v (v)}
                    {@const takenBy = owner.get(v)}
                    {@const mine = takenBy === zone.id}
                    {@const locked = takenBy !== undefined && !mine}
                    <button
                      type="button"
                      class="valve"
                      class:on={mine}
                      disabled={locked}
                      aria-pressed={mine}
                      title={locked ? `V${v} belongs to zone ${takenBy}` : `Valve V${v}`}
                      onclick={() => toggleValve(zone, v)}>V{v}</button>
                  {/each}
                  {#if zone.valves.length === 0}
                    <span class="valve-note">No valves, so this zone cannot open anything</span>
                  {/if}
                </div>
              </div>
            {/each}
          </div>
          {#if unassigned.length}
            <p class="notice warn">
              {unassigned.map((v) => `V${v}`).join(', ')} {unassigned.length === 1 ? 'belongs' : 'belong'} to no zone and
              {unassigned.length === 1 ? 'stays' : 'stay'} closed.
            </p>
          {/if}
        </section>

        <section class="section" id="reset">
          <div class="section-head">
            <h2>Reset</h2>
            <p>Erases every stored setting and restarts with defaults. The controller comes back as ZoneTherm-{controllerId}.</p>
          </div>
          <div>
            <button type="button" class="btn btn-danger" onclick={factoryReset}>Factory reset</button>
          </div>
        </section>
      </div>
    </div>
  {/if}
</main>

{#if loaded && !rebooting && (dirty || saving || saved || saveError)}
  <div class="savebar" role="status">
    <div class="savebar-inner">
      <span class="savebar-text" class:err={!!saveError}>
        {#if saveError}{saveError}
        {:else if saving}Saving…
        {:else if saved}Saved
        {:else}Unsaved changes{/if}
      </span>
      <div class="savebar-actions">
        <button type="button" class="btn btn-quiet" disabled={saving || !dirty} onclick={discard}>Discard</button>
        <button type="button" class="btn btn-primary" disabled={saving || !dirty} onclick={save}>Save</button>
      </div>
    </div>
  </div>
{/if}

<style>
  .with-bar{padding-bottom:128px;}
  .layout{display:grid;grid-template-columns:minmax(0,1fr);gap:32px;align-items:start;}
  .toc{display:none;}
  .sections{display:grid;gap:40px;min-width:0;max-width:720px;}
  .section{scroll-margin-top:calc(var(--nav-h) + 24px);}

  .zone-list{display:grid;}
  .zone-row{
    display:grid;grid-template-columns:24px minmax(0,1fr) auto;gap:10px 12px;align-items:center;
    padding:16px 0;border-top:1px solid var(--line);
  }
  .zone-row:first-child{border-top:0;padding-top:0;}
  .zone-id{color:var(--text-3);font-variant-numeric:tabular-nums;font-size:.8125rem;text-align:right;}
  .valves{grid-column:2 / -1;display:flex;flex-wrap:wrap;gap:6px;align-items:center;}
  .valve{
    height:28px;min-width:42px;padding:0 10px;border-radius:14px;border:1px solid var(--line-strong);
    background:transparent;color:var(--text-2);font-size:.75rem;font-weight:600;font-variant-numeric:tabular-nums;
    transition:background-color .12s,color .12s,border-color .12s;
  }
  .valve:hover{border-color:var(--text-3);color:var(--text);}
  .valve.on{background:var(--ink);color:var(--on-ink);border-color:var(--ink);}
  .valve:disabled{opacity:.35;cursor:not-allowed;text-decoration:line-through;}
  .valve-note{font-size:.75rem;color:var(--warn);margin-left:4px;}

  .savebar{
    position:fixed;left:0;right:0;bottom:var(--tabbar-h);z-index:9;
    background:var(--surface);border-top:1px solid var(--line);box-shadow:var(--shadow);
  }
  .savebar-inner{
    max-width:var(--page-w);margin:0 auto;padding:12px var(--page-x);
    display:flex;align-items:center;justify-content:space-between;gap:16px;
  }
  .savebar-text{font-size:.875rem;color:var(--text-2);}
  .savebar-text.err{color:var(--danger);}
  .savebar-actions{display:flex;gap:8px;}

  @media(min-width:900px){
    .layout{grid-template-columns:160px minmax(0,1fr);gap:48px;}
    .toc{display:grid;gap:2px;position:sticky;top:calc(var(--nav-h) + 32px);}
    .toc a{
      padding:6px 10px;border-radius:var(--r-sm);text-decoration:none;color:var(--text-2);font-size:.875rem;
      border-left:2px solid transparent;border-radius:0 var(--r-sm) var(--r-sm) 0;
    }
    .toc a:hover{color:var(--text);}
    .toc a.on{color:var(--text);border-left-color:var(--text);font-weight:500;}
  }
</style>
