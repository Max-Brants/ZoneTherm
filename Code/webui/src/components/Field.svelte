<script lang="ts">
  import type { Snippet } from 'svelte';

  let {
    label,
    id,
    hint,
    span = false,
    children,
  }: {
    label: string;
    id: string;
    hint?: string;
    span?: boolean;
    children: Snippet;
  } = $props();
</script>

<div class="field" class:span>
  <label for={id}>{label}</label>
  {@render children()}
  {#if hint}<p class="hint">{hint}</p>{/if}
</div>
