"""Minimal realm status: enough for a header orb and honest empty-states,
never enough to need privileges.

Three configurable checks ([realm] status_check), plus two signals that are
always read: catalogue presence (has the module ever written its sidecar?)
and live-heartbeat freshness (is anything running right now?).
"""

import json
import time

from fastapi import APIRouter

from .. import __version__
from ..context import ctx
from ..hostenv import process_probe_argv, process_probe_running
from ..util import run_cmd
from .runs import read_live

router = APIRouter()

CACHE_S = 5

_cache = {"t": 0.0, "data": None}


async def _systemd_state(unit):
    rc, out = await run_cmd(["systemctl", "show", unit,
                             "-p", "ActiveState,SubState,ActiveEnterTimestamp"])
    if rc != 0:
        return "UNKNOWN", ""
    props = dict(l.split("=", 1) for l in out.splitlines() if "=" in l)
    active = props.get("ActiveState", "")
    since = props.get("ActiveEnterTimestamp", "")
    if active == "active":
        return "ONLINE", since
    if active == "failed":
        return "FAILED", since
    if active in ("activating", "deactivating", "reloading"):
        return active.upper(), since
    return "OFFLINE", since


async def _process_state(name):
    """pgrep on POSIX, tasklist on Windows — see hostenv."""
    argv, needle = process_probe_argv(name)
    rc, out = await run_cmd(argv)
    running = process_probe_running(rc, out, needle)
    return ("ONLINE" if running else "OFFLINE"), ""


async def _realm_state(cfg):
    mode = cfg.resolved_status_check()
    if mode == "systemd" and cfg.realm_unit:
        return await _systemd_state(cfg.realm_unit)
    if mode == "process":
        return await _process_state(cfg.process_name)
    return "UNKNOWN", ""


@router.get("/api/status")
async def api_status():
    """The one payload the header polls: realm up/down, sidecar freshness,
    live run/plan counts, and the config problems banner."""
    now = time.time()
    if _cache["data"] is not None and now - _cache["t"] < CACHE_S:
        return _cache["data"]

    cfg = ctx.cfg
    realm, since = await _realm_state(cfg)

    catalogue = False
    try:
        cat = json.loads(cfg.testdungeons_file.read_text(encoding="utf-8",
                                                         errors="replace"))
        catalogue = bool(cat.get("dungeons"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        pass

    live = read_live()
    # A fresh heartbeat is proof of life regardless of what the configured
    # check said — trust the stronger signal.
    if live["runs"] or live["plans"]:
        realm = "ONLINE"

    data = {
        "realm": realm,
        "since": since,
        "statusCheck": cfg.resolved_status_check(),
        "catalogue": catalogue,
        "liveRuns": len(live["runs"]),
        "livePlans": len(live["plans"]),
        "bridge": cfg.bridge_type,
        "health": cfg.health(),
        "version": __version__,
    }
    _cache.update(t=now, data=data)
    return data
