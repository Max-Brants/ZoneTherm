<script lang="ts">
  // The mark: a Z drawn as one continuous stroke, warm at the supply end and
  // cool at the return - the two seasons the plant runs in. Mirrored by
  // public/favicon.svg and brand/ at the repo root; keep the three in step.
  let { size = 28, wordmark = true }: { size?: number; wordmark?: boolean } = $props();
</script>

<span class="brand">
  <svg class="mark" width={size} height={size} viewBox="0 0 32 32" aria-hidden="true">
    <defs>
      <linearGradient id="zt-flow" x1="0" y1="9.5" x2="0" y2="22.5" gradientUnits="userSpaceOnUse">
        <stop offset="0" stop-color="#F0673D" />
        <stop offset="1" stop-color="#4FA8E3" />
      </linearGradient>
    </defs>
    <rect width="32" height="32" rx="8" fill="var(--mark-tile, #1C1B19)" />
    <path
      d="M10 9.5h12l-12 13h12"
      fill="none"
      stroke="url(#zt-flow)"
      stroke-width="4.25"
      stroke-linecap="round"
      stroke-linejoin="round" />
  </svg>
  {#if wordmark}<span class="wordmark">ZoneTherm</span>{/if}
</span>

<style>
  .brand{display:inline-flex;align-items:center;gap:10px;color:var(--text);}
  .mark{display:block;flex-shrink:0;}
  .wordmark{font-weight:600;font-size:1.0625rem;letter-spacing:-.02em;line-height:1;}
</style>
