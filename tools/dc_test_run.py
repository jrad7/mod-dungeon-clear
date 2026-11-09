#!/usr/bin/env python3
"""
dc_test_run.py — everything known about one test run, by its id.

`.dc test` scatters a single run's evidence across five places:

  dc_testruns.jsonl        the finished-run record (schema v7-v14): comp, result,
                           fail reason, boss timeline, deaths, loot, pulls, status
                           timeline, pauses, watchdog config, and the teardown
                           DcDiag snapshot
  dc_testplans.jsonl       the campaign the run belonged to, and its siblings
  dc_testrun_live.json     live state — the ONLY source for a run still in flight
  DungeonClear*.log        the bot-level narrative, tagged `[DC:<BotName>]`, with
  Server.log / Errors.log  no run id on the line
  dungeonclear*.jsonl      opt-in decision captures, keyed by bot GUID

Answering "what happened in tr-20260801-174432-3?" by hand means grepping four
files and hand-correlating a time window against a bot roster. This does that:
it resolves the record, pulls the plan context, then slices every log in the
data dir down to the lines that belong to *this* run — the run's bot names
inside the run's time window — and reports the diagnostic signals it finds.

  python3 tools/dc_test_run.py tr-20260801-174432-3     # the post-mortem
  python3 tools/dc_test_run.py 20260801-174432-3        # `tr-` is optional
  python3 tools/dc_test_run.py tr-20260801-174432       # whole batch, tabulated
  python3 tools/dc_test_run.py tp-20260801-174427-1     # a plan and its runs
  python3 tools/dc_test_run.py last                     # most recent record
  python3 tools/dc_test_run.py --list 20                # recent run ids

Drill down once the summary points somewhere:

  --logs [NAME ...]   dump the sliced log lines (default: every log; NAME picks
                      files by substring, e.g. `--logs pull`)
  --grep RE           only lines matching RE (implies --logs)
  --level L           only lines at or above L (trace|debug|info|warn|error)
  --head N/--tail N   bound the dump
  --dump DIR          write each per-run slice to DIR/<runid>.<logfile>
  --json              the whole bundle as JSON (record + plan + live + slices)

The data dir (worldserver's cwd, `env/dist/bin`) is found by walking up from
this script; override with --data-dir or $DC_DATA_DIR.

Read-only: this opens files for reading and never writes anywhere except an
explicit --dump directory.
"""

import argparse
import glob
import json
import os
import re
import sys
import time
from collections import Counter, OrderedDict, deque
from datetime import datetime
from pathlib import Path

RUNS_FILE = "dc_testruns.jsonl"
PLANS_FILE = "dc_testplans.jsonl"
LIVE_FILE = "dc_testrun_live.json"

# Continuous-mode ("soak") sessions live in the Test Deck's data dir, not the
# worldserver's: <deck data_dir>/soaks/<sk-id>/{state.json,ledger.jsonl,evidence/}.
SOAKS_SUBDIR = "soaks"

# Opt-in decision captures. Keyed by bot GUID, not by run, so they are filtered
# on the run's comp GUIDs rather than on names.
DECISION_FILES = ["dungeonclear_decisions.jsonl", "dungeonclear_pull_decisions.jsonl"]

# Log lines are wall-clock local time; the record's *AtMs are true epoch ms.
# Pad the window: provisioning chatter precedes the recorded start and teardown
# (revive, recall, logout) trails the recorded end.
PAD_BEFORE_S = 10
PAD_AFTER_S = 30

# Log files are time-ordered, so a slice binary-searches its way to the window
# instead of scanning a multi-GB PlayerbotsEngine.log from the top, and stops
# once lines are this far past the window (threads interleave a few seconds out
# of order, never minutes). Files under SEEK_MIN_BYTES are just scanned.
SEEK_MIN_BYTES = 4 << 20
SEEK_SLACK_S = 5
STOP_SLACK_S = 60

LEVELS = ["TRACE", "DEBUG", "INFO", "WARN", "WARNING", "ERROR", "FATAL"]
LEVEL_RANK = {"TRACE": 0, "DEBUG": 1, "INFO": 2, "WARN": 3, "WARNING": 3, "ERROR": 4, "FATAL": 5}

# Tags a long scripted event stamps on its own diagnostic lines (the Karazhan
# chess conductor's state / order / assignment lines are `DC_CHESS ...`). Lines
# carrying one are pulled into the run's slice even when they name no bot, and
# the triage view prints them FIRST — for a scenario run they are the story,
# and the generic INFO stream would bury them.
EVENT_TAGS = ["DC_CHESS"]

# Substrings worth counting in a run's slice. These are the lines that have
# actually explained a stuck run before; extend freely, order is display order.
SIGNALS = [
    ("watchdog", ["watchdog", "no progress", "no-progress"]),
    ("stall", ["stalled", "stall:", "livelock", "gave up", "giving up"]),
    ("stuck / resnap", ["posStuck", "resnap", "stuck for", "rebuild failed"]),
    ("stranded recovery", ["stranded-recovery", "stranded recovery"]),
    ("unreachable", ["unreachable", "no path", "path ends short", "cannot reach", "can't reach"]),
    ("pull", ["pull released", "pull fizzled", "fizzle", "camp re-anchored", "camp anchor"]),
    # An aggro the scan never sized, answered with ADVANCED on a sweep map so the
    # maneuver drags it home instead of the walk-in fighting it where it bit.
    ("unplanned pull", ["unclassified aggro"]),
    # How each fight STARTED. The pull rows only cover fights DC pulled; an
    # objective that shows up here is one that joined a fight nobody pulled it into.
    ("first contact", ["first contact:"]),
    ("OBJECTIVE joined a fight", ["OBJECTIVE JOINED AN ONGOING FIGHT"]),
    ("late joiner", ["late joiner:"]),
    # Both halves of the MgT interrupt pass. The hits alone cannot say whether a low count
    # means the bots missed or that nothing kickable was cast, so the misses are grepped too.
    ("interrupt landed", ["interrupt: '"]),
    ("interrupt MISSED", ["interrupt MISS:"]),
    ("door", ["door blocked", "door-blocked", "door stalled", "forcing door", "force-open"]),
    ("combat", ["phantom combat", "stuck in combat", "flip-early", "regroup"]),
    ("death / rez", ["post-combat rez", "died", "wipe", "resurrect"]),
    ("event", ["event-step", "event fired", "event force", "TeleportParty"]),
    ("rest", ["smart rest", "drinking", "eating", "low mana"]),
    # Trial of the Champion's driver. The progress line is the run's timeline
    # (0 -> 9, with rewinds on a full wipe); a long dwell at 4 is the trample failing.
    ("ToC progress", ["DcToc progress="]),
    ("ToC click / pull", ["ToC — progress", "idle Argent soldier", "pulling it back"]),
    ("ToC WARN", ["black knight evaded", "has been on foot for"]),
    # The Oculus's flight driver. The DcOc line is the ascent's timeline (site,
    # cc=k/10, mounted, landed, phase); the rider rows are each member's essence,
    # mount, leg and dismount. A "dropped ground target" WARN is the rider-kill
    # trap being headed off; a rider death with killer == self is it firing.
    ("Oculus ascent", ["DcOc ring="]),
    ("Oculus rider", ["DcOc essence", "DcOc mount", "DcOc leg ", "DcOc dismount", "DcOc land"]),
    ("Oculus WARN", ["DcOc WARN"]),
]


# --------------------------------------------------------------------------
# locating the data


def find_data_dir(explicit):
    """Resolve worldserver's cwd — the dir holding the jsonl captures."""
    if explicit:
        p = Path(explicit).expanduser()
        if not (p / RUNS_FILE).exists():
            die(f"{p} has no {RUNS_FILE}")
        return p
    if os.environ.get("DC_DATA_DIR"):
        return find_data_dir(os.environ["DC_DATA_DIR"])
    starts = [Path(__file__).resolve().parent, Path.cwd().resolve()]
    for start in starts:
        for cand in [start, *start.parents]:
            hit = cand / "env" / "dist" / "bin"
            if (hit / RUNS_FILE).exists():
                return hit
            if (cand / RUNS_FILE).exists():
                return cand
    die(f"could not find {RUNS_FILE}; pass --data-dir (worldserver's cwd)")


def die(msg):
    print(f"dc_test_run: {msg}", file=sys.stderr)
    sys.exit(2)


def iter_jsonl(path):
    """Yield objects from a JSONL file, tolerating a half-written final line.

    A live worldserver appends to these, so the last line can be truncated
    mid-write; that is expected. A decode error anywhere else is corruption.
    """
    if not path.exists():
        return
    with path.open("r", encoding="utf-8", errors="replace") as fh:
        lines = fh.readlines()
    last = len(lines) - 1
    for i, line in enumerate(lines):
        line = line.strip()
        if not line:
            continue
        try:
            yield json.loads(line)
        except json.JSONDecodeError as exc:
            if i != last:
                print(f"  WARNING: {path.name} line {i+1} is not valid JSON: {exc}",
                      file=sys.stderr)


def load_live(data_dir):
    path = data_dir / LIVE_FILE
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8", errors="replace"))
    except (json.JSONDecodeError, OSError):
        return {}


# --------------------------------------------------------------------------
# ids and times


RUN_ID_RE = re.compile(r"^tr-(\d{8})-(\d{6})-(\d+)$")
PLAN_ID_RE = re.compile(r"^tp-(\d{8})-(\d{6})-(\d+)$")


def normalise_id(raw):
    """Accept `tr-...`, `tp-...`, a bare `20260801-174432-3`, or a prefix."""
    s = raw.strip()
    if s.lower() in ("last", "latest"):
        return s.lower()
    if s.startswith(("tr-", "tp-")):
        return s
    if re.match(r"^\d{8}-\d{6}", s):
        return "tr-" + s
    return s


def id_started_at(run_id):
    """Epoch seconds encoded in the id itself — the fallback when a run has no
    record yet (still in flight, or the server died before teardown)."""
    m = RUN_ID_RE.match(run_id) or PLAN_ID_RE.match(run_id)
    if not m:
        return None
    try:
        return time.mktime(datetime.strptime(m.group(1) + m.group(2), "%Y%m%d%H%M%S").timetuple())
    except ValueError:
        return None


def ts(epoch_s):
    if not epoch_s:
        return "-"
    return datetime.fromtimestamp(epoch_s).strftime("%Y-%m-%d %H:%M:%S")


def clock(epoch_s):
    if not epoch_s:
        return "--:--:--"
    return datetime.fromtimestamp(epoch_s).strftime("%H:%M:%S")


def dur(seconds):
    if seconds is None:
        return "-"
    seconds = int(seconds)
    if seconds < 60:
        return f"{seconds}s"
    return f"{seconds//60}m{seconds%60:02d}s ({seconds}s)"


def mmss(t):
    t = int(t or 0)
    return f"{t//60:02d}:{t%60:02d}"


def g(obj, *path, default=None):
    """Defaulted nested read — the record schema drifts additively, so every
    field is optional by construction."""
    cur = obj
    for key in path:
        if not isinstance(cur, dict) or key not in cur:
            return default
        cur = cur[key]
    return cur if cur is not None else default


# --------------------------------------------------------------------------
# record lookup


def load_runs(data_dir):
    return list(iter_jsonl(data_dir / RUNS_FILE))


def find_run(runs, run_id):
    for rec in reversed(runs):
        if rec.get("runId") == run_id:
            return rec
    return None


def find_plan(data_dir, plan_id):
    if not plan_id:
        return None
    for rec in reversed(list(iter_jsonl(data_dir / PLANS_FILE))):
        if rec.get("planId") == plan_id:
            return rec
    return None


def find_live_run(live, run_id):
    for r in live.get("runs", []) or []:
        if r.get("runId") == run_id:
            return r
    return None


def find_live_plan(live, plan_id):
    for p in live.get("plans", []) or []:
        if p.get("planId") == plan_id:
            return p
    return None


# --------------------------------------------------------------------------
# log slicing


LINE_RE = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s+(\w+)?\s*(.*)$", re.S)


def parse_stamp(text, cache):
    hit = cache.get(text)
    if hit is None:
        hit = time.mktime(datetime.strptime(text, "%Y-%m-%d %H:%M:%S").timetuple())
        cache[text] = hit
    return hit


def _stamp_of(raw, cache):
    """Epoch seconds of a raw (bytes) log line, or None for a continuation."""
    m = LINE_RE.match(raw.lstrip(b"\0").decode("utf-8", errors="replace"))
    if not m:
        return None
    try:
        return parse_stamp(m.group(1), cache)
    except ValueError:
        return None


# A probe reads at most this much looking for a stamped line; a region with no
# line breaks in it (a NUL run, see data_start) must not turn one bisection step
# into a multi-GB read.
PROBE_MAX_BYTES = 1 << 20


def _first_stamp_from(fh, offset, cache):
    """Stamp of the first timestamped line that STARTS at or after `offset`,
    looking no further than PROBE_MAX_BYTES; None when there is none in reach."""
    fh.seek(offset)
    budget = PROBE_MAX_BYTES
    if offset:
        partial = fh.readline(budget)  # finish the line `offset` landed inside
        budget -= len(partial)
        if not partial.endswith(b"\n"):
            return None
    while budget > 0:
        raw = fh.readline(budget)
        if not raw:
            return None
        budget -= len(raw)
        st = _stamp_of(raw, cache)
        if st is not None:
            return st
    return None


def data_start(fh):
    """First byte worth reading. A log truncated while worldserver still held
    it open (its appenders don't use O_APPEND) keeps being written at the old
    offset, so the file becomes a sparse hole of NULs — gigabytes of them, with
    no line breaks — followed by the live tail. SEEK_DATA skips the hole where
    the OS supports it; elsewhere this is 0 and the byte-bounded probes cope."""
    try:
        return os.lseek(fh.fileno(), 0, os.SEEK_DATA)
    except (AttributeError, OSError):
        return 0


def seek_offset(fh, size, target_s, cache, lo=0):
    """Byte offset from which reading forward reaches every line stamped at or
    after target_s: the lower bound of a binary search over line stamps. A probe
    that finds no stamp (a line longer than a probe) counts as "at or after",
    which only ever starts the read earlier than needed."""
    hi = size
    while hi - lo > (64 << 10):
        mid = (lo + hi) // 2
        st = _first_stamp_from(fh, mid, cache)
        if st is None or st >= target_s:
            hi = mid
        else:
            lo = mid
    return lo


def slice_log(path, start_s, end_s, matcher, cap=0, seek=True):
    """Return (lines, meta) for the lines of `path` inside the window that name
    one of this run's bots.

    `cap` > 0 keeps only the newest `cap` lines — a run's failure lives at its
    end, so a capped slice drops the oldest lines, never the stall. 0 = all.

    Correlation is name-plus-window because no log line carries a run id: bot
    characters are provisioned per run and never shared by two runs at once, so
    within the window a name identifies exactly one run. Continuation lines (no
    leading timestamp) inherit the previous line's verdict.

    With `seek`, a large file is entered at the window's start (binary search
    on the line stamps) and left once lines are well past its end, so the cost
    is the window, not the file. meta["first"] is still the file's first stamp
    — it is what says when the server session (and so the file) began.
    """
    out = deque(maxlen=cap) if cap > 0 else []
    meta = {"path": str(path), "name": path.name, "scanned": 0, "matched": 0,
            "first": None, "last": None, "truncated": False, "levels": Counter(),
            "seeked": False}
    cache = {}
    keep_prev = False
    prev_stamp = None
    try:
        fh = path.open("rb")
        size = path.stat().st_size
    except OSError:
        return list(out), meta
    with fh:
        start = data_start(fh)
        if seek and size >= SEEK_MIN_BYTES:
            meta["first"] = _first_stamp_from(fh, start, cache)
            if meta["first"] is None:
                # A raw console stream (Server.log): nothing to window on, and
                # far too big to count — report its size instead.
                meta["scanned"] = 1
                meta["sizeBytes"] = size
                return list(out), meta
            offset = seek_offset(fh, size, start_s - SEEK_SLACK_S, cache, start)
            fh.seek(offset)
            if offset:
                fh.readline(PROBE_MAX_BYTES)
            meta["seeked"] = offset > 0
        elif start:
            fh.seek(start)
        for rawb in fh:
            raw = rawb.lstrip(b"\0").decode("utf-8", errors="replace")
            meta["scanned"] += 1
            m = LINE_RE.match(raw)
            if not m:
                if keep_prev:
                    out.append((prev_stamp, "", raw.rstrip("\n")))
                continue
            stamp_text, level, body = m.group(1), (m.group(2) or ""), m.group(3).rstrip("\n")
            if level and level.upper() not in LEVEL_RANK:
                # No level column in this appender: the token belongs to the body.
                body = (level + " " + body).strip()
                level = ""
            try:
                stamp = parse_stamp(stamp_text, cache)
            except ValueError:
                keep_prev = False
                continue
            if meta["first"] is None:
                meta["first"] = stamp
            meta["last"] = stamp
            prev_stamp = stamp
            if seek and stamp > end_s + STOP_SLACK_S:
                break
            if stamp < start_s or stamp > end_s:
                keep_prev = False
                continue
            if not matcher.search(body):
                keep_prev = False
                continue
            keep_prev = True
            meta["matched"] += 1
            meta["levels"][level.upper() or "-"] += 1
            if cap > 0 and len(out) == cap:
                meta["truncated"] = True
            out.append((stamp, level.upper(), body))
    return list(out), meta


def build_matcher(names, ids):
    tokens = sorted({t for t in list(names) + list(ids) + EVENT_TAGS if t},
                    key=len, reverse=True)
    if not tokens:
        return re.compile(r"(?!)")
    return re.compile(r"(?<![\w-])(?:" + "|".join(re.escape(t) for t in tokens) + r")(?![\w-])")


def candidate_logs(data_dir, start_s):
    """Every *.log in the data dir that could still hold the window."""
    out = []
    for path in sorted(Path(p) for p in glob.glob(str(data_dir / "*.log"))):
        try:
            if path.stat().st_mtime < start_s:
                continue  # last written before the run began
        except OSError:
            continue
        out.append(path)
    return out


NUM_RE = re.compile(r"-?\d+(?:\.\d+)?")
TAG_RE = re.compile(r"^\[(?:DC:[^\]]+|dungeon-clear)\]\s*")


def normalise_msg(body):
    """Collapse a line to its shape so repeats group: drop the bot tag, drop
    coordinates and counters."""
    s = TAG_RE.sub("", body)
    s = NUM_RE.sub("#", s)
    return s[:110]


def scan_signals(lines):
    found = OrderedDict()
    for label, needles in SIGNALS:
        hits = []
        for stamp, level, body in lines:
            low = body.lower()
            if any(n.lower() in low for n in needles):
                hits.append((stamp, level, body))
        if hits:
            found[label] = hits
    return found


# --------------------------------------------------------------------------
# rendering


def hr(title, width=78):
    pad = max(0, width - len(title) - 3)
    return f"\n\033[1m── {title} {'─' * pad}\033[0m" if sys.stdout.isatty() \
        else f"\n── {title} {'─' * pad}"


def bold(s):
    return f"\033[1m{s}\033[0m" if sys.stdout.isatty() else s


def table(rows, headers):
    if not rows:
        return []
    cols = len(headers)
    widths = [len(h) for h in headers]
    srows = []
    for r in rows:
        cells = [("" if c is None else str(c)) for c in r] + [""] * (cols - len(r))
        srows.append(cells)
        for i, c in enumerate(cells[:cols]):
            widths[i] = max(widths[i], len(c))
    out = ["  " + "  ".join(h.ljust(widths[i]) for i, h in enumerate(headers)).rstrip()]
    out.append(("  " + "  ".join("-" * widths[i] for i in range(cols))).rstrip())
    for cells in srows:
        out.append("  " + "  ".join(cells[i].ljust(widths[i]) for i in range(cols)).rstrip())
    return out


def render_header(rec, live_rec, plan, plan_live, start_s, end_s):
    run_id = rec.get("runId") or (live_rec or {}).get("runId")
    lines = [bold(f"══ {run_id} ══")]
    name = rec.get("dungeonName") or (live_rec or {}).get("dungeonName") or "?"
    slug = rec.get("dungeon") or (live_rec or {}).get("dungeon") or "?"
    bits = [f"{name} ({slug})"]
    if rec.get("wing"):
        bits.append(f"wing {rec['wing']}")
    bits.append("heroic" if (rec.get("heroic") or (live_rec or {}).get("heroic")) else "normal")
    bits.append(f"level {rec.get('level') or (live_rec or {}).get('level') or '?'}")
    bits.append(f"map {rec.get('mapId') or (live_rec or {}).get('mapId') or '?'}")
    if rec.get("instanceId"):
        bits.append(f"instance {rec['instanceId']}")
    lines.append("  " + " · ".join(str(b) for b in bits))
    # Scenario runs (schema 13): a slice of the parent dungeon with its own
    # success predicate — say so before anything else, or "1/1 killed" and a
    # success with the chest still on the floor read as nonsense.
    if rec.get("scenario"):
        sc = [f"scenario of {rec.get('scenarioOf') or '?'}"]
        if rec.get("focus"):
            sc.append("focus " + ",".join(str(e) for e in rec["focus"]))
        sc.append(f"success {rec.get('successPredicate') or 'all-cleared'}")
        lines.append("  " + " · ".join(sc))

    if rec:
        result = rec.get("result", "?")
        lines.append(f"  result   : {bold(result)}")
        if rec.get("successBy"):
            tail = " (event tail still pending)" if rec.get("tailPending") else ""
            lines.append(f"  success  : by {rec['successBy']}{tail}")
        for field, label in (("failReason", "fail"), ("disableReason", "disabled"),
                             ("setupStage", "setup stage"), ("stallAtEnd", "stall at end"),
                             ("phaseAtEnd", "phase at end")):
            if rec.get(field):
                lines.append(f"  {label:<9}: {rec[field]}")
        if rec.get("wipeOnBoss") or rec.get("wipeOpponent"):
            lines.append(f"  wipe     : {rec.get('wipeOpponent') or '?'} "
                         f"(entry {rec.get('wipeOpponentEntry', 0)}) "
                         f"onBoss={bool(rec.get('wipeOnBoss'))}")
    else:
        lines.append(f"  result   : {bold('IN FLIGHT')} — no record yet, live state only")
        lines.append(f"  stage    : {live_rec.get('stage','?')} / {live_rec.get('state','?')}")
        if live_rec.get("stall"):
            lines.append(f"  stall    : {live_rec['stall']}")

    lines.append(f"  started  : {ts(start_s)}    ended: {ts(end_s) if rec else '(running)'}"
                 f"    duration: {dur(rec.get('durationS') if rec else (live_rec or {}).get('elapsedS'))}")

    gear = []
    ilvl = rec.get("gearIlvl") or (live_rec or {}).get("gearIlvl") or 0
    qual = rec.get("gearQuality") or 0
    gear.append(f"ilvl<={ilvl}" if ilvl else "ilvl unlimited")
    gear.append(f"quality<={qual}" if qual else "quality unlimited")
    if rec.get("compSeed"):
        gear.append(f"seed {rec['compSeed']}")
    gear.append("real-player roster" if rec.get("roster") else "bot roster")
    gear.append(f"schema v{rec.get('schema','?')}" if rec else "live")
    lines.append("  params   : " + " · ".join(gear))

    plan_id = rec.get("planId") or (live_rec or {}).get("planId")
    if plan_id:
        # runIds is ordered by duration, not launch, so it says nothing about
        # which of the N this run was — report the size only.
        ids = g(plan, "runIds", default=[]) if plan else []
        size = f" (1 of {len(ids)} runs)" if ids else ""
        state = g(plan, "result") or g(plan_live, "state") or "?"
        lines.append(f"  plan     : {plan_id}{size} — {state}")
    return lines


def render_comp(rec, live_rec):
    comp = rec.get("comp") or []
    if comp:
        rows = [[c.get("role", ""), c.get("name", ""), c.get("class", ""), c.get("spec", ""),
                 c.get("guid", ""), c.get("level", ""),
                 ("ROLE MISMATCH: " + c.get("detectedRole", "?")) if c.get("roleMismatch") else "",
                 f"from map {g(c,'from','map')}" if g(c, "from", "map") else ""]
                for c in comp]
        return table(rows, ["role", "name", "class", "spec", "guid", "lvl", "note", "origin"])
    bots = (live_rec or {}).get("bots") or []
    rows = [[b.get("role", ""), b.get("name", ""), f"cls {b.get('cls','')}",
             "alive" if b.get("alive") else "DEAD", f"hp {b.get('hp','?')}%",
             f"mp {b.get('mp')}%" if b.get("mp", -1) >= 0 else "",
             "in combat" if b.get("inCombat") else "",
             f"({b.get('x',0):.0f},{b.get('y',0):.0f},{b.get('z',0):.0f})"] for b in bots]
    return table(rows, ["role", "name", "class", "state", "hp", "mp", "combat", "pos"])


def render_bosses(rec, live_rec):
    out = []
    roster = rec.get("bossRoster") or []
    timeline = rec.get("bossTimeline") or []
    killed = {b.get("name"): b for b in timeline}
    total = rec.get("bossesTotal", (live_rec or {}).get("bossesTotal", 0))
    got = rec.get("bossesKilled", (live_rec or {}).get("bossesKilled", 0))
    out.append(f"  {got}/{total} killed")
    rows = []
    for name in roster:
        k = killed.get(name)
        rows.append(["✓" if k else "·", name,
                     mmss(k["t"]) if k else "", (k or {}).get("via", ""),
                     (k or {}).get("entry", "")])
    for k in timeline:
        if k.get("name") not in roster:
            rows.append(["✓", k.get("name", "?"), mmss(k.get("t")), k.get("via", ""), k.get("entry", "")])
    out += table(rows, ["", "boss", "at", "via", "entry"])
    # Objective-driven dungeons record an empty bossRoster and a generic
    # "objective" timeline entry; the diag roster is the only place the real
    # objective names and their end state survive.
    if not roster and (rec.get("diag") or {}).get("roster"):
        out.append("  objectives (from the teardown snapshot):")
        out += table([[o.get("order", ""), o.get("name", ""), o.get("kind", ""),
                       o.get("status", ""), o.get("doneVia", ""),
                       "TARGET" if o.get("isTarget") else ""]
                      for o in rec["diag"]["roster"]],
                     ["#", "name", "kind", "status", "via", ""])
    return out


def render_extras(rec):
    """The flat key/value map a running event published (schema 13 `extras`)."""
    extras = rec.get("extras") or {}
    if not extras:
        return []
    width = max(len(k) for k in extras)
    return [f"  {k:<{width}}  {v}" for k, v in extras.items()]


def render_event_lines(lines, limit):
    """Every slice line carrying an EVENT_TAGS tag, any level, in time order."""
    hits = [ln for ln in lines if any(tag in ln[2] for tag in EVENT_TAGS)]
    if not hits:
        return []
    shown = hits[-limit:] if limit > 0 else hits
    out = []
    if len(hits) > len(shown):
        out.append(f"  … {len(hits)-len(shown)} earlier (--notable 0 for all)")
    for stamp, level, body in shown:
        out.append(f"  {clock(stamp)} {level:<5} {body}")
    return out


def render_deaths(rec):
    deaths = rec.get("deaths") or []
    if not deaths:
        return ["  (none)"]
    rows = [[mmss(d.get("t")), d.get("name", "?"), d.get("opponent", "?"),
             d.get("opponentEntry", ""), "ON BOSS" if d.get("onBoss") else ""]
            for d in deaths]
    return table(rows, ["at", "who", "killed by", "entry", ""])


QUALITY_NAMES = {3: "rare", 4: "epic", 5: "legendary", 6: "artifact", 7: "heirloom"}


def render_loot(rec):
    if "loot" not in rec:
        return ["  (not recorded: pre-schema-14 record)"]
    loot = rec.get("loot") or []
    if not loot:
        return ["  (no rare-or-better items)"]
    rows = [[mmss(l.get("t")), l.get("name", "?"),
             l.get("item", "?") + (f" x{l['count']}" if (l.get("count") or 1) > 1 else ""),
             QUALITY_NAMES.get(l.get("quality"), l.get("quality", "")),
             l.get("roll") or "looted", l.get("source") or "?"]
            for l in loot]
    return table(rows, ["at", "who", "item", "quality", "how", "from"])


def render_pulls(rec):
    pulls = rec.get("pulls") or []
    if not pulls:
        return ["  (none)"]
    adv = sum(1 for p in pulls if p.get("advanced"))
    under = sum(1 for p in pulls if p.get("observed", 0) > p.get("predicted", 0))
    wiped = sum(1 for p in pulls if p.get("wipedHere"))
    out = [f"  {len(pulls)} pulls · {adv} advanced · {under} underestimated · {wiped} wiped here"
           + (f" · {rec['pullsElided']} elided" if rec.get("pullsElided") else "")]
    rows = [[mmss(p.get("t")), p.get("entry", ""), p.get("predicted", ""),
             f"{p.get('predictedThirds','')}/{p.get('ceilingThirds','')}",
             p.get("observed", ""), p.get("observedElites", ""),
             "adv" if p.get("advanced") else "leeroy",
             "WIPE" if p.get("wipedHere") else ""] for p in pulls]
    out += table(rows, ["at", "entry", "pred", "thirds/ceil", "obs", "elites", "mode", ""])
    return out


def render_status_timeline(rec, live_rec, limit):
    tl = rec.get("statusTimeline") or (live_rec or {}).get("recent") or []
    if not tl:
        return ["  (none)"]
    shown = tl if limit <= 0 or len(tl) <= limit else tl[:limit // 2] + [None] + tl[-(limit // 2):]
    out = []
    for e in shown:
        if e is None:
            out.append(f"  … {len(tl) - limit} more (--timeline 0 for all)")
            continue
        out.append(f"  {mmss(e.get('t')):>6}  {e.get('state','?'):<18} {e.get('detail','')}")
    return out


def render_pauses(rec):
    pauses = rec.get("pauses") or []
    if not pauses:
        return ["  (none)"]
    return [f"  {mmss(p.get('t')):>6}  {p.get('reason','?')}" for p in pauses]


# --- DC heartbeat -----------------------------------------------------------
# dcTickAgeMs is ms since that bot last evaluated a DC trigger ladder; -1 means
# the ladder has never run for it. A ladder is evaluated every AI tick, so a
# healthy age is hundreds of ms. Anything past a few seconds means the bot is not
# being updated at all -- which reads identically to "every DC rung declined" in
# every other diag field, and is the one thing this column exists to separate.
TICK_STALE_MS = 5000


def tick_cell(age):
    """Compact per-member column. Blank when the record predates the field."""
    if age is None:
        return ""
    if age < 0:
        return "NEVER"
    if age >= TICK_STALE_MS:
        return f"STALE {age / 1000:.0f}s"
    return f"{age}ms"


def fmt_tick_age(age):
    if age is None:
        return ""
    if age < 0:
        return "dcTick=NEVER  (this bot's DC ladder never ran)"
    if age >= TICK_STALE_MS:
        return (f"dcTick=STALE {age / 1000:.0f}s  "
                f"(the tank's DC ladder stopped being evaluated -- "
                f"compare the party's dcTick column)")
    return f"dcTick={age}ms"


def render_combat_blame(members):
    """Who is holding each flagged member in combat, and the verdict each holder earns.

    Answers the question a stuck run used to leave open: is the phantom-combat
    hatch armed, and if not, which of its guards is holding it back?  A holder
    that is alive + non-evading + navmesh-reachable reads as LEGIT and the hatch
    stands down on it forever, so a LEGIT row next to a member at full health
    with no attackers is the finding, not the absence of one.
    """
    # `holderRefs` is the schema marker, not `combatHolders`: a genuinely
    # unheld member emits an empty holder list, and a record written before
    # this diagnostic existed emits neither.  Keying on the list would report
    # every pre-schema-10 freeze as "no combat refs at all" — a confident,
    # wrong finding about the exact runs this was built to explain.
    flagged = [m for m in members if m.get("inCombat") and "holderRefs" in m]
    if not flagged:
        return []
    out = ["  combat blame (who is holding each flagged member):"]
    for m in flagged:
        holders = m.get("combatHolders") or []
        refs = m.get("holderRefs", len(holders))
        tags = []
        if m.get("phantomCombat"):
            tags.append("PHANTOM")
        if m.get("botState") == "noncombat":
            # The flag says fight, the engine says otherwise — every DC rung
            # bails on IsInCombat() here and every combat rung is out of reach.
            tags.append("OFF THE COMBAT ENGINE")
        out.append(f"    {m.get('name','?')}"
                   f"  attackers={m.get('attackers',0)}"
                   f"  victim={m.get('victim') or '-'}"
                   f"  refs={refs}"
                   + ("  ** " + ", ".join(tags) + " **" if tags else ""))
        if not holders:
            out.append("      (no combat refs at all — opaque/forced combat; the hatch "
                       "treats this as legitimate by design)"
                       if not refs else "      (all holder rows truncated)")
            continue
        rows = []
        for c in holders:
            flags = []
            if not c.get("alive"):
                flags.append("DEAD")
            if not c.get("sameMap"):
                flags.append("OTHER-MAP")
            if c.get("evading"):
                flags.append("EVADING")
            if c.get("suppressed"):
                flags.append("SUPPRESSED")
            if not c.get("canAttackMe"):
                flags.append("CANNOT-ATTACK-ME")
            # A trigger creature is an invisible script helper no bot can target,
            # so it can never be killed and the fight it holds has no end state.
            # Absent on schema <= 11 records, where such a holder scored LEGIT --
            # tr-20260902-121659-12/-13 are the reference pre-fix records.
            if c.get("trigger"):
                flags.append("TRIGGER")
            if c.get("pvp"):
                flags.append("pvp-ref")
            if is_under_mesh(c):
                flags.append("UNDER-MESH")
            # "-" is not "unreachable": the pathfind is skipped for a holder
            # already excluded by a cheaper guard (dead / other map / evading).
            if not c.get("reachChecked", True):
                path = "-"
            else:
                path = "reach" if c.get("reachable") else "UNREACH"
            rows.append([c.get("name", ""), c.get("entry", ""),
                         f"{c.get('dist',-1):.1f}yd", floor_cell(c), f"{c.get('hp',0)}%", path,
                         "LEGIT" if c.get("legitimate") else "phantom",
                         c.get("victim") or "-", " ".join(flags)])
        out += ["    " + line
                for line in table(rows, ["holder", "entry", "dist", "z/mesh", "hp", "path",
                                         "verdict", "fighting", "flags"])]
        if refs > len(holders):
            out.append(f"      (+{refs - len(holders)} more refs not shown)")
    return out


# Mirror of DcDiag::kUnderMeshYd (DcDiagSnapshot.h). Change the two together.
UNDER_MESH_YD = 3.0


def is_under_mesh(obj):
    f = obj.get("floor")
    return bool(f and f.get("meshOk") and obj.get("z", 0) < f.get("meshZ", 0) - UNDER_MESH_YD)


def floor_cell(obj):
    """'z/meshZ' for a probed row, '' for one written before the probe existed
    (or not probed), so an old record never shows a fabricated floor."""
    f = obj.get("floor")
    if not f:
        return ""
    mesh = f"{f.get('meshZ', 0):.1f}" if f.get("meshOk") else "none"
    return f"{obj.get('z', 0):.1f}/{mesh}"


def render_escort(d):
    """The active EscortCreature step: the driver's own search next to every
    copy of the escortee on the map. Absent unless an escort step was active
    at capture — including on every record written before the block existed."""
    e = d.get("escort")
    if not e:
        return []
    age = e.get("deadAirMs", -1)
    out = [f"  escort        : event {e.get('eventId')} '{e.get('eventName','')}' "
           f"step {e.get('stepIndex')} · escortee {e.get('entry')} · "
           f"driver search {e.get('searchRadius',0):.0f}yd -> "
           + ("SEES him" if e.get("driverSees") else "SEES NOBODY"),
           f"                  dead-air {'never stamped' if age < 0 else f'{age/1000:.1f}s'}"
           + (f" · instance data[{e.get('instanceDataId')}]={e.get('instanceData')} "
              f"(done at >= {e.get('instanceDataMin')})" if e.get("instanceDataId", -1) >= 0 else "")]
    copies = e.get("copies") or []
    if not copies:
        out.append("                  NO copy of the escortee anywhere on the map")
        return out
    rows = []
    for c in copies:
        flags = []
        if not c.get("alive"):
            flags.append(f"DEAD(state {c.get('deathState')})")
        if not c.get("inWorld"):
            flags.append("NOT-IN-WORLD")
        if not c.get("updateNeeded"):
            flags.append("NOT-UPDATED")
        if not c.get("gridLoaded"):
            flags.append("GRID-UNLOADED")
        if not c.get("seenByGridScan"):
            flags.append("GRID-BLIND")
        for k, tag in (("moving", "moving"), ("inCombat", "combat"),
                       ("evading", "EVADING"), ("gossipFlag", "gossip")):
            if c.get(k):
                flags.append(tag)
        rows.append([c.get("guid", ""), c.get("spawnId") or "summon",
                     f"({c.get('x',0):.1f},{c.get('y',0):.1f},{c.get('z',0):.1f})",
                     f"({c.get('homeX',0):.1f},{c.get('homeY',0):.1f})",
                     f"{c.get('distToTank',-1):.1f}", c.get("faction", ""), " ".join(flags)])
    out += ["    " + line for line in table(rows, ["guid", "spawnId", "pos", "home",
                                                     "distTank", "faction", "flags"])]
    if e.get("copyCount", len(copies)) > len(copies):
        out.append(f"      (+{e['copyCount'] - len(copies)} more copies not shown)")
    return out


def render_diag(rec):
    d = rec.get("diag") or {}
    if not d.get("valid"):
        return ["  (no snapshot captured)"]
    out = [f"  captured at {d.get('capturedAt','?')} — "
           f"phase={d.get('phase','?')} state={d.get('state','?')} "
           f"enabled={d.get('enabled')} paused={d.get('paused')}"]
    for k in ("detail", "stallReason", "pauseReason"):
        if d.get(k):
            out.append(f"  {k:<14}: {d[k]}")
    if d.get("pausedAtDoor"):
        out.append("  pausedAtDoor  : true")
    if d.get("smartRestLatched"):
        out.append("  smartRest     : latched")

    t = d.get("target") or {}
    if t:
        out.append(f"  target        : {t.get('nextName','?')} (entry {t.get('nextEntry',0)}) "
                   f"dist {t.get('distance',0):.1f}"
                   + ("  MISMATCH" if t.get("mismatch") else ""))
        out.append(f"                  sticky={t.get('sticky',0)} committed={t.get('committedEntry',0)} "
                   f"approach={t.get('approachEntry',0)}")
    r = d.get("route") or {}
    if r:
        out.append(f"  route         : reachable={r.get('reachable')} complete={r.get('complete')} "
                   f"seg {r.get('segmentIdx',0)}/{r.get('segments',0)} pt {r.get('pointIdx',0)} "
                   f"dev {r.get('deviation',0):.2f} offPath {r.get('offPathTicks',0)}")
        if r.get("failureReason"):
            out.append(f"                  failure: {r['failureReason']}")
        if r.get("startFarFromPoly"):
            out.append("                  startFarFromPoly=TRUE")
        if r.get("cursorPastPathEnd"):
            out.append("                  cursorPastPathEnd=TRUE")
    w = d.get("watchdogs") or {}
    if w:
        hot = {k: v for k, v in w.items() if v not in (0, False, "")}
        out.append("  watchdogs     : " + (", ".join(f"{k}={v}" for k, v in hot.items()) if hot else "all clear"))
    p = d.get("pull") or {}
    if p:
        out.append(f"  pull          : setting={p.get('setting')} phase={p.get('phase')} "
                   f"decision={p.get('decision')} forMs={p.get('phaseForMs')} "
                   f"fizzles={p.get('fizzleCount')} camp={p.get('hasCamp')}")
    wd = d.get("world") or {}
    if wd:
        out.append(f"  world         : map {wd.get('map')} inst {wd.get('instance')} "
                   f"({wd.get('x',0):.1f},{wd.get('y',0):.1f},{wd.get('z',0):.1f}) "
                   f"combat={wd.get('inCombat')} moving={wd.get('moving')} "
                   f"victim={wd.get('victim') or '-'}")
        out.append(f"                  encounterMask=0x{wd.get('completedEncounterMask',0):x} "
                   f"clearedAnchors={wd.get('clearedAnchors',0)} skipped={wd.get('skipped',0)}")
        # Heartbeat. Absent on records written before the field existed, so an
        # older run says nothing here rather than lying with a zero.
        age = wd.get("dcTickAgeMs")
        if age is not None:
            out.append(f"                  {fmt_tick_age(age)}")

    party = d.get("party") or {}
    members = party.get("members") or []
    if members:
        out.append(f"  party         : {party.get('size',0)} members, {party.get('alive',0)} alive, "
                   f"{party.get('offline',0)} offline, {party.get('inCombat',0)} in combat")
        rows = [[m.get("name", ""), m.get("guid", ""),
                 "alive" if m.get("alive") else "DEAD",
                 f"{m.get('hp',0)}%", f"{m.get('mp',0)}%",
                 f"{m.get('distToTank',0):.1f}",
                 floor_cell(m),
                 "combat" if m.get("inCombat") else "",
                 m.get("victim", ""),
                 ("dc" if m.get("dcStrategy") else "-") + "/" + ("cbt" if m.get("dcCombatStrategy") else "-"),
                 m.get("botState", ""),
                 tick_cell(m.get("dcTickAgeMs")),
                 " ".join(x for x in ("" if m.get("online") else "OFFLINE",
                                      "UNDER-MESH" if is_under_mesh(m) else "") if x)]
                for m in members]
        out += table(rows, ["name", "guid", "state", "hp", "mp", "distTank", "z/mesh", "", "victim",
                            "strat", "engine", "dcTick", ""])
        out += render_combat_blame(members)

    roster = d.get("roster") or []
    if roster:
        rows = [[o.get("order", ""), o.get("kind", ""), o.get("name", ""), o.get("entry", ""),
                 o.get("status", ""), o.get("doneVia", ""), o.get("encounterIndex", ""),
                 ("TARGET" if o.get("isTarget") else "") + (" sticky" if o.get("isSticky") else "")]
                for o in roster]
        out.append("  objective roster:")
        out += table(rows, ["#", "kind", "name", "entry", "status", "via", "enc", ""])
    out += render_escort(d)
    return out


def render_plan(plan, plan_live, runs_by_id, this_run):
    if not plan and not plan_live:
        return ["  (no plan record — plan may still be running)"]
    out = []
    if plan:
        out.append(f"  {plan.get('planId')} — {plan.get('result','?')}"
                   + (f" ({plan['abortReason']})" if plan.get("abortReason") else "")
                   + ("  [checkpoint — plan still running when written]"
                      if plan.get("checkpoint") else ""))
        req = plan.get("requested") or {}
        total = "endless" if plan.get("endless") else f"{req.get('total')} runs"
        out.append(f"  requested: {total}, {req.get('concurrent')} concurrent, "
                   f"level {req.get('level') or 'default'}, heroic={req.get('heroic')}, "
                   f"ilvl<={req.get('gearIlvl') or '∞'} quality<={req.get('gearQuality') or '∞'}")
        r = plan.get("runs") or {}
        out.append(f"  outcome  : {r.get('launched',0)} launched, {r.get('succeeded',0)} succeeded, "
                   f"{r.get('failed',0)} failed  ·  " +
                   ", ".join(f"{k}={v}" for k, v in (plan.get("verdicts") or {}).items()))
        d = plan.get("duration") or {}
        if d:
            out.append(f"  duration : min {dur(d.get('minS'))} · median {dur(d.get('medianS'))} · "
                       f"avg {dur(d.get('avgS'))} · max {dur(d.get('maxS'))}")
        for fr in plan.get("failReasons") or []:
            out.append(f"  fail x{fr.get('count',0)}: {fr.get('reason','?')}")
        funnel = plan.get("bossFunnel") or []
        if funnel:
            out.append("  boss funnel:")
            out += table([[f.get("name", ""), f.get("killed", 0), f.get("wiped", 0)] for f in funnel],
                         ["boss", "killed", "wiped"])
        pool = plan.get("pool") or []
        if pool:
            out.append(f"  pool ({plan.get('pick') or 'bag'}):")
            rows = []
            for e in pool:
                r = e.get("runs") or {}
                n = r.get("launched", 0)
                rows.append([(e.get("token") or "?") + (":heroic" if e.get("heroic") else "")
                             + ("" if e.get("inPool", True) else " (removed)"),
                             n, r.get("succeeded", 0), r.get("failed", 0),
                             f"{100 * r.get('succeeded', 0) / n:.0f}%" if n else "-",
                             dur((e.get("duration") or {}).get("medianS"))])
            out += table(rows, ["entry", "runs", "ok", "fail", "rate", "median"])
        p = plan.get("pulls") or {}
        if p:
            out.append(f"  pulls    : {p.get('count',0)} · advanced {p.get('advanced',0)} · "
                       f"underestimated {p.get('underestimated',0)} · obs p50/p90/max "
                       f"{p.get('observedP50')}/{p.get('observedP90')}/{p.get('observedMax')} · "
                       f"err p50/p90 {p.get('errorP50')}/{p.get('errorP90')}")
    elif plan_live:
        out.append(f"  {plan_live.get('planId')} — {plan_live.get('state','?')} (LIVE): "
                   f"{plan_live.get('launched',0)}/{plan_live.get('total',0)} launched, "
                   f"{plan_live.get('succeeded',0)} ok, {plan_live.get('failed',0)} failed, "
                   f"{plan_live.get('active',0)} active, elapsed {dur(plan_live.get('elapsedS'))}")

    ids = (plan or {}).get("runIds") or []
    if len(ids) > 60:
        ids = ids[-60:]  # a soak's plan lists hundreds; the newest are the relevant ones
    if ids:
        rows = []
        for rid in ids:
            sib = runs_by_id.get(rid)
            rows.append(["→" if rid == this_run else "", rid,
                         (sib or {}).get("result", "(no record)"),
                         f"{(sib or {}).get('bossesKilled','?')}/{(sib or {}).get('bossesTotal','?')}",
                         dur((sib or {}).get("durationS")),
                         ((sib or {}).get("failReason") or "")[:52]])
        out.append("  sibling runs:")
        out += table(rows, ["", "runId", "result", "bosses", "duration", "fail reason"])
    return out


def session_start(slices):
    """When worldserver boots it reopens every appender with mode 'w', so the
    earliest line across all logs marks the start of the current server session
    — and anything that happened before it has been wiped."""
    firsts = [m["first"] for _, m in slices if m["first"] is not None]
    return min(firsts) if firsts else None


def render_log_summary(slices, start_s, end_s, names, session_s, run_start_s):
    out = [f"  window {clock(start_s)} → {clock(end_s)}  ·  bots: {', '.join(names)}"]
    if session_s:
        out.append(f"  server session began {ts(session_s)}"
                   + ("" if not run_start_s or session_s <= run_start_s + 2
                      else "  ⚠ AFTER this run started"))
    rows = []
    for lines, meta in slices:
        levels = " ".join(f"{k}:{v}" for k, v in sorted(meta["levels"].items(),
                                                        key=lambda kv: -LEVEL_RANK.get(kv[0], 9)))
        rows.append([meta["name"], meta["matched"],
                     f"{meta['scanned']}{' (seeked)' if meta.get('seeked') else ''}",
                     clock(meta["first"]), levels,
                     "⚠ capped: oldest lines dropped" if meta["truncated"] else ""])
    out += table(rows, ["log", "run lines", "lines read", "opens", "levels", ""])
    return out


def render_untimestamped(data_dir, metas):
    """Server.log / Auth.log / Errors.log are raw console streams with no time
    column, so they cannot be windowed or attributed to a run. Name them, and
    show Errors.log outright — it is short and a script error there explains a
    whole class of weird run behaviour."""
    if not metas:
        return []
    def size_of(m):
        if m.get("sizeBytes"):
            return f"{m['sizeBytes'] / (1 << 20):.0f} MB"
        return f"{m['scanned']} lines"
    out = ["  no time column, so not attributable to a run — check by hand: "
           + ", ".join(f"{m['name']} ({size_of(m)})" for m in metas)]
    for m in metas:
        if m["name"] != "Errors.log":
            continue
        try:
            body = (data_dir / m["name"]).read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for line in body[:15]:
            out.append(f"    Errors.log: {line}")
        if len(body) > 15:
            out.append(f"    … {len(body)-15} more")
    return out


def render_signals(lines):
    found = scan_signals(lines)
    if not found:
        return ["  (no known-signal lines in this run's slice)"]
    out = []
    for label, hits in found.items():
        out.append(f"  {label:<20} {len(hits):>5}   first {clock(hits[0][0])}  last {clock(hits[-1][0])}")
        shapes = Counter(normalise_msg(b) for _, _, b in hits).most_common(3)
        for shape, n in shapes:
            out.append(f"      x{n:<5} {shape}")
    return out


def render_notable(lines, limit):
    notable = [ln for ln in lines if LEVEL_RANK.get(ln[1], 0) >= 2]
    if not notable:
        return ["  (none — the slice is all DEBUG/TRACE)"]
    out = []
    shown = notable[:limit] if limit > 0 else notable
    for stamp, level, body in shown:
        out.append(f"  {clock(stamp)} {level:<5} {body}")
    if len(notable) > len(shown):
        out.append(f"  … {len(notable)-len(shown)} more (--notable 0 for all)")
    return out


def render_chatter(lines, top):
    chatter = [ln for ln in lines if LEVEL_RANK.get(ln[1], 0) < 2]
    if not chatter:
        return []
    counts = Counter(normalise_msg(b) for _, _, b in chatter)
    out = [f"  {len(chatter)} DEBUG/TRACE lines, {len(counts)} distinct shapes — top {top}:"]
    for shape, n in counts.most_common(top):
        out.append(f"  {n:>6}  {shape}")
    return out


def render_decisions(data_dir, guids):
    out = []
    for fname in DECISION_FILES:
        path = data_dir / fname
        if not path.exists():
            continue
        hits = [r for r in iter_jsonl(path) if r.get("guid") in guids]
        out.append(f"  {fname}: {len(hits)} records for this run's bots")
        for r in hits[:5]:
            out.append("    " + json.dumps(r)[:150])
        if len(hits) > 5:
            out.append(f"    … {len(hits)-5} more (read {path} directly)")
    return out or ["  (no decision captures on disk — they are opt-in)"]


# --------------------------------------------------------------------------
# modes


def cmd_list(data_dir, n):
    runs = load_runs(data_dir)
    live = load_live(data_dir)
    rows = []
    for rec in runs[-n:]:
        rows.append([rec.get("runId", ""), rec.get("dungeon", ""),
                     "H" if rec.get("heroic") else "n", rec.get("result", ""),
                     f"{rec.get('bossesKilled','?')}/{rec.get('bossesTotal','?')}",
                     dur(rec.get("durationS")), rec.get("planId", ""),
                     (rec.get("failReason") or "")[:44]])
    print("\n".join(table(rows, ["runId", "dungeon", "", "result", "bosses",
                                 "duration", "plan", "fail reason"])))
    live_runs = live.get("runs") or []
    if live_runs:
        print(f"\n  {len(live_runs)} run(s) IN FLIGHT:")
        rows = [[r.get("runId", ""), r.get("dungeon", ""), r.get("stage", ""), r.get("state", ""),
                 f"{r.get('bossesKilled','?')}/{r.get('bossesTotal','?')}",
                 dur(r.get("elapsedS")), r.get("stall", "")] for r in live_runs]
        print("\n".join(table(rows, ["runId", "dungeon", "stage", "state", "bosses",
                                     "elapsed", "stall"])))


def cmd_batch(matches, live_matches, prefix):
    print(bold(f"══ {len(matches)+len(live_matches)} runs matching '{prefix}' ══"))
    rows = [[r.get("runId", ""), r.get("dungeon", ""), r.get("result", ""),
             f"{r.get('bossesKilled','?')}/{r.get('bossesTotal','?')}",
             dur(r.get("durationS")), (r.get("failReason") or "")[:56]] for r in matches]
    rows += [[r.get("runId", ""), r.get("dungeon", ""), "IN FLIGHT",
              f"{r.get('bossesKilled','?')}/{r.get('bossesTotal','?')}",
              dur(r.get("elapsedS")), (r.get("stall") or r.get("state") or "")[:56]]
             for r in live_matches]
    print("\n".join(table(rows, ["runId", "dungeon", "result", "bosses", "duration",
                                 "fail reason / state"])))
    verdicts = Counter(r.get("result", "?") for r in matches)
    if verdicts:
        print("\n  verdicts: " + ", ".join(f"{k}={v}" for k, v in verdicts.most_common()))
    print("\n  pass a single run id for the full post-mortem.")


def cmd_plan(data_dir, plan_id, runs, live):
    plan = find_plan(data_dir, plan_id)
    plan_live = find_live_plan(live, plan_id)
    if not plan and not plan_live:
        die(f"no plan {plan_id} in {PLANS_FILE} or {LIVE_FILE}")
    runs_by_id = {r.get("runId"): r for r in runs}
    for r in live.get("runs") or []:
        runs_by_id.setdefault(r.get("runId"), None)
    print(bold(f"══ {plan_id} ══"))
    print("\n".join(render_plan(plan, plan_live, runs_by_id, None)))
    if not (plan or {}).get("runIds"):
        kids = [r for r in runs if r.get("planId") == plan_id]
        kids += [r for r in (live.get("runs") or []) if r.get("planId") == plan_id]
        if kids:
            print("\n  runs attributed to this plan:")
            print("\n".join(table([[k.get("runId", ""), k.get("result", "IN FLIGHT"),
                                    dur(k.get("durationS") or k.get("elapsedS")),
                                    (k.get("failReason") or k.get("stall") or "")[:56]] for k in kids],
                                  ["runId", "result", "duration", "fail reason"])))


# --------------------------------------------------------------------------
# continuous-mode (soak) sessions


def default_deck_data_dir():
    """The Test Deck's default data_dir (hostenv.default_data_dir)."""
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA") or str(Path.home() / "AppData" / "Local")
        return Path(base) / "ac-testdeck"
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "ac-testdeck"


def find_soak_dir(explicit, soak_id):
    roots = []
    if explicit:
        roots.append(Path(explicit).expanduser())
    if os.environ.get("DC_SOAK_DIR"):
        roots.append(Path(os.environ["DC_SOAK_DIR"]).expanduser())
    roots.append(default_deck_data_dir() / SOAKS_SUBDIR)
    for root in roots:
        for cand in (root / soak_id, root / SOAKS_SUBDIR / soak_id):
            if (cand / "state.json").exists():
                return cand
    die(f"no soak {soak_id} under {', '.join(str(r) for r in roots)} "
        "(pass --soak-dir <deck data_dir>/soaks)")


# Failure reasons embed names, numbers and coordinates; strip them so the same
# failure mode groups across runs ("Moroes pulled early" x7). The Test Deck's
# soak page uses the same rule (testdeck/soak.py cluster_reason).
CLUSTER_NUM_RE = re.compile(r"-?\d+(?:\.\d+)?")
CLUSTER_QUOTED_RE = re.compile(r"'[^']*'|\"[^\"]*\"")
CLUSTER_WS_RE = re.compile(r"\s+")


def cluster_reason(reason):
    s = CLUSTER_QUOTED_RE.sub("'…'", reason or "")
    s = CLUSTER_NUM_RE.sub("#", s)
    s = CLUSTER_WS_RE.sub(" ", s).strip()
    return s[:120] or "(no reason)"


# Retired dungeon tokens -> their replacements, mirroring
# DcTestDungeonRegistry::Aliases (the sidecar's "aliases"). Applied at read time
# so pre-split `brs` history joins `lbrs` in the soak tables; the records
# themselves are never rewritten.
TOKEN_ALIASES = {"brs": "lbrs"}


def entry_key(rec):
    token = rec.get("dungeon") or "?"
    return TOKEN_ALIASES.get(token, token) + (":heroic" if rec.get("heroic") else "")


def cmd_soak(soak_dir, limit):
    try:
        state = json.loads((soak_dir / "state.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        die(f"cannot read {soak_dir / 'state.json'}: {exc}")
    ledger = list(iter_jsonl(soak_dir / "ledger.jsonl"))
    cfg = state.get("config") or {}
    pool = ", ".join(TOKEN_ALIASES.get(e.get("token") or "?", e.get("token") or "?")
                     + (":heroic" if e.get("heroic") else "")
                     for e in cfg.get("pool") or [])
    ok = sum(1 for r in ledger if r.get("result") == "success")
    fails = [r for r in ledger if r.get("result") != "success"]
    lost = sum(1 for r in ledger if r.get("result") == "lost")
    created = (state.get("createdAtMs") or 0) / 1000
    stopped = (state.get("stoppedAtMs") or 0) / 1000
    span = (stopped or time.time()) - created if created else 0

    print(bold(f"══ {state.get('soakId', soak_dir.name)} — continuous ══"))
    print(f"  status     {state.get('status', '?')}"
          + (f" — {state['statusDetail']}" if state.get("statusDetail") else ""))
    print(f"  owner      {state.get('owner', '?')}   started {ts(created) if created else '?'}"
          + (f"   stopped {ts(stopped)}" if stopped else "") + f"   ({dur(int(span))})")
    print(f"  pool       {pool or '?'}")
    print(f"  concurrent {cfg.get('concurrent', '?')}   pick {cfg.get('pick', 'bag')}"
          f"   plans {', '.join(state.get('planIds') or []) or '-'}"
          f"   restarts {state.get('restarts', 0)}")
    rate = f"{100 * ok / len(ledger):.0f}%" if ledger else "-"
    per_h = f"{len(ledger) / (span / 3600):.1f}/h" if span > 60 else "-"
    print(f"  runs       {len(ledger)}   ok {ok}   fail {len(fails)} (lost {lost})"
          f"   success {rate}   {per_h}")

    print(hr("PER DUNGEON"))
    by = OrderedDict()
    for r in ledger:
        by.setdefault(entry_key(r), []).append(r)
    rows = []
    for key, recs in sorted(by.items(), key=lambda kv: -len(kv[1])):
        good = [r for r in recs if r.get("result") == "success"]
        durs = sorted(int(r.get("durationS") or 0) for r in good)
        med = durs[len(durs) // 2] if durs else 0
        strip = "".join("✓" if r.get("result") == "success" else "✗" for r in recs[-20:])
        rows.append([key, len(recs), len(good), len(recs) - len(good),
                     f"{100 * len(good) / len(recs):.0f}%", dur(med) if med else "-", strip])
    print("\n".join(table(rows, ["dungeon", "runs", "ok", "fail", "rate", "median", "last 20"]))
          if rows else "  no runs yet")

    print(hr("FAILURE CLUSTERS"))
    clusters = Counter()
    where = {}
    for r in fails:
        c = (r.get("result") or "?") + ": " + cluster_reason(r.get("failReason"))
        clusters[c] += 1
        where.setdefault(c, Counter())[entry_key(r)] += 1
    rows = [[n, c[:80], ", ".join(f"{k}×{v}" for k, v in where[c].most_common(4))]
            for c, n in clusters.most_common(25)]
    print("\n".join(table(rows, ["n", "verdict: reason", "where"])) if rows else "  no failures")

    print(hr(f"FAILURES (newest {limit})" if limit else "FAILURES"))
    shown = list(reversed(fails))[:limit] if limit else list(reversed(fails))
    ev_root = soak_dir / "evidence"
    rows = []
    for r in shown:
        rid = r.get("runId", "")
        ev = ev_root / rid
        mark = "✓" if (ev / "report.txt").exists() else ("…" if ev.exists() else "-")
        rows.append([clock((r.get("endedAtMs") or r.get("startedAtMs") or 0) / 1000), rid,
                     entry_key(r), r.get("result", ""),
                     f"{r.get('bossesKilled', '?')}/{r.get('bossesTotal', '?')}",
                     dur(r.get("durationS") or r.get("elapsedS")), mark,
                     (r.get("failReason") or "")[:50]])
    print("\n".join(table(rows, ["ended", "runId", "dungeon", "result", "bosses", "duration",
                                 "ev", "fail reason"])) if rows else "  none")
    if fails:
        print(f"\n  evidence: {ev_root}/<runId>/report.txt · full post-mortem: dc_test_run.py <runId>")


# --------------------------------------------------------------------------


def cmd_lab(data_dir, wanted):
    """Pull Lab ids: lr-... (one scenario run) or lb-... (a batch)."""
    path = data_dir / "dc_labruns.jsonl"
    if not path.exists():
        die(f"no {path} — no Lab run has finished on this server yet")
    recs = []
    with open(path) as fh:
        for line in fh:
            try:
                recs.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    if wanted.startswith("lb-"):
        hits = [r for r in recs if (r.get("batchId") or "").startswith(wanted)]
    else:
        hits = [r for r in recs if (r.get("runId") or "").startswith(wanted)]
    if not hits:
        die(f"no Lab record matching {wanted}")
    if len(hits) > 1:
        print(f"{len(hits)} Lab run(s) for {wanted}:")
        for r in hits:
            print(f"  {r.get('runId'):<34} {r.get('scenario', '?'):<48} {r.get('verdict', '?'):<16} "
                  f"end={r.get('end', '?'):<12} {r.get('summary', '')}")
        return
    r = hits[0]
    print(f"{r['runId']}  {r.get('scenario')}  verdict={r.get('verdict')}  end={r.get('end')}  "
          f"{(r.get('durationMs') or 0) / 1000:.1f}s  seed={r.get('seed')}  host={r.get('host')}")
    if r.get("knownFailure"):
        print(f"  known failure: {r['knownFailure']}")
    print(f"  module {r.get('sha') or '?'}  playerbots {r.get('pbSha') or '?'}")
    for oid, o in sorted((r.get("oracles") or {}).items(), key=lambda kv: int(kv[0][1:])):
        exp = (r.get("expected") or {}).get(oid)
        mark = o.get("res", "?")
        print(f"  {oid:<4} {mark:<5}" + (f" (expected {exp})" if exp else "") +
              (f" @{(o.get('t') or 0) / 1000:.1f}s {o.get('detail', '')}" if mark == "fail" else ""))
    trace = r.get("trace")
    if trace:
        tp = Path(trace)
        if not tp.is_absolute():
            tp = data_dir / tp
        print(f"\n  trace: {tp}\n  drill down: tools/lab_trace.py {r['runId']} [--timeline|--actions BOT|--oracle O1]")
    log = data_dir / "DungeonClear.log"
    if log.exists():
        lines = [l.rstrip() for l in open(log, errors="ignore") if f"LAB {r['runId']}" in l]
        if lines:
            print("\n  log:")
            for l in lines[-40:]:
                print("   " + l)


def main():
    ap = argparse.ArgumentParser(
        description="Everything known about one .dc test run, by its id.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="examples:\n"
               "  dc_test_run.py tr-20260801-174432-3\n"
               "  dc_test_run.py tr-20260801-174432-3 --logs pull --grep 'scout-lag'\n"
               "  dc_test_run.py tr-20260801-174432-3 --dump /tmp/run3\n"
               "  dc_test_run.py sk-20260928-220000\n"
               "  dc_test_run.py --list 20\n")
    ap.add_argument("run_id", nargs="?",
                    help="tr-<id>, tp-<planid>, sk-<soakid>, an id prefix, or 'last'")
    ap.add_argument("--soak-dir", help="Test Deck soaks dir for sk- ids "
                                       "(default: $DC_SOAK_DIR, then <deck data_dir>/soaks)")
    ap.add_argument("--failures", type=int, default=40, metavar="N",
                    help="sk-: newest N failures to list (0 = all)")
    ap.add_argument("--data-dir", help="worldserver cwd (default: found by walking up to env/dist/bin)")
    ap.add_argument("--log-dir", help="where the *.log files are, when worldserver.conf's "
                                      "LogsDir is not its cwd (default: --data-dir)")
    ap.add_argument("--list", nargs="?", type=int, const=20, metavar="N",
                    help="list the N most recent runs and exit")
    ap.add_argument("--logs", nargs="*", metavar="NAME",
                    help="dump sliced log lines; NAME substrings pick files (default: all)")
    ap.add_argument("--grep", metavar="RE", help="only log lines matching RE (implies --logs)")
    ap.add_argument("--level", metavar="L", help="only log lines at or above this level")
    ap.add_argument("--head", type=int, default=0, help="first N dumped lines")
    ap.add_argument("--tail", type=int, default=0, help="last N dumped lines")
    ap.add_argument("--max-lines", type=int, default=0, metavar="N",
                    help="keep only the newest N lines per log file (default 0 = uncapped)")
    ap.add_argument("--dump", metavar="DIR", help="write each per-run log slice to DIR")
    ap.add_argument("--pad", type=int, default=None, metavar="S",
                    help=f"seconds of slack on both ends of the window "
                         f"(default {PAD_BEFORE_S} before / {PAD_AFTER_S} after)")
    ap.add_argument("--timeline", type=int, default=40, metavar="N",
                    help="status-timeline entries to show (0 = all)")
    ap.add_argument("--notable", type=int, default=60, metavar="N",
                    help="INFO+ log lines to show (0 = all)")
    ap.add_argument("--chatter", type=int, default=12, metavar="N",
                    help="repeated DEBUG/TRACE shapes to show (0 = skip)")
    ap.add_argument("--no-logs", action="store_true", help="skip the log slice entirely (fast)")
    ap.add_argument("--no-diag", action="store_true", help="skip the diag snapshot")
    ap.add_argument("--json", action="store_true", help="emit the whole bundle as JSON")
    args = ap.parse_args()

    if args.run_id and args.run_id.strip().startswith("sk-"):
        cmd_soak(find_soak_dir(args.soak_dir, args.run_id.strip()), args.failures)
        return

    data_dir = find_data_dir(args.data_dir)
    if args.list is not None:
        cmd_list(data_dir, args.list)
        return
    if not args.run_id:
        ap.error("a run id is required (or --list)")

    runs = load_runs(data_dir)
    live = load_live(data_dir)
    wanted = normalise_id(args.run_id)

    if wanted in ("last", "latest"):
        if not runs:
            die("no runs recorded yet")
        wanted = runs[-1]["runId"]
    if wanted.startswith("tp-"):
        cmd_plan(data_dir, wanted, runs, live)
        return
    if wanted.startswith(("lr-", "lb-")):
        cmd_lab(data_dir, wanted)
        return

    rec = find_run(runs, wanted)
    live_rec = find_live_run(live, wanted)
    if not rec and not live_rec:
        matches = [r for r in runs if (r.get("runId") or "").startswith(wanted)]
        live_matches = [r for r in (live.get("runs") or [])
                        if (r.get("runId") or "").startswith(wanted)]
        if len(matches) + len(live_matches) == 1:
            rec = matches[0] if matches else None
            live_rec = live_matches[0] if live_matches else None
            wanted = (rec or live_rec)["runId"]
        elif matches or live_matches:
            cmd_batch(matches, live_matches, wanted)
            return
        else:
            die(f"no run '{wanted}' in {RUNS_FILE} or {LIVE_FILE} "
                f"({len(runs)} records on file; try --list)")

    rec = rec or {}
    plan_id = rec.get("planId") or (live_rec or {}).get("planId") or ""
    plan = find_plan(data_dir, plan_id)
    plan_live = find_live_plan(live, plan_id)

    # Window. A finished record has authoritative bounds; a live run is bounded
    # by the id's own timestamp and now.
    start_s = (rec.get("startedAtMs") or 0) / 1000 or id_started_at(wanted) or 0
    end_s = (rec.get("endedAtMs") or 0) / 1000
    if not end_s:
        elapsed = (live_rec or {}).get("elapsedS")
        end_s = (start_s + elapsed) if (start_s and elapsed) else time.time()
    pad_before = args.pad if args.pad is not None else PAD_BEFORE_S
    pad_after = args.pad if args.pad is not None else PAD_AFTER_S
    win_start, win_end = start_s - pad_before, end_s + pad_after

    names = [c.get("name") for c in (rec.get("comp") or []) if c.get("name")]
    if not names:
        names = [b.get("name") for b in ((live_rec or {}).get("bots") or []) if b.get("name")]
    guids = {c.get("guid") for c in (rec.get("comp") or []) if c.get("guid")}

    log_dir = Path(args.log_dir).expanduser() if args.log_dir else data_dir
    slices = []
    untimestamped = []
    all_lines = []
    if not args.no_logs and start_s:
        matcher = build_matcher(names, [wanted, plan_id])
        for path in candidate_logs(log_dir, win_start):
            lines, meta = slice_log(path, win_start, win_end, matcher, args.max_lines)
            if meta["first"] is None:
                if meta["scanned"]:
                    untimestamped.append(meta)
                continue
            slices.append((lines, meta))
            all_lines.extend(lines)
        all_lines.sort(key=lambda ln: ln[0])

    if args.json:
        print(json.dumps({
            "runId": wanted, "record": rec or None, "live": live_rec,
            "plan": plan, "planLive": plan_live,
            "window": {"startS": start_s, "endS": end_s, "bots": names,
                       "serverSessionStartS": session_start(slices)},
            "logs": [{"file": m["name"], "matched": m["matched"], "opensAtS": m["first"],
                      "lines": [{"t": t, "level": lv, "msg": b} for t, lv, b in ls]}
                     for ls, m in slices],
        }, indent=1, default=str))
        return

    print("\n".join(render_header(rec, live_rec, plan, plan_live, start_s, end_s)))
    print(hr("PARTY"));            print("\n".join(render_comp(rec, live_rec)))
    print(hr("BOSSES"));           print("\n".join(render_bosses(rec, live_rec)))
    extras = render_extras(rec)
    if extras:
        print(hr("EVENT EXTRAS"));  print("\n".join(extras))
    print(hr("DEATHS"));           print("\n".join(render_deaths(rec)))
    print(hr("LOOT"));             print("\n".join(render_loot(rec)))
    print(hr("PULLS"));            print("\n".join(render_pulls(rec)))
    print(hr("STATUS TIMELINE"));  print("\n".join(render_status_timeline(rec, live_rec, args.timeline)))
    print(hr("PAUSES"));           print("\n".join(render_pauses(rec)))

    wd = rec.get("watchdog") or {}
    if wd:
        print(hr("WATCHDOG BUDGETS"))
        print("  " + " · ".join(f"{k} {v}s" for k, v in wd.items()))
    fp = rec.get("finalTankPos") or {}
    if fp:
        print(f"  final tank pos: map {fp.get('map')} "
              f"({fp.get('x',0):.1f}, {fp.get('y',0):.1f}, {fp.get('z',0):.1f})")

    if not args.no_diag:
        print(hr("DIAG SNAPSHOT"))
        print("\n".join(render_diag(rec)))

    if plan_id:
        print(hr("PLAN CONTEXT"))
        print("\n".join(render_plan(plan, plan_live, {r.get("runId"): r for r in runs}, wanted)))

    if not args.no_logs:
        session_s = session_start(slices)
        print(hr("LOGS"))
        print("\n".join(render_log_summary(slices, win_start, win_end, names, session_s, start_s)))
        if untimestamped:
            print("\n".join(render_untimestamped(log_dir, untimestamped)))
        event_lines = render_event_lines(all_lines, args.notable)
        if event_lines:
            print(hr("EVENT LINES (" + ", ".join(EVENT_TAGS) + ")"))
            print("\n".join(event_lines))
        print(hr("SIGNALS"))
        print("\n".join(render_signals(all_lines)))
        print(hr("NOTABLE LOG LINES (INFO+)"))
        print("\n".join(render_notable(all_lines, args.notable)))
        if args.chatter:
            print(hr("REPEATED CHATTER"))
            print("\n".join(render_chatter(all_lines, args.chatter)))
        if guids:
            print(hr("DECISION CAPTURES"))
            print("\n".join(render_decisions(data_dir, guids)))

        want_dump = args.logs is not None or args.grep or args.level
        if want_dump:
            pat = re.compile(args.grep) if args.grep else None
            floor = LEVEL_RANK.get((args.level or "").upper(), 0)
            picks = [s for s in slices
                     if not args.logs or any(n.lower() in s[1]["name"].lower() for n in args.logs)]
            for lines, meta in picks:
                sel = [ln for ln in lines
                       if (not pat or pat.search(ln[2])) and LEVEL_RANK.get(ln[1], 0) >= floor]
                if args.head:
                    sel = sel[:args.head]
                if args.tail:
                    sel = sel[-args.tail:]
                print(hr(f"{meta['name']} — {len(sel)} lines"))
                for stamp, level, body in sel:
                    print(f"  {clock(stamp)} {level:<5} {body}")

        if args.dump:
            out_dir = Path(args.dump).expanduser()
            out_dir.mkdir(parents=True, exist_ok=True)
            for lines, meta in slices:
                dest = out_dir / f"{wanted}.{meta['name']}"
                with dest.open("w", encoding="utf-8") as fh:
                    for stamp, level, body in lines:
                        fh.write(f"{ts(stamp)} {level} {body}\n")
                print(f"  wrote {dest} ({len(lines)} lines)")

    if not args.no_logs and slices:
        session_s = session_start(slices)
        if session_s and start_s and session_s > start_s + 2:
            print(f"\n  NOTE: this run started {ts(start_s)}, but the current server session\n"
                  f"        only began {ts(session_s)}. Worldserver reopens every appender with\n"
                  "        mode 'w', so the run's log evidence was wiped by that restart — the\n"
                  "        record and the diag snapshot above are all that survive.")


if __name__ == "__main__":
    main()
