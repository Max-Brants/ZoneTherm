<script lang="ts">
  import { post } from '../lib/api';
  import type { Thermostat } from '../lib/types';
  import { clampTemp, fmtTemp, SETPOINT_MAX, SETPOINT_MIN, SETPOINT_STEP } from '../lib/format';
  import Icon from '../lib/Icon.svelte';
  import Pill from '../components/Pill.svelte';
  import Switch from '../components/Switch.svelte';
  import Stepper from '../components/Stepper.svelte';

  let { t, onChanged }: { t: Thermostat; onChanged: () => void } = $props();

  const name = $derived(t.name || `Zone ${t.id}`);
  const linked = $derived(t.status === 'Active');
  const hasReading = $derived(linked && t.currentTemp !== 0);

  // ---- Setpoint: optimistic and debounced ----------------------------------
  // The card shows the value being dialled, sends it 700 ms after the last
  // press, and keeps showing it while the thermostat catches up - the wall
  // unit only echoes a TrOverride back on one of its next polls.
  const HOLD_MS = 15000;
  // svelte-ignore state_referenced_locally -- initial value; the effect re-syncs
  let target = $state(clampTemp(t.setpoint));
  let sending = $state(false);
  let failed = $state(false);
  let holdUntil = 0;
  let timer: ReturnType<typeof setTimeout> | undefined;

  $effect(() => {
    const fromServer = clampTemp(t.setpoint);
    if (Date.now() >= holdUntil) target = fromServer;
  });

  function dial(next: number): void {
    target = next;
    failed = false;
    holdUntil = Date.now() + HOLD_MS;
    clearTimeout(timer);
    timer = setTimeout(send, 700);
  }

  async function send(): Promise<void> {
    sending = true;
    try {
      await post(`/api/thermostat/${t.id}/settemp?temp=${target.toFixed(1)}`);
      setTimeout(onChanged, 400);
    } catch (err) {
      console.error('Setpoint update failed', err);
      failed = true;
    } finally {
      sending = false;
    }
  }

  // ---- On / off -------------------------------------------------------------
  // svelte-ignore state_referenced_locally -- initial value; the effect re-syncs
  let enabled = $state(t.enabled);
  $effect(() => {
    enabled = t.enabled;
  });

  async function setEnabled(on: boolean): Promise<void> {
    enabled = on;
    try {
      await post(`/api/thermostat/${t.id}/enable?enabled=${on}`);
      setTimeout(onChanged, 400);
    } catch (err) {
      console.error('Enable toggle failed', err);
      enabled = t.enabled;
    }
  }

  // ---- Rename ---------------------------------------------------------------
  let editing = $state(false);
  let draft = $state('');

  function startEdit(): void {
    draft = name;
    editing = true;
  }

  function cancelEdit(): void {
    editing = false;
  }

  async function commitEdit(): Promise<void> {
    if (!editing) return;
    editing = false;
    const next = draft.trim();
    if (!next || next === name) return;
    try {
      await post(`/api/thermostat/${t.id}/setname?name=${encodeURIComponent(next)}`);
      setTimeout(onChanged, 400);
    } catch (err) {
      console.error('Rename failed', err);
    }
  }

  function focusAndSelect(node: HTMLInputElement): void {
    node.focus();
    node.select();
  }
</script>

<article class="zone" class:off={!enabled} class:silent={!linked}>
  <header>
    {#if editing}
      <input
        class="name-edit"
        type="text"
        maxlength="32"
        aria-label="Zone name"
        bind:value={draft}
        use:focusAndSelect
        onblur={commitEdit}
        onkeydown={(e) => {
          if (e.key === 'Enter') commitEdit();
          else if (e.key === 'Escape') cancelEdit();
        }} />
    {:else}
      <button type="button" class="name" onclick={startEdit} title="Rename this zone">
        <span>{name}</span>
        <Icon name="pencil" />
      </button>
    {/if}
    <Switch checked={enabled} onchange={setEnabled} label="{name} on" />
  </header>

  <div class="reading">
    <p class="temp">
      {#if hasReading}
        <span class="value">{fmtTemp(t.currentTemp)}</span><span class="unit">°C</span>
      {:else}
        <span class="value none" aria-label="No reading">—</span>
      {/if}
    </p>
    {#if !linked}
      <Pill tone="warn" icon="warning">No signal</Pill>
    {:else if !enabled}
      <Pill>Off</Pill>
    {:else if t.action === 'heating'}
      <Pill tone="heat" icon="flame" filled>Heating</Pill>
    {:else if t.action === 'cooling'}
      <Pill tone="cool" icon="snowflake" filled>Cooling</Pill>
    {:else}
      <Pill>Idle</Pill>
    {/if}
  </div>

  <footer>
    <span class="set-label">Set to</span>
    {#if failed}<span class="err">Not sent</span>{/if}
    <Stepper
      value={target}
      min={SETPOINT_MIN}
      max={SETPOINT_MAX}
      step={SETPOINT_STEP}
      label="{name} setpoint"
      pending={sending}
      format={fmtTemp}
      onchange={dial} />
  </footer>
</article>

<style>
  .zone{
    background:var(--surface);border:1px solid var(--line);border-radius:var(--r-lg);
    padding:18px 20px 16px;display:grid;gap:14px;min-width:0;
  }
  header{display:flex;align-items:center;justify-content:space-between;gap:12px;min-height:28px;}
  .name{
    display:inline-flex;align-items:center;gap:6px;min-width:0;margin:-4px -6px;padding:4px 6px;
    border:0;border-radius:var(--r-sm);background:transparent;color:var(--text);
    font-weight:600;font-size:.9375rem;line-height:1.3;text-align:left;
  }
  .name span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
  .name :global(.icon){width:13px;height:13px;color:var(--text-3);opacity:0;transition:opacity .12s;}
  .name:hover :global(.icon),.name:focus-visible :global(.icon){opacity:1;}
  .name-edit{
    flex:1;min-width:0;height:28px;margin:-4px -6px;padding:0 6px;border-radius:var(--r-sm);
    border:1px solid var(--text);background:var(--surface);color:var(--text);
    font-weight:600;font-size:.9375rem;outline:none;
  }
  .reading{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;}
  .temp{display:flex;align-items:baseline;gap:3px;line-height:1;font-variant-numeric:tabular-nums;}
  .temp .value{font-size:2.75rem;font-weight:500;letter-spacing:-.035em;color:var(--text);}
  .temp .unit{font-size:.875rem;font-weight:500;color:var(--text-3);}
  .temp .none{color:var(--text-3);font-weight:400;}
  .reading :global(.pill){margin-bottom:4px;}
  footer{display:flex;align-items:center;gap:12px;padding-top:14px;border-top:1px solid var(--line);}
  .set-label{font-size:.8125rem;color:var(--text-2);margin-right:auto;}
  .err{font-size:.75rem;color:var(--danger);font-weight:500;}
  .off .temp .value,.silent .temp .value{color:var(--text-3);}
  .off .name{color:var(--text-2);}
</style>
