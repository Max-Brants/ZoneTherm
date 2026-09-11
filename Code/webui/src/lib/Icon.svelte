<script lang="ts">
  import { icons, type IconName } from './icons';

  let { name }: { name: IconName } = $props();
</script>

<svg class="icon" viewBox="0 0 24 24" aria-hidden="true">{@html icons[name]}</svg>
