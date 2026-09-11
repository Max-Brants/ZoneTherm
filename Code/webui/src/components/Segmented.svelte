<script lang="ts" generics="T extends string">
  import Icon from '../lib/Icon.svelte';
  import type { IconName } from '../lib/icons';

  interface Option {
    value: T;
    label: string;
    icon?: IconName;
  }

  let {
    options,
    value,
    onchange,
    label,
    disabled = false,
  }: {
    options: Option[];
    value: T;
    onchange: (value: T) => void;
    label: string;
    disabled?: boolean;
  } = $props();
</script>

<div class="seg" role="radiogroup" aria-label={label}>
  {#each options as option (option.value)}
    <button
      type="button"
      role="radio"
      aria-checked={value === option.value}
      class:on={value === option.value}
      {disabled}
      onclick={() => value !== option.value && onchange(option.value)}>
      {#if option.icon}<Icon name={option.icon} />{/if}
      {option.label}
    </button>
  {/each}
</div>

<style>
  .seg{display:inline-flex;padding:3px;border-radius:var(--r-md);background:var(--surface-3);gap:2px;}
  button{
    display:inline-flex;align-items:center;gap:6px;height:30px;padding:0 12px;border:0;border-radius:6px;
    background:transparent;color:var(--text-2);font-weight:500;font-size:.8125rem;white-space:nowrap;
    transition:background-color .12s,color .12s;
  }
  button:hover{color:var(--text);}
  button.on{background:var(--surface);color:var(--text);box-shadow:0 1px 2px rgba(0,0,0,.08);}
  button:disabled{cursor:default;opacity:.6;}
  button :global(.icon){width:15px;height:15px;}
</style>
