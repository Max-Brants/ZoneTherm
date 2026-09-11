<script lang="ts">
  import { onMount } from 'svelte';
  import { fetchJSON } from '../lib/api';
  import { system, systemError, refreshSystem } from '../lib/system';
  import type { Thermostat, ThermostatsResponse } from '../lib/types';
  import { fmtBytes } from '../lib/format';
  import PageHeader from '../components/PageHeader.svelte';
  import Pill from '../components/Pill.svelte';
  import Icon from '../lib/Icon.svelte';

  const REFRESH_MS = 5000;

  let zones: Thermostat[] | null = $state(null);
  let error = $state(false);

  async function load(): Promise<void> {
    try {
      const data = await fetchJSON<ThermostatsResponse>('/api/thermostats');
      zones = data.thermostats ?? [];
      error = false;
    } catch (err) {
      console.error('Failed to load thermostat links', err);
      error = true;
    }
  }

  onMount(() => {
    load();
    refreshSystem();
    const timer = setInterval(load, REFRESH_MS);
    return () => clearInterval(timer);
  });

  const lede = $derived.by((): string => {
    if (!zones) return error ? 'The controller is not answering.' : 'Reading the thermostat links.';
    const total = zones.length;
    const silent = zones.filter((z) => z.status !== 'Active').length;
    const failing = zones.filter((z) => z.failedRequests > 0).length;
    const parts = [
      silent === 0
        ? `All ${total} thermostats are linked.`
        : `${silent} of ${total} thermostats ${silent === 1 ? 'has' : 'have'} no signal.`,
    ];
    if (failing) parts.push(`${failing} ${failing === 1 ? 'link has' : 'links have'} failed requests.`);
    return parts.join(' ');
  });

  const controller = $derived<[string, string][]>(
    $system
      ? [
          ['Hostname', $system.hostname],
          ['Network', $system.network],
          ['IP address', $system.ip],
          ['MAC address', $system.mac],
          ['Uptime', $system.uptime],
          ['Free heap', fmtBytes($system.heap)],
          ['Firmware', $system.fwVersion],
          ['ESP-IDF', $system.sdkVersion],
          ['CPU clock', `${$system.cpuFreq} MHz`],
          ['Flash', fmtBytes($system.flashSize)],
        ]
      : []
  );

  const stateLabel: Record<string, string> = { idle: 'Idle', off: 'Off' };

  function restart(): void {
    if (!confirm('Restart the controller now?')) return;
    // Full page load on purpose: the firmware serves restart.html and reboots.
    window.location.href = '/restart';
  }
</script>

<main class="page">
  <PageHeader title="Diagnostics" {lede} />

  <section class="section">
    <div class="section-head">
      <h2>Thermostat links</h2>
      <p>
        One OpenTherm channel per zone. A thermostat counts as linked while it keeps polling;
        a minute of silence means it is unplugged or has failed.
      </p>
    </div>

    {#if zones && zones.length}
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Zone</th>
              <th>Link</th>
              <th class="num">Requests</th>
              <th class="num">Failed</th>
              <th class="num">Error</th>
              <th>Valves</th>
              <th>State</th>
            </tr>
          </thead>
          <tbody>
            {#each zones as z (z.id)}
              <tr>
                <td><span class="zone-id">{z.id}</span>{z.name || `Zone ${z.id}`}</td>
                <td>
                  {#if z.status === 'Active'}Linked{:else}<Pill tone="warn" icon="warning">No signal</Pill>{/if}
                </td>
                <td class="num">{z.totalRequests}</td>
                <td class="num" class:bad={z.failedRequests > 0}>{z.failedRequests}</td>
                <td class="num">{z.errorCode ? z.errorCode : '—'}</td>
                <td>
                  {#if z.valves.length}
                    {z.valves.map((v) => `V${v}`).join(' ')}
                    <span class="faint">{z.valveOpen ? 'open' : 'closed'}</span>
                  {:else}
                    <span class="none">none</span>
                  {/if}
                </td>
                <td>
                  {#if z.action === 'heating'}
                    <Pill tone="heat" icon="flame" filled>Heating</Pill>
                  {:else if z.action === 'cooling'}
                    <Pill tone="cool" icon="snowflake" filled>Cooling</Pill>
                  {:else}
                    <span class="muted">{stateLabel[z.action] ?? z.action}</span>
                  {/if}
                </td>
              </tr>
            {/each}
          </tbody>
        </table>
      </div>
    {:else if error}
      <p class="notice danger">The controller is not answering. Retrying every 5 seconds.</p>
    {:else}
      <p class="muted">Loading…</p>
    {/if}
  </section>

  <section class="section">
    <div class="section-head">
      <h2>Controller</h2>
    </div>
    {#if $system}
      <dl class="kv">
        {#each controller as [key, value] (key)}
          <div><dt>{key}</dt><dd>{value}</dd></div>
        {/each}
      </dl>
      <div>
        <button type="button" class="btn" onclick={restart}><Icon name="power" />Restart controller</button>
      </div>
    {:else if $systemError}
      <p class="notice danger">System information is unavailable.</p>
    {:else}
      <p class="muted">Loading…</p>
    {/if}
  </section>
</main>

<style>
  .zone-id{display:inline-block;min-width:20px;color:var(--text-3);font-variant-numeric:tabular-nums;}
  .bad{color:var(--danger);font-weight:500;}
  .none{color:var(--warn);}
  td .faint{margin-left:6px;}
</style>
