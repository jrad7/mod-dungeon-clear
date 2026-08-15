/* Roster builder: browse real characters, fill five positional slots
 * (tank, heal, dps ×3 — the order is the worldserver contract), save named
 * rosters, launch. Refusal reasons (online, no instance budget, cross-
 * faction) are shown before the launch button can be pressed. */

import { useCallback, useEffect, useMemo, useState } from "react";
import { useNavigate } from "react-router-dom";
import { api, ApiError } from "../api/client";
import type {
  Account,
  Catalogue,
  Character,
  CommandReply,
  SavedRoster,
} from "../api/types";
import {
  CLASS_NAME,
  CLASS_TEXT_COLOR,
  ROSTER_SLOTS,
} from "../data/wow";
import {
  Card,
  CardTitle,
  ConfirmButton,
  EmptyState,
  selectOnFocus,
  Spinner,
  useToast,
} from "../components/ui";
import { useSession } from "../auth/SessionContext";

type Slots = (Character | null)[];

export default function RosterPage() {
  const toast = useToast();
  const navigate = useNavigate();
  const { session } = useSession();

  const [chars, setChars] = useState<Character[] | null>(null);
  const [accounts, setAccounts] = useState<Account[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [filters, setFilters] = useState({
    search: "",
    cls: 0,
    faction: "",
    accountId: 0,
    minlevel: "1",
    maxlevel: "80",
  });
  const [slots, setSlots] = useState<Slots>([null, null, null, null, null]);
  const [dragFrom, setDragFrom] = useState<number | null>(null);
  const [dragOver, setDragOver] = useState<number | null>(null);
  const [saved, setSaved] = useState<SavedRoster[]>([]);
  const [rosterName, setRosterName] = useState("");
  const [dungeon, setDungeon] = useState("");
  const [heroic, setHeroic] = useState(false);
  const [catalogue, setCatalogue] = useState<Catalogue | null>(null);

  const refreshChars = useCallback(async () => {
    try {
      const q = new URLSearchParams();
      if (filters.search) q.set("search", filters.search);
      if (filters.cls) q.set("cls", String(filters.cls));
      if (filters.faction) q.set("faction", filters.faction);
      if (filters.accountId) q.set("accountId", String(filters.accountId));
      /* Held as text so the boxes can be emptied and retyped; an empty box
         means "no bound on this side", which is the range's own end. */
      q.set("minlevel", String(Number(filters.minlevel) || 1));
      q.set("maxlevel", String(Number(filters.maxlevel) || 80));
      const r = await api.get<{ characters: Character[] }>(
        `/api/characters?${q}`,
      );
      setChars(r.characters);
      setError(null);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }, [filters]);

  /* Debounced, because every one of these is a `mysql` SUBPROCESS on the
     server: typing a six-letter name fired six process spawns and six queries
     against the character database, five of whose answers were thrown away as
     the next keystroke landed. 250ms is below the threshold where the list
     feels like it lags behind the box. */
  useEffect(() => {
    const t = setTimeout(() => void refreshChars(), 250);
    return () => clearTimeout(t);
  }, [refreshChars]);

  useEffect(() => {
    api
      .get<{ accounts: Account[] }>("/api/accounts")
      .then((r) => setAccounts(r.accounts))
      .catch(() => {});
    api
      .get<{ rosters: SavedRoster[] }>("/api/rosters")
      .then((r) => setSaved(r.rosters))
      .catch(() => {});
    api
      .get<Catalogue>("/api/testdungeons")
      .then((c) => {
        setCatalogue(c);
        if (c.dungeons?.length) setDungeon(c.dungeons[0].token);
      })
      .catch(() => {});
  }, []);

  const picked = new Set(slots.filter(Boolean).map((c) => c!.name.toLowerCase()));
  const full = slots.every(Boolean);
  const partyFaction = slots.find(Boolean)?.faction;

  function draft(c: Character) {
    const free = slots.findIndex((s) => s === null);
    if (free === -1) return;
    setSlots(slots.map((s, i) => (i === free ? c : s)));
  }

  /* Drag a filled slot onto another slot to swap the two — the slot POSITION
   * is the role, so this is how you fix "my healer landed in a dps slot". */
  function dropOn(target: number) {
    if (dragFrom === null || dragFrom === target) return;
    const next = [...slots];
    [next[dragFrom], next[target]] = [next[target], next[dragFrom]];
    setSlots(next);
  }

  function refusal(c: Character): string | null {
    if (picked.has(c.name.toLowerCase())) return "drafted";
    /* Server-side rule, surfaced rather than hidden: running someone else's
     * character logs it in, teleports it and re-gears it, so it takes the
     * same GM level as the other actions that affect other people. */
    if (!c.owned) return "another account";
    if (c.online) return "online — must be logged out";
    if (c.instancesLeft <= 0) return "no instance budget this hour";
    if (partyFaction && c.faction !== partyFaction)
      return `party is ${partyFaction}`;
    return null;
  }

  async function launch() {
    const members = slots.map((s) => s!.name);
    try {
      const r = await api.post<CommandReply>("/api/testruns/start-roster", {
        dungeon,
        members,
        heroic,
      });
      if (r.pending) throw new ApiError(503, r.reply.join(" "));
      if (!r.ok && r.reply.length) throw new ApiError(500, r.reply.join(" "));
      toast("ok", "Roster run started");
      navigate("/live");
    } catch (e) {
      toast("error", e instanceof Error ? e.message : String(e));
    }
  }

  async function saveRoster() {
    if (!rosterName.trim() || !full) return;
    try {
      await api.post("/api/rosters", {
        name: rosterName.trim(),
        members: slots.map((s) => s!.name),
      });
      const r = await api.get<{ rosters: SavedRoster[] }>("/api/rosters");
      setSaved(r.rosters);
      toast("ok", `Roster “${rosterName.trim()}” saved`);
    } catch (e) {
      toast("error", e instanceof Error ? e.message : String(e));
    }
  }

  /* Re-resolve names against the live list so a renamed or deleted character
   * shows as missing instead of failing at launch.
   *
   * Against the UNFILTERED list, not the one on screen: resolving against the
   * current filters meant that having typed a name in the search box — or
   * narrowed to one class — made every other member of a perfectly good saved
   * roster report itself "missing", which reads as data loss rather than as a
   * filter. The filters are reset too, so the browser agrees with the party. */
  async function loadRoster(r: SavedRoster) {
    let pool = chars ?? [];
    try {
      const all = await api.get<{ characters: Character[] }>(
        "/api/characters?minlevel=1&maxlevel=80",
      );
      pool = all.characters;
      setChars(all.characters);
      setFilters({
        search: "",
        cls: 0,
        faction: "",
        accountId: 0,
        minlevel: "1",
        maxlevel: "80",
      });
    } catch {
      /* Fall back to what is already on screen rather than refusing to load. */
    }
    const byName = new Map(pool.map((c) => [c.name.toLowerCase(), c]));
    setSlots(
      r.members.map((n) => byName.get(n.toLowerCase()) ?? null) as Slots,
    );
    const missing = r.members.filter((n) => !byName.has(n.toLowerCase()));
    if (missing.length)
      toast(
        "error",
        `No longer on this realm: ${missing.join(", ")} — re-pick those slots`,
      );
  }

  const heroicOk = useMemo(() => {
    const d = catalogue?.dungeons.find((d) => d.token === dungeon);
    return (d?.heroicLevel ?? 0) > 0;
  }, [catalogue, dungeon]);

  const input =
    "rounded-lg border border-ink-700 bg-ink-950 px-2.5 py-1.5 text-sm outline-none focus:border-iris-400/60";

  return (
    <div>
      <h1 className="text-2xl font-semibold">Roster</h1>
      <p className="mt-1 text-sm text-ink-400">
        Send a hand-picked party of real characters instead of pool bots. Slots
        are positional: tank, healer, then three DPS.
      </p>
      {session?.authenticated && !session.admin && (
        <p className="mt-1 text-sm text-ink-500">
          You can draft characters on your own account. Running someone else's
          logs it in, teleports it and re-gears it, so it takes an admin GM
          level.
        </p>
      )}

      <div className="mt-6 grid grid-cols-1 gap-4 xl:grid-cols-[1fr_380px]">
        {/* character browser */}
        <Card>
          <CardTitle
            right={
              <button
                onClick={() => void refreshChars()}
                className="text-xs text-ink-500 hover:text-ink-300"
              >
                refresh
              </button>
            }
          >
            Characters
          </CardTitle>
          <div className="mb-3 flex flex-wrap gap-2">
            <input
              placeholder="Name…"
              className={`${input} w-32`}
              value={filters.search}
              onChange={(e) =>
                setFilters({ ...filters, search: e.target.value.replace(/[^A-Za-z]/g, "") })
              }
            />
            <select
              className={input}
              value={filters.cls}
              onChange={(e) =>
                setFilters({ ...filters, cls: Number(e.target.value) })
              }
            >
              <option value={0}>any class</option>
              {Object.entries(CLASS_NAME).map(([id, n]) => (
                <option key={id} value={id}>
                  {n}
                </option>
              ))}
            </select>
            <select
              className={input}
              value={filters.faction}
              onChange={(e) =>
                setFilters({ ...filters, faction: e.target.value })
              }
            >
              <option value="">any faction</option>
              <option value="alliance">Alliance</option>
              <option value="horde">Horde</option>
            </select>
            <select
              className={input}
              value={filters.accountId}
              onChange={(e) =>
                setFilters({ ...filters, accountId: Number(e.target.value) })
              }
            >
              <option value={0}>any account</option>
              {accounts.map((a) => (
                <option key={a.id} value={a.id}>
                  {a.username} ({a.chars})
                </option>
              ))}
            </select>
            <div className="flex items-center gap-1 text-sm text-ink-500">
              lv
              <input
                type="text"
                inputMode="numeric"
                value={filters.minlevel}
                onChange={(e) =>
                  setFilters({
                    ...filters,
                    minlevel: e.target.value.replace(/[^\d]/g, ""),
                  })
                }
                {...selectOnFocus}
                className={`${input} w-16`}
              />
              –
              <input
                type="text"
                inputMode="numeric"
                value={filters.maxlevel}
                onChange={(e) =>
                  setFilters({
                    ...filters,
                    maxlevel: e.target.value.replace(/[^\d]/g, ""),
                  })
                }
                {...selectOnFocus}
                className={`${input} w-16`}
              />
            </div>
          </div>

          {error ? (
            <EmptyState icon="⚠️" title="Character list unavailable">
              {error}
            </EmptyState>
          ) : chars === null ? (
            <Spinner label="loading characters…" />
          ) : chars.length === 0 ? (
            <EmptyState icon="🔍" title="No characters match" />
          ) : (
            <div className="max-h-[28rem] overflow-y-auto rounded-lg border border-ink-800">
              <table className="w-full text-sm">
                <thead className="sticky top-0 bg-ink-900 text-left text-xs uppercase tracking-wide text-ink-500">
                  <tr>
                    <th className="px-3 py-2">Name</th>
                    <th className="px-2 py-2">Lv</th>
                    <th className="hidden px-2 py-2 sm:table-cell">Spec</th>
                    <th className="hidden px-2 py-2 md:table-cell">Account</th>
                    <th className="px-2 py-2"></th>
                  </tr>
                </thead>
                <tbody>
                  {chars.map((c) => {
                    const why = refusal(c);
                    return (
                      <tr
                        key={c.guid}
                        className="border-t border-ink-800/60 hover:bg-ink-800/30"
                      >
                        <td className="px-3 py-1.5">
                          <span
                            className="font-medium"
                            style={{ color: CLASS_TEXT_COLOR[c.cls] }}
                          >
                            {c.name}
                          </span>
                          <span className="ml-2 text-xs text-ink-600">
                            {c.faction === "horde" ? "H" : "A"}
                          </span>
                        </td>
                        <td className="px-2 py-1.5 text-ink-400">{c.level}</td>
                        <td className="hidden px-2 py-1.5 text-ink-400 sm:table-cell">
                          {c.spec || "—"}
                          {c.specPoints?.length === 3 && c.spec && (
                            <span className="ml-1 text-xs text-ink-600">
                              {c.specPoints.join("/")}
                            </span>
                          )}
                        </td>
                        <td className="hidden px-2 py-1.5 text-ink-500 md:table-cell">
                          {c.account}
                        </td>
                        <td className="px-2 py-1.5 text-right">
                          {why ? (
                            <span className="text-xs text-ink-600">{why}</span>
                          ) : (
                            <button
                              onClick={() => draft(c)}
                              disabled={full}
                              className="rounded-lg border border-ink-700 px-2.5 py-1 text-xs text-iris-300 hover:border-iris-600 disabled:opacity-40"
                            >
                              draft
                            </button>
                          )}
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>
          )}
        </Card>

        {/* party + launch */}
        <div className="space-y-4">
          <Card>
            <CardTitle>Party</CardTitle>
            <div className="space-y-2">
              {ROSTER_SLOTS.map((role, i) => (
                <div
                  key={i}
                  draggable={slots[i] !== null}
                  onDragStart={() => setDragFrom(i)}
                  onDragEnd={() => {
                    setDragFrom(null);
                    setDragOver(null);
                  }}
                  onDragOver={(e) => {
                    e.preventDefault();
                    setDragOver(i);
                  }}
                  onDragLeave={() => setDragOver((o) => (o === i ? null : o))}
                  onDrop={(e) => {
                    e.preventDefault();
                    dropOn(i);
                    setDragFrom(null);
                    setDragOver(null);
                  }}
                  className={`flex items-center justify-between rounded-xl border px-3 py-2 transition ${
                    dragOver === i && dragFrom !== null && dragFrom !== i
                      ? "border-iris-500/70 bg-iris-500/5"
                      : "border-ink-800 bg-ink-950/60"
                  } ${slots[i] ? "cursor-grab active:cursor-grabbing" : ""} ${
                    dragFrom === i ? "opacity-50" : ""
                  }`}
                >
                  <span className="w-12 text-xs font-semibold uppercase tracking-wide text-ink-500">
                    {role}
                  </span>
                  {slots[i] ? (
                    <>
                      <span className="px-1 text-ink-700" aria-hidden>
                        ⠿
                      </span>
                      <span
                        className="flex-1 px-1 font-medium"
                        style={{ color: CLASS_TEXT_COLOR[slots[i]!.cls] }}
                      >
                        {slots[i]!.name}
                        <span className="ml-2 text-xs text-ink-500">
                          lv {slots[i]!.level}
                          {slots[i]!.spec ? ` · ${slots[i]!.spec}` : ""}
                        </span>
                      </span>
                      <button
                        onClick={() =>
                          setSlots(slots.map((s, j) => (j === i ? null : s)))
                        }
                        className="text-ink-600 hover:text-red-300"
                      >
                        ✕
                      </button>
                    </>
                  ) : (
                    <span className="flex-1 px-2 text-sm text-ink-600">
                      empty — draft from the left
                    </span>
                  )}
                </div>
              ))}
              <p className="text-xs text-ink-600">
                Drag a filled slot onto another to swap roles.
              </p>
            </div>

            <div className="mt-4 space-y-3">
              <label className="block text-sm text-ink-400">
                Dungeon
                <select
                  className={`${input} mt-1 w-full`}
                  value={dungeon}
                  onChange={(e) => setDungeon(e.target.value)}
                >
                  {(catalogue?.dungeons ?? []).map((d) => (
                    <option key={d.token + d.wing} value={d.token}>
                      {d.name} (lv {d.level})
                    </option>
                  ))}
                </select>
              </label>
              {heroicOk && (
                <label className="flex items-center gap-2 text-sm">
                  <input
                    type="checkbox"
                    checked={heroic}
                    onChange={(e) => setHeroic(e.target.checked)}
                    className="accent-iris-500"
                  />
                  Heroic
                </label>
              )}
              <button
                disabled={!full || !dungeon}
                onClick={() => void launch()}
                className="w-full rounded-xl bg-iris-600 px-4 py-2.5 font-medium text-white hover:bg-iris-500 disabled:opacity-40"
              >
                Launch roster run
              </button>
            </div>
          </Card>

          <Card>
            <CardTitle>Saved rosters</CardTitle>
            <div className="flex gap-2">
              <input
                placeholder="Roster name…"
                className={`${input} flex-1`}
                value={rosterName}
                onChange={(e) => setRosterName(e.target.value)}
              />
              <button
                disabled={!full || !rosterName.trim()}
                onClick={() => void saveRoster()}
                className="rounded-lg border border-ink-700 px-3 py-1.5 text-sm text-ink-200 hover:border-iris-600 disabled:opacity-40"
              >
                Save
              </button>
            </div>
            {saved.length === 0 ? (
              <p className="mt-3 text-sm text-ink-600">
                Nothing saved yet — fill the party and give it a name.
              </p>
            ) : (
              <ul className="mt-3 space-y-1.5">
                {saved.map((r) => (
                  <li
                    key={r.name}
                    className="flex items-center justify-between rounded-lg border border-ink-800/70 px-3 py-1.5 text-sm"
                  >
                    <button
                      onClick={() => void loadRoster(r)}
                      className="flex-1 text-left hover:text-iris-200"
                      title={r.members.join(", ")}
                    >
                      {r.name}
                      {r.owner && !r.mine && (
                        <span className="ml-2 text-xs text-ink-600">
                          by {r.owner}
                        </span>
                      )}
                      <span className="ml-2 text-xs text-ink-600">
                        {r.members.join(", ")}
                      </span>
                    </button>
                    {/* Your own rosters, and anyone's if you are an admin —
                        the same rule the DELETE route enforces. */}
                    {r.writable && (
                      <ConfirmButton
                        label="✕"
                        message={`Delete roster “${r.name}”?`}
                        className="ml-2 text-ink-600 hover:text-red-300"
                        onConfirm={() => {
                          api
                            .del(`/api/rosters/${encodeURIComponent(r.name)}`)
                            .then(() =>
                              setSaved(saved.filter((x) => x.name !== r.name)),
                            )
                            .catch((e) => toast("error", e.message));
                        }}
                      />
                    )}
                  </li>
                ))}
              </ul>
            )}
          </Card>
        </div>
      </div>
    </div>
  );
}
