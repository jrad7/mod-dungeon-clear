"""The API surface: catalogue validation, command construction, live
timeline accrual, roster guards, history clears. The bridge is stubbed to a
recorder; sidecar files are written into the throwaway tree."""

import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from testdeck import bridge as B          # noqa: E402
from testdeck.context import ctx          # noqa: E402
from testdeck.routes.runs import TimelineStore  # noqa: E402

CATALOGUE = {
    "limits": {"maxConcurrent": 5, "maxPlans": 3, "planMaxTotal": 50},
    "gearDefaults": {"ilvl": 0, "quality": 0},
    "qualities": [{"v": 1, "label": "normal"}, {"v": 4, "label": "epic"}],
    "dungeons": [
        {"token": "blackfathom", "name": "Blackfathom Deeps", "mapId": 48,
         "level": 24, "heroicLevel": 0, "wing": "",
         "gear": [{"ilvl": 25, "label": "budget"}, {"ilvl": 40, "label": "rich"}]},
        {"token": "mechanar", "name": "The Mechanar", "mapId": 554,
         "level": 70, "heroicLevel": 70, "wing": "",
         "gear": [{"ilvl": 100, "label": "normal"}],
         "gearHeroic": [{"ilvl": 115, "label": "heroic"}]},
    ],
}


def write_catalogue(cfg):
    cfg.testdungeons_file.write_text(json.dumps(CATALOGUE))


class RecordingBridge:
    """Answers every command with a canned exact reply and records it."""

    def __init__(self, lines=None, ok=True):
        self.cmds = []
        self.lines = lines or ["done"]
        self.ok = ok

    async def exec(self, cmd):
        self.cmds.append(cmd)
        return B.BridgeReply(self.ok, True, list(self.lines))

    async def probe(self):
        return True, ""


def use_bridge(lines=None, ok=True):
    br = RecordingBridge(lines, ok)
    ctx.bridge = br
    return br


# ---------------------------------------------------------------------------
# Single-run start
# ---------------------------------------------------------------------------


def test_run_start_builds_command(client, cfg):
    write_catalogue(cfg)
    br = use_bridge(["Test run started"])
    r = client.post("/api/testruns/start",
                    json={"dungeon": "blackfathom", "level": 24, "seed": 7,
                          "ilvl": 25, "quality": 4})
    assert r.status_code == 200, r.text
    assert br.cmds == [".dc test start blackfathom level=24 seed=7 ilvl=25 quality=4"]
    body = r.json()
    assert body["pending"] is False and body["ok"] is True


def test_run_start_pending_detected(client, cfg):
    write_catalogue(cfg)
    use_bridge(["test driver 'Dcdriver' is logging in — retry in a few seconds"])
    r = client.post("/api/testruns/start", json={"dungeon": "blackfathom"})
    assert r.status_code == 200
    assert r.json()["pending"] is True


def test_run_start_refusals(client, cfg):
    write_catalogue(cfg)
    br = use_bridge()
    bad = [
        {"dungeon": "nope"},
        {"dungeon": "blackfathom", "heroic": True},          # no heroic mode
        {"dungeon": "blackfathom", "ilvl": 33},              # not on ladder
        {"dungeon": "blackfathom", "quality": 9},
        {"dungeon": "blackfathom", "level": 99},
    ]
    for payload in bad:
        assert client.post("/api/testruns/start", json=payload).status_code == 400
    assert br.cmds == []


def test_run_start_without_catalogue_is_503(client, cfg):
    use_bridge()
    r = client.post("/api/testruns/start", json={"dungeon": "blackfathom"})
    assert r.status_code == 503


# ---------------------------------------------------------------------------
# Plans
# ---------------------------------------------------------------------------


def test_plan_start_builds_command(client, cfg):
    write_catalogue(cfg)
    br = use_bridge()
    r = client.post("/api/testplans/start",
                    json={"dungeon": "mechanar", "total": 10, "concurrent": 2,
                          "heroic": True, "ilvl": 115})
    assert r.status_code == 200, r.text
    assert br.cmds == [".dc test plan start mechanar total=10 heroic "
                       "concurrent=2 ilvl=115"]


def test_plan_total_capped_by_catalogue(client, cfg):
    write_catalogue(cfg)
    use_bridge()
    r = client.post("/api/testplans/start",
                    json={"dungeon": "blackfathom", "total": 51})
    assert r.status_code == 400
    assert "<= 50" in r.json()["detail"]


def test_plan_heroic_ladder_selected(client, cfg):
    """heroic + normal-ladder ilvl must be refused (the ladders differ)."""
    write_catalogue(cfg)
    use_bridge()
    r = client.post("/api/testplans/start",
                    json={"dungeon": "mechanar", "total": 1, "heroic": True,
                          "ilvl": 100})
    assert r.status_code == 400


def test_stop_run_and_plan_validate_ids(client, cfg):
    br = use_bridge()
    assert client.post("/api/testruns/stop",
                       json={"runId": "tr-20260101-000000-1"}).status_code == 200
    assert client.post("/api/testruns/stop",
                       json={"runId": "rm -rf /"}).status_code == 400
    assert client.post("/api/testplans/stop",
                       json={"planId": "all"}).status_code == 200
    assert br.cmds == [".dc test stop tr-20260101-000000-1",
                       ".dc test plan stop all"]


# ---------------------------------------------------------------------------
# Live + timeline
# ---------------------------------------------------------------------------


def write_live(cfg, runs=None, plans=None, ts=None, active=True):
    cfg.testrun_live_file.write_text(json.dumps({
        "active": active, "ts": ts if ts is not None else int(time.time()),
        "runs": runs or [], "plans": plans or []}))


def test_live_carries_accumulated_timeline(client, cfg):
    run = {"runId": "tr-1", "recent": [{"t": 1, "state": "A", "detail": "x"}]}
    write_live(cfg, runs=[run])
    r1 = client.get("/api/testruns/live").json()
    assert r1["runs"][0]["timeline"] == [{"t": 1, "state": "A", "detail": "x"}]
    # Heartbeat window slides; the accumulated timeline keeps both entries.
    run["recent"] = [{"t": 2, "state": "B", "detail": "y"}]
    write_live(cfg, runs=[run])
    r2 = client.get("/api/testruns/live").json()
    assert [e["state"] for e in r2["runs"][0]["timeline"]] == ["A", "B"]


def test_stale_heartbeat_reads_as_idle(client, cfg):
    write_live(cfg, runs=[{"runId": "tr-1"}], ts=int(time.time()) - 60)
    live = client.get("/api/testruns/live").json()
    assert live == {"runs": [], "plans": []}


def test_timeline_store_dedupes_and_caps():
    tl = TimelineStore()
    tl.accrue([{"runId": "r", "recent": [{"t": 1, "state": "A", "detail": ""}]}])
    tl.accrue([{"runId": "r", "recent": [{"t": 1, "state": "A", "detail": ""},
                                         {"t": 2, "state": "B", "detail": ""}]}])
    assert len(tl.rows("r")) == 2


# ---------------------------------------------------------------------------
# History + clear
# ---------------------------------------------------------------------------


def test_history_tails_newest_first(client, cfg):
    cfg.testruns_file.write_text(
        '{"runId": "tr-1"}\n{"runId": "tr-2"}\nnot json\n')
    runs = client.get("/api/testruns").json()["runs"]
    assert [r["runId"] for r in runs] == ["tr-2", "tr-1"]


def test_clear_refused_while_live(client, cfg):
    write_live(cfg, runs=[{"runId": "tr-1"}])
    assert client.post("/api/testruns/clear").status_code == 409


def test_clear_truncates_without_sudo(client, cfg):
    write_live(cfg, active=False)
    cfg.testruns_file.write_text('{"runId": "tr-1"}\n')
    r = client.post("/api/testruns/clear")
    assert r.status_code == 200, r.text
    assert r.json()["cleared"] == 1
    assert cfg.testruns_file.read_text() == ""
    assert cfg.testruns_file.with_suffix(".jsonl.bak").exists()


# ---------------------------------------------------------------------------
# Rosters
# ---------------------------------------------------------------------------

MEMBERS = ["Tanky", "Healy", "Dpsa", "Dpsb", "Dpsc"]


def test_roster_save_load_delete(client, cfg):
    r = client.post("/api/rosters", json={"name": "alpha", "members": MEMBERS})
    assert r.status_code == 200
    got = client.get("/api/rosters").json()["rosters"]
    assert got == [{"name": "alpha", "members": MEMBERS, "owner": "Tester",
                    "mine": True, "writable": True}]
    assert client.delete("/api/rosters/alpha").status_code == 200


def test_roster_name_guards(client):
    bad_sets = [
        MEMBERS[:4],                                  # wrong count
        MEMBERS[:4] + ["Tanky"],                      # duplicate
        MEMBERS[:4] + ["Bad'Name"],                   # injection charset
        MEMBERS[:4] + ["X"],                          # too short
    ]
    for members in bad_sets:
        r = client.post("/api/rosters", json={"name": "x", "members": members})
        assert r.status_code == 400, members


def test_roster_start_happy_path(client, cfg, monkeypatch):
    write_catalogue(cfg)
    br = use_bridge()

    async def fake_query(which, sql):
        if "FROM characters c" in sql and "c.name IN" in sql:
            return [[n, "24", "1", "0", "5", "acct"] for n in MEMBERS]
        if "account_instance_times" in sql:
            return []
        return []

    import testdeck.routes.roster as R
    monkeypatch.setattr(R, "mysql_query", fake_query)
    r = client.post("/api/testruns/start-roster",
                    json={"dungeon": "blackfathom", "members": MEMBERS})
    assert r.status_code == 200, r.text
    assert br.cmds == [".dc test start blackfathom "
                       "party=Tanky,Healy,Dpsa,Dpsb,Dpsc"]
    assert r.json()["roles"]["Tanky"] == "tank"


def test_roster_start_refuses_online_and_missing(client, cfg, monkeypatch):
    write_catalogue(cfg)
    use_bridge()

    async def fake_query(which, sql):
        if "c.name IN" in sql:
            rows = [[n, "24", "1", "0", "5", "acct"] for n in MEMBERS[:-1]]
            rows[0][3] = "1"                     # Tanky is online
            return rows                          # Dpsc missing entirely
        return []

    import testdeck.routes.roster as R
    monkeypatch.setattr(R, "mysql_query", fake_query)
    r = client.post("/api/testruns/start-roster",
                    json={"dungeon": "blackfathom", "members": MEMBERS})
    assert r.status_code == 400                  # missing reported first
    assert "Dpsc" in r.json()["detail"]


def test_character_search_is_case_insensitive(client, monkeypatch):
    """`characters.name` is utf8mb4_bin, so the raw LIKE was case-sensitive and
    typing a name in lower case found nothing. The needle is normalized to the
    Capitalized form the core stores, whatever case was typed."""
    seen = []

    async def fake_query(which, sql):
        seen.append(sql)
        return []

    import testdeck.routes.roster as R
    monkeypatch.setattr(R, "mysql_query", fake_query)
    for typed in ("tanky", "TANKY", "TaNkY", "Tanky"):
        seen.clear()
        r = client.get(f"/api/characters?search={typed}")
        assert r.status_code == 200, r.text
        assert "c.name LIKE 'Tanky%'" in seen[0], typed


def test_specs_count_points_not_talents(monkeypatch):
    """The triple is POINTS SPENT, not talents taken.

    A talent is stored at its current rank only — ranks 1 and 2 of a 3/3 talent
    are deleted when rank 3 is learned — so one character_talent row can be
    worth up to five points. Counting rows reported 24/2/3 for a 55/8/8 warrior.
    """
    import asyncio

    import testdeck.routes.roster as R

    # spell -> (tab, rank), the shape talent_tables() builds from Talent.dbc.
    spell_tab = {101: (1, 5),     # one 5/5 talent in the deep tree
                 102: (1, 3),     # one 3/3
                 201: (2, 1)}     # one 1/1 elsewhere
    tab_info = {1: {"name": "Arms", "page": 0}, 2: {"name": "Fury", "page": 1}}
    monkeypatch.setattr(R, "_talent_cache", (spell_tab, tab_info))

    async def fake_query(which, sql):
        return [["7", "101"], ["7", "102"], ["7", "201"]]

    monkeypatch.setattr(R, "mysql_query", fake_query)
    out = asyncio.run(R.character_specs([7]))
    assert out[7]["points"] == [8, 1, 0]         # not [2, 1, 0]
    assert out[7]["spec"] == "Arms"              # the tree holding the points
