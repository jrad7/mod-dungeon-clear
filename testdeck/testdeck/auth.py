"""GM-account login, signed session cookies, and the CSRF check.

Threat model, stated plainly so the choices below can be judged against it:

  * Test Deck issues privileged worldserver commands (`.dc test …`) and reads
    the character database. It is meant to live on a LAN and be handed to a
    small circle of testers.
  * The realistic attackers are (a) anything else on that LAN and (b) a
    browser on a tester's machine visiting a hostile page that tries to POST
    here. It is NOT designed to be exposed to the internet; if you must, put
    it behind a reverse proxy with TLS.

Unlike the operator dashboards this grew out of, there is no shared password:
login is a real game account, verified against the auth database's SRP6
salt+verifier (see srp6.py), and gated on GM level (account_access). That
gives each tester their own credentials, lets the server audit-log who
started what, and means revoking one person is `account set gmlevel <u> 0 -1`.

Implementation notes:

  * The session cookie is `b64(user).aid.gmlevel.sid.expiry.hmac`, signed with
    a secret persisted in data_dir. Stateless, so sessions survive restarts;
    delete the secret file to invalidate everything at once.
  * Authorization does NOT trust the gmlevel in the cookie. Every /api/*
    request re-reads the account's gmlevel and ban state from the auth
    database (cached ACCOUNT_STATE_TTL_S per account), so `account set gmlevel
    <u> 0 -1` locks someone out within the minute — which is what this file
    always claimed revocation was, and now is. A database outage falls back to
    the cookie's own DB-verified value rather than locking the realm out.
  * `secure` is set on the cookie only when the request arrived over HTTPS.
    Setting it unconditionally on a plain-HTTP LAN deck would make the browser
    drop the cookie and login would silently not work.
  * The login throttle is per-IP AND per-username, so a storm against one
    account from many addresses is still slowed.
  * Comparisons are constant-time (hmac.compare_digest) throughout, and an
    unknown account still pays for a verifier computation so the miss cannot
    be told from a wrong password by a stopwatch.
"""

import base64
import hashlib
import hmac
import re
import secrets
import time
from collections import deque
from dataclasses import dataclass

from fastapi import APIRouter, HTTPException, Request, Response
from pydantic import BaseModel

from . import srp6
from .context import ctx
from .mysql import mysql_query, sql_int, sql_str

router = APIRouter()

COOKIE_NAME = "tdeck"
CSRF_HEADER = "x-testdeck"

# The one user-typed value that reaches SQL. AzerothCore account names are
# ASCII, max 20 chars (Utf8ToUpperOnlyLatin + the account create validation);
# this allowlist is the injection guard for the mysql-CLI transport — every
# permitted character is inert inside a single-quoted SQL literal.
USERNAME_RE = re.compile(r"^[A-Za-z0-9_.-]{1,20}$")

# Fed to the verifier check when no account row came back, purely so the
# unknown-account path costs the same modular exponentiation as the
# wrong-password one. Both answer with the same 401; without this they are
# still told apart by a stopwatch, which is the whole attack the shared
# message exists to prevent.
_TIMING_SALT = bytes(32)
_TIMING_VERIFIER = bytes(32)


def _b64(raw):
    return base64.urlsafe_b64encode(raw).decode().rstrip("=")


def _unb64(s):
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


# ---------------------------------------------------------------------------
# Account verification (shared by the login route and `check-auth`)
# ---------------------------------------------------------------------------


@dataclass
class VerifyResult:
    ok: bool
    stage: str = ""          # where it failed: db / account / password / gmlevel
    message: str = ""
    account_id: int = 0
    gmlevel: int = -1


async def verify_account(username, password, cfg=None):
    """The full check: account row exists, SRP6 verifier matches, gmlevel
    clears the bar. Returns a VerifyResult; never raises for a bad login."""
    cfg = cfg or ctx.cfg
    if not USERNAME_RE.fullmatch(username or ""):
        return VerifyResult(False, "account", "invalid account name")
    try:
        rows = await mysql_query(
            "auth",
            "SELECT id, HEX(salt), HEX(verifier) FROM account "
            f"WHERE username = {sql_str(username)}")
    except (RuntimeError, OSError, TimeoutError) as e:
        return VerifyResult(False, "db", f"auth database unavailable: {e}")
    if rows is None:
        return VerifyResult(False, "db",
                            "auth database is not configured (worldserver_conf "
                            "missing or has no LoginDatabaseInfo)")
    if not rows:
        srp6.check_login(username, password, _TIMING_SALT, _TIMING_VERIFIER)
        return VerifyResult(False, "account", "unknown account")
    try:
        account_id = int(rows[0][0])
        salt = bytes.fromhex(rows[0][1])
        verifier = bytes.fromhex(rows[0][2])
    except (ValueError, IndexError):
        return VerifyResult(False, "db", "unexpected account row shape")

    if not srp6.check_login(username, password, salt, verifier):
        return VerifyResult(False, "password", "wrong password",
                            account_id=account_id)

    try:
        gmlevel, banned = await account_state(account_id, cfg, fresh=True)
    except (RuntimeError, OSError, TimeoutError) as e:
        return VerifyResult(False, "db", f"auth database unavailable: {e}",
                            account_id=account_id)
    if banned:
        return VerifyResult(False, "banned", "this account is banned",
                            account_id=account_id, gmlevel=gmlevel)
    if gmlevel < cfg.min_gmlevel:
        return VerifyResult(
            False, "gmlevel",
            f"this account has GM level {gmlevel}, but Test Deck requires "
            f"{cfg.min_gmlevel} — grant it with: account set gmlevel "
            f"{username} {cfg.min_gmlevel} -1",
            account_id=account_id, gmlevel=gmlevel)
    return VerifyResult(True, account_id=account_id, gmlevel=gmlevel)


# ---------------------------------------------------------------------------
# Live account state
# ---------------------------------------------------------------------------
#
# The cookie is signed, so the gmlevel inside it cannot be forged — but it is
# a snapshot of what was true at login, and a session lasts hours. That made
# the revocation story this module advertises ("revoking one person is
# `account set gmlevel <u> 0 -1`") false in practice: the demoted tester kept
# every privilege until their session expired.
#
# So authorization reads the database, not the cookie. One query per account
# per TTL keeps that affordable — each call is a `mysql` subprocess, so a
# per-request round trip would be real overhead on a page that polls.

ACCOUNT_STATE_TTL_S = 60

_state_cache = {}        # account_id -> (checked_at, gmlevel, banned)


def reset_state_cache():
    """For tests, and for anything that wants the next check to be live."""
    _state_cache.clear()


async def account_state(account_id, cfg=None, fresh=False):
    """(gmlevel, banned) for an account, cached for ACCOUNT_STATE_TTL_S.

    Raises the underlying error if the database cannot be reached and there is
    nothing cached; callers decide what an outage means for them.
    """
    cfg = cfg or ctx.cfg
    account_id = int(account_id)
    now = time.time()
    hit = _state_cache.get(account_id)
    if not fresh and hit and now - hit[0] < ACCOUNT_STATE_TTL_S:
        return hit[1], hit[2]

    try:
        rows = await mysql_query(
            "auth",
            "SELECT (SELECT COALESCE(MAX(gmlevel), 0) FROM account_access "
            f"        WHERE id = {sql_int(account_id)} "
            f"          AND (RealmID = -1 OR RealmID = {sql_int(cfg.realm_id)})), "
            "       (SELECT COUNT(*) FROM account_banned "
            f"        WHERE id = {sql_int(account_id)} AND active = 1)")
    except (RuntimeError, OSError, TimeoutError):
        if hit:
            # A database blip must not log the realm's testers out; the last
            # known answer was itself DB-verified.
            return hit[1], hit[2]
        raise
    gmlevel = int(rows[0][0]) if rows and rows[0] and rows[0][0] else 0
    banned = bool(rows and len(rows[0]) > 1 and rows[0][1] not in ("", "0", None))
    _state_cache[account_id] = (now, gmlevel, banned)
    return gmlevel, banned


# ---------------------------------------------------------------------------
# Session secret + cookie
# ---------------------------------------------------------------------------


def session_secret(cfg):
    """Persisted, so a restart does not log everybody out. Generated on first
    use with 0600 permissions."""
    import os
    path = cfg.secret_file
    try:
        raw = path.read_bytes()
        if len(raw) >= 32:
            return raw
    except OSError:
        pass
    cfg.data_dir.mkdir(parents=True, exist_ok=True)
    raw = secrets.token_bytes(32)
    path.write_bytes(raw)
    os.chmod(path, 0o600)
    return raw


def make_token(cfg, username, account_id, gmlevel):
    """`b64(user).aid.gmlevel.sid.expiry.hmac`. Stateless: the signature is the
    whole check, so there is no session table to grow or lose on restart. The
    username is base64ed so its charset can never confuse the dot-parsing.

    The account id rides along because authorization needs it — "may this
    tester run that character?" is a question about accounts — and looking it
    up per request would be another subprocess. The gmlevel is still here for
    audit lines and for the DB-outage fallback, but it is no longer what
    authorization reads; see account_state().
    """
    sid = secrets.token_urlsafe(12)
    expiry = int(time.time() + cfg.session_hours * 3600)
    body = (f"{_b64(username.encode())}.{int(account_id)}.{int(gmlevel)}"
            f".{sid}.{expiry}")
    sig = hmac.new(session_secret(cfg), body.encode(), hashlib.sha256).digest()
    return f"{body}.{_b64(sig)}"


@dataclass
class Session:
    username: str
    gmlevel: int
    account_id: int = 0


def parse_token(cfg, token):
    """The Session inside a valid cookie, else None.

    A token in the pre-account-id format no longer parses, so the security
    release logs everyone out once. That is the intended outcome, not a
    migration to paper over.
    """
    if not token:
        return None
    try:
        user_b64, aid, gmlevel, sid, expiry, sig = token.split(".")
        body = f"{user_b64}.{aid}.{gmlevel}.{sid}.{expiry}"
        want = hmac.new(session_secret(cfg), body.encode(), hashlib.sha256).digest()
        if not hmac.compare_digest(want, _unb64(sig)):
            return None
        if int(expiry) <= time.time():
            return None
        return Session(_unb64(user_b64).decode("utf-8"), int(gmlevel), int(aid))
    except (ValueError, TypeError):
        return None


def set_session_cookie(response, cfg, request, username, account_id, gmlevel):
    response.set_cookie(
        COOKIE_NAME, make_token(cfg, username, account_id, gmlevel),
        max_age=cfg.session_hours * 3600,
        httponly=True,        # JavaScript never needs to read it
        samesite="strict",    # and it never rides along on a cross-site request
        # Only when this request actually arrived over TLS. Setting it
        # unconditionally would be worse than not setting it: on the plain
        # HTTP a LAN deck serves, the browser would drop the cookie and login
        # would appear to succeed and then silently not work.
        secure=request.url.scheme == "https",
        path="/",
    )


def request_session(request):
    """The Session on this request, else None. Routes use this for the admin
    gate and for audit lines.

    Prefers the copy the middleware re-validated against the database over the
    one in the cookie, so a route's authorization decision is never made on a
    stale gmlevel. The check is a separate flag, not `is not None` on the
    session: a re-validation that REFUSED the cookie stores None, and falling
    back to the cookie there would undo the refusal."""
    if getattr(request.state, "session_checked", False):
        return request.state.session
    return parse_token(ctx.cfg, request.cookies.get(COOKIE_NAME))


async def live_session(request, cfg=None):
    """The session on this request with its gmlevel refreshed from the auth
    database, or None if the cookie is invalid or the account no longer
    qualifies (demoted below min_gmlevel, or banned).

    A database outage falls back to the cookie's own gmlevel rather than
    locking everyone out: that value was DB-verified at login, and a testing
    tool that dies with its database helps nobody.
    """
    cfg = cfg or ctx.cfg
    s = parse_token(cfg, request.cookies.get(COOKIE_NAME))
    if s is None:
        return None
    if not s.account_id:
        return s
    try:
        gmlevel, banned = await account_state(s.account_id, cfg)
    except (RuntimeError, OSError, TimeoutError):
        return s
    if banned or gmlevel < cfg.min_gmlevel:
        return None
    return Session(s.username, gmlevel, s.account_id)


def require_admin(request, what):
    """403 unless the session's gmlevel clears [auth] admin_gmlevel."""
    s = request_session(request)
    if s is None or s.gmlevel < ctx.cfg.admin_gmlevel:
        raise HTTPException(
            403, f"{what} requires GM level {ctx.cfg.admin_gmlevel}")
    return s


# ---------------------------------------------------------------------------
# Login rate limit
# ---------------------------------------------------------------------------


class LoginThrottle:
    """Per-key sliding window, keyed by IP and by username. In memory and
    therefore reset by a restart — fine: this exists to make a password
    guessing storm slow, not to be an audit trail."""

    def __init__(self, max_attempts, window_s):
        self.max = max_attempts
        self.window = window_s
        self._fails = {}      # key -> deque[timestamp]

    def _recent(self, key, create=False):
        """Timestamps still inside the window. Expiring a key drops it from
        the map entirely unless a caller is about to write to it: the map is
        keyed partly by attacker-chosen usernames, and an unauthenticated
        request is enough to add one, so it must not only ever grow."""
        q = self._fails.get(key)
        if q is None:
            if not create:
                return ()
            q = self._fails.setdefault(key, deque())
        cutoff = time.time() - self.window
        while q and q[0] < cutoff:
            q.popleft()
        if not q and not create:
            self._fails.pop(key, None)
        return q

    def blocked(self, *keys):
        return any(len(self._recent(k)) >= self.max for k in keys)

    def record_failure(self, *keys):
        for k in keys:
            self._recent(k, create=True).append(time.time())

    def clear(self, *keys):
        for k in keys:
            self._fails.pop(k, None)


# ---------------------------------------------------------------------------
# Middleware
# ---------------------------------------------------------------------------


async def auth_middleware(request: Request, call_next):
    """Every /api/* route except the login surface needs a valid session.

    Non-API paths (the SPA shell and its assets) are public — the app itself
    redirects to its login screen client-side. Sits behind restrict_to_lan,
    so an off-LAN request never reaches even that."""
    path = request.url.path
    if not path.startswith("/api/") or path == "/api/login":
        return await call_next(request)

    # Re-validated against the auth database (cached, see account_state), so a
    # demotion or a ban takes hold within the minute instead of at session
    # expiry. Stashed on the request so routes and audit lines read the same
    # live answer without querying again.
    session = await live_session(request)
    request.state.session = session
    request.state.session_checked = True

    # /api/session is what the SPA reads to decide whether to show the login
    # screen — it must answer, not 401, when there is no session.
    if path == "/api/session":
        return await call_next(request)

    if session is None:
        from fastapi.responses import JSONResponse
        return JSONResponse({"detail": "not logged in"}, status_code=401)

    # CSRF. SameSite=Strict already stops the cookie riding along on a
    # cross-site request; this is the second lock, and it is the one that
    # does not depend on the browser being recent. A cross-site form post
    # cannot set a custom header, and fetch() sets it for free.
    if request.method not in ("GET", "HEAD", "OPTIONS"):
        if request.headers.get(CSRF_HEADER) is None:
            from fastapi.responses import JSONResponse
            return JSONResponse({"detail": f"missing {CSRF_HEADER} header"},
                                status_code=403)

    return await call_next(request)


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------


class LoginRequest(BaseModel):
    username: str
    password: str


def _peer(request):
    return request.client.host if request.client else "?"


@router.get("/api/session")
async def api_session(request: Request):
    """What the SPA reads on boot to decide whether to show the login screen.
    Public by definition."""
    s = request_session(request)
    if s is None:
        return {"authenticated": False}
    return {"authenticated": True, "username": s.username, "gmlevel": s.gmlevel,
            "accountId": s.account_id,
            "admin": s.gmlevel >= ctx.cfg.admin_gmlevel}


@router.post("/api/login")
async def api_login(req: LoginRequest, request: Request, response: Response):
    cfg = ctx.cfg
    ip = _peer(request)
    username = req.username.strip()
    throttle = ctx.throttle

    if throttle.blocked(f"ip:{ip}", f"user:{username.lower()}"):
        raise HTTPException(
            429, f"too many failed logins — wait "
                 f"{max(1, cfg.login_window_s // 60)} minutes")

    res = await verify_account(username, req.password, cfg)
    if not res.ok:
        if res.stage == "db":
            # An operator problem, not a credential problem — say so.
            raise HTTPException(503, res.message)
        throttle.record_failure(f"ip:{ip}", f"user:{username.lower()}")
        print(f"testdeck: failed login for {username!r} from {ip} "
              f"({res.stage})", flush=True)
        if res.stage == "banned":
            raise HTTPException(403, res.message)
        if res.stage == "gmlevel":
            # The one specific refusal: the tester's password was right, and
            # the fix is actionable by the operator.
            raise HTTPException(403, res.message)
        # Unknown account and wrong password are deliberately the same
        # answer, so a probe cannot enumerate account names.
        raise HTTPException(401, "wrong username or password")

    throttle.clear(f"ip:{ip}", f"user:{username.lower()}")
    set_session_cookie(response, cfg, request, username, res.account_id,
                       res.gmlevel)
    print(f"testdeck: {username} logged in from {ip} "
          f"(gmlevel {res.gmlevel})", flush=True)
    return {"ok": True, "username": username, "gmlevel": res.gmlevel,
            "admin": res.gmlevel >= cfg.admin_gmlevel}


@router.post("/api/logout")
async def api_logout(response: Response):
    response.delete_cookie(COOKIE_NAME, path="/")
    return {"ok": True}
