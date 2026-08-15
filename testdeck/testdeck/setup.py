"""`python3 -m testdeck setup` — write a testdeck.toml by looking at the host.

The old first run was "copy the example, read 100 lines of comments, edit five
of them". Almost all of that is derivable: an AzerothCore checkout announces
its own layout, and worldserver.conf already says whether SOAP is on and on
which port. This asks only for what the host cannot know — where the install
is if the guess is wrong, and the bridge account's credentials.

Every function below the prompts is pure, so the detection can be tested
without a console.
"""

import glob
import os
import re
import shutil
import sys
from pathlib import Path

from . import hostenv

# Where worldserver.conf hides relative to whatever the operator points at:
# the conf itself, an etc/ dir, a configs/ dir, a dist/ root, a workspace root,
# or bin/.
#
# etc/ and configs/ are both first-class: ConfigMgr::GetConfigPath() is the
# compiled-in _CONF_DIR on POSIX (an etc/ beside bin/) but the hardcoded,
# working-directory-relative "configs/" on Windows. Every Windows install
# therefore keeps its conf in configs/, which is exactly the shape this list
# used to have no entry for — so the wizard found nothing on Windows and the
# first thing a new user saw was a path prompt.
CONF_CANDIDATES = (
    "worldserver.conf",
    "etc/worldserver.conf",
    "configs/worldserver.conf",
    "dist/etc/worldserver.conf",
    "dist/configs/worldserver.conf",
    "env/dist/etc/worldserver.conf",
    "env/dist/configs/worldserver.conf",
    "../etc/worldserver.conf",
    "../configs/worldserver.conf",
    "build/etc/worldserver.conf",
)

# The worldserver binary, whichever name this platform builds it under. Both
# are tried everywhere: the deck may be reading a tree copied from the other
# platform, and a wrong guess here costs nothing.
WORLDSERVER_EXES = ("worldserver.exe", "worldserver")

# The config directory name the core hardcodes on Windows, relative to the
# worldserver's working directory (ConfigMgr::GetConfigPath).
WINDOWS_CONF_DIR = "configs"

DEFAULT_SOAP_USER = "tdbridge"
DEFAULT_SOAP_PORT = 7878


# ---------------------------------------------------------------------------
# Detection (pure)
# ---------------------------------------------------------------------------


def find_worldserver_conf(candidate):
    """Locate worldserver.conf from any sensible path the operator might give.

    A .conf.dist is accepted as a last resort: it means the server has been
    built but never configured, and saying so beats "not found".
    """
    if not candidate:
        return None
    p = Path(str(candidate)).expanduser()
    if p.is_file():
        return p
    for rel in CONF_CANDIDATES:
        hit = (p / rel).resolve()
        if hit.is_file():
            return hit
    for rel in CONF_CANDIDATES:
        hit = (p / (rel + ".dist")).resolve()
        if hit.is_file():
            return hit
    return None


def find_server_root(conf, dist=None):
    """The worldserver's working directory, as best this host can tell.

    This is the anchor for everything the server writes and nearly everything
    it reads: `LogsDir` and `DataDir` are resolved against it, the core looks
    for module configs under `configs/` beneath it on Windows, and
    mod-dungeon-clear opens its `dc_*` sidecars by relative name, so they land
    in it too. Test Deck used to have no such concept and assumed <dist>/bin
    was all three, which is true of a POSIX `acore.sh` install and of nothing
    else — an all-in-one Windows pack runs its exe straight out of the install
    root, and every derived path missed by one level.

    The binary is the evidence: whatever directory holds worldserver(.exe) is
    what a launcher, a shortcut or a service is started from. Failing that,
    fall back to the shape of the config directory — a conf inside `configs/`
    can only have been found by a server running one level above it.
    """
    conf = Path(conf)
    dist = Path(dist) if dist else conf.parent.parent
    for d in (dist / "bin", dist, conf.parent.parent, conf.parent):
        for exe in WORLDSERVER_EXES:
            if (d / exe).is_file():
                return d
    # One level down, for a pack that names that directory something other
    # than bin/ (server/, Server/, RelWithDebInfo/). Sorted so the answer does
    # not depend on directory order, and one level only — a full walk of an
    # AzerothCore tree is the slowest thing this app could do.
    for exe in WORLDSERVER_EXES:
        hits = sorted(glob.glob(str(Path(glob.escape(str(dist))) / "*" / exe)))
        if hits:
            return Path(hits[0]).parent
    if conf.parent.name.lower() == WINDOWS_CONF_DIR:
        return conf.parent.parent
    return dist / "bin"


def resolve_under(root, value):
    """A path from worldserver.conf, as the core itself would resolve it.

    Relative values (`LogsDir = "../logs/worldserver/"`, `DataDir = "."`) are
    relative to the working directory, not to the config file.
    """
    text = str(value or "").strip()
    if not text or text in (".", "./", ".\\"):
        return Path(root)
    p = Path(text)
    if p.is_absolute():
        return p
    # Normalised lexically, not with resolve(): `LogsDir = "../logs/..."` is
    # the form the AzerothCore Windows guide hands out, and a path printed in
    # the banner with a `..` still in the middle of it reads as a bug. Purely
    # textual, so nothing here touches the filesystem or follows a symlink.
    return Path(os.path.normpath(Path(root) / p))


def layout_from_conf(conf):
    """{base, dist, server_root, log_dir, worldserver_conf} for a conf file.

    A POSIX AzerothCore installs as <dist>/etc/worldserver.conf with the
    binaries in <dist>/bin, so the conf's grandparent is the whole layout;
    `base` is the workspace above it, one more level up when the dist sits in
    the conventional env/dist. A Windows pack is flatter — exe, configs/ and
    data all in one directory — and there `base` is that directory, because
    there is nothing above it that belongs to the install.
    """
    conf = Path(conf).resolve()
    dist = conf.parent.parent
    server_root = find_server_root(conf, dist)
    if conf.parent == server_root:
        # The conf is not in an etc/ or configs/ at all — it sits next to the
        # binary, which is what `worldserver -c worldserver.conf` looks like.
        # Then the grandparent above is somebody else's directory, and the one
        # folder in front of us is the entire install.
        dist = server_root
    logs_dir, _data_dir = read_server_paths(conf)
    log_dir = resolve_under(server_root, logs_dir)
    if server_root == dist:
        base = dist
    else:
        base = dist.parent.parent if dist.parent.name == "env" else dist.parent
    return {"base": base, "dist": dist, "server_root": server_root,
            "log_dir": log_dir, "worldserver_conf": conf}


def install_roots(app_dir):
    """Directories to look for an install under, nearest-match last resort.

    The deck lives at <core>/modules/mod-dungeon-clear/testdeck, and the built
    server can sit on either side of that:

        parents[3]  <workspace>/env/dist   — the core checked out beside env/
        parents[2]  <core>/env/dist        — what `acore.sh` builds by default

    Both are real layouts, and only the first used to be tried, so the stock
    install was the one the wizard could not find. The workspace stays first
    so a host that already resolved that way keeps the same answer; note that
    find_worldserver_conf() accepts a .conf.dist as a last resort, and trying
    the nearer root first would let an unconfigured template outrank a real
    conf one level up.
    """
    app_dir = Path(app_dir).resolve()
    parents = app_dir.parents
    roots = [parents[3] if len(parents) > 3 else None,
             parents[2] if len(parents) > 2 else None,
             app_dir.parent,
             Path.cwd()]
    return [r for r in roots if r is not None]


def guess_layout(app_dir):
    """The install this checkout most likely belongs to, or None.

    Only trusted where a worldserver.conf actually turns up — a guess that
    names a directory nobody built into is worse than asking.
    """
    for candidate in install_roots(app_dir):
        conf = find_worldserver_conf(candidate)
        if conf:
            return layout_from_conf(conf)
    return None


def _read_conf(conf_path):
    """A worldserver.conf's text, or "" if it cannot be read."""
    try:
        return Path(conf_path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def _last_setting(text, key, default=None):
    """The value of `Key = …` in an AzerothCore conf, or `default`.

    Read the way the core reads it (Config.cpp): the value is everything after
    the first `=`, trimmed, with every `"` removed — quotes are how a path with
    spaces is written, not delimiters, and there is no such thing as a trailing
    comment. A commented-out line never matches, because the core skips any
    line starting with `#`.

    Last assignment wins. These files are meant to be appended to, and a
    duplicated key is a broken conf either way (the core keeps the first and
    prints an error), so this only decides what to report about a file nobody
    should have.

    The empty string is a real value — `LogsDir = ""` means the working
    directory itself — so an unset key and an empty one stay distinguishable.
    """
    hits = re.findall(rf'^[ \t]*{re.escape(key)}[ \t]*=(.*)$', text, re.M)
    if not hits:
        return default
    return hits[-1].replace('"', "").strip()


def read_soap_settings(conf_path):
    """(enabled, ip, port) as worldserver.conf currently has them."""
    text = _read_conf(conf_path)
    if not text:
        return False, "127.0.0.1", DEFAULT_SOAP_PORT

    enabled = _last_setting(text, "SOAP.Enabled", "0") == "1"
    ip = _last_setting(text, "SOAP.IP", "") or "127.0.0.1"
    try:
        port = int(_last_setting(text, "SOAP.Port", "") or DEFAULT_SOAP_PORT)
    except ValueError:
        port = DEFAULT_SOAP_PORT
    return enabled, ip, port


def read_server_paths(conf_path):
    """(LogsDir, DataDir) verbatim, as worldserver.conf states them.

    Returned unresolved on purpose: both are relative to the worldserver's
    working directory rather than to the config file, so resolve_under() is
    what turns them into real paths once that directory is known.
    """
    text = _read_conf(conf_path)
    return (_last_setting(text, "LogsDir", "") or "",
            _last_setting(text, "DataDir", "") or ".")


def soap_url(ip, port):
    """0.0.0.0 means "listen everywhere", which is not an address to dial."""
    host = "127.0.0.1" if ip in ("0.0.0.0", "::", "") else ip
    return f"http://{host}:{port}/"


# ---------------------------------------------------------------------------
# Rendering (pure)
# ---------------------------------------------------------------------------


def _toml_str(value):
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_toml(values):
    """The config file for these answers — only the keys that differ from the
    defaults, so it stays readable and the example file remains the reference
    for everything else."""
    layout = values["layout"]
    lines = [
        "# DC Test Deck — written by `python3 -m testdeck setup`.",
        "# Every other setting has a default; see testdeck.example.toml.",
        "",
        "[paths]",
        f"base = {_toml_str(layout['base'])}",
    ]
    # Spell out the derived paths only where this install does not match the
    # stock layout config.py would infer from `base`.
    if layout["dist"] != layout["base"] / "env" / "dist":
        lines.append(f"dist = {_toml_str(layout['dist'])}")
    if layout["worldserver_conf"] != layout["dist"] / "etc" / "worldserver.conf":
        lines.append(
            f"worldserver_conf = {_toml_str(layout['worldserver_conf'])}")
    # config.py re-derives both of these from the conf, so writing them is not
    # required — but on any install that is not the stock <dist>/bin shape they
    # are the two values that explain every other path, and a config nobody can
    # read is a config nobody can fix.
    server_root = layout.get("server_root")
    if server_root and server_root != layout["dist"] / "bin":
        lines.append("# The worldserver's working directory: where it writes "
                     "its dc_* sidecar")
        lines.append("# files, and what the relative paths in worldserver.conf "
                     "are relative to.")
        lines.append(f"server_root = {_toml_str(server_root)}")
    if server_root and layout["log_dir"] != server_root:
        lines.append("# worldserver.conf redirects its logs here (LogsDir).")
        lines.append(f"log_dir = {_toml_str(layout['log_dir'])}")
    if values.get("mysql_bin"):
        lines.append("# No installer puts the MySQL client on PATH on Windows.")
        lines.append(f"mysql_bin = {_toml_str(values['mysql_bin'])}")

    lines += [
        "",
        "[server]",
        'host = "0.0.0.0"',
        f"port = {int(values['port'])}",
        "",
        "[bridge]",
        'type = "soap"',
        f"soap_url = {_toml_str(values['soap_url'])}",
        f"soap_user = {_toml_str(values['soap_user'])}",
    ]
    if values.get("soap_pass"):
        lines.append(f"soap_pass = {_toml_str(values['soap_pass'])}")
    else:
        lines.append("# Password comes from the TESTDECK_SOAP_PASS environment"
                     " variable.")
        lines.append("#soap_pass = \"\"")

    lines += [
        "",
        "[realm]",
        '# "process" watches for the worldserver process; see the example file',
        "# for the systemd option.",
        'status_check = "process"',
        f"process_name = {_toml_str(values['process_name'])}",
        "",
    ]
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# The console flow
# ---------------------------------------------------------------------------


def _ask(prompt, default="", secret=False, interactive=True):
    if not interactive:
        return default
    suffix = f" [{default}]" if default and not secret else ""
    try:
        if secret:
            import getpass
            answer = getpass.getpass(f"{prompt}: ")
        else:
            answer = input(f"{prompt}{suffix}: ")
    except (EOFError, KeyboardInterrupt):
        print()
        raise SystemExit("setup aborted.")
    return answer.strip() or default


def _yes(prompt, interactive=True):
    if not interactive:
        return True
    return _ask(f"{prompt} [Y/n]", "y", interactive=True).lower() not in ("n", "no")


def run(app_dir, config_path=None, force=False, interactive=None):
    """Write a config. Returns the path written, or None if nothing was."""
    if interactive is None:
        interactive = sys.stdin.isatty()

    target = Path(config_path).expanduser() if config_path else \
        Path(app_dir) / "testdeck.toml"

    print("DC Test Deck — setup\n")
    if target.is_file() and not force:
        print(f"{target} already exists.")
        if not _yes("Overwrite it?", interactive):
            print("Left it alone. Run `python3 -m testdeck check` to test it.")
            return None
        print()

    # -- 1. the install -----------------------------------------------------
    layout = guess_layout(app_dir)
    if layout:
        print("Found an AzerothCore install:")
        print(f"  worldserver.conf   {layout['worldserver_conf']}")
        print(f"  working directory  {layout['server_root']}")
        if layout["log_dir"] != layout["server_root"]:
            print(f"  logs               {layout['log_dir']}")
        if not _yes("Is that the server you want to drive?", interactive):
            layout = None
        print()
    while layout is None:
        answer = _ask("Path to your AzerothCore install (or to "
                      "worldserver.conf)", "", interactive=interactive)
        if not answer:
            print("A path is required — Test Deck reads the server's own "
                  "config and log directory.\n", file=sys.stderr)
            if not interactive:
                return None
            continue
        conf = find_worldserver_conf(answer)
        if not conf:
            print(f"No worldserver.conf under {answer}.\n", file=sys.stderr)
            if not interactive:
                return None
            continue
        layout = layout_from_conf(conf)
        print(f"  using {conf}\n")

    # -- 2. the web port ----------------------------------------------------
    port = _ask("Port for the Test Deck web UI", "8790", interactive=interactive)
    try:
        port = int(port)
    except ValueError:
        port = 8790

    # -- 3. the bridge ------------------------------------------------------
    enabled, ip, soap_port = read_soap_settings(layout["worldserver_conf"])
    print("\nTest Deck drives the worldserver over its SOAP interface.")
    if enabled:
        print(f"  worldserver.conf has SOAP enabled on {ip}:{soap_port}. Good.")
    else:
        print(f"  SOAP is currently OFF in {layout['worldserver_conf']}.")
        print("  Set these there and restart the worldserver:\n")
        print("      SOAP.Enabled = 1")
        print('      SOAP.IP      = "127.0.0.1"')
        print(f"      SOAP.Port    = {soap_port}\n")
    print("  It needs a dedicated account at administrator level:\n")
    print(f"      account create {DEFAULT_SOAP_USER} <a long random password>")
    print(f"      account set gmlevel {DEFAULT_SOAP_USER} 3 -1\n")

    soap_user = _ask("SOAP account name", DEFAULT_SOAP_USER,
                     interactive=interactive)
    soap_pass = _ask(f"Password for {soap_user} (blank = read "
                     "$TESTDECK_SOAP_PASS at startup)", "", secret=True,
                     interactive=interactive)

    # -- 4. host facts it can work out itself -------------------------------
    mysql = hostenv.find_mysql(None, layout["base"])
    mysql_bin = "" if (mysql and shutil.which("mysql")) else (mysql or "")
    process_name = "worldserver.exe" if hostenv.IS_WINDOWS else "worldserver"

    text = render_toml({
        "layout": layout, "port": port, "soap_url": soap_url(ip, soap_port),
        "soap_user": soap_user, "soap_pass": soap_pass,
        "mysql_bin": mysql_bin, "process_name": process_name,
    })
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
    except OSError as e:
        print(f"\ncannot write {target}: {e}", file=sys.stderr)
        return None

    # The file can hold the bridge password; keep it off other users' eyes on
    # POSIX. Windows inherits the user profile's ACL, which is already right.
    if soap_pass and not hostenv.IS_WINDOWS:
        try:
            os.chmod(target, 0o600)
        except OSError:
            pass

    print(f"\nWrote {target}")
    if not mysql:
        print("\nWARNING: no MySQL client found. Test Deck runs its database "
              "queries\nthrough it, so login and the roster picker need one. "
              "Install it and\nset [paths] mysql_bin, or put it on PATH.")
    return target
