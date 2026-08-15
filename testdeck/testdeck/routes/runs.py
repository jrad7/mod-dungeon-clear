"""mod-dungeon-clear test runs: single-run starts, the live heartbeat, the
finished-run history, and the accumulated status timeline between the two.

Sidecar files (written by the module into the worldserver cwd):

    dc_testrun_live.json   overwritten every ~2s while anything is running
    dc_testruns.jsonl      one line per finished run
"""

import asyncio
import json
import re
import time

from fastapi import APIRouter, HTTPException, Request
from pydantic import BaseModel

from .. import bridge as bridge_mod
from ..auth import request_session, require_admin
from ..context import ctx
from ..util import clear_jsonl, tail_jsonl

router = APIRouter()

# A heartbeat older than this means the worldserver died mid-run; the card
# should empty out rather than show a frozen run forever.
HEARTBEAT_STALE_S = 15


def audit(request, action):
    s = request_session(request)
    who = s.username if s else "?"
    print(f"testdeck: {who}: {action}", flush=True)


def read_live():
    """Raw read of the heartbeat the mod-dungeon-clear manager overwrites
    every ~2s while any run or plan is active. A stale heartbeat (worldserver
    crash mid-run) or an inactive/missing file reads as nothing live."""
    try:
        data = json.loads(
            ctx.cfg.testrun_live_file.read_text(encoding="utf-8", errors="replace"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return {"runs": [], "plans": []}
    if not data.get("active") or time.time() - data.get("ts", 0) > HEARTBEAT_STALE_S:
        return {"runs": [], "plans": []}
    return {"runs": data.get("runs", []), "plans": data.get("plans", [])}


class TimelineStore:
    """Full status timeline per live run.

    The heartbeat is a *window*: the module trims run.recent to the last 8
    entries, so the whole history of a run only exists as the union of every
    heartbeat ever written. The finished-run record carries the complete
    statusTimeline, but that only lands when the run ends — this is what lets
    a card show the whole log while the run is still going.

    Accumulating in the server rather than the browser means the timeline
    survives a page reload, is shared by every open tab, and keeps filling
    while nobody is watching.
    """

    MAX_ROWS = 4000            # ~11h of one entry/10s; a run is capped far below
    KEEP_AFTER_END_S = 600

    def __init__(self):
        self._runs = {}        # runId -> {"rows": [...], "keys": set, "seen": ts}

    def accrue(self, runs):
        """Merge this heartbeat's window into each run's accumulated timeline."""
        now = time.time()
        for run in runs:
            run_id = run.get("runId")
            if not run_id:
                continue
            tl = self._runs.setdefault(run_id, {"rows": [], "keys": set(), "seen": now})
            tl["seen"] = now
            for e in run.get("recent") or []:
                # (t, state, detail) is the identity: heartbeats overlap
                # heavily, and two genuinely distinct entries in the same
                # second with the same text are indistinguishable to a reader
                # anyway.
                key = (e.get("t"), e.get("state"), e.get("detail"))
                if key in tl["keys"]:
                    continue
                tl["keys"].add(key)
                tl["rows"].append(e)
            if len(tl["rows"]) > self.MAX_ROWS:
                dropped = tl["rows"][:-self.MAX_ROWS]
                tl["rows"] = tl["rows"][-self.MAX_ROWS:]
                for e in dropped:
                    tl["keys"].discard((e.get("t"), e.get("state"), e.get("detail")))
        # Drop runs that ended a while ago; their history lives in the JSONL.
        for run_id in [k for k, v in self._runs.items()
                       if now - v["seen"] > self.KEEP_AFTER_END_S]:
            self._runs.pop(run_id, None)

    def rows(self, run_id):
        tl = self._runs.get(run_id)
        return tl["rows"] if tl else None


async def loop_timeline():
    """Poll the heartbeat at its own write cadence. The frontend polls at 5s
    and the module keeps only 8 lines, so a browser-driven merge would lose
    entries during a burst of state changes."""
    while True:
        try:
            ctx.timelines.accrue(read_live()["runs"])
        except Exception as e:                        # noqa: BLE001
            print(f"testdeck: timeline loop error: {e}", flush=True)
        await asyncio.sleep(2)


@router.get("/api/testruns/live")
async def api_testruns_live():
    """In-progress test runs + plans, each run carrying "timeline": the full
    accumulated status history rather than the heartbeat's 8-line window."""
    live = read_live()
    ctx.timelines.accrue(live["runs"])   # stay correct even if the poll loop died
    for run in live["runs"]:
        rows = ctx.timelines.rows(run.get("runId"))
        if rows:
            run["timeline"] = rows
    return live


@router.get("/api/testruns")
async def api_testruns(limit: int = 100):
    """Test-run history: tail dc_testruns.jsonl, newest first."""
    return {"runs": tail_jsonl(ctx.cfg.testruns_file, limit)}


async def refuse_if_live(what):
    """Clearing history while a run/plan is mid-flight would drop the rows the
    live cards are about to be reconciled against — make the user stop first."""
    live = await api_testruns_live()
    if live["runs"] or live["plans"]:
        raise HTTPException(409, f"test runs or plans are still active — stop "
                                 f"them before clearing {what}")


class RunStartRequest(BaseModel):
    dungeon: str
    heroic: bool = False
    level: int = 0
    seed: int = 0
    # Gear ceiling: 0 = inherit the server's AiPlayerbot.AutoGear* values,
    # -1 = no limit, >0 = that item level. quality is 0 (inherit) or 1..5.
    ilvl: int = 0
    quality: int = 0


@router.post("/api/testruns/start")
async def api_testruns_start(req: RunStartRequest, request: Request):
    """Start ONE pool run (randomised 5-bot comp) at a dungeon.

    The worldserver may answer that the headless test driver is still logging
    in — the start did NOT happen and the reply carries pending=true; the
    frontend retries, or falls back to a plan of total=1 (plans wait the
    driver out server-side)."""
    from .plans import catalogue_rows, check_dungeon, check_gear

    _cat, rows = await catalogue_rows()
    check_dungeon(rows, req.dungeon, req.heroic)
    if not 0 <= req.level <= 80:
        raise HTTPException(400, "level must be 0..80")
    if req.seed < 0:
        raise HTTPException(400, "seed must be >= 0")
    check_gear(rows, req.dungeon, req.heroic, req.ilvl, req.quality)

    cmd = f".dc test start {req.dungeon}"
    if req.heroic:
        cmd += " heroic"
    if req.level:
        cmd += f" level={req.level}"
    if req.seed:
        cmd += f" seed={req.seed}"
    if req.ilvl:
        cmd += " ilvl=none" if req.ilvl == -1 else f" ilvl={req.ilvl}"
    if req.quality:
        cmd += f" quality={req.quality}"
    audit(request, cmd)
    reply = await ctx.bridge.exec(cmd)
    return bridge_mod.public_reply(reply, cmd)


class RunStopRequest(BaseModel):
    runId: str


@router.post("/api/testruns/stop")
async def api_testruns_stop(req: RunStopRequest, request: Request):
    """Abort one live run (exact runId, e.g. tr-20260719-113000-1) or 'all'.

    Deliberately NOT the same as stopping a plan: `.dc test stop all` also
    stops every active plan first, but a single-run stop leaves its plan
    running — the scheduler simply launches a replacement, which is what
    "stop this run" means inside a campaign. The UI says so in its confirm."""
    if req.runId != "all" and not re.fullmatch(r"tr-[A-Za-z0-9-]{1,40}", req.runId):
        raise HTTPException(400, "bad runId")
    cmd = f".dc test stop {req.runId}"
    audit(request, cmd)
    reply = await ctx.bridge.exec(cmd)
    return bridge_mod.public_reply(reply, cmd)


@router.post("/api/testruns/clear")
async def api_testruns_clear(request: Request):
    """Wipe dc_testruns.jsonl (backed up to dc_testruns.jsonl.bak)."""
    require_admin(request, "clearing run history")
    await refuse_if_live("run history")
    audit(request, "clear run history")
    return await clear_jsonl(ctx.cfg.testruns_file, "test-run history",
                             use_sudo=ctx.cfg.use_sudo)
