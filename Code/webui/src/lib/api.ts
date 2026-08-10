export async function fetchJSON<T>(url: string, options: RequestInit = {}): Promise<T> {
  const res = await fetch(url, { cache: 'no-store', ...options });
  if (!res.ok) {
    // The firmware answers errors as {"error": "..."} (ApiRoutes sendError).
    // Surface that instead of a bare status - a rejected config save should
    // say which rule it broke.
    let detail = `HTTP ${res.status}`;
    try {
      const body = await res.json();
      if (body?.error) detail = body.error as string;
    } catch {
      /* not a JSON error envelope - keep the status */
    }
    throw new Error(detail);
  }
  return res.json() as Promise<T>;
}

export const post = (url: string): Promise<Response> => fetch(url, { method: 'POST' });
