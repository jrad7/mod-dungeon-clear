"""setup.py: detection and rendering, with no console involved.

The wizard's value is that it guesses right, so the guessing is what is tested
here — the prompts are a thin shell over these functions.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from testdeck import config as tdconfig    # noqa: E402
from testdeck import setup as tdsetup      # noqa: E402

CONF = 'SOAP.Enabled = 1\nSOAP.IP = "0.0.0.0"\nSOAP.Port = 7979\n'


def make_tree(root, conf_text=CONF, rel="env/dist"):
    dist = root / rel
    (dist / "etc").mkdir(parents=True)
    (dist / "bin").mkdir(parents=True)
    (dist / "etc" / "worldserver.conf").write_text(conf_text)
    return dist / "etc" / "worldserver.conf"


def test_finds_conf_from_the_workspace_root(tmp_path):
    conf = make_tree(tmp_path)
    assert tdsetup.find_worldserver_conf(tmp_path) == conf


def test_finds_conf_from_the_dist_dir(tmp_path):
    conf = make_tree(tmp_path)
    assert tdsetup.find_worldserver_conf(conf.parent.parent) == conf


def test_finds_conf_from_the_bin_dir(tmp_path):
    """`bin/` is where people live, so it is what they paste."""
    conf = make_tree(tmp_path)
    assert tdsetup.find_worldserver_conf(conf.parent.parent / "bin") == conf


def test_finds_conf_when_pointed_straight_at_it(tmp_path):
    conf = make_tree(tmp_path)
    assert tdsetup.find_worldserver_conf(conf) == conf


def test_falls_back_to_conf_dist(tmp_path):
    """A built-but-never-configured server: better to say which file is
    missing its settings than to claim there is no install here."""
    dist = tmp_path / "env" / "dist"
    (dist / "etc").mkdir(parents=True)
    (dist / "etc" / "worldserver.conf.dist").write_text(CONF)
    found = tdsetup.find_worldserver_conf(tmp_path)
    assert found and found.name == "worldserver.conf.dist"


def test_no_install_is_none(tmp_path):
    assert tdsetup.find_worldserver_conf(tmp_path) is None
    assert tdsetup.find_worldserver_conf("") is None


def test_layout_of_the_conventional_env_dist(tmp_path):
    conf = make_tree(tmp_path)
    lay = tdsetup.layout_from_conf(conf)
    assert lay["base"] == tmp_path.resolve()
    assert lay["dist"] == (tmp_path / "env" / "dist").resolve()
    assert lay["log_dir"] == (tmp_path / "env" / "dist" / "bin").resolve()


def test_layout_of_a_flat_install(tmp_path):
    """Windows builds commonly land in <root>\\dist, with no env/ level."""
    conf = make_tree(tmp_path, rel="dist")
    lay = tdsetup.layout_from_conf(conf)
    assert lay["base"] == tmp_path.resolve()
    assert lay["dist"] == (tmp_path / "dist").resolve()


# ---- the Windows all-in-one shape -----------------------------------------
#
# One directory holds worldserver.exe, its configs/ and its data. The core
# hardcodes GetConfigPath() to "configs/" on Windows, relative to the working
# directory, so this is not one packager's idea — it is the only shape a
# Windows server can have.


def make_pack(root, conf_text=CONF, exe="worldserver.exe"):
    """A Windows all-in-one install; returns its worldserver.conf."""
    (root / "configs").mkdir(parents=True)
    (root / "configs" / "worldserver.conf").write_text(conf_text)
    if exe:
        (root / exe).write_text("MZ")
    return root / "configs" / "worldserver.conf"


def test_finds_conf_in_a_windows_configs_dir(tmp_path):
    """The shape the wizard could not see at all: every Windows install keeps
    its conf in configs/, and no candidate looked there."""
    conf = make_pack(tmp_path / "SingleCraft")
    assert tdsetup.find_worldserver_conf(tmp_path / "SingleCraft") == conf


def test_layout_of_a_windows_pack(tmp_path):
    """The exe's directory is the working directory, and for a pack that has
    nothing above it, the workspace too."""
    root = (tmp_path / "SingleCraft").resolve()
    conf = make_pack(root)
    lay = tdsetup.layout_from_conf(conf)
    assert lay["server_root"] == root
    assert lay["dist"] == root
    assert lay["base"] == root
    assert lay["log_dir"] == root          # LogsDir unset = the working dir


def test_server_root_is_the_bin_dir_of_a_posix_install(tmp_path):
    conf = make_tree(tmp_path)
    (tmp_path / "env" / "dist" / "bin" / "worldserver").write_text("elf")
    assert tdsetup.find_server_root(conf) == tmp_path / "env" / "dist" / "bin"


def test_server_root_without_a_binary_reads_the_config_dir(tmp_path):
    """A host that installed the server as a service, or a tree read over a
    share: no binary to point at, so the shape of the config directory is the
    only evidence left. configs/ can only be below a working directory."""
    conf = make_pack(tmp_path / "SingleCraft", exe=None)
    assert tdsetup.find_server_root(conf) == (tmp_path / "SingleCraft")
    posix = make_tree(tmp_path / "linux")
    assert tdsetup.find_server_root(posix) == \
        tmp_path / "linux" / "env" / "dist" / "bin"


def test_layout_of_a_conf_beside_the_binary(tmp_path):
    """`worldserver -c worldserver.conf` in one flat folder. The install is
    that folder — not its parent, which belongs to somebody else."""
    root = (tmp_path / "SingleCraft").resolve()
    root.mkdir()
    (root / "worldserver.exe").write_text("MZ")
    conf = root / "worldserver.conf"
    conf.write_text(CONF)
    lay = tdsetup.layout_from_conf(conf)
    assert lay["server_root"] == root
    assert lay["dist"] == root
    assert lay["base"] == root


def test_server_root_finds_an_exe_one_level_down(tmp_path):
    """Not every pack calls that directory bin/."""
    root = tmp_path / "SingleCraft"
    conf = make_pack(root, exe=None)
    (root / "Server").mkdir()
    (root / "Server" / "worldserver.exe").write_text("MZ")
    assert tdsetup.find_server_root(conf) == root / "Server"


def test_log_dir_follows_logs_dir_from_the_conf(tmp_path):
    """`LogsDir = "../logs/worldserver/"` is what the AzerothCore Windows
    guide itself tells people to write, and it is resolved against the working
    directory — not against the config file."""
    root = (tmp_path / "SingleCraft").resolve()
    conf = make_pack(root, conf_text='LogsDir = "../logs/worldserver/"\n')
    lay = tdsetup.layout_from_conf(conf)
    assert lay["log_dir"] == root.parent / "logs" / "worldserver"


def test_absolute_logs_dir_is_left_alone(tmp_path):
    root = (tmp_path / "SingleCraft").resolve()
    elsewhere = (tmp_path / "elsewhere").resolve()
    conf = make_pack(root, conf_text=f'LogsDir = "{elsewhere}"\n')
    assert tdsetup.layout_from_conf(conf)["log_dir"] == elsewhere


def test_server_paths_default_to_the_working_directory(tmp_path):
    conf = make_tree(tmp_path)
    assert tdsetup.read_server_paths(conf) == ("", ".")
    assert tdsetup.resolve_under(tmp_path, "") == tmp_path
    assert tdsetup.resolve_under(tmp_path, ".") == tmp_path


def test_conf_values_are_read_the_way_the_core_reads_them(tmp_path):
    """Config.cpp takes everything after the first '=', trims it and deletes
    every quote — quotes are how a path with spaces is written, not
    delimiters."""
    conf = make_tree(tmp_path, conf_text='DataDir = "C:/Program Files/ac data"\n')
    assert tdsetup.read_server_paths(conf)[1] == "C:/Program Files/ac data"


def test_commented_out_settings_are_not_read(tmp_path):
    conf = make_tree(tmp_path, conf_text='#LogsDir = "nope"\nLogsDir = "logs"\n')
    assert tdsetup.read_server_paths(conf)[0] == "logs"


def test_rendered_config_spells_out_a_windows_pack(tmp_path):
    """server_root is the value every other path hangs off on this layout, so
    the written config has to state it rather than leave it to be re-guessed
    by a reader."""
    root = tmp_path / "SingleCraft"
    conf = make_pack(root, conf_text='LogsDir = "logs"\n')
    text = tdsetup.render_toml({
        "layout": tdsetup.layout_from_conf(conf), "port": 8790,
        "soap_url": "http://127.0.0.1:7878/", "soap_user": "u",
        "soap_pass": "", "mysql_bin": "", "process_name": "worldserver.exe",
    })
    parsed = tdconfig.parse_toml(text)
    assert Path(parsed["paths"]["server_root"]) == root.resolve()
    assert Path(parsed["paths"]["log_dir"]) == (root / "logs").resolve()


def test_a_windows_pack_config_round_trips(tmp_path):
    """The whole point of the wizard: what it writes, config.load() reads back
    as the same install."""
    root = tmp_path / "SingleCraft"
    conf = make_pack(root, conf_text=CONF + 'LogsDir = "logs"\nDataDir = "data"\n')
    lay = tdsetup.layout_from_conf(conf)
    text = tdsetup.render_toml({
        "layout": lay, "port": 8790, "soap_url": "http://127.0.0.1:7979/",
        "soap_user": "u", "soap_pass": "", "mysql_bin": "",
        "process_name": "worldserver.exe",
    })
    out = tmp_path / "testdeck.toml"
    out.write_text(text)
    cfg = tdconfig.load(str(out), app_dir=tmp_path / "app")
    assert cfg.server_root == lay["server_root"]
    assert cfg.log_dir == lay["log_dir"]
    assert cfg.worldserver_conf == conf.resolve()


# ---- finding the install from the checkout's own position -----------------
#
# The deck sits at <core>/modules/mod-dungeon-clear/testdeck. Where the built
# server is relative to THAT is the whole question the wizard's first screen
# answers, and both shapes below are ordinary AzerothCore installs.


def make_checkout(root, core="azerothcore-wotlk"):
    """The deck's own directory inside a core checkout under `root`."""
    app_dir = root / core / "modules" / "mod-dungeon-clear" / "testdeck"
    app_dir.mkdir(parents=True)
    return app_dir


def test_guesses_the_install_beside_the_core_checkout(tmp_path):
    """<workspace>/env/dist, with the core checked out next to env/."""
    conf = make_tree(tmp_path)
    app_dir = make_checkout(tmp_path)
    assert tdsetup.guess_layout(app_dir)["worldserver_conf"] == conf


def test_guesses_the_stock_acore_sh_install(tmp_path):
    """<core>/env/dist — what `acore.sh` builds by default, and the layout the
    wizard used to miss entirely: it is one level NEARER than the workspace
    shape above, so only looking four levels up found nothing and the first
    thing a new user saw was a path prompt."""
    core = tmp_path / "azerothcore-wotlk"
    core.mkdir()
    conf = make_tree(core)
    app_dir = make_checkout(tmp_path)
    assert tdsetup.guess_layout(app_dir)["worldserver_conf"] == conf


def test_a_real_conf_outranks_a_template_one_level_nearer(tmp_path):
    """find_worldserver_conf() accepts a .conf.dist as a last resort, so the
    order the roots are tried in decides this: an unconfigured template in the
    core checkout must not beat the conf of the server actually being run."""
    conf = make_tree(tmp_path)
    core = tmp_path / "azerothcore-wotlk"
    (core / "env" / "dist" / "etc").mkdir(parents=True)
    (core / "env" / "dist" / "etc" / "worldserver.conf.dist").write_text(CONF)
    app_dir = make_checkout(tmp_path)
    assert tdsetup.guess_layout(app_dir)["worldserver_conf"] == conf


def test_no_install_anywhere_guesses_nothing(tmp_path, monkeypatch):
    """Better to ask than to name a directory nobody built into."""
    monkeypatch.chdir(tmp_path)
    app_dir = make_checkout(tmp_path)
    assert tdsetup.guess_layout(app_dir) is None


def test_soap_settings_read_from_the_conf(tmp_path):
    conf = make_tree(tmp_path)
    assert tdsetup.read_soap_settings(conf) == (True, "0.0.0.0", 7979)


def test_soap_disabled_is_reported(tmp_path):
    conf = make_tree(tmp_path, conf_text="SOAP.Enabled = 0\n")
    enabled, _, port = tdsetup.read_soap_settings(conf)
    assert enabled is False and port == 7878      # default port when unset


def test_soap_last_assignment_wins(tmp_path):
    """AzerothCore confs are appended to; the core reads the last value."""
    conf = make_tree(tmp_path, conf_text="SOAP.Enabled = 0\nSOAP.Enabled = 1\n")
    assert tdsetup.read_soap_settings(conf)[0] is True


def test_wildcard_soap_ip_is_dialled_as_loopback():
    assert tdsetup.soap_url("0.0.0.0", 7878) == "http://127.0.0.1:7878/"
    assert tdsetup.soap_url("10.0.0.9", 7878) == "http://10.0.0.9:7878/"


def test_rendered_config_round_trips(tmp_path):
    """Whatever setup writes, config.load() must read back unchanged — the
    wizard is worthless if its output needs hand-fixing."""
    conf = make_tree(tmp_path)
    lay = tdsetup.layout_from_conf(conf)
    text = tdsetup.render_toml({
        "layout": lay, "port": 8791, "soap_url": "http://127.0.0.1:7979/",
        "soap_user": "tdbridge", "soap_pass": "hunter2",
        "mysql_bin": "", "process_name": "worldserver",
    })
    out = tmp_path / "testdeck.toml"
    out.write_text(text)
    cfg = tdconfig.load(str(out), app_dir=tmp_path / "app")
    assert cfg.port == 8791
    assert cfg.bridge_type == "soap"
    assert cfg.soap_user == "tdbridge"
    assert cfg.resolved_soap_pass() == "hunter2"
    assert cfg.log_dir == lay["log_dir"]
    assert cfg.worldserver_conf == conf.resolve()


def test_rendered_config_omits_derivable_paths(tmp_path):
    conf = make_tree(tmp_path)
    text = tdsetup.render_toml({
        "layout": tdsetup.layout_from_conf(conf), "port": 8790,
        "soap_url": "http://127.0.0.1:7878/", "soap_user": "u",
        "soap_pass": "", "mysql_bin": "", "process_name": "worldserver",
    })
    assert "\ndist =" not in text and "\nlog_dir =" not in text


def test_rendered_config_spells_out_a_nonstandard_layout(tmp_path):
    conf = make_tree(tmp_path, rel="dist")
    text = tdsetup.render_toml({
        "layout": tdsetup.layout_from_conf(conf), "port": 8790,
        "soap_url": "http://127.0.0.1:7878/", "soap_user": "u",
        "soap_pass": "", "mysql_bin": "", "process_name": "worldserver",
    })
    assert "\ndist =" in text


def test_windows_paths_survive_toml_escaping(tmp_path):
    """Backslashes in a basic TOML string are escapes; an unescaped
    C:\\Users\\... would silently become something else."""
    text = tdsetup.render_toml({
        "layout": {"base": Path(r"C:\ac"), "dist": Path(r"C:\ac\env\dist"),
                   "log_dir": Path(r"C:\ac\env\dist\bin"),
                   "worldserver_conf": Path(r"C:\ac\env\dist\etc\worldserver.conf")},
        "port": 8790, "soap_url": "http://127.0.0.1:7878/", "soap_user": "u",
        "soap_pass": "", "mysql_bin": r"C:\Program Files\MySQL\bin\mysql.exe",
        "process_name": "worldserver.exe",
    })
    parsed = tdconfig.parse_toml(text)
    assert parsed["paths"]["mysql_bin"] == r"C:\Program Files\MySQL\bin\mysql.exe"


def test_run_writes_a_usable_config(tmp_path, monkeypatch):
    """End to end with no console: detect, render, write, load."""
    conf = make_tree(tmp_path)
    app = tmp_path / "azerothcore-wotlk" / "modules" / "mod-dc" / "testdeck"
    app.mkdir(parents=True)
    written = tdsetup.run(app, interactive=False)
    assert written == app / "testdeck.toml"
    cfg = tdconfig.load(str(written), app_dir=app)
    assert cfg.worldserver_conf == conf.resolve()
    assert cfg.bridge_type == "soap"


def test_run_leaves_an_existing_config_alone_when_declined(tmp_path, monkeypatch):
    """Re-running the launcher must never silently overwrite a config someone
    hand-tuned."""
    make_tree(tmp_path)
    app = tmp_path / "azerothcore-wotlk" / "modules" / "mod-dc" / "testdeck"
    app.mkdir(parents=True)
    existing = app / "testdeck.toml"
    existing.write_text("[server]\nport = 9\n")
    monkeypatch.setattr(tdsetup, "_ask", lambda *a, **k: "n")
    assert tdsetup.run(app, interactive=True, force=False) is None
    assert existing.read_text() == "[server]\nport = 9\n"


def test_force_overwrites(tmp_path):
    make_tree(tmp_path)
    app = tmp_path / "azerothcore-wotlk" / "modules" / "mod-dc" / "testdeck"
    app.mkdir(parents=True)
    (app / "testdeck.toml").write_text("[server]\nport = 9\n")
    assert tdsetup.run(app, interactive=False, force=True) is not None
    assert tdconfig.load(str(app / "testdeck.toml"), app_dir=app).port == 8790
