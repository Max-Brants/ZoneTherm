// The firmware answers errors as {"error": "..."} (ApiRoutes sendError).
// Surface that instead of a bare status - a rejected config save should say
// which rule it broke.
async function errorDetail(res: Response): Promise<string> {
  let detail = `HTTP ${res.status}`;
  try {
    const body = await res.json();
    if (body?.error) detail = body.error as string;
  } catch {
    /* not a JSON error envelope - keep the status */
  }
  return detail;
}

export async function fetchJSON<T>(url: string, options: RequestInit = {}): Promise<T> {
  const res = await fetch(url, { cache: 'no-store', ...options });
  if (!res.ok) throw new Error(await errorDetail(res));
  return res.json() as Promise<T>;
}

// Fire-and-forget command endpoints (/api/thermostat/N/settemp and friends).
// Throws on a non-2xx answer with the firmware's error text.
export async function post(url: string): Promise<void> {
  const res = await fetch(url, { method: 'POST', cache: 'no-store' });
  if (!res.ok) throw new Error(await errorDetail(res));
}
