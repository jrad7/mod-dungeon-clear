"""GET /api/status: the configurable realm check + sidecar signals."""

import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import testdeck.routes.status as S  # noqa: E402


def fresh_cache():
    S._cache.update(t=0.0, data=None)


def test_status_none_mode_relies_on_sidecars(client, cfg):
    fresh_cache()
    body = client.get("/api/status").json()
    assert body["realm"] == "UNKNOWN"
    assert body["catalogue"] is False
    assert body["liveRuns"] == 0


def test_status_process_mode(client, cfg, monkeypatch):
    fresh_cache()
    cfg.status_check = "process"

    async def fake_run(argv, cwd=None, timeout=20):
        assert argv[:2] == ["pgrep", "-x"]
        return 0, "1234\n"
    monkeypatch.setattr(S, "run_cmd", fake_run)
    assert client.get("/api/status").json()["realm"] == "ONLINE"


def test_status_systemd_mode(client, cfg, monkeypatch):
    fresh_cache()
    cfg.status_check = "systemd"
    cfg.realm_unit = "ac-worldserver"

    async def fake_run(argv, cwd=None, timeout=20):
        assert argv[0] == "systemctl"
        return 0, "ActiveState=active\nSubState=running\nActiveEnterTimestamp=today\n"
    monkeypatch.setattr(S, "run_cmd", fake_run)
    body = client.get("/api/status").json()
    assert body["realm"] == "ONLINE" and body["since"] == "today"


def test_fresh_heartbeat_overrides_unknown(client, cfg):
    fresh_cache()
    cfg.testrun_live_file.write_text(json.dumps(
        {"active": True, "ts": int(time.time()),
         "runs": [{"runId": "tr-1"}], "plans": []}))
    body = client.get("/api/status").json()
    assert body["realm"] == "ONLINE"       # proof of life beats "UNKNOWN"
    assert body["liveRuns"] == 1
