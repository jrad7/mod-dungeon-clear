import { NavLink, Outlet } from "react-router-dom";
import { api } from "../api/client";
import { usePoll } from "../api/hooks";
import type { Status } from "../api/types";
import { useSession } from "../auth/SessionContext";
import { createContext, useContext } from "react";

const StatusCtx = createContext<Status | null>(null);
export function useStatus() {
  return useContext(StatusCtx);
}

const NAV = [
  { to: "/", label: "Launch", icon: "🚀" },
  { to: "/live", label: "Live", icon: "📡" },
  { to: "/roster", label: "Roster", icon: "🛡️" },
  { to: "/history", label: "History", icon: "📜" },
  { to: "/logs", label: "Logs", icon: "🧾" },
];

function RealmOrb({ status }: { status: Status | null }) {
  const realm = status?.realm ?? "UNKNOWN";
  const color =
    realm === "ONLINE"
      ? "bg-emerald-400 shadow-emerald-400/50"
      : realm === "FAILED"
        ? "bg-red-500 shadow-red-500/50"
        : realm === "OFFLINE"
          ? "bg-ink-600"
          : "bg-amber-500/70";
  return (
    <span className="flex items-center gap-2 text-sm text-ink-400">
      <span
        className={`inline-block h-2.5 w-2.5 rounded-full shadow-[0_0_8px] ${color}`}
        title={`realm ${realm.toLowerCase()} (via ${status?.statusCheck ?? "…"})`}
      />
      <span className="hidden sm:inline">
        {realm === "ONLINE"
          ? "realm online"
          : realm === "UNKNOWN"
            ? "realm unknown"
            : `realm ${realm.toLowerCase()}`}
      </span>
    </span>
  );
}

function NavItems({ live }: { live: number }) {
  return (
    <>
      {NAV.map((n) => (
        <NavLink
          key={n.to}
          to={n.to}
          end={n.to === "/"}
          className={({ isActive }) =>
            `flex items-center gap-2 rounded-lg px-3 py-2 text-sm transition ` +
            (isActive
              ? "bg-iris-500/15 text-iris-200 ring-1 ring-inset ring-iris-500/30"
              : "text-ink-400 hover:bg-ink-800/60 hover:text-ink-200")
          }
        >
          <span aria-hidden>{n.icon}</span>
          <span>{n.label}</span>
          {n.to === "/live" && live > 0 && (
            <span className="ml-auto rounded-full bg-emerald-500/20 px-2 text-xs text-emerald-300">
              {live}
            </span>
          )}
        </NavLink>
      ))}
    </>
  );
}

export default function AppShell() {
  const { session, logout } = useSession();
  const { data: status } = usePoll(
    () => api.get<Status>("/api/status"),
    5000,
  );
  const live = (status?.liveRuns ?? 0) + (status?.livePlans ?? 0);
  const problems = status?.health?.problems ?? [];

  return (
    <StatusCtx.Provider value={status}>
      <div className="min-h-dvh text-ink-100 lg:flex">
        {/* Sidebar (>= lg) */}
        <aside className="hidden lg:flex lg:w-56 lg:flex-col lg:border-r lg:border-ink-800/70 lg:bg-ink-900/40 lg:p-4">
          <div className="mb-6 flex items-center gap-2 px-2">
            <span className="text-2xl">⚔️</span>
            <div>
              <div className="font-semibold leading-tight">DC Test Deck</div>
              <div className="text-xs text-ink-500">dungeon testing</div>
            </div>
          </div>
          <nav className="flex flex-col gap-1">
            <NavItems live={live} />
          </nav>
          <div className="mt-auto space-y-3 px-2 pt-6 text-sm">
            <RealmOrb status={status} />
            <div className="flex items-center justify-between text-ink-500">
              <span title={`gmlevel ${session?.gmlevel ?? "?"}`}>
                {session?.username}
              </span>
              <button
                onClick={() => void logout()}
                className="text-ink-500 underline-offset-2 hover:text-ink-300 hover:underline"
              >
                log out
              </button>
            </div>
            {/* The version is on screen because the first question on every
                bug report is which build produced it, and a tester cannot be
                asked to read the server's console to find out. */}
            {status?.version && (
              <div className="text-xs text-ink-700">v{status.version}</div>
            )}
          </div>
        </aside>

        {/* Main column */}
        <div className="flex min-h-dvh flex-1 flex-col">
          {/* Top bar (mobile) */}
          <header className="flex items-center justify-between border-b border-ink-800/70 bg-ink-900/40 px-4 py-3 lg:hidden">
            <div className="flex items-center gap-2 font-semibold">
              <span>⚔️</span> DC Test Deck
            </div>
            <div className="flex items-center gap-3">
              <RealmOrb status={status} />
              <button
                onClick={() => void logout()}
                className="text-sm text-ink-400"
              >
                log out
              </button>
            </div>
          </header>

          {problems.length > 0 && (
            <div className="border-b border-amber-900/40 bg-amber-950/30 px-4 py-2 text-sm text-amber-200/90">
              {problems.map((p, i) => (
                <div key={i}>
                  <span className="mr-2 rounded bg-amber-900/50 px-1.5 py-0.5 text-xs uppercase">
                    {p.level}
                  </span>
                  {p.message}
                </div>
              ))}
            </div>
          )}

          <main className="flex-1 px-4 py-6 pb-24 lg:px-8 lg:pb-8">
            <Outlet />
          </main>

          {/* Bottom tab bar (mobile) */}
          <nav className="fixed inset-x-0 bottom-0 z-20 flex justify-around border-t border-ink-800 bg-ink-950/85 px-2 py-1 backdrop-blur lg:hidden">
            {NAV.map((n) => (
              <NavLink
                key={n.to}
                to={n.to}
                end={n.to === "/"}
                className={({ isActive }) =>
                  `flex flex-col items-center rounded-lg px-3 py-1.5 text-xs ` +
                  (isActive ? "text-iris-300" : "text-ink-500")
                }
              >
                <span className="text-base" aria-hidden>
                  {n.icon}
                </span>
                {n.label}
              </NavLink>
            ))}
          </nav>
        </div>
      </div>
    </StatusCtx.Provider>
  );
}
