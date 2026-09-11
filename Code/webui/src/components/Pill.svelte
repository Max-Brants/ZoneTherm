<script lang="ts">
  import type { Snippet } from 'svelte';
  import Icon from '../lib/Icon.svelte';
  import type { IconName } from '../lib/icons';

  // A pill encodes a real state and nothing else. `filled` is reserved for the
  // one state the page is about (a zone that is heating or cooling right now).
  let {
    tone = 'neutral',
    icon,
    filled = false,
    children,
  }: {
    tone?: 'neutral' | 'heat' | 'cool' | 'warn' | 'danger';
    icon?: IconName;
    filled?: boolean;
    children: Snippet;
  } = $props();
</script>

<span class="pill {tone}" class:filled>
  {#if icon}<Icon name={icon} />{/if}
  {@render children()}
</span>

<style>
  .pill{
    display:inline-flex;align-items:center;gap:5px;height:26px;padding:0 10px;border-radius:13px;
    font-size:.75rem;font-weight:600;line-height:1;white-space:nowrap;
    border:1px solid var(--line-strong);color:var(--text-2);background:transparent;
  }
  .pill :global(.icon){width:13px;height:13px;stroke-width:2;}
  .heat{color:var(--heat);border-color:var(--heat);}
  .heat.filled{background:var(--heat);color:var(--on-heat);}
  .cool{color:var(--cool);border-color:var(--cool);}
  .cool.filled{background:var(--cool);color:var(--on-cool);}
  .warn{color:var(--warn);border-color:transparent;background:var(--warn-soft);}
  .danger{color:var(--danger);border-color:transparent;background:var(--danger-soft);}
</style>
