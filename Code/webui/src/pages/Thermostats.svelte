<script lang="ts">
  import { onMount } from 'svelte';
  import { fetchJSON } from '../lib/api';
  import type { Season, Thermostat, ThermostatsResponse } from '../lib/types';
  import ThermoCard from './ThermoCard.svelte';

  const REFRESH_MS = 5000;

  let thermostats: Thermostat[] | null = $state(null); // null = still loading
  let mode: Season = $state('heating');
  let fwVersion = $state('');
  let lastUpdate = $state('Never');
  let error = $state(false);

  async function load(): Promise<void> {
    try {
      const data = await fetchJSON<ThermostatsResponse>('/api/thermostats');
      thermostats = data.thermostats ?? [];
      mode = data.mode;
      fwVersion = data.fwVersion;
      lastUpdate = new Date().toLocaleTimeString();
      error = false;
    } catch (err) {
      console.error('Failed to load thermostats', err);
      thermostats = null;
      error = true;
    }
  }

  onMount(() => {
    load();
    const timer = setInterval(load, REFRESH_MS);
    return () => clearInterval(timer);
  });
</script>

<main class="page">
  <div class="page-hero">
    <div>
      <span class="eyebrow">Live Control</span>
      <h1>Thermostat Diagnostics</h1>
    </div>
    <div class="meta-inline">
      <span>Season <strong>{mode === 'cooling' ? 'Cooling' : 'Heating'}</strong></span>
      <span>Firmware {#if fwVersion}<strong>{fwVersion}</strong>{:else}<strong class="loading">Loading…</strong>{/if}</span>
      <span>Last update <strong>{lastUpdate}</strong></span>
    </div>
  </div>
  <section class="thermo-grid">
    {#if error}
      <div class="error">Unable to load thermostat data. Retrying…</div>
    {:else if thermostats === null}
      <div class="empty">Loading thermostat data…</div>
    {:else if thermostats.length === 0}
      <div class="empty">No thermostats reported by the controller.</div>
    {:else}
      {#each thermostats as t (t.id)}
        <ThermoCard {t} onChanged={load} />
      {/each}
    {/if}
  </section>
  <footer>Auto-refresh every 5 seconds. Data refreshed: <span>{lastUpdate}</span>.</footer>
</main>
