<script lang="ts">
  import { onMount } from 'svelte';
  import { system } from '../lib/system';
  import { fetchJSON, post } from '../lib/api';
  import type { UpdateStatus } from '../lib/types';
  import Icon from '../lib/Icon.svelte';

  type OtaMode = 'fr' | 'fs';
  interface Status {
    message: string;
    type: '' | 'error' | 'success';
  }

  // ---- GitHub releases ----------------------------------------------------

  let gh: UpdateStatus | null = $state(null);
  let ghError = $state('');

  // Explicit local type: TS narrows `gh` to its initial null inside a $derived
  // initializer, same quirk as `files` below.
  const ghBusy = $derived.by((): boolean => {
    const s: UpdateStatus | null = gh;
    return s?.busy === true || s?.state === 'checking' || s?.state === 'downloading';
  });

  async function refreshGh(): Promise<void> {
    try {
      gh = await fetchJSON<UpdateStatus>('/api/update');
      ghError = '';
    } catch (err) {
      console.error('Failed to load update status', err);
      ghError = 'Could not read update status.';
    }
  }

  async function ghAction(path: string): Promise<void> {
    ghError = '';
    try {
      const res = await post(path);
      if (!res.ok) {
        const body = (await res.json().catch(() => null)) as { error?: string } | null;
        ghError = body?.error ?? `HTTP ${res.status}`;
        return;
      }
      await refreshGh();
    } catch (err) {
      console.error('Update request failed', err);
      ghError = 'Request failed.';
    }
  }

  onMount(() => {
    // Self-scheduling rather than setInterval: a check or download needs a
    // live progress bar, but an idle controller should not be polled every
    // two seconds forever.
    let timer: ReturnType<typeof setTimeout>;
    let stopped = false;

    const loop = async (): Promise<void> => {
      await refreshGh();
      if (stopped) return;
      timer = setTimeout(loop, ghBusy ? 1500 : 20000);
    };
    loop();

    return () => {
      stopped = true;
      clearTimeout(timer);
    };
  });

  let files: FileList | null = $state(null);
  let mode: OtaMode = $state('fr');
  let status: Status | null = $state(null);
  let progress: number | null = $state(null); // 0-100 while uploading, null = hidden
  let uploading = $state(false);
  let uploadReachedEnd = false;

  // Explicit type: TS narrows `files` to its initial null in this initializer.
  const file = $derived<File | null>(files?.[0] ?? null);
  const fileHint = $derived(
    file
      ? `${file.name} (${(file.size / 1048576).toFixed(2)} MB)`
      : 'Drop or choose a firmware/filesystem .bin image'
  );

  function setStatus(message: string, type: Status['type']): void {
    status = { message, type };
  }

  function redirectHomeSoon(): void {
    setTimeout(() => {
      window.location.href = '/';
    }, 8000);
  }

  function upload(event: SubmitEvent): void {
    event.preventDefault();
    if (!file) {
      setStatus('Select a .bin file before uploading.', 'error');
      return;
    }
    const selected = file;
    if (!selected.name.toLowerCase().endsWith('.bin')) {
      setStatus('Only .bin images are accepted.', 'error');
      return;
    }
    uploading = true;
    uploadReachedEnd = false;
    setStatus('Uploading… keep the device powered on.', '');
    progress = 0;

    const xhr = new XMLHttpRequest();
    fetch(`/ota/start?mode=${encodeURIComponent(mode)}`, { cache: 'no-store' })
      .then((response) =>
        response.ok
          ? response.text()
          : response.text().then((text) => Promise.reject(new Error(text || `HTTP ${response.status}`)))
      )
      .then(() => {
        xhr.open('POST', `/update?mode=${encodeURIComponent(mode)}`);
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.setRequestHeader('X-OTA-Filename', selected.name);
        xhr.send(selected);
      })
      .catch((error: unknown) => {
        setStatus(`Update failed: ${error instanceof Error ? error.message : error}`, 'error');
        uploading = false;
        progress = null;
      });

    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        progress = Math.round((e.loaded / e.total) * 100);
        uploadReachedEnd = progress >= 100;
      }
    };
    xhr.onload = () => {
      if (xhr.status === 200) {
        uploadReachedEnd = true;
        progress = 100;
        setStatus('Upload complete. The controller will reboot shortly.', 'success');
        redirectHomeSoon();
      } else {
        setStatus(`Update failed: ${xhr.responseText || 'HTTP ' + xhr.status}`, 'error');
        uploading = false;
      }
    };
    xhr.onerror = () => {
      // The device drops the socket when it reboots right after a full upload.
      if (uploadReachedEnd) {
        progress = 100;
        setStatus('Upload complete. The controller is rebooting; reconnect in a few seconds.', 'success');
        redirectHomeSoon();
        return;
      }
      setStatus('Network error during upload. Please retry.', 'error');
      uploading = false;
      progress = null;
    };
  }
</script>

<main class="page page-narrow">
  <div class="page-hero">
    <div>
      <span class="eyebrow">Maintenance</span>
      <h1>Firmware Update</h1>
    </div>
    <div class="meta-inline">
      <span>Device <strong>{$system?.hostname ?? '…'}</strong></span>
      <span>Firmware <strong>{$system?.fwVersion ?? '…'}</strong></span>
    </div>
  </div>
  <div class="card gh-card">
    <div class="gh-head">
      <h2>Automatic updates</h2>
      {#if gh}
        <span class="pill" class:on={gh.updateAvailable} class:warn={gh.state === 'failed'}>
          {#if gh.state === 'downloading'}Installing…
          {:else if gh.state === 'checking'}Checking…
          {:else if gh.state === 'installed'}Rebooting…
          {:else if gh.updateAvailable}Update available
          {:else if gh.state === 'failed'}Check failed
          {:else if gh.checked}Up to date
          {:else}Not checked yet{/if}
        </span>
      {/if}
    </div>

    {#if gh?.pendingVerify}
      <p class="status">This firmware is on trial. It is confirmed automatically once the
        controller has held a network connection for a minute; if it cannot, the previous
        version is restored on the next restart.</p>
    {/if}

    <div class="meta-inline">
      <span>Installed <strong>{gh?.installedVersion ?? $system?.fwVersion ?? '…'}</strong></span>
      <span>Latest <strong>{gh?.latestVersion || (gh?.checked ? '—' : 'unknown')}</strong></span>
      <span>Mode <strong>{gh?.autoInstall ? 'Automatic' : 'Notify only'}</strong></span>
    </div>

    {#if gh && gh.progress >= 0}
      <div class="progress"><span style="width:{gh.progress}%"></span></div>
    {/if}

    {#if ghError}
      <p class="status error">{ghError}</p>
    {:else if gh?.message}
      <p class="status" class:error={gh.state === 'failed'} class:success={gh.state === 'installed'}>{gh.message}</p>
    {/if}

    <div class="gh-actions">
      <button type="button" class="btn" disabled={ghBusy} onclick={() => ghAction('/api/update/check')}>
        <Icon name="refresh" /> Check now
      </button>
      <button
        type="button"
        class="btn btn-primary"
        disabled={ghBusy || !gh?.updateAvailable}
        onclick={() => ghAction('/api/update/install')}>
        <Icon name="download" /> Install {gh?.latestVersion ?? ''}
      </button>
      {#if gh?.releaseUrl}
        <a class="gh-link" href={gh.releaseUrl} target="_blank" rel="noreferrer noopener">Release notes</a>
      {/if}
    </div>
    <p class="hint">Releases are pulled from GitHub over HTTPS. Repository, schedule and
      automatic installation are set on the <a href="/config">Config</a> page.</p>
  </div>

  <div class="card update-card">
    <h2>Manual upload</h2>
    <label class="dropzone">
      <input type="file" accept=".bin" bind:files onchange={() => { status = null; progress = null; }}>
      <Icon name="upload" />
      <span>{fileHint}</span>
    </label>
    <div class="meta-inline">
      <span>IP <strong>{$system?.ip ?? '…'}</strong></span>
      <span>MAC <strong>{$system?.mac ?? '…'}</strong></span>
    </div>
    <form onsubmit={upload}>
      <fieldset>
        <legend>Update target</legend>
        <div class="modes">
          <label class="chip" class:selected={mode === 'fr'}>
            <input type="radio" name="mode" value="fr" bind:group={mode}>
            <Icon name="download" />
            Firmware
          </label>
          <label class="chip" class:selected={mode === 'fs'}>
            <input type="radio" name="mode" value="fs" bind:group={mode}>
            <Icon name="folder" />
            Filesystem
          </label>
        </div>
      </fieldset>
      {#if progress !== null}
        <div class="progress"><span style="width:{progress}%"></span></div>
      {/if}
      {#if status}
        <p class="status" class:error={status.type === 'error'} class:success={status.type === 'success'}>{status.message}</p>
      {/if}
      <button type="submit" class="btn btn-primary" disabled={!file || uploading}>Upload and Flash</button>
    </form>
  </div>
  <footer>© {new Date().getFullYear()} ZoneTherm · Device reboots after a successful upload.</footer>
</main>

<style>
  form{display:grid;gap:18px}
  .gh-card{display:grid;gap:14px;margin-bottom:1.25rem}
  .gh-card h2,.update-card h2{margin:0;font-size:1.05rem}
  .gh-head{display:flex;align-items:center;justify-content:space-between;gap:1rem;flex-wrap:wrap}
  .pill{padding:.2rem .6rem;border-radius:999px;font-size:.8rem;border:1px solid rgba(128,128,128,.4);opacity:.85}
  .pill.on{border-color:var(--success);color:var(--success);opacity:1}
  .pill.warn{border-color:var(--danger);color:var(--danger);opacity:1}
  .gh-actions{display:flex;gap:.75rem;align-items:center;flex-wrap:wrap}
  .gh-link{font-size:.85rem;opacity:.8}
  .hint{font-size:.85rem;opacity:.75;margin:0}
</style>
