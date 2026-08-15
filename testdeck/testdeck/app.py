"""The app factory: build a FastAPI app around one Config.

Nothing here is import-time work — `create_app(cfg)` is the only entry point,
which is what lets the test suite stand up a second app against a temporary
config in the same process.
"""

import asyncio
import contextlib
import ipaddress

from fastapi import FastAPI, Request
from fastapi.responses import FileResponse, PlainTextResponse
from fastapi.staticfiles import StaticFiles

from . import __version__, auth, bridge as bridge_mod, context

# Host values that are always answered for, whatever the config says: a
# loopback browser is how the operator's own machine reaches the deck.
LOCAL_HOSTS = frozenset({"localhost", "127.0.0.1", "[::1]", "::1"})

# Sent on every response. The SPA is entirely self-hosted — Vite emits real
# .js/.css files and React sets styles through the CSSOM rather than as markup
# — so nothing here needs 'unsafe-inline' or 'unsafe-eval', and the policy can
# be the strict one. `data:` is for the favicon and inline SVG icons.
#
# frame-ancestors 'none' is the one that matters most for a tool like this:
# it stops a page a tester has open from framing the deck and driving it.
SECURITY_HEADERS = {
    "Content-Security-Policy": (
        "default-src 'self'; script-src 'self'; style-src 'self'; "
        "img-src 'self' data:; font-src 'self'; connect-src 'self'; "
        "object-src 'none'; base-uri 'none'; form-action 'self'; "
        "frame-ancestors 'none'"
    ),
    "X-Content-Type-Options": "nosniff",
    "X-Frame-Options": "DENY",           # for anything predating frame-ancestors
    "Referrer-Policy": "no-referrer",
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Resource-Policy": "same-origin",
}


def split_host(header):
    """The name part of a Host header, port removed. "" when there is none.

    IPv6 arrives bracketed ("[::1]:8790"); the brackets are kept, because the
    bracketed form is what a browser sends and what allowed_hosts is written
    with."""
    if not header:
        return ""
    host = header.strip()
    if host.startswith("["):                      # [::1]:8790 or [::1]
        end = host.find("]")
        return host[:end + 1] if end >= 0 else host
    return host.rsplit(":", 1)[0] if ":" in host else host


def host_allowed(cfg, header):
    """Whether this Host header is one this deck answers to.

    A bare IP literal always passes: DNS rebinding needs a NAME to re-point,
    so a client that dialled an address cannot be a rebinding victim — and an
    address is exactly what the startup banner tells testers to type. Names
    have to be named, in [server] allowed_hosts.
    """
    host = split_host(header)
    if not host:
        # HTTP/1.1 requires a Host header; something handmade is talking to us.
        return False
    if host.lower() in LOCAL_HOSTS:
        return True
    try:
        ipaddress.ip_address(host.strip("[]"))
        return True
    except ValueError:
        pass
    return host.lower() in cfg.allowed_hosts


def create_app(cfg, start_collectors=True):
    from .routes import logs, plans, roster, runs, status

    timelines = runs.TimelineStore()
    throttle = auth.LoginThrottle(cfg.login_max_attempts, cfg.login_window_s)
    bridge = bridge_mod.make_bridge(cfg)
    context.init(cfg, bridge=bridge, timelines=timelines, throttle=throttle)

    @contextlib.asynccontextmanager
    async def lifespan(_app):
        tasks = []
        if start_collectors:
            tasks = [asyncio.create_task(runs.loop_timeline())]
        try:
            yield
        finally:
            for t in tasks:
                t.cancel()

    app = FastAPI(title="DC Test Deck", version=__version__, lifespan=lifespan)

    # Middleware order: Starlette wraps each newly added layer AROUND the
    # previous ones, so the last registered runs first. Registering auth here
    # and restrict_to_lan below puts the address filter on the outside — an
    # off-LAN request never even reaches the login endpoint.
    app.middleware("http")(auth.auth_middleware)

    @app.middleware("http")
    async def restrict_to_lan(request: Request, call_next):
        """Reject any request that does not come from an allowed network.

        Kept in front of authentication as defence in depth: this app issues
        privileged worldserver commands, so narrowing who can even reach the
        login endpoint is worth the two lines."""
        peer = request.client.host if request.client else None
        try:
            addr = ipaddress.ip_address(peer)
        except (TypeError, ValueError):
            return PlainTextResponse("forbidden", status_code=403)
        if not any(addr in net for net in cfg.allowed_nets):
            return PlainTextResponse("forbidden", status_code=403)
        return await call_next(request)

    @app.middleware("http")
    async def check_host(request: Request, call_next):
        """Reject a request whose Host header is a name we do not answer to.

        This is the anti-DNS-rebinding lock, and it is the outermost one. The
        address filter above cannot see this attack: a browser on the tester's
        LAN is a legitimate source address, and a hostile page it visits can
        point a name it controls at this server and then talk to us with the
        page's own origin. Requiring the Host to be a literal IP (or an
        operator-named host) breaks that, because rebinding needs a NAME."""
        if not host_allowed(cfg, request.headers.get("host")):
            return PlainTextResponse("forbidden: unexpected Host header",
                                     status_code=403)
        return await call_next(request)

    @app.middleware("http")
    async def security_headers(request: Request, call_next):
        """Registered last, so it is the OUTERMOST layer and stamps every
        response — including the 403s the two filters below return, which
        never reach an inner layer at all."""
        resp = await call_next(request)
        for k, v in SECURITY_HEADERS.items():
            resp.headers.setdefault(k, v)
        return resp

    for r in (auth.router, status.router, runs.router, plans.router,
              roster.router, logs.router):
        app.include_router(r)

    # Vite emits content-hashed filenames under assets/, safe to cache
    # forever. index.html must always be revalidated — it is the pointer to
    # the current hashes.
    if (cfg.web_dist / "assets").is_dir():
        class ImmutableStatic(StaticFiles):
            def file_response(self, *args, **kwargs):
                resp = super().file_response(*args, **kwargs)
                resp.headers["Cache-Control"] = "public, max-age=31536000, immutable"
                return resp

        app.mount("/assets", ImmutableStatic(directory=str(cfg.web_dist / "assets")),
                  name="assets")

    @app.get("/{path:path}")
    async def spa(path: str):
        """The SPA: real files from dist/ when they exist, index.html for
        every client-routed path (/live, /roster, …).

        The containment check is resolve-then-compare, never a scan for
        ".." — that is a blocklist, and it missed the case that mattered:
        `dist / "/etc/passwd"` is `/etc/passwd`, because joining an ABSOLUTE
        path discards everything on the left. This route is public (the login
        screen has to be reachable), so that was an unauthenticated read of
        any file the server user can open, session.secret and
        worldserver.conf included. Resolving first also collapses symlinks
        and, on Windows, drive-absolute ("C:/…") and UNC ("//host/share")
        paths, which the string check never saw at all.
        """
        if path:
            root = cfg.web_dist.resolve()
            try:
                f = (root / path.lstrip("/\\")).resolve()
                if f.is_relative_to(root) and f.is_file():
                    return FileResponse(f, headers={"Cache-Control": "no-cache"})
            except (OSError, ValueError):
                pass
        index = cfg.web_dist / "index.html"
        if not index.is_file():
            return PlainTextResponse(
                "DC Test Deck: dist/index.html is missing — the web UI has not "
                "been built. See README (npm run build).", status_code=503)
        return FileResponse(index, headers={"Cache-Control": "no-cache"})

    return app
