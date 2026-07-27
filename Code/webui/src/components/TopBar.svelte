<script lang="ts">
  import { navigate } from '../lib/router';
  import { system, systemError } from '../lib/system';
  import Icon from '../lib/Icon.svelte';

  let { active = '/' }: { active?: string } = $props();

  const links = [
    { href: '/', label: 'Dashboard' },
    { href: '/thermostats', label: 'Thermostats' },
    { href: '/config', label: 'Config' },
    { href: '/update', label: 'Update' },
  ];

  let menuOpen = $state(false);

  function go(event: MouseEvent, href: string): void {
    event.preventDefault();
    menuOpen = false;
    navigate(href);
  }
</script>

<header class="topbar">
  <div class="topbar-inner">
    <a class="brand" href="/" onclick={(e) => go(e, '/')}>
      <span class="brand-mark"><Icon name="thermometer" /></span>
      ZoneTherm
    </a>
    <nav class="toplinks">
      {#each links as link}
        <a
          class="toplink"
          class:active={active === link.href}
          href={link.href}
          onclick={(e) => go(e, link.href)}>{link.label}</a>
      {/each}
    </nav>
    <div class="topbar-status">
      <span class="dot pulse" style="color:var({$systemError ? '--danger' : '--success'})"></span>
      {#if $system}
        <span>{$system.hostname}</span>
      {:else if $systemError}
        <span>Unavailable</span>
      {:else}
        <span class="loading">Loading…</span>
      {/if}
    </div>
    <button
      type="button"
      class="menu-toggle"
      aria-label={menuOpen ? 'Close menu' : 'Open menu'}
      aria-expanded={menuOpen}
      onclick={() => (menuOpen = !menuOpen)}>
      <Icon name={menuOpen ? 'close' : 'menu'} />
    </button>
  </div>
  {#if menuOpen}
    <nav class="mobile-menu">
      {#each links as link}
        <a
          class="mobile-link"
          class:active={active === link.href}
          href={link.href}
          onclick={(e) => go(e, link.href)}>{link.label}</a>
      {/each}
    </nav>
  {/if}
</header>
