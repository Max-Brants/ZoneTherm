<script lang="ts">
  import type { Snippet } from 'svelte';

  // Every page opens the same way: the title, one sentence that states what
  // the page currently says, and any page-level control on the right.
  let { title, lede, actions }: { title: string; lede?: string; actions?: Snippet } = $props();
</script>

<header class="page-head">
  <div class="page-head-text">
    <h1>{title}</h1>
    {#if lede}<p class="lede">{lede}</p>{/if}
  </div>
  {#if actions}<div class="page-head-actions">{@render actions()}</div>{/if}
</header>
