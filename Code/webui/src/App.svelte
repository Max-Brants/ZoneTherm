<script lang="ts">
  import { onMount, type Component } from 'svelte';
  import { path } from './lib/router';
  import { system, refreshSystem } from './lib/system';
  import TopBar from './components/TopBar.svelte';
  import Dashboard from './pages/Dashboard.svelte';
  import Thermostats from './pages/Thermostats.svelte';
  import Config from './pages/Config.svelte';
  import Update from './pages/Update.svelte';

  const routes: Record<string, { component: Component; title: string }> = {
    '/': { component: Dashboard, title: 'Dashboard' },
    '/thermostats': { component: Thermostats, title: 'Thermostats' },
    '/config': { component: Config, title: 'Config' },
    '/update': { component: Update, title: 'Update' },
  };

  const route = $derived(routes[$path] ?? routes['/']);
  const Page = $derived(route.component);

  $effect(() => {
    document.title = `${route.title} / ${$system?.hostname ?? 'ZoneTherm'}`;
  });

  onMount(() => {
    refreshSystem();
    const timer = setInterval(refreshSystem, 15000);
    return () => clearInterval(timer);
  });
</script>

<TopBar active={$path} />
<Page />
