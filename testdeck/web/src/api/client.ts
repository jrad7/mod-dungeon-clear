/* The one fetch wrapper. Adds the CSRF header on mutations, decodes JSON
 * errors, and turns a 401 into a global "logged out" signal the session
 * context listens for — an expired session shows the login screen instead of
 * every page silently blanking. */

export class ApiError extends Error {
  status: number;
  constructor(status: number, detail: string) {
    super(detail);
    this.status = status;
  }
}

type Listener = () => void;
const logoutListeners = new Set<Listener>();

export function onUnauthorized(fn: Listener): () => void {
  logoutListeners.add(fn);
  return () => logoutListeners.delete(fn);
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const method = init?.method ?? "GET";
  const headers: Record<string, string> = {
    ...(init?.headers as Record<string, string>),
  };
  if (method !== "GET") headers["X-TestDeck"] = "1";
  if (init?.body) headers["Content-Type"] = "application/json";

  const resp = await fetch(path, { ...init, headers });
  if (resp.status === 401 && path !== "/api/session" && path !== "/api/login") {
    logoutListeners.forEach((fn) => fn());
  }
  let body: unknown = null;
  try {
    body = await resp.json();
  } catch {
    /* non-JSON error body */
  }
  if (!resp.ok) {
    const detail =
      (body as { detail?: string } | null)?.detail ?? `HTTP ${resp.status}`;
    throw new ApiError(resp.status, detail);
  }
  return body as T;
}

export const api = {
  get: <T>(path: string) => request<T>(path),
  post: <T>(path: string, payload?: unknown) =>
    request<T>(path, {
      method: "POST",
      body: payload === undefined ? undefined : JSON.stringify(payload),
    }),
  del: <T>(path: string) => request<T>(path, { method: "DELETE" }),
};
