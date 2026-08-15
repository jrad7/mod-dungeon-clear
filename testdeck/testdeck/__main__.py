"""`python3 -m testdeck <command>` — Test Deck's command line.

    setup       write a testdeck.toml by inspecting this host (start here)
    serve       run the server (whatever supervises it calls this)
    check       load the config, run startup validation, probe the bridge
    check-auth  interactively verify a GM account login end to end
    sudoers     print sudoers.d rules (ONLY needed for sudo screen/tmux setups)

All take the same --config as the server, so what they report is what the
server will see. `setup` is the exception: it runs before a config exists.
"""

import argparse
import os
import sys
from pathlib import Path

from . import __version__, config as tdconfig, hostenv
from .bridge import hardcopy_path

APP_DIR = Path(__file__).resolve().parent.parent


def cmd_serve(cfg, args):
    """Start the server. Problems found at startup are printed here and shown
    in the app's own banner — never a reason to refuse to boot, since a
    server that will not start cannot tell you why it will not start."""
    import uvicorn

    from .app import create_app

    print(f"testdeck {__version__} — config: {cfg.source or '(defaults)'} "
          f"· toml: {tdconfig.TOML_IMPL} · data: {cfg.data_dir}", file=sys.stderr)
    for p in cfg.problems:
        print(f"testdeck: {p.level}: [{p.key}] {_problem_text(p)}",
              file=sys.stderr)

    busy = _port_in_use(cfg.host, cfg.port)
    if busy:
        print(f"\ntestdeck: port {cfg.port} is already in use.\n"
              "  Another Test Deck is probably running — open "
              f"http://127.0.0.1:{cfg.port} instead,\n"
              "  or set a different [server] port in the config.\n",
              file=sys.stderr)
        return 1

    urls = hostenv.listen_urls(cfg.host, cfg.port)
    print("\n  DC Test Deck is running. Open:\n", file=sys.stderr)
    for u in urls:
        print(f"      {u}", file=sys.stderr)
    if len(urls) > 1:
        print("\n  Testers on the same network use the second address.",
              file=sys.stderr)
    print("\n  Press Ctrl+C to stop.\n", file=sys.stderr)

    if getattr(args, "open", False):
        _open_browser_when_up(urls[0])
    uvicorn.run(create_app(cfg), host=cfg.host, port=cfg.port, log_level="warning")
    return 0


def _problem_text(p):
    """Most config messages open with the section they are about ("[bridge]
    …"), and both printers already show the key — don't say it twice."""
    prefix = f"[{p.key}] "
    return p.message[len(prefix):] if p.message.startswith(prefix) else p.message


def _port_in_use(host, port):
    """Bind-test the port before uvicorn does.

    Double-clicking the launcher twice is the single most likely mistake, and
    uvicorn's own failure ("error while attempting to bind") does not tell the
    user that the thing they wanted is already running.

    SO_REUSEADDR is what asyncio's create_server sets on POSIX, so this probe
    has to set it too or it answers a different question than the one that
    matters. Without it, every connection the deck was serving when it stopped
    leaves a TIME_WAIT socket on this port for ~60s, a plain bind fails, and
    restarting the deck after a code change is refused with "already in use"
    while nothing is listening at all.

    Windows is deliberately excluded: there SO_REUSEADDR lets a second socket
    bind a port something is actively LISTENING on, so setting it would make
    this probe answer False for the one case it exists to catch — the launcher
    double-clicked twice.
    """
    import socket

    family = socket.AF_INET6 if ":" in host else socket.AF_INET
    probe = "" if host in ("0.0.0.0", "::") else host
    s = socket.socket(family, socket.SOCK_STREAM)
    try:
        if os.name != "nt":
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((probe, port))
        return False
    except OSError:
        return True
    finally:
        s.close()


def _open_browser_when_up(url, tries=40, delay=0.25):
    """Open a browser once the port answers.

    uvicorn.run() blocks, so this waits on a thread — and it waits for the
    socket rather than sleeping a fixed time, because a browser that opens
    before the bind shows a connection error the user has to fix by hand.
    """
    import socket
    import threading
    import time
    import urllib.parse
    import webbrowser

    parsed = urllib.parse.urlsplit(url)
    target = (parsed.hostname, parsed.port or 80)

    def wait():
        for _ in range(tries):
            try:
                with socket.create_connection(target, timeout=0.5):
                    webbrowser.open(url)
                    return
            except OSError:
                time.sleep(delay)

    threading.Thread(target=wait, daemon=True).start()


def _check_driver_character(cfg):
    """Findings about the headless test driver character.

    Not part of config.validate(): that runs on every boot and stays
    filesystem-only, while this needs the characters database. It lives here
    because a driver problem is invisible until the first launch fails, and
    the failure names a config key rather than the setup step behind it.

    Levels are "ok" / "info" (printed as a line of the report) and
    "warn" / "error" (counted as problems, and an error is what makes `check`
    exit non-zero). A character that has not been created YET is "info": the
    module provisions it on first use, so a fresh, entirely correct host has
    no driver until it launches something, and reporting that as a fault
    would fail every new install on the way in.
    """
    import asyncio

    from . import mysql as tdmysql

    try:
        wanted, verdict, actual, account = asyncio.run(
            tdmysql.driver_character_status(cfg))
    except Exception as e:                      # noqa: BLE001 — advisory only
        return [("warn", f"could not check the test driver character: {e}")]

    if verdict == "unknown":
        return [("warn",
                 f"could not reach the characters database to check for the "
                 f"test driver character '{wanted}' — every launch needs it, "
                 f"so verify it by hand if launches fail")]
    if verdict == "missing":
        provisions_on = tdmysql.driver_account(cfg)
        if provisions_on:
            return [("info",
                     f"test driver character '{wanted}' does not exist yet — "
                     f"mod-dungeon-clear creates it, and the account "
                     f"'{provisions_on}', the first time a run is launched. "
                     f"That first launch may answer 'is logging in — retry in "
                     f"a few seconds' while it happens")]
        return [("error",
                 f"the test driver character '{wanted}' does not exist, and "
                 f"DungeonClear.TestRun.DriverAccount is empty, which turns "
                 f"off the module's own provisioning. EVERY run Test Deck "
                 f"starts needs the driver: a launch from here is a console "
                 f"command, which has no in-game player to anchor the run, so "
                 f"the module logs this character in as the stand-in GM. "
                 f"Create it on a dedicated plain player account, or set "
                 f"DriverAccount back to an account name and let the module "
                 f"do it")]
    if verdict == "case":
        return [("error",
                 f"DungeonClear.TestRun.DriverCharacter is '{wanted}' but the "
                 f"character is actually named '{actual}'. The lookup is "
                 f"case-sensitive and reports only 'not found' — set the conf "
                 f"to '{actual}' exactly")]

    out = [("ok", f"test driver character '{wanted}'"
                  + (f" on account '{account}'" if account else ""))]
    prefix = tdmysql.bot_account_prefix(cfg)
    if account and account.lower().startswith(prefix.lower()):
        out.append(("warn",
                    f"the driver character '{wanted}' is on '{account}', which "
                    f"looks like a random-bot account (prefix '{prefix}'). The "
                    f"random-bot rotation will log it out from under a run — "
                    f"move it to a dedicated account"))
    return out


def cmd_check(cfg, _args):
    print(f"testdeck {__version__}")
    print(f"  config    {cfg.source or '(none found — using derived defaults)'}")
    print(f"  toml      {tdconfig.TOML_IMPL}")
    print(f"  base      {cfg.base}")
    print(f"  server    {cfg.server_root}  (worldserver working directory)")
    print(f"  log_dir   {cfg.log_dir}")
    print(f"  data_dir  {cfg.data_dir}")
    # The one line that answers "why is the Live view empty?" — it names the
    # file rather than the directory it was looked for in, and says outright
    # when nothing has been found yet.
    live = cfg.testrun_live_file
    print(f"  sidecars  {live.parent}  "
          f"({'found' if live.is_file() else 'no ' + live.name + ' yet'})")
    print(f"  mysql     {cfg.resolved_mysql() or '(not found)'}")
    print(f"  listen    {cfg.host}:{cfg.port}")
    print(f"  bridge    {cfg.bridge_type}" + (
        f" (sudo)" if cfg.use_sudo and cfg.bridge_type != "soap" else ""))
    print(f"  realm     status via {cfg.resolved_status_check()}")
    print(f"  auth      gmlevel >= {cfg.min_gmlevel} "
          f"(admin >= {cfg.admin_gmlevel}), realm_id {cfg.realm_id}")

    driver = _check_driver_character(cfg)
    for level, message in driver:
        if level in ("ok", "info"):
            print(f"  driver    {message}")

    findings = [(p.level, p.key, _problem_text(p)) for p in cfg.problems]
    findings += [(level, "driver", message) for level, message in driver
                 if level not in ("ok", "info")]
    if not findings:
        print("\nno problems found.")
        return 0
    print(f"\n{len(findings)} problem(s):")
    for level, key, message in findings:
        print(f"  {level:5} {key + ':':10} {message}")
    return 1 if any(level == "error" for level, _, _ in findings) else 0


def cmd_check_auth(cfg, args):
    """Verify a GM account can log in: DB row, SRP6 verifier, gmlevel.

    This is the tool that retires the SRP6 byte-order risk — run it against a
    real account before trusting the login endpoint."""
    import asyncio
    import getpass

    from . import auth

    username = args.username
    try:
        password = getpass.getpass(f"Password for {username}: ")
    except (EOFError, KeyboardInterrupt):
        print("\naborted.", file=sys.stderr)
        return 1

    async def go():
        return await auth.verify_account(username, password, cfg=cfg)

    res = asyncio.run(go())
    if res.ok:
        print(f"OK — account id {res.account_id}, gmlevel {res.gmlevel}")
        return 0
    print(f"FAILED at {res.stage}: {res.message}", file=sys.stderr)
    return 1


def cmd_sudoers(cfg, args):
    """Rules for sudo screen/tmux setups only. A SOAP bridge, or a same-user
    screen/tmux session, needs no sudo at all — say so instead of printing
    rules nobody should install."""
    if cfg.bridge_type == "soap" or not cfg.use_sudo:
        print("# This configuration does not use sudo ([bridge] "
              f"type = {cfg.bridge_type!r}, use_sudo = "
              f"{str(cfg.use_sudo).lower()}).\n"
              "# Nothing to install — Test Deck runs fully unprivileged.")
        return 0
    tmpl_path = Path(__file__).resolve().parent.parent / "sudoers.d" / "ac-testdeck.in"
    try:
        tmpl = tmpl_path.read_text(encoding="utf-8")
    except OSError as e:
        print(f"cannot read {tmpl_path}: {e}", file=sys.stderr)
        return 2
    body = (tmpl
            .replace("@USER@", args.user)
            .replace("@SCREEN@", cfg.screen_session)
            .replace("@TMUX@", cfg.tmux_target)
            .replace("@HARDCOPY@", str(hardcopy_path(cfg)))
            .replace("@TESTRUNS@", str(cfg.testruns_file))
            .replace("@TESTPLANS@", str(cfg.testplans_file)))
    print(body, end="")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python3 -m testdeck",
                                 description="Test Deck helper commands")
    ap.add_argument("--config", metavar="PATH", help="path to testdeck.toml")
    ap.add_argument("--version", action="version", version=f"testdeck {__version__}")
    sub = ap.add_subparsers(dest="cmd", required=True)

    st = sub.add_parser("setup", help="write a testdeck.toml for this host")
    st.add_argument("--force", action="store_true",
                    help="overwrite an existing config without asking")
    st.add_argument("--non-interactive", action="store_true",
                    help="take every default; ask nothing")

    sv = sub.add_parser("serve", help="run the server")
    sv.add_argument("--open", action="store_true",
                    help="open a browser once the server is listening")

    sub.add_parser("check", help="validate the config and report problems")

    ca = sub.add_parser("check-auth", help="verify a GM account login end to end")
    ca.add_argument("username", help="game account name")

    sp = sub.add_parser("sudoers", help="print sudoers.d rules (sudo setups only)")
    sp.add_argument("--user", default=None,
                    help="the user Test Deck runs as (default: $USER)")

    args = ap.parse_args(argv)

    # setup runs BEFORE a config exists, so it must not load one first.
    if args.cmd == "setup":
        from . import setup as tdsetup
        written = tdsetup.run(APP_DIR, args.config, force=args.force,
                              interactive=False if args.non_interactive else None)
        if written is None:
            return 1
        print()
        try:
            cfg = tdconfig.load(str(written))
        except tdconfig.ConfigError as e:
            print(f"testdeck: {e}", file=sys.stderr)
            return 2
        tdconfig.validate(cfg)
        cmd_check(cfg, args)
        if any(p.level == "error" for p in cfg.problems):
            print("\nThe server still starts, and shows these in its own "
                  "banner — fix them\nand re-check with `python3 -m testdeck "
                  "check`.")
        # Writing the file IS the job. What `check` then found is advice, not
        # a failure: the caller (usually the launcher) must go on to serve, so
        # that a half-configured deck can explain itself in the browser rather
        # than refusing to open at all.
        return 0

    try:
        cfg = tdconfig.load(args.config)
    except tdconfig.ConfigError as e:
        print(f"testdeck: {e}", file=sys.stderr)
        return 2

    if args.cmd == "sudoers":
        import getpass
        args.user = args.user or getpass.getuser()
        # No privilege probing: `sudoers` is what you run BEFORE the rules
        # exist, so reporting their absence is noise.
        tdconfig.validate(cfg, check_privileges=False)
        return cmd_sudoers(cfg, args)

    tdconfig.validate(cfg)
    if args.cmd == "serve":
        return cmd_serve(cfg, args)
    if args.cmd == "check-auth":
        return cmd_check_auth(cfg, args)
    return cmd_check(cfg, args)


if __name__ == "__main__":
    raise SystemExit(main())
