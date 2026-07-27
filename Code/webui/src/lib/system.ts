import { writable } from 'svelte/store';
import { fetchJSON } from './api';
import type { SystemInfo } from './types';

// Shared /api/system snapshot: the top bar shows the hostname on every page
// and several pages need fwVersion/ip/mac/mode from the same payload.
export const system = writable<SystemInfo | null>(null);
export const systemError = writable(false);

export async function refreshSystem(): Promise<void> {
  try {
    system.set(await fetchJSON<SystemInfo>('/api/system'));
    systemError.set(false);
  } catch (err) {
    console.error('Failed to load system info', err);
    systemError.set(true);
  }
}
