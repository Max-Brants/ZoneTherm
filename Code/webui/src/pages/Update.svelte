<script lang="ts">
  import { onMount } from 'svelte';
  import { system } from '../lib/system';
  import { fetchJSON, post } from '../lib/api';
  import type { UpdateStatus } from '../lib/types';
  import PageHeader from '../components/PageHeader.svelte';
  import Pill from '../components/Pill.svelte';
  import Segmented from '../components/Segmented.svelte';
  import Icon from '../lib/Icon.svelte';

  type OtaMode = 'fr' | 'fs';
  interface Status {
    message: string;
    type: '' | 'error' | 'success';
  }

  // ---- GitHub releases ------------------------------------------------------

  let gh: UpdateStatus | null = $state(null);
  let ghError = $state('');

  // Explicit local type: TS narrows `gh` to its initial null inside a $derived
  // initializer, same quirk as `files` below.
  const ghBusy = $derived.by((): boolean => {
    const s: UpdateStatus | null = gh;
    return s?.busy === true || s?.state === 'checking' || s?.state === 'downloading';
  });

  const ghLabel = $derived.by((): string => {
    const s: UpdateStatus | null = gh;
    if (!s) return '';
    if (s.state === 'downloading') return 'Installing';
    if (s.state === 'checking') return 'Checking';
    if (s.state === 'installed') return 'Restarting';
    if (s.state === 'failed') return 'Check failed';
    if (s.updateAvailable) return 'Update available';
    return s.checked ? 'Up to date' : 'Not checked yet';
  });

  const lede = $derived.by((): string => {
    const s: UpdateStatus | null = gh;
    const installed = s?.installedVersion || $system?.fwVersion;
    if (!installed) return 'Reading the update status.';
    if (s?.updateAvailable && s.latestVersion) {
      return `Firmware ${installed} is installed. Release ${s.latestVersion} is available.`;
    }
    if (s?.checked && s.state !== 'failed') return `Firmware ${installed} is installed and is the latest release.`;
    return `Firmware ${installed} is installed. The controller has not confirmed the latest release yet.`;
  });

  async function refreshGh(): Promise<void> {
    try {
      gh = await fetchJSON<UpdateStatus>('/api/update');
      ghError = '';
    } catch (err) {
      console.error('Failed to load update status', err);
      ghError = 'Could not read the update status.';
    }
  }

  async function ghAction(path: string): Promise<void> {
    ghError = '';
    try {
      await post(path);
      await refreshGh();
    } catch (err) {
      console.error('Update request failed', err);
      ghError = err instanceof Error ? err.message : 'Request failed.';
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

  // ---- Manual upload --------------------------------------------------------

  let files: FileList | null = $state(null);
  let mode: OtaMode = $state('fr');
  let status: Status | null = $state(null);
  let progress: number | null = $state(null); // 0-100 while uploading, null = hidden
  let uploading = $state(false);
  let uploadReachedEnd = false;

  // Explicit type: TS narrows `files` to its initial null in this initializer.
  const file = $derived<File | null>(files?.[0] ?? null);

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
      setStatus('Choose a .bin image first.', 'error');
      return;
    }
    const selected = file;
    if (!selected.name.toLowerCase().endsWith('.bin')) {
      setStatus('Only .bin images are accepted.', 'error');
      return;
    }
    uploading = true;
    uploadReachedEnd = false;
    setStatus('Uploading. Keep the controller powered.', '');
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
        setStatus('Upload complete. The controller is restarting.', 'success');
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
        setStatus('Upload complete. The controller is restarting; reconnect in a few seconds.', 'success');
        redirectHomeSoon();
        return;
      }
      setStatus('The upload was interrupted. Try again.', 'error');
      uploading = false;
      progress = null;
    };
  }
</script>

<main class="page narrow">
  <PageHeader title="Update" {lede} />

  <section class="panel gh">
    <div class="panel-head">
      <h2>Releases on GitHub</h2>
      {#if gh}
        {#if gh.state === 'failed'}
          <Pill tone="danger">{ghLabel}</Pill>
        {:else if gh.updateAvailable && !ghBusy}
          <Pill tone="neutral" filled>{ghLabel}</Pill>
        {:else}
          <Pill>{ghLabel}</Pill>
        {/if}
      {/if}
    </div>

    {#if gh?.pendingVerify}
      <p class="notice">
        This firmware is on trial. It is confirmed once the controller has held a network connection for a
        minute; if it cannot, the previous version is restored on the next restart.
      </p>
    {/if}

    <dl class="kv">
      <div><dt>Installed</dt><dd>{gh?.installedVersion ?? $system?.fwVersion ?? '…'}</dd></div>
      <div><dt>Latest</dt><dd>{gh?.latestVersion || (gh?.checked ? 'unknown' : 'not checked')}</dd></div>
      <div><dt>On a new release</dt><dd>{gh?.autoInstall ? 'Install automatically' : 'Notify only'}</dd></div>
      <div>
        <dt>Release notes</dt>
        <dd>
          {#if gh?.releaseUrl}
            <a href={gh.releaseUrl} target="_blank" rel="noreferrer noopener">GitHub<Icon name="external" /></a>
          {:else}
            <span class="faint">none yet</span>
          {/if}
        </dd>
      </div>
    </dl>

    {#if gh && gh.progress >= 0}
      <div class="progress" role="progressbar" aria-valuenow={gh.progress} aria-valuemin="0" aria-valuemax="100">
        <span style="width:{gh.progress}%"></span>
      </div>
    {/if}

    {#if ghError}
      <p class="notice danger">{ghError}</p>
    {:else if gh?.message}
      <p class="notice" class:danger={gh.state === 'failed'}>{gh.message}</p>
    {/if}

    <div class="actions">
      <button type="button" class="btn" disabled={ghBusy} onclick={() => ghAction('/api/update/check')}>
        <Icon name="refresh" />Check now
      </button>
      <button
        type="button"
        class="btn btn-primary"
        disabled={ghBusy || !gh?.updateAvailable}
        onclick={() => ghAction('/api/update/install')}>
        <Icon name="download" />Install {gh?.latestVersion ?? ''}
      </button>
    </div>
    <p class="hint">
      Repository, schedule and automatic installation are set under <a href="/config">Settings</a>.
    </p>
  </section>

  <section class="panel">
    <div class="panel-head">
      <h2>Manual upload</h2>
    </div>
    <form onsubmit={upload}>
      <label class="dropzone" class:has-file={!!file}>
        <input
          type="file"
          accept=".bin"
          bind:files
          onchange={() => {
            status = null;
            progress = null;
          }} />
        <Icon name="upload" />
        {#if file}
          <span class="file-name">{file.name}</span>
          <span class="faint">{(file.size / 1048576).toFixed(2)} MB</span>
        {:else}
          <span>Drop a .bin image here, or choose one</span>
          <span class="faint">firmware.bin or littlefs.bin from a release</span>
        {/if}
      </label>

      <div class="target">
        <span class="target-label">Flash as</span>
        <Segmented
          label="Update target"
          value={mode}
          disabled={uploading}
          options={[
            { value: 'fr', label: 'Firmware', icon: 'chip' },
            { value: 'fs', label: 'Web interface', icon: 'folder' },
          ]}
          onchange={(v) => (mode = v)} />
      </div>

      {#if progress !== null}
        <div class="progress" role="progressbar" aria-valuenow={progress} aria-valuemin="0" aria-valuemax="100">
          <span style="width:{progress}%"></span>
        </div>
      {/if}
      {#if status}
        <p class="notice" class:danger={status.type === 'error'}>{status.message}</p>
      {/if}

      <div class="actions">
        <button type="submit" class="btn btn-primary" disabled={!file || uploading}>Upload and flash</button>
      </div>
    </form>
  </section>
</main>

<style>
  .narrow{max-width:720px;}
  .panel{display:grid;gap:16px;}
  .panel-head{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;}
  .kv{grid-template-columns:1fr;}
  .kv a{display:inline-flex;align-items:center;gap:4px;}
  .kv a :global(.icon){width:13px;height:13px;}
  .actions{display:flex;gap:8px;flex-wrap:wrap;}
  .hint{font-size:.8125rem;color:var(--text-3);}
  form{display:grid;gap:16px;}
  .dropzone{
    position:relative;display:grid;justify-items:center;gap:4px;padding:28px 20px;text-align:center;
    border:1px dashed var(--line-strong);border-radius:var(--r-md);background:var(--surface-2);
    color:var(--text-2);cursor:pointer;transition:border-color .12s,background-color .12s;
  }
  .dropzone:hover,.dropzone:focus-within{border-color:var(--text);color:var(--text);}
  .dropzone.has-file{border-style:solid;}
  .dropzone input{position:absolute;inset:0;opacity:0;cursor:pointer;}
  .dropzone :global(.icon){width:22px;height:22px;margin-bottom:6px;}
  .file-name{font-weight:500;color:var(--text);word-break:break-all;}
  .target{display:flex;align-items:center;gap:12px;flex-wrap:wrap;}
  .target-label{font-size:.8125rem;color:var(--text-2);}
</style>
