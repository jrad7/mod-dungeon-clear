import { useState, type FormEvent } from "react";
import { api, ApiError } from "../api/client";
import { useSession } from "./SessionContext";

/* A 401 is "you typed it wrong"; a 503 or a 403 is "the operator has something
 * to fix", and the server's message for those already names the fix — a
 * missing MySQL client, an unconfigured auth database, or the exact
 * `account set gmlevel` line to run. Painting the two the same red taught
 * first-time users to re-type their password at a problem no password
 * could solve, so the operator ones get their own treatment and a pointer at
 * the command that diagnoses the host. */
const OPERATOR_STATUS = new Set([403, 500, 502, 503, 504]);

export default function LoginPage() {
  const { refresh } = useSession();
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState<{ text: string; operator: boolean } | null>(
    null,
  );
  const [busy, setBusy] = useState(false);

  async function submit(e: FormEvent) {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      await api.post("/api/login", { username, password });
      await refresh();
    } catch (err) {
      setError(
        err instanceof ApiError
          ? { text: err.message, operator: OPERATOR_STATUS.has(err.status) }
          : {
              text: "Could not reach the Test Deck server — it may have stopped.",
              operator: true,
            },
      );
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="min-h-dvh flex items-center justify-center px-4">
      <form
        onSubmit={submit}
        className="w-full max-w-sm rounded-2xl border border-ink-800 bg-ink-900/70 p-8 shadow-xl backdrop-blur"
      >
        <div className="mb-6 text-center">
          <div className="text-3xl">⚔️</div>
          <h1 className="mt-2 text-xl font-semibold text-ink-100">
            DC Test Deck
          </h1>
          <p className="mt-1 text-sm text-ink-400">
            Sign in with your game GM account
          </p>
        </div>

        <label className="block text-sm text-ink-400">
          Account name
          <input
            className="mt-1 w-full rounded-lg border border-ink-700 bg-ink-950 px-3 py-2 text-ink-100 outline-none focus:border-iris-400/60"
            value={username}
            onChange={(e) => setUsername(e.target.value)}
            autoComplete="username"
            autoFocus
            required
          />
        </label>

        <label className="mt-4 block text-sm text-ink-400">
          Password
          <input
            type="password"
            className="mt-1 w-full rounded-lg border border-ink-700 bg-ink-950 px-3 py-2 text-ink-100 outline-none focus:border-iris-400/60"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            autoComplete="current-password"
            required
          />
        </label>

        {error && (
          <div
            role="alert"
            className={`mt-4 rounded-lg border px-3 py-2 text-sm ${
              error.operator
                ? "border-amber-900/60 bg-amber-950/40 text-amber-200"
                : "border-red-900/60 bg-red-950/40 text-red-300"
            }`}
          >
            {error.text}
            {error.operator && (
              <p className="mt-2 text-xs text-amber-200/70">
                This is a server-side problem, not your password. Whoever runs
                the deck can list every fault on the host with{" "}
                <code className="rounded bg-black/30 px-1">
                  python3 -m testdeck check
                </code>
                .
              </p>
            )}
          </div>
        )}

        <button
          disabled={busy}
          className="mt-6 w-full rounded-lg bg-iris-600 px-4 py-2 font-medium text-white transition hover:bg-iris-500 disabled:opacity-50"
        >
          {busy ? "Signing in…" : "Sign in"}
        </button>

        <p className="mt-4 text-center text-xs text-ink-500">
          Access requires a GM account on this realm.
        </p>
      </form>
    </div>
  );
}
