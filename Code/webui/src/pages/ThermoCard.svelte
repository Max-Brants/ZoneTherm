<script lang="ts">
  import { post } from '../lib/api';
  import type { Thermostat, ThermostatAction } from '../lib/types';
  import Icon from '../lib/Icon.svelte';
  import type { IconName } from '../lib/icons';

  let { t, onChanged }: { t: Thermostat; onChanged: () => void } = $props();

  const actionLabels: Record<ThermostatAction, string> = {
    heating: 'Heating',
    cooling: 'Cooling',
    idle: 'Idle',
    off: 'Off',
  };
  const actionIcons: Record<ThermostatAction, IconName> = {
    heating: 'flame',
    cooling: 'snowflake',
    idle: 'circle',
    off: 'power',
  };

  const statusClass = $derived(
    `status-${String(t.status || 'unknown').toLowerCase().replace(/[^a-z0-9]+/g, '-')}`
  );
  const actionClass = $derived(
    t.action === 'heating' ? 'action-heat' : t.action === 'cooling' ? 'action-cool' : ''
  );
  const serverName = $derived(t.name || `Thermostat ${t.id}`);

  const formatTemp = (value: number): string => Number(value).toFixed(1);
  const clampTemp = (value: number): number => Math.min(30, Math.max(5, value));

  let nameEl: HTMLInputElement | undefined = $state();
  let tempEl: HTMLInputElement | undefined = $state();
  // svelte-ignore state_referenced_locally -- initial values; the effects below re-sync
  let name = $state(t.name || `Thermostat ${t.id}`);
  // svelte-ignore state_referenced_locally
  let tempValue = $state(formatTemp(t.setpoint));

  // Refresh the editable fields from the server unless the user is typing in them.
  $effect(() => {
    if (document.activeElement !== nameEl) name = serverName;
  });
  $effect(() => {
    const fresh = formatTemp(t.setpoint);
    if (document.activeElement !== tempEl) tempValue = fresh;
  });

  function adjust(step: number): void {
    tempValue = clampTemp((parseFloat(tempValue) || 0) + step).toFixed(1);
  }

  async function apply(): Promise<void> {
    const value = clampTemp(parseFloat(tempValue) || 0);
    tempValue = value.toFixed(1);
    try {
      await post(`/api/thermostat/${t.id}/settemp?temp=${value.toFixed(1)}`);
      setTimeout(onChanged, 400);
    } catch (err) {
      console.error('Temperature update failed', err);
    }
  }

  async function setEnabled(event: Event): Promise<void> {
    const enabled = (event.currentTarget as HTMLInputElement).checked;
    try {
      await post(`/api/thermostat/${t.id}/enable?enabled=${enabled}`);
      setTimeout(onChanged, 400);
    } catch (err) {
      console.error('Enable toggle failed', err);
    }
  }

  async function rename(): Promise<void> {
    const trimmed = name.trim();
    if (!trimmed) {
      name = serverName;
      return;
    }
    try {
      await post(`/api/thermostat/${t.id}/setname?name=${encodeURIComponent(trimmed)}`);
      setTimeout(onChanged, 400);
    } catch (err) {
      console.error('Name update failed', err);
    }
  }
</script>

<article class="card thermo-card {statusClass}">
  <header>
    <!-- svelte-ignore a11y_missing_content -- the name <input> is the heading text -->
    <h2>
      <span class="dot pulse"></span>
      <input
        type="text"
        class="name-input"
        bind:this={nameEl}
        bind:value={name}
        title={serverName}
        maxlength="32"
        aria-label="Thermostat name"
        onchange={rename}
        onkeydown={(e) => e.key === 'Enter' && (e.currentTarget as HTMLInputElement).blur()} />
    </h2>
    <span class="tag">{t.status}</span>
  </header>
  <span class="tag {actionClass}"><Icon name={actionIcons[t.action] ?? 'circle'} />{actionLabels[t.action] ?? 'Idle'}</span>
  <div class="temp">{formatTemp(t.currentTemp)}°C</div>
  <div class="setpoint">Setpoint <strong>{formatTemp(t.setpoint)}°C</strong></div>
  <div class="row">
    <button type="button" class="btn btn-circle" onclick={() => adjust(-0.5)}>−</button>
    <input
      type="number"
      class="temp-input"
      bind:this={tempEl}
      bind:value={tempValue}
      min="5"
      max="30"
      step="0.5"
      inputmode="decimal" />
    <button type="button" class="btn btn-circle" onclick={() => adjust(0.5)}>+</button>
    <button type="button" class="btn btn-primary" onclick={apply}>Apply</button>
  </div>
  <div class="toggles">
    <label class="switch">
      <input type="checkbox" checked={t.enabled} onchange={setEnabled} />
      <span class="track"></span>Enabled
    </label>
  </div>
  <div class="details">
    <span>
      {#if t.valves?.length}
        {t.valves.length === 1 ? 'Valve' : 'Valves'}
        <strong>{t.valves.map((v) => `V${v}`).join(' ')}</strong>
        {t.valveOpen ? 'open' : 'closed'}
      {:else}
        Valves <strong>none</strong>
      {/if}
    </span>
    <span>Requests <strong>{t.totalRequests}</strong></span>
    <span>Failed <strong>{t.failedRequests}</strong></span>
    {#if t.errorCode}<span>Error {t.errorCode}</span>{/if}
  </div>
</article>
