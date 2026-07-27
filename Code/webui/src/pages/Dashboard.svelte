<script lang="ts">
  import { navigate } from '../lib/router';
  import { system, systemError, refreshSystem } from '../lib/system';
  import { post } from '../lib/api';
  import type { Season } from '../lib/types';
  import Icon from '../lib/Icon.svelte';
  import type { IconName } from '../lib/icons';

  const stats = $derived<{ icon: IconName; label: string; value: string }[]>(
    $system
      ? [
          { icon: 'monitor', label: 'Device IP', value: $system.ip },
          { icon: 'network', label: 'MAC Address', value: $system.mac },
          { icon: 'chip', label: 'Free Heap', value: `${$system.heap} bytes` },
          { icon: 'clock', label: 'Uptime', value: $system.uptime },
          { icon: 'gauge', label: 'CPU Frequency', value: `${$system.cpuFreq} MHz` },
          { icon: 'flash', label: 'Flash Size', value: `${$system.flashSize} bytes` },
          { icon: 'memory', label: 'SDK Version', value: $system.sdkVersion },
        ]
      : []
  );

  async function setMode(mode: Season): Promise<void> {
    try {
      await post(`/api/system/mode?mode=${mode}`);
      await refreshSystem();
    } catch (err) {
      console.error('System mode update failed', err);
    }
  }
</script>

<main class="page">
  <div class="page-hero">
    <div>
      <span class="eyebrow">System Overview</span>
      <h1>ZoneTherm</h1>
    </div>
  </div>

  <section class="section">
    <h2>System Mode</h2>
    <p>Sets whether the whole plant currently calls for heating or cooling. Applies to all thermostats.</p>
    <div class="modes">
      <button
        type="button"
        class="chip"
        class:selected={$system?.mode === 'heating'}
        onclick={() => setMode('heating')}><Icon name="flame" />Heating</button>
      <button
        type="button"
        class="chip"
        class:selected={$system?.mode === 'cooling'}
        onclick={() => setMode('cooling')}><Icon name="snowflake" />Cooling</button>
    </div>
  </section>

  <section class="section">
    <h2>System Snapshot</h2>
    <div class="stat-grid">
      {#if $system}
        {#each stats as stat}
          <div class="stat-tile">
            <span class="icon-badge"><Icon name={stat.icon} /></span>
            <span class="stat-body">
              <span class="stat-label">{stat.label}</span>
              <span class="stat-value">{stat.value}</span>
            </span>
          </div>
        {/each}
      {:else if $systemError}
        <div class="empty">System information unavailable. Retrying…</div>
      {:else}
        <div class="empty">Loading system information…</div>
      {/if}
    </div>
  </section>

  <section class="section">
    <h2>Quick Actions</h2>
    <div class="action-grid">
      <article class="card">
        <div class="icon-badge"><Icon name="thermometer" /></div>
        <h2>Thermostat Control</h2>
        <p>Review temperatures, adjust setpoints, and enable or disable zones.</p>
        <a class="btn btn-primary" href="/thermostats" onclick={(e) => { e.preventDefault(); navigate('/thermostats'); }}>Open Thermostats</a>
      </article>
      <article class="card">
        <div class="icon-badge"><Icon name="download" /></div>
        <h2>Firmware Update</h2>
        <p>
          Current firmware:
          {#if $system}<span>{$system.fwVersion}</span>{:else}<span class="loading">Loading…</span>{/if}.
        </p>
        <a class="btn btn-primary" href="/update" onclick={(e) => { e.preventDefault(); navigate('/update'); }}>Go to Update</a>
      </article>
      <article class="card">
        <div class="icon-badge"><Icon name="refresh" /></div>
        <h2>Device Utilities</h2>
        <p>Restart the controller safely without unplugging power.</p>
        <!-- Full page load on purpose: the firmware serves restart.html and reboots. -->
        <a class="btn btn-primary" href="/restart">Restart Device</a>
      </article>
    </div>
  </section>

  <footer>
    © {new Date().getFullYear()} ZoneTherm · Version
    {#if $system}<span>{$system.fwVersion}</span>{:else}<span class="loading">Loading…</span>{/if}
  </footer>
</main>
