<script lang="ts">
  let {
    checked,
    onchange,
    label,
    disabled = false,
  }: {
    checked: boolean;
    onchange: (checked: boolean) => void;
    label: string;
    disabled?: boolean;
  } = $props();
</script>

<button
  type="button"
  role="switch"
  class="switch"
  class:on={checked}
  aria-checked={checked}
  aria-label={label}
  {disabled}
  onclick={() => onchange(!checked)}>
  <span class="knob"></span>
</button>

<style>
  .switch{
    position:relative;width:38px;height:22px;flex-shrink:0;padding:0;border:0;border-radius:11px;
    background:var(--line-strong);transition:background-color .15s;
  }
  .switch.on{background:var(--ink);}
  .switch:disabled{opacity:.45;cursor:not-allowed;}
  .knob{
    position:absolute;top:2px;left:2px;width:18px;height:18px;border-radius:50%;
    background:var(--surface);box-shadow:0 1px 2px rgba(0,0,0,.25);transition:transform .15s;
  }
  .switch.on .knob{transform:translateX(16px);background:var(--on-ink);}
</style>
