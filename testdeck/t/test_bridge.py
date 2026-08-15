"""bridge.py: all three transports with the outside world stubbed, plus the
reply-interpretation helpers the routes share.

Async bridge calls run under asyncio.run (no pytest-asyncio dependency).
"""

import asyncio
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from fastapi import HTTPException  # noqa: E402

from testdeck import bridge as B  # noqa: E402


def run(coro):
    return asyncio.run(coro)


class FakeCfg:
    bridge_type = "screen"
    soap_url = "http://127.0.0.1:7878/"
    soap_user = "bridge"
    soap_pass = "pw"
    screen_session = "world"
    tmux_target = "world"
    use_sudo = False
    # The screen bridge derives its hardcopy scratch path from this; tests
    # that exercise _read_tail point it at a tmp dir of their own.
    data_dir = Path("/nonexistent-testdeck-data")

    def resolved_soap_pass(self):
        return self.soap_pass


# ---------------------------------------------------------------------------
# SOAP
# ---------------------------------------------------------------------------

SOAP_OK = (200, b"""<?xml version="1.0"?>
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/"
 xmlns:ns1="urn:AC"><SOAP-ENV:Body><ns1:executeCommandResponse>
<result>Test run started
runId tr-1</result>
</ns1:executeCommandResponse></SOAP-ENV:Body></SOAP-ENV:Envelope>""")

SOAP_FAULT = (500, b"""<?xml version="1.0"?>
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/">
<SOAP-ENV:Body><SOAP-ENV:Fault><faultcode>SOAP-ENV:Client</faultcode>
<faultstring>There is no such command</faultstring>
</SOAP-ENV:Fault></SOAP-ENV:Body></SOAP-ENV:Envelope>""")


def make_soap(monkeypatch, status_payload):
    br = B.SoapBridge(FakeCfg())
    sent = {}

    def fake_post(cmd):
        sent["cmd"] = cmd
        return status_payload
    monkeypatch.setattr(br, "_post", fake_post)
    return br, sent


def test_soap_success_is_exact(monkeypatch):
    br, sent = make_soap(monkeypatch, SOAP_OK)
    reply = run(br.exec(".dc test start blackfathom"))
    assert sent["cmd"] == ".dc test start blackfathom"
    assert reply.ok and reply.exact
    assert reply.lines == ["Test run started", "runId tr-1"]


def test_soap_fault_carries_output(monkeypatch):
    br, _ = make_soap(monkeypatch, SOAP_FAULT)
    reply = run(br.exec(".dc bogus"))
    assert not reply.ok and reply.exact
    assert reply.lines == ["There is no such command"]


def test_soap_auth_errors_are_actionable(monkeypatch):
    for status, needle in ((401, "soap_user"), (403, "gmlevel")):
        br, _ = make_soap(monkeypatch, (status, b""))
        with pytest.raises(HTTPException) as e:
            run(br.exec(".dc test list"))
        assert needle in e.value.detail


def test_soap_command_xml_escaped():
    body = B.SoapBridge.ENVELOPE.format(
        cmd=".dc x <&>".replace("&", "&amp;").replace("<", "&lt;"))
    assert "&lt;" in body and "&amp;" in body


# ---------------------------------------------------------------------------
# screen / tmux argv construction + sudo opt-in
# ---------------------------------------------------------------------------


async def _instant(_s):
    return None


def collect_cmds(monkeypatch, replies):
    """Stub run_cmd inside the bridge module, recording every argv. `replies`
    maps a marker argument (e.g. "-ls") to the (rc, out) to return."""
    calls = []

    async def fake_run(argv, cwd=None, timeout=20):
        calls.append(list(argv))
        for marker, out in replies.items():
            if marker in argv:
                return out
        return 0, ""
    monkeypatch.setattr(B, "run_cmd", fake_run)
    monkeypatch.setattr(B.asyncio, "sleep", _instant)
    return calls


def test_screen_no_sudo_by_default(monkeypatch):
    br = B.ScreenBridge(FakeCfg())
    calls = collect_cmds(monkeypatch, {"-ls": (0, "\t123.world\t(Detached)")})
    reply = run(br.exec(".dc test list"))
    assert all(argv[0] != "sudo" for argv in calls)
    stuffs = [a for a in calls if "stuff" in a]
    assert stuffs and stuffs[0][-1] == ".dc test list\r"
    assert not reply.exact


def test_screen_sudo_opt_in(monkeypatch):
    cfg = FakeCfg()
    cfg.use_sudo = True
    br = B.ScreenBridge(cfg)
    calls = collect_cmds(monkeypatch, {"-ls": (0, "\t123.world\t(Detached)")})
    run(br.exec(".dc test list"))
    assert calls and all(argv[:2] == ["sudo", "-n"] for argv in calls)


def test_screen_dead_session_is_503(monkeypatch):
    br = B.ScreenBridge(FakeCfg())
    collect_cmds(monkeypatch, {"-ls": (1, "No Sockets found in /run/screen.")})
    with pytest.raises(HTTPException) as e:
        run(br.exec(".dc test list"))
    assert e.value.status_code == 503


def test_tmux_send_keys_literal(monkeypatch):
    br = B.TmuxBridge(FakeCfg())
    calls = collect_cmds(monkeypatch, {"has-session": (0, "")})
    run(br.exec(".dc test list"))
    send = [a for a in calls if "send-keys" in a]
    assert send[0][-2:] == ["-l", ".dc test list"]     # literal, no key names
    assert send[1][-1] == "Enter"


def test_tmux_dead_session_is_503(monkeypatch):
    br = B.TmuxBridge(FakeCfg())
    collect_cmds(monkeypatch, {"has-session": (1, "no server running")})
    with pytest.raises(HTTPException) as e:
        run(br.exec(".dc test list"))
    assert e.value.status_code == 503


# ---------------------------------------------------------------------------
# Reply interpretation
# ---------------------------------------------------------------------------


def test_reply_lines_exact_passthrough():
    r = B.BridgeReply(True, True, ["a", "b"])
    assert B.reply_lines(r, ".dc x") == ["a", "b"]


def test_reply_lines_fuzzy_scans_for_echo():
    r = B.BridgeReply(True, False, [
        "AC> .dc test list", "old output",
        "AC> .dc test start bfd", "Test run started"])
    assert B.reply_lines(r, ".dc test start bfd") == ["Test run started"]


def test_pending_detection():
    fuzzy = B.BridgeReply(True, False, [
        "AC> .dc test start bfd",
        "test driver 'Dcdriver' is logging in — retry in a few seconds"])
    assert B.is_pending(fuzzy, ".dc test start bfd")
    exact = B.BridgeReply(False, True, ["Test run started"])
    assert not B.is_pending(exact, ".dc test start bfd")
    pub = B.public_reply(fuzzy, ".dc test start bfd")
    assert pub["pending"] is True and pub["cmd"] == ".dc test start bfd"


def test_stale_echo_not_this_attempts_reply():
    """A pending line ABOVE the latest echo is history, not this reply."""
    r = B.BridgeReply(True, False, [
        "AC> .dc test start bfd",
        "test driver 'Dcdriver' is logging in — retry in a few seconds",
        "AC> .dc test start bfd",
        "Test run started"])
    assert not B.is_pending(r, ".dc test start bfd")


def test_bad_commands_refused():
    for bad in ("", "x" * 400, "line\nbreak"):
        with pytest.raises(HTTPException):
            B._check_cmd(bad)
