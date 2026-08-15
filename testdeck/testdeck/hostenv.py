"""Facts about the host that differ between Windows and everything else.

Test Deck gets handed to whoever is running AzerothCore, and a large share of
solo installs are on Windows. The POSIX assumptions that used to be scattered
through config, status and the database layer live here instead — one function
each — so a Windows host is a supported configuration rather than a scatter of
small failures with unhelpful messages.

Nothing here touches the config object; callers pass in what they know.
"""

import glob
import os
import socket
from pathlib import Path

IS_WINDOWS = os.name == "nt"

MYSQL_EXE = "mysql.exe" if IS_WINDOWS else "mysql"

# Where MySQL/MariaDB installers actually put the client on Windows. None of
# them add it to PATH, so "not on PATH" is the normal state there, not a sign
# of a broken install — which is why this list exists at all.
WINDOWS_MYSQL_GLOBS = (
    r"C:\Program Files\MySQL\MySQL Server *\bin",
    r"C:\Program Files (x86)\MySQL\MySQL Server *\bin",
    r"C:\Program Files\MariaDB *\bin",
    r"C:\Program Files (x86)\MariaDB *\bin",
    r"C:\xampp\mysql\bin",
    r"C:\wamp64\bin\mysql\mysql*\bin",
    r"C:\ProgramData\MySQL\MySQL Server *\bin",
)


def default_data_dir():
    """Runtime state (saved rosters, session secret) for this user."""
    if IS_WINDOWS:
        root = os.environ.get("LOCALAPPDATA") or Path.home() / "AppData" / "Local"
        return Path(root) / "ac-testdeck"
    root = os.environ.get("XDG_DATA_HOME") or Path.home() / ".local" / "share"
    return Path(root) / "ac-testdeck"


def find_mysql(explicit=None, base=None):
    """Absolute path to the mysql client, or None if this host has none.

    Order: what the operator configured, then PATH, then the AzerothCore tree
    itself (the Windows all-in-one bundles ship a server with its client), then
    the usual installer locations.
    """
    import shutil

    if explicit:
        p = Path(str(explicit)).expanduser()
        if p.is_dir():
            p = p / MYSQL_EXE
        return str(p) if p.is_file() else None

    found = shutil.which("mysql")
    if found:
        return found

    if not IS_WINDOWS:
        return None

    if base:
        # Bounded depth: an AzerothCore workspace is wide, and a full walk of
        # it on first run would be the slowest thing the app ever does.
        for depth in ("*", "*/*", "*/*/*"):
            for hit in glob.glob(str(Path(base) / depth / "bin" / MYSQL_EXE)):
                return hit
    for pattern in WINDOWS_MYSQL_GLOBS:
        for d in sorted(glob.glob(pattern), reverse=True):   # newest version
            candidate = Path(d) / MYSQL_EXE
            if candidate.is_file():
                return str(candidate)
    return None


# ---- "is the worldserver running?" ----------------------------------------

PROCESS_TOOL = "tasklist" if IS_WINDOWS else "pgrep"


def process_probe_argv(name):
    """(argv, needle) for a read-only "is <name> running?" check.

    `needle` is None where the exit status answers the question (pgrep) and a
    lowercased string where the output has to be read: tasklist exits 0 whether
    or not its filter matched, printing an "INFO: No tasks..." line on a miss.
    """
    if IS_WINDOWS:
        exe = name if name.lower().endswith(".exe") else name + ".exe"
        return ["tasklist", "/FI", f"IMAGENAME eq {exe}", "/NH"], exe.lower()
    return ["pgrep", "-x", name], None


def process_probe_running(rc, out, needle):
    if needle is None:
        return rc == 0
    return rc == 0 and needle in (out or "").lower()


# ---- telling the operator where to point a browser ------------------------


def lan_address():
    """This host's primary LAN address, or None.

    Opening a UDP socket toward a public address picks the interface the OS
    would route through without sending a single packet — the portable way to
    answer "which of my addresses can a tester on the LAN reach?".
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 53))
        addr = s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()
    return None if addr.startswith("127.") else addr


def listen_urls(host, port):
    """Every URL worth printing for a server bound to (host, port)."""
    if host not in ("0.0.0.0", "::", ""):
        return [f"http://{host}:{port}"]
    urls = [f"http://127.0.0.1:{port}"]
    lan = lan_address()
    if lan:
        urls.append(f"http://{lan}:{port}")
    return urls
