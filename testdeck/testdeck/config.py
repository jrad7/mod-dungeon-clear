"""Configuration: everything Test Deck needs to know about this host.

Test Deck is meant to be handed to strangers, so nothing about the host is
assumed: not how the worldserver is launched (screen, tmux, foreground,
docker), not whether sudo is available, not whether systemd exists. Anything
host-shaped lives in one TOML file; anything privileged is opt-in.

Load order for the config path: `--config PATH`, then `$TESTDECK_CONFIG`, then
`./testdeck.toml`, `~/.config/testdeck.toml`, `/etc/testdeck.toml`.
Missing config is not fatal — the server starts on derived defaults and says
so in the status banner, because a server that will not boot cannot tell you
why it will not boot.

Every path has a default derived from `[paths] base`, so a stock layout needs
almost nothing spelled out. `validate()` then checks that what was derived
actually exists and returns the problems for the banner rather than failing
per-request later.
"""

import ipaddress
import os
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

# Real TOML when the host has it (3.11+ stdlib, or tomli), our subset reader
# otherwise. See _toml.py for why the fallback exists.
try:
    import tomllib as _toml_mod
    TOML_IMPL = "tomllib"
except ModuleNotFoundError:      # pragma: no cover - depends on host python
    try:
        import tomli as _toml_mod
        TOML_IMPL = "tomli"
    except ModuleNotFoundError:
        _toml_mod = None
        TOML_IMPL = "bundled"

from . import _toml as _fallback_toml
from . import hostenv

# Relative to the current directory first, then the app's own directory (a
# double-clicked launcher does not necessarily set the working directory to
# the checkout), then the per-user and system locations.
CONFIG_SEARCH = [
    Path("testdeck.toml"),
    Path.home() / ".config" / "testdeck.toml",
    Path("/etc/testdeck.toml"),
]

DEFAULT_ALLOWED_NETS = ["127.0.0.0/8", "10.0.0.0/8", "172.16.0.0/12",
                        "192.168.0.0/16", "::1/128", "fc00::/7", "fe80::/10"]

BRIDGE_TYPES = ("soap", "screen", "tmux")
STATUS_CHECKS = ("auto", "systemd", "process", "none")


def parse_toml(text):
    """Parse TOML with whichever implementation this host has."""
    if _toml_mod is not None:
        return _toml_mod.loads(text)
    return _fallback_toml.loads(text)


class ConfigError(Exception):
    """The config file exists but cannot be used."""


@dataclass
class Problem:
    """One thing wrong with this installation, shown in the status banner.

    `level` is "error" (a feature is dead) or "warn" (a feature is degraded);
    `key` groups them so the frontend can show one line per subsystem.
    """
    level: str
    key: str
    message: str

    def public(self):
        return {"level": self.level, "key": self.key, "message": self.message}


@dataclass
class Config:
    source: Path = None
    # paths
    base: Path = None
    dist: Path = None
    server_root: Path = None        # the worldserver's working directory
    log_dir: Path = None
    worldserver_conf: Path = None
    playerbots_conf: Path = None
    dungeonclear_conf: Path = None
    dbc_dir: Path = None
    data_dir: Path = None
    app_dir: Path = None            # the testdeck/ checkout (holds dist/)
    mysql_bin: str = ""             # "" = look on PATH, then the usual places
    # server
    host: str = "0.0.0.0"
    port: int = 8790
    allowed_nets: list = field(default_factory=list)
    allowed_hosts: set = field(default_factory=set)   # extra Host: NAMES
    # auth
    min_gmlevel: int = 1
    admin_gmlevel: int = 3
    realm_id: int = -1
    session_hours: int = 12
    login_max_attempts: int = 5
    login_window_s: int = 300
    # bridge (worldserver command transport)
    bridge_type: str = "soap"
    soap_url: str = "http://127.0.0.1:7878/"
    soap_user: str = ""
    soap_pass: str = ""              # or $TESTDECK_SOAP_PASS
    screen_session: str = "worldserver"
    tmux_target: str = "worldserver"
    use_sudo: bool = False
    # realm status
    status_check: str = "auto"
    realm_unit: str = ""
    process_name: str = "worldserver"
    # dungeon-clear sidecars (names within log_dir)
    dc_testruns: str = "dc_testruns.jsonl"
    dc_testplans: str = "dc_testplans.jsonl"
    dc_live: str = "dc_testrun_live.json"
    dc_dungeons: str = "dc_test_dungeons.json"
    # misc
    bot_account_prefix_default: str = "rndbot"
    driver_character_default: str = "Dcdriver"
    driver_account_default: str = "dcdriver"
    problems: list = field(default_factory=list)
    _mysql_resolved: str = None

    # -- derived paths ------------------------------------------------------

    def sidecar_dirs(self):
        """Where a mod-dungeon-clear sidecar file could be, best first.

        The module opens these by relative name, so they land in whatever
        directory the worldserver was started from — `server_root`. The rest
        are there because that directory is a deduction, not a fact: a service
        definition, a shortcut's "Start in", or a launcher script can set a
        working directory this host has no way to read back. Looking in the
        other plausible roots costs three stat calls and is the difference
        between a Live view that works and one that is permanently empty.
        """
        out = []
        for d in (self.server_root, self.log_dir, self.dist, self.base):
            if d is not None and d not in out:
                out.append(d)
        return out

    def sidecar(self, name):
        """The path a named sidecar actually has on this host.

        An absolute name is taken as given — that is how an operator who moved
        a file with one of the module's DC_* environment variables says so.
        Otherwise: wherever it already exists, else the working directory,
        which is where the module will create it.
        """
        p = Path(name)
        if p.is_absolute():
            return p
        for d in self.sidecar_dirs():
            if (d / p).is_file():
                return d / p
        return self.server_root / p

    @property
    def testruns_file(self):
        return self.sidecar(self.dc_testruns)

    @property
    def testplans_file(self):
        return self.sidecar(self.dc_testplans)

    @property
    def testrun_live_file(self):
        return self.sidecar(self.dc_live)

    @property
    def testdungeons_file(self):
        return self.sidecar(self.dc_dungeons)

    @property
    def rosters_file(self):
        return self.data_dir / "rosters.json"

    @property
    def secret_file(self):
        return self.data_dir / "session.secret"

    @property
    def web_dist(self):
        return self.app_dir / "dist"

    def resolved_status_check(self):
        """What `auto` means on this host: systemd when a unit is named,
        else a process-table check."""
        if self.status_check != "auto":
            return self.status_check
        return "systemd" if self.realm_unit else "process"

    def resolved_mysql(self):
        """Absolute path to the mysql client, or "" if this host has none.

        Resolved once and remembered: on Windows the fallback search walks the
        installer directories, and every query would otherwise repeat it.
        """
        if self._mysql_resolved is None:
            self._mysql_resolved = hostenv.find_mysql(self.mysql_bin, self.base) or ""
        return self._mysql_resolved

    def resolved_soap_pass(self):
        return self.soap_pass or os.environ.get("TESTDECK_SOAP_PASS", "")

    def problem(self, level, key, message):
        self.problems.append(Problem(level, key, message))

    def health(self):
        return {"problems": [p.public() for p in self.problems],
                "config": str(self.source) if self.source else None,
                "tomlImpl": TOML_IMPL}


# ---------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------


def config_search_paths(app_dir=None):
    """Every location a config is looked for, in order."""
    paths = [CONFIG_SEARCH[0]]
    if app_dir:
        paths.append(Path(app_dir) / "testdeck.toml")
    paths.extend(CONFIG_SEARCH[1:])
    return paths


def find_config(explicit=None, app_dir=None):
    """The config path to use, or None if this host has no config file."""
    if explicit:
        return Path(explicit).expanduser()
    env = os.environ.get("TESTDECK_CONFIG")
    if env:
        return Path(env).expanduser()
    for c in config_search_paths(app_dir):
        if c.expanduser().is_file():
            return c.expanduser()
    return None


def load(explicit=None, app_dir=None):
    """Build a Config. Never raises for a *missing* file; a malformed or
    unusable one raises ConfigError, because guessing past a typo would issue
    worldserver commands against a host the operator did not describe."""
    app_dir = Path(app_dir or Path(__file__).resolve().parent.parent)
    path = find_config(explicit, app_dir)
    data = {}
    if path is not None:
        if not path.is_file():
            raise ConfigError(f"config file not found: {path}")
        try:
            data = parse_toml(path.read_text(encoding="utf-8"))
        except Exception as e:
            raise ConfigError(f"{path}: {e}") from e

    cfg = Config(source=path, app_dir=app_dir)
    if path is None:
        cfg.problem("warn", "config",
                    "no testdeck.toml found — running on derived defaults. "
                    "Run `python3 -m testdeck setup` to write one.")

    _load_paths(cfg, data.get("paths", {}), app_dir)
    _load_server(cfg, data.get("server", {}))
    _load_auth(cfg, data.get("auth", {}))
    _load_bridge(cfg, data.get("bridge", {}))
    _load_realm(cfg, data.get("realm", {}))
    _load_dc(cfg, data.get("dungeonclear", {}))
    return cfg


def _p(value, default=None):
    return Path(str(value)).expanduser() if value else default


def _first_existing(candidates, want_file=False):
    """The first candidate that is really there, else the first candidate.

    Falling back to the first rather than to None is deliberate: a path that
    does not exist is still the path to name in the banner, so the operator is
    told which file to go and produce instead of being told nothing at all.
    """
    real = [c for c in candidates if c is not None]
    for c in real:
        if c.is_file() if want_file else c.is_dir():
            return c
    return real[0] if real else None


def _module_conf(cfg, name):
    """Where the core would load a module's .conf from on this host.

    ConfigMgr::LoadModulesConfigs reads `GetConfigPath() + "modules/"`, and
    GetConfigPath() is the compiled-in _CONF_DIR on POSIX but the literal,
    working-directory-relative "configs/" on Windows. So the module confs sit
    beside worldserver.conf in an ordinary install — and on Windows they sit
    under the working directory even when worldserver.conf was passed in from
    somewhere else entirely, which is why both are tried.
    """
    windows_dir = cfg.server_root / "configs" / "modules" / name
    beside_conf = cfg.worldserver_conf.parent / "modules" / name
    stock_dist = cfg.dist / "etc" / "modules" / name
    order = ([windows_dir, beside_conf, stock_dist] if hostenv.IS_WINDOWS
             else [beside_conf, stock_dist, windows_dir])
    return _first_existing(order, want_file=True)


def _load_paths(cfg, sec, app_dir):
    # With no [paths] base to derive from, look for the install the same way
    # the wizard does rather than assuming one shape of workspace — `acore.sh`
    # builds into the core checkout's own env/dist, which is a level nearer
    # than the positional guess below ever reached. Calling setup's own
    # function is what keeps `check` on a config-less host and `setup` from
    # ever naming two different installs.
    #
    # A spelled-out `base` disables it entirely: the documented contract is
    # that every other path derives from that value, and detection quietly
    # overriding it would break the one key most configs set.
    from . import setup as _setup

    found = None if sec.get("base") else _setup.guess_layout(app_dir)
    # Failing that, the workspace four levels above a module checkout
    # (…/<workspace>/azerothcore-wotlk/modules/mod-dungeon-clear/testdeck).
    # Chained .parent rather than .parents[3]: it saturates at the filesystem
    # root instead of raising for a shallow path.
    guess = app_dir.parent.parent.parent.parent
    cfg.base = _p(sec.get("base"), found["base"] if found else guess)
    cfg.dist = _p(sec.get("dist"),
                  found["dist"] if found else cfg.base / "env" / "dist")
    cfg.worldserver_conf = _p(sec.get("worldserver_conf"),
                              found["worldserver_conf"] if found
                              else cfg.dist / "etc" / "worldserver.conf")

    # The worldserver's working directory, and then everything the server
    # itself resolves against it. `<dist>/bin` used to stand in for all of
    # this, which is true of a POSIX acore.sh install and of nothing else: a
    # Windows all-in-one pack runs its exe out of the install root, keeps its
    # configs in `configs/` beside it, and commonly points LogsDir at
    # `../logs/worldserver/`. Deriving from what the install states about
    # itself is the difference between supporting one layout and supporting
    # the ones people actually have.
    cfg.server_root = _p(sec.get("server_root"),
                         _setup.find_server_root(cfg.worldserver_conf,
                                                 cfg.dist))
    logs_dir, data_dir = _setup.read_server_paths(cfg.worldserver_conf)
    cfg.log_dir = _p(sec.get("log_dir"),
                     _setup.resolve_under(cfg.server_root, logs_dir))

    cfg.playerbots_conf = _p(sec.get("playerbots_conf"),
                             _module_conf(cfg, "playerbots.conf"))
    cfg.dungeonclear_conf = _p(sec.get("dungeonclear_conf"),
                               _module_conf(cfg, "mod_dungeon_clear.conf"))
    cfg.data_dir = _p(sec.get("data_dir"), hostenv.default_data_dir())
    # DataDir is where the extracted client data lives; the DBCs the roster
    # picker reads for talent specs are the `dbc` inside it. The other
    # candidates are for a pack that ships them somewhere of its own — this is
    # a warn-level nicety, so a wider search costs nothing if it misses.
    cfg.dbc_dir = _p(sec.get("dbc_dir"),
                     _first_existing([
                         _setup.resolve_under(cfg.server_root, data_dir) / "dbc",
                         cfg.server_root / "dbc",
                         cfg.dist / "data" / "dbc",
                         cfg.dist / "dbc",
                     ]))
    cfg.mysql_bin = str(sec.get("mysql_bin", ""))


def _load_server(cfg, sec):
    cfg.host = str(sec.get("host", "0.0.0.0"))
    cfg.port = int(sec.get("port", 8790))
    # Absent means "use the defaults"; present-but-empty is a mistake worth
    # refusing, because quietly substituting the permissive default list for
    # what reads like a lockdown fails in the dangerous direction.
    nets = sec.get("allowed_nets")
    if nets is None:
        nets = DEFAULT_ALLOWED_NETS
    cfg.allowed_nets = []
    for c in nets:
        if not str(c).strip():
            continue
        try:
            cfg.allowed_nets.append(ipaddress.ip_network(str(c).strip()))
        except ValueError as e:
            raise ConfigError(f"[server] allowed_nets: {c!r}: {e}") from e
    if not cfg.allowed_nets:
        raise ConfigError("[server] allowed_nets is empty — that would refuse "
                          "every request. Use 0.0.0.0/0 to allow all.")
    # Host NAMES this deck answers to, beyond localhost and any IP literal
    # (see app.host_allowed). Only a reverse proxy or a real DNS entry needs
    # this; typing the address the banner prints never does.
    cfg.allowed_hosts = {str(h).strip().lower()
                         for h in (sec.get("allowed_hosts") or [])
                         if str(h).strip()}


def _load_auth(cfg, sec):
    cfg.min_gmlevel = int(sec.get("min_gmlevel", 1))
    cfg.admin_gmlevel = int(sec.get("admin_gmlevel", 3))
    cfg.realm_id = int(sec.get("realm_id", -1))
    # 12h, not the 72h this shipped with: the deck serves plain HTTP on a LAN,
    # so the cookie is sniffable, and the window a stolen one stays useful in
    # is the thing to shrink. A tester logs in once a session either way.
    cfg.session_hours = int(sec.get("session_hours", 12))
    cfg.login_max_attempts = int(sec.get("login_max_attempts", 5))
    cfg.login_window_s = int(sec.get("login_window_s", 300))
    if cfg.min_gmlevel < 1:
        raise ConfigError("[auth] min_gmlevel must be at least 1 — 0 would "
                          "admit every player account")


def _load_bridge(cfg, sec):
    cfg.bridge_type = str(sec.get("type", "soap"))
    if cfg.bridge_type not in BRIDGE_TYPES:
        raise ConfigError(f"[bridge] type must be one of {', '.join(BRIDGE_TYPES)}"
                          f" — got {cfg.bridge_type!r}")
    cfg.soap_url = str(sec.get("soap_url", "http://127.0.0.1:7878/"))
    cfg.soap_user = str(sec.get("soap_user", ""))
    cfg.soap_pass = str(sec.get("soap_pass", ""))
    cfg.screen_session = str(sec.get("screen_session", "worldserver"))
    cfg.tmux_target = str(sec.get("tmux_target", "worldserver"))
    cfg.use_sudo = bool(sec.get("use_sudo", False))


def _load_realm(cfg, sec):
    cfg.status_check = str(sec.get("status_check", "auto"))
    if cfg.status_check not in STATUS_CHECKS:
        raise ConfigError(f"[realm] status_check must be one of "
                          f"{', '.join(STATUS_CHECKS)} — got {cfg.status_check!r}")
    cfg.realm_unit = str(sec.get("unit", ""))
    cfg.process_name = str(sec.get("process_name", "worldserver"))


def _load_dc(cfg, sec):
    cfg.dc_testruns = str(sec.get("testruns_file", cfg.dc_testruns))
    cfg.dc_testplans = str(sec.get("testplans_file", cfg.dc_testplans))
    cfg.dc_live = str(sec.get("live_file", cfg.dc_live))
    cfg.dc_dungeons = str(sec.get("dungeons_file", cfg.dc_dungeons))


# ---------------------------------------------------------------------------
# Startup validation — collect everything wrong, once, at boot
# ---------------------------------------------------------------------------


def validate(cfg, check_privileges=True):
    """Check paths and tools, appending to cfg.problems. Returns cfg.

    Nothing here is fatal. Each finding disables or degrades one feature, and
    the banner tells the operator which.
    """
    try:
        cfg.data_dir.mkdir(parents=True, exist_ok=True)
        # 0700, and re-applied on an existing directory rather than only at
        # creation: this holds the session secret (forging a cookie from it
        # is full admin) and the screen bridge's hardcopy scratch file, so no
        # other local user has any business reading or writing here.
        os.chmod(cfg.data_dir, 0o700)
    except OSError as e:
        cfg.problem("error", "data_dir",
                    f"cannot create data directory {cfg.data_dir}: {e} — "
                    "saved rosters and sessions will not persist")

    if not cfg.base.is_dir():
        cfg.problem("error", "paths",
                    f"base {cfg.base} does not exist — every path derived "
                    "from it will be wrong")
    # Two different directories, and saying which is which is most of the
    # help: the panels read the files the module writes into the worldserver's
    # working directory, while the log viewer reads whatever LogsDir points at.
    if not cfg.server_root.is_dir():
        cfg.problem("error", "paths",
                    f"server_root {cfg.server_root} does not exist — that is "
                    "where the worldserver runs and where it writes the dc_* "
                    "files, so every test-run panel will be empty. Set [paths] "
                    "server_root to the directory holding worldserver"
                    f"{'.exe' if hostenv.IS_WINDOWS else ''}.")
    if not cfg.log_dir.is_dir():
        cfg.problem("warn", "paths",
                    f"log_dir {cfg.log_dir} does not exist — the log viewer "
                    "will be empty. It follows LogsDir in worldserver.conf; "
                    "set [paths] log_dir if the logs are somewhere else.")
    if not cfg.worldserver_conf.is_file():
        cfg.problem("error", "paths",
                    f"worldserver_conf {cfg.worldserver_conf} not found — "
                    "login, the character list and roster runs need its "
                    "database credentials")
    if not cfg.playerbots_conf.is_file():
        cfg.problem("warn", "paths",
                    f"playerbots_conf {cfg.playerbots_conf} not found — "
                    f"falling back to the default bot account prefix "
                    f"'{cfg.bot_account_prefix_default}' when filtering the "
                    "roster picker")
    if not cfg.dbc_dir.is_dir():
        cfg.problem("warn", "paths",
                    f"dbc_dir {cfg.dbc_dir} not found — the roster picker "
                    "cannot show talent specs (needs Talent.dbc and "
                    "TalentTab.dbc from the client data)")
    if not (cfg.web_dist / "index.html").is_file():
        cfg.problem("error", "frontend",
                    f"{cfg.web_dist}/index.html not found — the web UI has "
                    "not been built (the repo ships it; a fresh build is "
                    "`cd web && npm install && npm run build`)")

    if not cfg.resolved_mysql():
        where = ("no mysql.exe on PATH or in the usual MySQL/MariaDB install "
                 "directories" if hostenv.IS_WINDOWS
                 else "the 'mysql' client is not on PATH")
        cfg.problem("error", "tools",
                    f"{where} — login, characters and rosters are dead. "
                    "Point [paths] mysql_bin at the client binary.")

    _check_bridge(cfg, check_privileges)
    _check_status(cfg)
    return cfg


def _check_bridge(cfg, check_privileges):
    """Per-transport tool checks. Only the configured transport is probed —
    a tmux host must not be nagged about screen."""
    if cfg.bridge_type == "soap":
        if not cfg.soap_user:
            cfg.problem("error", "bridge",
                        "[bridge] type is 'soap' but soap_user is unset — "
                        "set the SOAP account name (SEC_ADMINISTRATOR, see "
                        "README) or switch transports")
        if not cfg.resolved_soap_pass():
            cfg.problem("error", "bridge",
                        "[bridge] soap_pass is unset and $TESTDECK_SOAP_PASS "
                        "is empty — the SOAP bridge cannot authenticate")
        return

    tool = "screen" if cfg.bridge_type == "screen" else "tmux"
    if hostenv.IS_WINDOWS:
        cfg.problem("error", "bridge",
                    f"[bridge] type is '{tool}', which does not exist on "
                    "Windows — use type = \"soap\" (worldserver.conf: "
                    "SOAP.Enabled = 1)")
        return
    if not shutil.which(tool):
        cfg.problem("error", "bridge",
                    f"[bridge] type is '{tool}' but '{tool}' is not on PATH — "
                    "every worldserver command is dead")
    if cfg.use_sudo and check_privileges:
        try:
            rc = subprocess.run(["sudo", "-n", "true"], capture_output=True,
                                timeout=5).returncode
        except (OSError, subprocess.SubprocessError):
            rc = 1
        if rc != 0:
            cfg.problem("error", "bridge",
                        "[bridge] use_sudo is on but passwordless sudo "
                        "(sudo -n) is refused for this user — install the "
                        "sudoers.d snippet (python3 -m testdeck sudoers) or "
                        "turn use_sudo off")


def _check_status(cfg):
    mode = cfg.resolved_status_check()
    if mode == "systemd":
        if not shutil.which("systemctl"):
            cfg.problem("warn", "realm",
                        "[realm] status_check resolves to systemd but "
                        "'systemctl' is not on PATH — the realm orb will "
                        "rely on sidecar freshness only")
        elif not cfg.realm_unit:
            cfg.problem("warn", "realm",
                        "[realm] status_check is 'systemd' but unit is unset "
                        "— name the worldserver unit or use 'process'")
    elif mode == "process":
        if not shutil.which(hostenv.PROCESS_TOOL):
            cfg.problem("warn", "realm",
                        f"'{hostenv.PROCESS_TOOL}' is not on PATH — the realm "
                        "orb will rely on sidecar freshness only")
