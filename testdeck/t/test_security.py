"""Regressions for the three holes the pre-release security audit found.

Each test here is a working exploit that used to succeed. They are grouped in
their own file rather than folded into test_routes/test_auth because what they
assert is not "the feature works" but "this specific attack does not" — and
that distinction is worth keeping visible to whoever edits these routes next.
"""

import asyncio
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from conftest import make_client                  # noqa: E402
from testdeck.app import create_app, host_allowed, split_host   # noqa: E402


# ---------------------------------------------------------------------------
# Unauthenticated arbitrary file read through the SPA catch-all
# ---------------------------------------------------------------------------
#
# `dist / "/etc/passwd"` is `/etc/passwd`: joining an absolute path throws the
# left side away, and the old guard only scanned for "..". The route is public
# by design (the login screen has to load), so this was any file the server
# user could open, to anyone who could reach the port.


def raw_get(app, path, client=("192.168.1.50", 51000)):
    """GET with the path put into the ASGI scope verbatim.

    An HTTP client normalises "//etc/passwd" into a netloc long before it
    becomes a request, but curl --path-as-is, a raw socket and (via %2F) a
    browser fetch() all deliver it intact — so the scope is where this has to
    be exercised.
    """
    out = []

    async def receive():
        return {"type": "http.request", "body": b"", "more_body": False}

    async def send(message):
        out.append(message)

    async def go():
        await app({
            "type": "http", "asgi": {"version": "3.0"}, "http_version": "1.1",
            "method": "GET", "scheme": "http", "path": path,
            "raw_path": path.encode(), "query_string": b"", "root_path": "",
            "headers": [(b"host", b"127.0.0.1:8999")], "client": client,
            "server": ("127.0.0.1", 8999),
        }, receive, send)

    asyncio.run(go())
    status = next(m["status"] for m in out if m["type"] == "http.response.start")
    body = b"".join(m.get("body", b"") for m in out
                    if m["type"] == "http.response.body")
    return status, body


@pytest.mark.parametrize("attack", [
    "//etc/passwd",              # absolute POSIX path
    "//etc/hostname",
    "/C:/Windows/win.ini",       # drive-absolute; a single slash is enough
    "//attacker.example.com/share/x",   # UNC — an outbound SMB fetch
])
def test_spa_refuses_absolute_paths(cfg, attack):
    """Anything resolving outside dist/ falls through to index.html."""
    app = create_app(cfg, start_collectors=False)
    status, body = raw_get(app, attack)
    assert status == 200
    assert body == b"<!doctype html><title>DC Test Deck</title>"


def test_spa_refuses_the_session_secret(cfg):
    """The specific read that turned this into full compromise: the signing
    secret is enough to mint a cookie with any gmlevel."""
    secret = cfg.data_dir / "session.secret"
    secret.write_bytes(b"S" * 32)
    app = create_app(cfg, start_collectors=False)
    status, body = raw_get(app, "/" + str(secret))
    assert status == 200
    assert b"SSSS" not in body


def test_spa_refuses_the_worldserver_conf(cfg):
    """worldserver.conf carries the credentials for all three databases."""
    app = create_app(cfg, start_collectors=False)
    status, body = raw_get(app, "/" + str(cfg.worldserver_conf))
    assert status == 200
    assert b"LoginDatabaseInfo" not in body


def test_spa_refuses_dot_segments(cfg):
    with make_client(cfg) as c:
        r = c.get("/assets/../../../../etc/passwd")
        assert b"root:" not in r.content


def test_spa_refuses_symlink_out_of_dist(cfg, tmp_path):
    """Containment is checked after resolve(), so a symlink planted inside
    dist/ is not a way out either."""
    outside = tmp_path / "outside.txt"
    outside.write_text("secret")
    link = cfg.web_dist / "escape.txt"
    try:
        link.symlink_to(outside)
    except (OSError, NotImplementedError):
        pytest.skip("this host cannot create symlinks")
    with make_client(cfg) as c:
        assert b"secret" not in c.get("/escape.txt").content


def test_spa_still_serves_real_files(cfg):
    """The fix must not break the thing the route is for."""
    (cfg.web_dist / "assets" / "app.js").write_text("console.log(1)")
    with make_client(cfg) as c:
        r = c.get("/assets/app.js")
        assert r.status_code == 200 and b"console.log(1)" in r.content
        # and a client-routed path is still the SPA shell
        assert b"DC Test Deck" in c.get("/live").content


# ---------------------------------------------------------------------------
# DNS rebinding: the Host check
# ---------------------------------------------------------------------------
#
# allowed_nets cannot see this attack. The browser making the request really
# is on the LAN; what it has been told is that a name the attacker controls
# lives at this address. Rebinding needs a NAME, so addresses stay free and
# names have to be listed.


@pytest.mark.parametrize("header,ok", [
    ("127.0.0.1:8999", True),
    ("127.0.0.1", True),
    ("192.168.1.50:8790", True),
    ("localhost:8790", True),
    ("[::1]:8790", True),
    ("evil.example.com", False),
    ("evil.example.com:8790", False),
    ("", False),
    (None, False),
])
def test_host_allowed(cfg, header, ok):
    assert host_allowed(cfg, header) is ok


def test_allowed_hosts_lets_a_named_proxy_through(cfg):
    cfg.allowed_hosts = {"deck.example.com"}
    assert host_allowed(cfg, "deck.example.com:8790")
    assert host_allowed(cfg, "DECK.EXAMPLE.COM")      # names are case-insensitive
    assert not host_allowed(cfg, "other.example.com")


def test_split_host_strips_the_port_not_the_v6_address():
    assert split_host("192.168.1.5:8790") == "192.168.1.5"
    assert split_host("[fe80::1]:8790") == "[fe80::1]"
    assert split_host("[fe80::1]") == "[fe80::1]"


def test_rebound_host_is_refused_before_login(cfg, monkeypatch):
    """The check sits outside authentication: a rebinding page cannot even
    reach the login endpoint, let alone an API route."""
    from conftest import TEST_PASSWORD, TEST_USER, fake_auth_db
    from fastapi.testclient import TestClient

    fake_auth_db(monkeypatch)
    app = create_app(cfg, start_collectors=False)
    with TestClient(app, client=("192.168.1.50", 51000),
                    base_url="http://evil.example.com") as c:
        r = c.post("/api/login", json={"username": TEST_USER,
                                       "password": TEST_PASSWORD})
        assert r.status_code == 403
        assert c.get("/api/status").status_code == 403
        assert c.get("/").status_code == 403


# ---------------------------------------------------------------------------
# The screen bridge's hardcopy scratch file
# ---------------------------------------------------------------------------
#
# With use_sudo the hardcopy is written by root and read back by us. At a
# constant path in a world-writable /tmp, any local user could plant a symlink
# there and aim root's write — or our privileged read — wherever they liked.


def test_hardcopy_lives_in_the_private_data_dir(cfg):
    from testdeck.bridge import hardcopy_path

    path = hardcopy_path(cfg)
    assert path.parent == cfg.data_dir
    assert str(path) != "/tmp/ac-testdeck-hardcopy.txt"     # the old constant
    assert not hasattr(__import__("testdeck.bridge", fromlist=["x"]),
                       "HARDCOPY_PATH")


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX mode bits")
def test_data_dir_is_private(cfg):
    """0700 is what makes the path above unplantable, so it is asserted here
    rather than left as a comment."""
    assert (cfg.data_dir.stat().st_mode & 0o777) == 0o700


def test_hardcopy_symlink_is_not_followed(cfg, tmp_path):
    """Belt and braces behind the 0700 directory: if a hardcopy ever comes
    back as a symlink, its target is not read."""
    from testdeck.bridge import ScreenBridge, hardcopy_path

    target = tmp_path / "root-only.txt"
    target.write_text("credentials")
    link = hardcopy_path(cfg)
    try:
        link.symlink_to(target)
    except (OSError, NotImplementedError):
        pytest.skip("this host cannot create symlinks")

    bridge = ScreenBridge(cfg)
    # `screen` is not running here, so the hardcopy call is a no-op and the
    # planted link is all _read_tail finds — exactly the attacker's setup.
    assert asyncio.run(bridge._read_tail()) == []


def test_sudoers_names_the_new_hardcopy_path(cfg, capsys):
    """The rules are generated from the same function the bridge calls, so
    they cannot drift apart — and must no longer mention /tmp."""
    from testdeck.__main__ import cmd_sudoers
    from testdeck.bridge import hardcopy_path

    cfg.bridge_type = "screen"
    cfg.use_sudo = True

    class Args:
        user = "acore"

    assert cmd_sudoers(cfg, Args()) == 0
    out = capsys.readouterr().out
    assert str(hardcopy_path(cfg)) in out
    assert "/tmp/ac-testdeck-hardcopy.txt" not in out


# ---------------------------------------------------------------------------
# Response headers
# ---------------------------------------------------------------------------


def test_security_headers_on_every_response(cfg):
    with make_client(cfg) as c:
        for path in ("/", "/live", "/api/status"):
            h = c.get(path).headers
            assert "frame-ancestors 'none'" in h["content-security-policy"]
            assert "'unsafe-inline'" not in h["content-security-policy"]
            assert h["x-frame-options"] == "DENY"
            assert h["x-content-type-options"] == "nosniff"
            assert h["referrer-policy"] == "no-referrer"


def test_security_headers_survive_a_rejection(cfg):
    """The two filters return before any inner layer runs, so the header
    middleware has to be the outermost one to stamp their 403s."""
    from fastapi.testclient import TestClient

    app = create_app(cfg, start_collectors=False)
    with TestClient(app, client=("8.8.8.8", 51000),
                    base_url="http://127.0.0.1:8999") as c:
        r = c.get("/")                      # refused by restrict_to_lan
        assert r.status_code == 403
        assert r.headers["x-frame-options"] == "DENY"


# ---------------------------------------------------------------------------
# SQL escaping
# ---------------------------------------------------------------------------
#
# Every caller still validates against an allowlist; this is the layer that
# means a caller which forgets to is not immediately an injection.


@pytest.mark.parametrize("raw,want", [
    ("Tanky", "'Tanky'"),
    ("O'Brien", "'O''Brien'"),
    ("a\\b", "'a\\\\b'"),
    ("line\nbreak", "'line\\nbreak'"),
    ("' OR 1=1 --", "''' OR 1=1 --'"),
    ("\x00", "'\\0'"),
])
def test_sql_str_escapes(raw, want):
    from testdeck.mysql import sql_str

    assert sql_str(raw) == want


def test_sql_str_cannot_leave_the_literal():
    """The property that matters, stated as a property: whatever goes in, the
    result is one balanced literal — an odd number of quotes would mean the
    value had escaped it."""
    from testdeck.mysql import sql_str

    for raw in ["'", "''", "\\'", "\\\\'", "x' UNION SELECT 1 -- ", "\\"]:
        out = sql_str(raw)
        assert out.startswith("'") and out.endswith("'")
        assert (out.count("'") - 2) % 2 == 0, raw


def test_sql_int_refuses_text():
    from testdeck.mysql import sql_int

    assert sql_int(7) == "7"
    assert sql_int("7") == "7"
    for bad in ["1 OR 1=1", "", None, "0x41"]:
        with pytest.raises((ValueError, TypeError)):
            sql_int(bad)


def test_sql_ident_quotes_and_doubles_backticks():
    from testdeck.mysql import sql_ident

    assert sql_ident("acore_auth") == "`acore_auth`"
    assert sql_ident("we`ird") == "`we``ird`"


def test_username_with_a_quote_never_reaches_the_query(cfg, monkeypatch):
    """Belt (USERNAME_RE) and braces (sql_str): the regex refuses it first, so
    no query is issued at all."""
    from testdeck import auth as tdauth

    seen = []

    async def spy(which, sql):
        seen.append(sql)
        return []

    monkeypatch.setattr(tdauth, "mysql_query", spy)
    with make_client(cfg) as c:
        c.headers["X-TestDeck"] = "1"
        r = c.post("/api/login", json={"username": "a' OR 1=1 -- ",
                                       "password": "x"})
        assert r.status_code == 401
    assert seen == []


# ---------------------------------------------------------------------------
# Session: live gmlevel, ban, Secure flag, lifetime
# ---------------------------------------------------------------------------


def test_demotion_takes_effect_without_waiting_for_expiry(cfg, monkeypatch):
    """The finding: gmlevel was baked into the cookie, so `account set gmlevel
    <u> 0 -1` did nothing until the session expired."""
    from conftest import TEST_ACCOUNT_ID, TEST_PASSWORD, TEST_USER, fake_auth_db
    from testdeck import auth as tdauth

    fake_auth_db(monkeypatch, gmlevel=3)
    tdauth.reset_state_cache()
    with make_client(cfg) as c:
        c.headers["X-TestDeck"] = "1"
        assert c.post("/api/login", json={"username": TEST_USER,
                                          "password": TEST_PASSWORD}).status_code == 200
        assert c.get("/api/status").status_code == 200

        fake_auth_db(monkeypatch, gmlevel=0)     # demoted on the server
        tdauth.reset_state_cache()               # TTL elapses
        assert c.get("/api/status").status_code == 401
        assert c.get("/api/session").json() == {"authenticated": False}
    assert TEST_ACCOUNT_ID                        # (the fake really keyed on it)


def test_admin_gate_follows_the_live_level_not_the_cookie(cfg, monkeypatch):
    from conftest import TEST_PASSWORD, TEST_USER, fake_auth_db
    from testdeck import auth as tdauth

    fake_auth_db(monkeypatch, gmlevel=3)
    tdauth.reset_state_cache()
    with make_client(cfg) as c:
        c.headers["X-TestDeck"] = "1"
        c.post("/api/login", json={"username": TEST_USER,
                                   "password": TEST_PASSWORD})
        assert c.get("/api/session").json()["admin"] is True

        fake_auth_db(monkeypatch, gmlevel=1)     # still allowed in, not admin
        tdauth.reset_state_cache()
        s = c.get("/api/session").json()
        assert s["authenticated"] is True and s["admin"] is False
        # and an admin-only route now refuses
        assert c.post("/api/testruns/clear").status_code == 403


def test_a_banned_account_loses_its_session(cfg, monkeypatch):
    from conftest import TEST_PASSWORD, TEST_USER, fake_auth_db
    from testdeck import auth as tdauth

    fake_auth_db(monkeypatch, gmlevel=3)
    tdauth.reset_state_cache()
    with make_client(cfg) as c:
        c.headers["X-TestDeck"] = "1"
        c.post("/api/login", json={"username": TEST_USER,
                                   "password": TEST_PASSWORD})

        async def banned(which, sql):
            if "account_banned" in sql:
                return [["3", "1"]]
            return []

        monkeypatch.setattr(tdauth, "mysql_query", banned)
        tdauth.reset_state_cache()
        assert c.get("/api/status").status_code == 401


def test_a_db_outage_does_not_log_everyone_out(cfg, monkeypatch):
    """Failing closed here would mean a database blip takes the whole realm's
    testers with it; the cookie's own level was DB-verified at login."""
    from conftest import TEST_PASSWORD, TEST_USER, fake_auth_db
    from testdeck import auth as tdauth

    fake_auth_db(monkeypatch, gmlevel=3)
    tdauth.reset_state_cache()
    with make_client(cfg) as c:
        c.headers["X-TestDeck"] = "1"
        c.post("/api/login", json={"username": TEST_USER,
                                   "password": TEST_PASSWORD})

        async def dead(which, sql):
            raise RuntimeError("can't connect to local MySQL server")

        monkeypatch.setattr(tdauth, "mysql_query", dead)
        tdauth.reset_state_cache()
        assert c.get("/api/status").status_code == 200


def test_cookie_flags(cfg, monkeypatch):
    from conftest import TEST_PASSWORD, TEST_USER, fake_auth_db

    fake_auth_db(monkeypatch)
    with make_client(cfg) as c:
        c.headers["X-TestDeck"] = "1"
        r = c.post("/api/login", json={"username": TEST_USER,
                                       "password": TEST_PASSWORD})
        setc = r.headers["set-cookie"]
        assert "HttpOnly" in setc
        assert "SameSite=strict" in setc
        # Not Secure over plain HTTP — a browser would drop the cookie and
        # login would appear to work and then not.
        assert "Secure" not in setc


def test_cookie_is_secure_over_https(cfg, monkeypatch):
    from conftest import TEST_PASSWORD, TEST_USER, fake_auth_db
    from fastapi.testclient import TestClient

    fake_auth_db(monkeypatch)
    app = create_app(cfg, start_collectors=False)
    with TestClient(app, client=("127.0.0.1", 51000),
                    base_url="https://127.0.0.1:8999") as c:
        c.headers["X-TestDeck"] = "1"
        r = c.post("/api/login", json={"username": TEST_USER,
                                       "password": TEST_PASSWORD})
        assert r.status_code == 200
        assert "Secure" in r.headers["set-cookie"]


def test_default_session_is_not_three_days():
    from testdeck.config import Config

    assert Config().session_hours == 12


def test_a_pre_account_id_token_is_refused(cfg):
    """The old 5-field format must not validate — its bearer would have no
    account id, and every ownership check would read 0."""
    import hashlib
    import hmac

    from testdeck import auth as tdauth

    body = f"{tdauth._b64(b'Tester')}.3.sid.{2 ** 31}"
    sig = hmac.new(tdauth.session_secret(cfg), body.encode(),
                   hashlib.sha256).digest()
    assert tdauth.parse_token(cfg, f"{body}.{tdauth._b64(sig)}") is None


def test_throttle_does_not_grow_without_bound():
    """The map is keyed partly by attacker-chosen usernames and an
    unauthenticated request is enough to add one."""
    from testdeck.auth import LoginThrottle

    t = LoginThrottle(max_attempts=5, window_s=0)
    for i in range(500):
        t.record_failure(f"user:{i}")
        t.blocked(f"user:{i}")             # expiring read drops the key
    assert len(t._fails) < 50


# ---------------------------------------------------------------------------
# Who may run whose characters
# ---------------------------------------------------------------------------


def roster_db(monkeypatch, account_id):
    """Character rows all owned by `account_id`."""
    from conftest import MEMBERS_DEFAULT
    from testdeck import mysql as tdmysql
    from testdeck.routes import roster as tdroster

    async def fake(which, sql):
        if "FROM characters c" in sql and "c.name IN" in sql:
            return [[n, "24", "1", "0", str(account_id), "someacct"]
                    for n in MEMBERS_DEFAULT]
        if "account_instance_times" in sql:
            return []
        return []

    monkeypatch.setattr(tdroster, "mysql_query", fake)
    monkeypatch.setattr(tdmysql, "mysql_query", fake)
    return fake


def test_non_admin_cannot_run_another_accounts_characters(cfg, monkeypatch):
    from conftest import (MEMBERS_DEFAULT, TEST_ACCOUNT_ID, TEST_PASSWORD,
                          TEST_USER, fake_auth_db)
    from test_routes import CATALOGUE, use_bridge
    from testdeck import auth as tdauth

    cfg.testdungeons_file.write_text(__import__("json").dumps(CATALOGUE))
    fake_auth_db(monkeypatch, gmlevel=1)         # a plain tester
    tdauth.reset_state_cache()
    with make_client(cfg) as c:
        c.headers["X-TestDeck"] = "1"
        c.post("/api/login", json={"username": TEST_USER,
                                   "password": TEST_PASSWORD})
        use_bridge()
        roster_db(monkeypatch, account_id=TEST_ACCOUNT_ID + 1)   # not theirs
        r = c.post("/api/testruns/start-roster",
                   json={"dungeon": "blackfathom", "members": MEMBERS_DEFAULT})
        assert r.status_code == 403
        assert "another account" in r.json()["detail"]


def test_a_tester_can_run_their_own_characters(cfg, monkeypatch):
    from conftest import (MEMBERS_DEFAULT, TEST_ACCOUNT_ID, TEST_PASSWORD,
                          TEST_USER, fake_auth_db)
    from test_routes import CATALOGUE, use_bridge
    from testdeck import auth as tdauth

    cfg.testdungeons_file.write_text(__import__("json").dumps(CATALOGUE))
    fake_auth_db(monkeypatch, gmlevel=1)
    tdauth.reset_state_cache()
    with make_client(cfg) as c:
        c.headers["X-TestDeck"] = "1"
        c.post("/api/login", json={"username": TEST_USER,
                                   "password": TEST_PASSWORD})
        br = use_bridge()
        roster_db(monkeypatch, account_id=TEST_ACCOUNT_ID)        # theirs
        r = c.post("/api/testruns/start-roster",
                   json={"dungeon": "blackfathom", "members": MEMBERS_DEFAULT})
        assert r.status_code == 200, r.text
        assert br.cmds and br.cmds[0].startswith(".dc test start blackfathom")


def test_an_admin_can_run_anyones_characters(cfg, monkeypatch):
    from conftest import (MEMBERS_DEFAULT, TEST_ACCOUNT_ID, TEST_PASSWORD,
                          TEST_USER, fake_auth_db)
    from test_routes import CATALOGUE, use_bridge
    from testdeck import auth as tdauth

    cfg.testdungeons_file.write_text(__import__("json").dumps(CATALOGUE))
    fake_auth_db(monkeypatch, gmlevel=3)
    tdauth.reset_state_cache()
    with make_client(cfg) as c:
        c.headers["X-TestDeck"] = "1"
        c.post("/api/login", json={"username": TEST_USER,
                                   "password": TEST_PASSWORD})
        use_bridge()
        roster_db(monkeypatch, account_id=TEST_ACCOUNT_ID + 99)
        r = c.post("/api/testruns/start-roster",
                   json={"dungeon": "blackfathom", "members": MEMBERS_DEFAULT})
        assert r.status_code == 200, r.text


def as_request(session):
    """The minimum a roster route reads off a Request: the middleware's
    already-validated session. Going through HTTP here would mean logging in
    as three different accounts to assert one rule."""
    req = type("Req", (), {"cookies": {}})()
    req.state = type("State", (), {})()
    req.state.session_checked = True
    req.state.session = session
    return req


def test_overwriting_another_testers_roster_needs_admin(cfg):
    """Creating is open to anyone; the roster list is shared, so editing
    somebody else's saved work is not."""
    from fastapi import HTTPException

    from conftest import MEMBERS_DEFAULT
    from testdeck.auth import Session
    from testdeck.context import ctx
    from testdeck.routes import roster as tdroster

    ctx.cfg = cfg
    tdroster.save_rosters({"alpha": {"members": MEMBERS_DEFAULT,
                                     "owner": "Someone"}})
    body = tdroster.RosterSaveRequest(name="alpha", members=MEMBERS_DEFAULT)

    with pytest.raises(HTTPException) as e:
        asyncio.run(tdroster.api_rosters_save(
            body, as_request(Session("Other", 1, 5))))
    assert e.value.status_code == 403
    assert "Someone" in e.value.detail

    # The owner may, and so may an admin.
    for s in (Session("Someone", 1, 5), Session("Admin", cfg.admin_gmlevel, 6)):
        assert asyncio.run(tdroster.api_rosters_save(body, as_request(s)))["ok"]


def test_deleting_another_testers_roster_needs_admin(cfg):
    from fastapi import HTTPException

    from conftest import MEMBERS_DEFAULT
    from testdeck.auth import Session
    from testdeck.context import ctx
    from testdeck.routes import roster as tdroster

    ctx.cfg = cfg
    tdroster.save_rosters({"alpha": {"members": MEMBERS_DEFAULT,
                                     "owner": "Someone"}})

    with pytest.raises(HTTPException) as e:
        asyncio.run(tdroster.api_rosters_delete(
            "alpha", as_request(Session("Other", 1, 5))))
    assert e.value.status_code == 403

    assert asyncio.run(tdroster.api_rosters_delete(
        "alpha", as_request(Session("Someone", 1, 5))))["ok"]


def test_legacy_bare_list_rosters_still_load(cfg):
    from testdeck.context import ctx
    from testdeck.routes import roster as tdroster

    ctx.cfg = cfg
    cfg.rosters_file.write_text('{"old": ["Tanky","Healy","Dpsa","Dpsb","Dpsc"]}')
    loaded = tdroster.load_rosters()
    assert loaded["old"]["members"][0] == "Tanky"
    assert loaded["old"]["owner"] == ""      # unclaimed: first saver owns it
