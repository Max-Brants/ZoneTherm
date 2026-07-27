import { writable } from 'svelte/store';

// Minimal history-based router. The firmware serves the SPA shell for every
// page route, so deep links and reloads work without a catch-all rewrite.
export const path = writable(window.location.pathname);

export function navigate(to: string): void {
  if (to === window.location.pathname) return;
  history.pushState({}, '', to);
  path.set(to);
}

window.addEventListener('popstate', () => path.set(window.location.pathname));
