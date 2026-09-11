<script lang="ts">
  import { onMount, type Component } from 'svelte';
  import { path } from './lib/router';
  import { refreshSystem } from './lib/system';
  import Nav from './components/Nav.svelte';
  import Zones from './pages/Zones.svelte';
  import Diagnostics from './pages/Diagnostics.svelte';
  import Settings from './pages/Settings.svelte';
  import Update from './pages/Update.svelte';

  // Every path here is also a page route in src/web/HttpServer.cpp, so deep
  // links and reloads work without a catch-all rewrite.
  const routes: Record<string, { component: Component; title: string }> = {
    '/': { component: Zones, title: 'Zones' },
    '/diagnostics': { component: Diagnostics, title: 'Diagnostics' },
    '/thermostats': { component: Diagnostics, title: 'Diagnostics' }, // pre-redesign URL
    '/config': { component: Settings, title: 'Settings' },
    '/update': { component: Update, title: 'Update' },
  };

  const active = $derived(routes[$path] ? $path : '/');
  const route = $derived(routes[active]);
  const Page = $derived(route.component);

  $effect(() => {
    document.title = `${route.title} - ZoneTherm`;
  });

  onMount(() => {
    refreshSystem();
    const timer = setInterval(refreshSystem, 15000);
    return () => clearInterval(timer);
  });
</script>

<Nav {active} />
<Page />
