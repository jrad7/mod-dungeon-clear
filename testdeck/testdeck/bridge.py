"""The worldserver command bridge — pluggable, because Test Deck must not
assume how someone else's worldserver is launched.

Three transports, selected by `[bridge] type`:

  soap    The worldserver's built-in SOAP interface (SOAP.Enabled = 1).
          Synchronous: the reply IS the command's real output (`exact=True`).
          Works however the server is launched; needs no privileges at all.

  screen  The worldserver runs in a GNU screen session. The command is typed
          in with `screen -X stuff` and the console tail is read back with
          `hardcopy` — the reply is mostly history (`exact=False`); callers
          find the echo of their own command to know where their reply starts.

  tmux    Same idea with `tmux send-keys` / `capture-pane`.

`use_sudo` applies to screen/tmux only, for sessions owned by another user
(e.g. worldserver running as root). Off by default: nothing here assumes
passwordless sudo exists.

Every `.dc test …` the routes issue goes through `ctx.bridge.exec()`.
"""

import asyncio
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field

from fastapi import HTTPException

from .util import run_cmd

# The screen hardcopy scratch file. It lives in data_dir — a 0700 directory
# owned by the Test Deck user — and NOT in /tmp, which is the whole point.
#
# With use_sudo the file is written by ROOT and read back by us. In a
# world-writable directory any local user could pre-plant a symlink at a
# known constant path and have root's `hardcopy` write console text through
# it (clobbering, say, a systemd unit), or have our `sudo cat` hand them the
# contents of a root-only file in an HTTP reply. A 0700 directory nobody else
# can create entries in removes the plant.
#
# It is derived from the config rather than being a constant because the
# sudoers rules are generated from this same function — `python3 -m testdeck
# sudoers` fills @HARDCOPY@ in from it, so the two can never drift.
HARDCOPY_NAME = "screen-hardcopy.txt"


def hardcopy_path(cfg):
    return cfg.data_dir / HARDCOPY_NAME

# How long to wait after stuffing a command before reading the screen/tmux
# console back. Long enough for the worldserver's reply to have been printed,
# short enough that a form submit still feels immediate.
REPLY_DELAY_S = 0.8

# The console tail handed back to callers of the fuzzy transports.
TAIL_LINES = 35

MAX_CMD_LEN = 300


@dataclass
class BridgeReply:
    ok: bool
    exact: bool                      # True: lines are exactly this command's output
    lines: list = field(default_factory=list)

    def text(self):
        return "\n".join(self.lines)


def _check_cmd(cmd):
    cmd = cmd.strip()
    if not cmd or len(cmd) > MAX_CMD_LEN or any(c in cmd for c in "\r\n"):
        raise HTTPException(400, "bad command")
    return cmd


class SoapBridge:
    """POST an executeCommand envelope to the worldserver's SOAP port.

    The core's contract (src/server/apps/worldserver/ACSoap/ACSoap.cpp):
    HTTP Basic auth against a real account that must be SEC_ADMINISTRATOR;
    success returns <result> with the command's printed output; a failed
    command comes back as a SOAP fault whose faultstring is that output.
    Namespace ns1 = "urn:AC".
    """

    def __init__(self, cfg):
        self.url = cfg.soap_url
        self.user = cfg.soap_user
        self.password = cfg.resolved_soap_pass()

    ENVELOPE = (
        '<?xml version="1.0" encoding="utf-8"?>'
        '<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/"'
        ' xmlns:ns1="urn:AC">'
        "<SOAP-ENV:Body><ns1:executeCommand><command>{cmd}</command>"
        "</ns1:executeCommand></SOAP-ENV:Body></SOAP-ENV:Envelope>"
    )

    def _post(self, cmd):
        import base64
        import urllib.error
        import urllib.request

        body = self.ENVELOPE.format(
            cmd=cmd.replace("&", "&amp;").replace("<", "&lt;")).encode("utf-8")
        req = urllib.request.Request(self.url, data=body, method="POST")
        cred = base64.b64encode(f"{self.user}:{self.password}".encode()).decode()
        req.add_header("Authorization", f"Basic {cred}")
        req.add_header("Content-Type", "text/xml; charset=utf-8")
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                return resp.status, resp.read()
        except urllib.error.HTTPError as e:
            # gSOAP answers faults (and auth refusals) with non-200 statuses;
            # the body is still the envelope we want to read.
            return e.code, e.read()

    @staticmethod
    def _text_of(payload, *localnames):
        try:
            root = ET.fromstring(payload)
        except ET.ParseError:
            return None
        for el in root.iter():
            tag = el.tag.rsplit("}", 1)[-1]
            if tag in localnames:
                return el.text or ""
        return None

    async def exec(self, cmd):
        cmd = _check_cmd(cmd)
        try:
            status, payload = await asyncio.to_thread(self._post, cmd)
        except OSError as e:
            raise HTTPException(
                503, f"cannot reach the worldserver SOAP endpoint at "
                     f"{self.url}: {e} — is the server running and "
                     "SOAP.Enabled = 1?")
        if status == 401:
            raise HTTPException(
                502, "the SOAP bridge account was refused (401) — check "
                     "[bridge] soap_user / soap_pass")
        if status == 403:
            raise HTTPException(
                502, "the SOAP bridge account is not SEC_ADMINISTRATOR (403) "
                     "— run: account set gmlevel <soap_user> 3 -1")
        out = self._text_of(payload, "result")
        if out is not None:
            return BridgeReply(True, True, out.splitlines())
        fault = self._text_of(payload, "faultstring")
        if fault is not None:
            # A refused command, with its real message — that IS the reply.
            return BridgeReply(False, True, fault.splitlines())
        raise HTTPException(502, f"unexpected SOAP response (HTTP {status})")

    async def probe(self):
        """(ok, message) for `check` and /api/status."""
        try:
            reply = await self.exec("server info")
        except HTTPException as e:
            return False, e.detail
        return reply.ok, reply.lines[0] if reply.lines else ""


class _SessionBridge:
    """Shared shape of the screen and tmux transports: type the command into
    the session the worldserver console lives in, wait, read the tail back."""

    tool = ""

    def __init__(self, cfg):
        self.cfg = cfg
        self.use_sudo = cfg.use_sudo

    def _argv(self, *argv):
        return ["sudo", "-n", *argv] if self.use_sudo else list(argv)

    async def _alive(self):
        raise NotImplementedError

    async def _send(self, cmd):
        raise NotImplementedError

    async def _read_tail(self):
        raise NotImplementedError

    async def exec(self, cmd):
        cmd = _check_cmd(cmd)
        ok, why = await self._alive()
        if not ok:
            raise HTTPException(503, why)
        rc, out = await self._send(cmd)
        if rc != 0:
            raise HTTPException(500, f"{self.tool} send failed: {out.strip()[:200]}")
        await asyncio.sleep(REPLY_DELAY_S)
        lines = await self._read_tail()
        while lines and not lines[-1]:
            lines.pop()
        return BridgeReply(True, False, lines[-TAIL_LINES:])

    async def probe(self):
        ok, why = await self._alive()
        return ok, "" if ok else why


class ScreenBridge(_SessionBridge):
    tool = "screen"

    async def _alive(self):
        rc, out = await run_cmd(self._argv("screen", "-ls"))
        import re
        session = self.cfg.screen_session
        if re.search(rf"[.]{re.escape(session)}[\s(]", out):
            return True, ""
        return False, (f"worldserver screen session '{session}' not found — "
                       "is the server running? ([bridge] screen_session)")

    async def _send(self, cmd):
        return await run_cmd(self._argv(
            "screen", "-S", self.cfg.screen_session, "-p", "0",
            "-X", "stuff", cmd + "\r"))

    async def _read_tail(self):
        path = hardcopy_path(self.cfg)
        await run_cmd(self._argv(
            "screen", "-S", self.cfg.screen_session, "-p", "0",
            "-X", "hardcopy", str(path)))
        text = ""
        # Belt and braces on top of the 0700 directory: a hardcopy that came
        # back as a symlink is not something to follow, whoever managed it.
        if path.is_symlink():
            await run_cmd(self._argv("rm", "-f", str(path)))
            return []
        try:
            text = path.read_text(errors="replace")
        except PermissionError:
            # screen wrote it as another user; read it the same way.
            _, text = await run_cmd(self._argv("cat", str(path)))
        except OSError:
            pass
        await run_cmd(self._argv("rm", "-f", str(path)))
        return [l.rstrip() for l in text.splitlines()]


class TmuxBridge(_SessionBridge):
    tool = "tmux"

    async def _alive(self):
        target = self.cfg.tmux_target
        rc, out = await run_cmd(self._argv("tmux", "has-session", "-t", target))
        if rc == 0:
            return True, ""
        return False, (f"tmux session '{target}' not found — is the server "
                       "running? ([bridge] tmux_target)")

    async def _send(self, cmd):
        # -l sends the command literally (no key-name interpretation), then
        # Enter submits it as its own key.
        rc, out = await run_cmd(self._argv(
            "tmux", "send-keys", "-t", self.cfg.tmux_target, "-l", cmd))
        if rc != 0:
            return rc, out
        return await run_cmd(self._argv(
            "tmux", "send-keys", "-t", self.cfg.tmux_target, "Enter"))

    async def _read_tail(self):
        rc, out = await run_cmd(self._argv(
            "tmux", "capture-pane", "-p", "-t", self.cfg.tmux_target))
        if rc != 0:
            return []
        return [l.rstrip() for l in out.splitlines()]


def make_bridge(cfg):
    return {"soap": SoapBridge, "screen": ScreenBridge,
            "tmux": TmuxBridge}[cfg.bridge_type](cfg)


# ---------------------------------------------------------------------------
# Reply interpretation, shared by the routes
# ---------------------------------------------------------------------------

# What the module answers when the headless test driver is still logging in
# (DcTestDriver.cpp): the start did NOT happen and the caller should retry.
PENDING_MARKERS = ("is logging in", "retry in a few seconds")


def reply_lines(reply, cmd):
    """The lines that belong to `cmd`. Exact transports return their whole
    reply; fuzzy ones scan for the last echo of the command and return what
    follows it (the tail is mostly history)."""
    if reply.exact:
        return reply.lines
    for i in range(len(reply.lines) - 1, -1, -1):
        if cmd in reply.lines[i]:
            return reply.lines[i + 1:]
    return reply.lines


def is_pending(reply, cmd):
    """True when the worldserver said the test driver is still logging in."""
    return any(m in l for l in reply_lines(reply, cmd) for m in PENDING_MARKERS)


def public_reply(reply, cmd):
    """The JSON shape every command route returns."""
    lines = reply_lines(reply, cmd)
    return {"ok": reply.ok, "exact": reply.exact, "cmd": cmd,
            "pending": is_pending(reply, cmd), "reply": lines[-TAIL_LINES:]}
