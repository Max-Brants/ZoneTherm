<script lang="ts">
  import { navigate } from '../lib/router';
  import { system, systemError } from '../lib/system';
  import Brand from './Brand.svelte';
  import Icon from '../lib/Icon.svelte';
  import type { IconName } from '../lib/icons';

  let { active }: { active: string } = $props();

  // `match` lists every path a tab owns: /thermostats is the pre-redesign
  // URL of the channel diagnostics and still resolves there.
  const links: { href: string; label: string; icon: IconName; match: string[] }[] = [
    { href: '/', label: 'Zones', icon: 'zones', match: ['/'] },
    { href: '/diagnostics', label: 'Diagnostics', icon: 'activity', match: ['/diagnostics', '/thermostats'] },
    { href: '/config', label: 'Settings', icon: 'sliders', match: ['/config'] },
    { href: '/update', label: 'Update', icon: 'download', match: ['/update'] },
  ];

  function go(event: MouseEvent, href: string): void {
    event.preventDefault();
    navigate(href);
  }
</script>

<header class="nav">
  <div class="nav-inner">
    <a class="home" href="/" onclick={(e) => go(e, '/')} aria-label="ZoneTherm, zones">
      <Brand />
    </a>
    <nav class="tabs" aria-label="Primary">
      {#each links as link (link.href)}
        <a
          href={link.href}
          class="tab"
          class:on={link.match.includes(active)}
          aria-current={link.match.includes(active) ? 'page' : undefined}
          onclick={(e) => go(e, link.href)}>{link.label}</a>
      {/each}
    </nav>
    <div class="device">
      {#if $systemError}
        <span class="offline">Controller offline</span>
      {:else if $system}
        <span>{$system.hostname}</span>
      {/if}
    </div>
  </div>
</header>

<nav class="tabbar" aria-label="Primary">
  {#each links as link (link.href)}
    <a
      href={link.href}
      class:on={link.match.includes(active)}
      aria-current={link.match.includes(active) ? 'page' : undefined}
      onclick={(e) => go(e, link.href)}>
      <Icon name={link.icon} />
      <span>{link.label}</span>
    </a>
  {/each}
</nav>

<style>
  .nav{position:sticky;top:0;z-index:10;background:var(--bg);border-bottom:1px solid var(--line);}
  .nav-inner{max-width:var(--page-w);margin:0 auto;height:var(--nav-h);padding:0 var(--page-x);display:flex;align-items:center;gap:32px;}
  .home{text-decoration:none;display:inline-flex;}
  .tabs{display:flex;gap:4px;align-self:stretch;}
  .tab{
    display:inline-flex;align-items:center;padding:0 10px;text-decoration:none;
    color:var(--text-2);font-weight:500;font-size:.875rem;position:relative;
    transition:color .12s;
  }
  .tab:hover{color:var(--text);}
  .tab.on{color:var(--text);}
  .tab.on::after{content:"";position:absolute;left:10px;right:10px;bottom:-1px;height:2px;background:var(--text);}
  .device{margin-left:auto;font-size:.8125rem;color:var(--text-2);font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
  .offline{color:var(--danger);font-weight:500;}

  .tabbar{display:none;}

  @media(max-width:720px){
    .tabs{display:none;}
    .nav-inner{gap:16px;}
    .tabbar{
      display:grid;grid-template-columns:repeat(4,1fr);
      position:fixed;left:0;right:0;bottom:0;z-index:10;height:var(--tabbar-h);
      padding-bottom:env(safe-area-inset-bottom);
      background:var(--surface);border-top:1px solid var(--line);
    }
    .tabbar a{
      display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px;
      text-decoration:none;color:var(--text-3);font-size:.6875rem;font-weight:500;
    }
    .tabbar a.on{color:var(--text);}
    .tabbar :global(.icon){width:22px;height:22px;stroke-width:1.6;}
  }
</style>
