<script lang="ts">
  import Icon from '../lib/Icon.svelte';

  let {
    value,
    min,
    max,
    step,
    onchange,
    label,
    pending = false,
    format = (v: number) => String(v),
  }: {
    value: number;
    min: number;
    max: number;
    step: number;
    onchange: (value: number) => void;
    label: string;
    pending?: boolean;
    format?: (value: number) => string;
  } = $props();

  function nudge(direction: -1 | 1): void {
    const raw = value + direction * step;
    const next = Math.min(max, Math.max(min, Math.round(raw / step) * step));
    if (next !== value) onchange(next);
  }
</script>

<div class="stepper" class:pending>
  <button type="button" aria-label="Lower {label}" disabled={value <= min} onclick={() => nudge(-1)}>
    <Icon name="minus" />
  </button>
  <output aria-label={label} aria-live="polite">{format(value)}</output>
  <button type="button" aria-label="Raise {label}" disabled={value >= max} onclick={() => nudge(1)}>
    <Icon name="plus" />
  </button>
</div>

<style>
  .stepper{display:inline-flex;align-items:center;border:1px solid var(--line-strong);border-radius:var(--r-md);background:var(--surface);height:36px;}
  button{
    width:36px;height:100%;border:0;background:transparent;color:var(--text);display:grid;place-items:center;
    border-radius:var(--r-md);transition:background-color .12s;
  }
  button:hover{background:var(--surface-2);}
  button:disabled{color:var(--text-3);cursor:not-allowed;background:transparent;}
  button :global(.icon){width:16px;height:16px;}
  output{
    min-width:52px;text-align:center;font-weight:600;font-size:.9375rem;font-variant-numeric:tabular-nums;
    color:var(--text);transition:color .12s;
  }
  .pending output{color:var(--text-3);}
</style>
