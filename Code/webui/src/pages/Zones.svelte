<script lang="ts">
  import { onMount } from 'svelte';
  import { fetchJSON, post } from '../lib/api';
  import type { Season, Thermostat, ThermostatsResponse } from '../lib/types';
  import PageHeader from '../components/PageHeader.svelte';
  import Segmented from '../components/Segmented.svelte';
  import ZoneCard from './ZoneCard.svelte';

  const REFRESH_MS = 5000;

  let zones: Thermostat[] | null = $state(null); // null = not loaded yet
  let season: Season = $state('heating');
  let error = $state(false);
  let seasonBusy = $state(false);

  async function load(): Promise<void> {
    try {
      const data = await fetchJSON<ThermostatsResponse>('/api/thermostats');
      zones = data.thermostats ?? [];
      season = data.mode;
      error = false;
    } catch (err) {
      console.error('Failed to load zones', err);
      error = true;
    }
  }

  async function setSeason(next: Season): Promise<void> {
    seasonBusy = true;
    try {
      await post(`/api/system/mode?mode=${next}`);
      season = next;
      await load();
    } catch (err) {
      console.error('Season change failed', err);
    } finally {
      seasonBusy = false;
    }
  }

  onMount(() => {
    load();
    const timer = setInterval(load, REFRESH_MS);
    return () => clearInterval(timer);
  });

  // The one sentence the page is for: which rooms are asking for heat.
  const lede = $derived.by((): string => {
    if (!zones) return error ? 'The controller is not answering.' : 'Reading the thermostats.';
    const total = zones.length;
    const calling = zones.filter((z) => z.action === season).length;
    const off = zones.filter((z) => !z.enabled).length;
    const silent = zones.filter((z) => z.status !== 'Active').length;
    const demand = season === 'heating' ? 'heat' : 'cooling';
    const parts = [
      calling === 0
        ? `No rooms are calling for ${demand}.`
        : `${calling} of ${total} rooms ${calling === 1 ? 'is' : 'are'} calling for ${demand}.`,
    ];
    if (off) parts.push(`${off} switched off.`);
    if (silent) parts.push(`${silent} without a thermostat signal.`);
    return parts.join(' ');
  });
</script>

<main class="page">
  <PageHeader title="Zones" {lede}>
    {#snippet actions()}
      <div class="season">
        <span class="season-label">Plant season</span>
        <Segmented
          label="Plant season"
          value={season}
          disabled={seasonBusy || zones === null}
          options={[
            { value: 'heating', label: 'Heating', icon: 'flame' },
            { value: 'cooling', label: 'Cooling', icon: 'snowflake' },
          ]}
          onchange={setSeason} />
      </div>
    {/snippet}
  </PageHeader>

  {#if error && zones}
    <p class="notice danger">Lost contact with the controller. Showing the last known state.</p>
  {/if}

  {#if zones === null}
    {#if error}
      <p class="notice danger">Retrying every 5 seconds. Check that the controller is powered and on the network.</p>
    {:else}
      <p class="muted">Loading…</p>
    {/if}
  {:else if zones.length === 0}
    <p class="notice">The controller reports no zones.</p>
  {:else}
    <section class="zone-grid" aria-label="Zones">
      {#each zones as zone (zone.id)}
        <ZoneCard t={zone} onChanged={load} />
      {/each}
    </section>
  {/if}
</main>

<style>
  .season{display:flex;align-items:center;gap:10px;}
  .season-label{font-size:.8125rem;color:var(--text-2);}
  .zone-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:16px;}
</style>
