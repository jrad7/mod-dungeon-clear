"""config.py: derivation from [paths] base, refusals, and the portability
knobs ([bridge], [realm])."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from testdeck import config as tdconfig  # noqa: E402


def load_toml(tmp_path, text, app_dir=None):
    p = tmp_path / "testdeck.toml"
    p.write_text(text)
    return tdconfig.load(str(p), app_dir=app_dir or tmp_path / "app")


def test_paths_derive_from_base(tmp_path):
    cfg = load_toml(tmp_path, f'[paths]\nbase = "{tmp_path}"\n')
    assert cfg.dist == tmp_path / "env" / "dist"
    assert cfg.log_dir == tmp_path / "env" / "dist" / "bin"
    assert cfg.worldserver_conf == cfg.dist / "etc" / "worldserver.conf"
    assert cfg.testruns_file == cfg.log_dir / "dc_testruns.jsonl"


def make_install(root, rel="env/dist"):
    """A built server under `root`; returns its worldserver.conf."""
    dist = root / rel
    (dist / "etc").mkdir(parents=True)
    (dist / "bin").mkdir(parents=True)
    (dist / "etc" / "worldserver.conf").write_text("")
    return dist / "etc" / "worldserver.conf"


def test_a_spelled_out_base_is_never_second_guessed(tmp_path):
    """Detection must not reach around an explicit base: the documented
    contract is that every other path derives from that value."""
    make_install(tmp_path / "elsewhere")
    app_dir = tmp_path / "elsewhere" / "modules" / "mod-dungeon-clear" / "testdeck"
    app_dir.mkdir(parents=True)
    cfg = load_toml(tmp_path, f'[paths]\nbase = "{tmp_path / "named"}"\n',
                    app_dir=app_dir)
    assert cfg.base == tmp_path / "named"
    assert cfg.dist == tmp_path / "named" / "env" / "dist"


def test_paths_without_a_base_find_the_stock_install(tmp_path, monkeypatch):
    """No config at all is a real state — `check` runs there, and the launcher
    serves there when setup is declined. The paths it reports have to name the
    install the deck is actually inside, including the `acore.sh` layout that
    the positional four-levels-up guess never reached."""
    monkeypatch.chdir(tmp_path)
    core = tmp_path / "azerothcore-wotlk"
    conf = make_install(core)
    app_dir = core / "modules" / "mod-dungeon-clear" / "testdeck"
    app_dir.mkdir(parents=True)

    cfg = tdconfig.load(None, app_dir=app_dir)
    assert cfg.worldserver_conf == conf
    assert cfg.base == core
    assert cfg.log_dir == core / "env" / "dist" / "bin"


def test_paths_without_a_base_fall_back_to_the_positional_guess(tmp_path,
                                                                monkeypatch):
    """Nothing found anywhere still has to produce a config — the server boots
    on derived defaults and says what is wrong in its banner."""
    monkeypatch.chdir(tmp_path)
    app_dir = tmp_path / "core" / "modules" / "mod-dungeon-clear" / "testdeck"
    app_dir.mkdir(parents=True)
    cfg = tdconfig.load(None, app_dir=app_dir)
    assert cfg.base == tmp_path
    assert cfg.dist == tmp_path / "env" / "dist"


# ---- the Windows all-in-one shape -----------------------------------------
#
# Reported as "Test Deck does not run on Windows" (issue #16): the log
# directory, the module confs and the DBCs were all reported missing on an
# install where all three existed, and the launch page and Live view stayed
# empty. One cause behind all of it — <dist>/bin was standing in for the
# worldserver's working directory, which on a Windows pack is the install root
# itself.


def make_pack(root, conf_text="", exe="worldserver.exe"):
    """A Windows all-in-one install: exe, configs/, and its own data."""
    (root / "configs" / "modules").mkdir(parents=True)
    (root / "configs" / "worldserver.conf").write_text(conf_text)
    (root / exe).write_text("MZ")
    return root / "configs" / "worldserver.conf"


def pack_config(tmp_path, root, extra=""):
    """A config naming nothing but the pack — every path under test derives."""
    return load_toml(tmp_path, f'[paths]\nbase = "{root.as_posix()}"\n'
                               f'dist = "{root.as_posix()}"\n'
                               f'worldserver_conf = '
                               f'"{(root / "configs" / "worldserver.conf").as_posix()}"\n'
                     + extra)


def test_windows_pack_paths_come_off_the_working_directory(tmp_path):
    root = tmp_path / "SingleCraft"
    make_pack(root)
    cfg = pack_config(tmp_path, root)
    assert cfg.server_root == root
    assert cfg.log_dir == root               # LogsDir unset = the working dir
    assert cfg.testruns_file == root / "dc_testruns.jsonl"
    assert cfg.testrun_live_file == root / "dc_testrun_live.json"


def test_windows_pack_finds_the_module_confs(tmp_path):
    """The core hardcodes GetConfigPath() to "configs/" on Windows, so
    configs/modules/ is where a module conf is — not <dist>/etc/modules."""
    root = tmp_path / "SingleCraft"
    make_pack(root)
    (root / "configs" / "modules" / "playerbots.conf").write_text("")
    (root / "configs" / "modules" / "mod_dungeon_clear.conf").write_text("")
    cfg = pack_config(tmp_path, root)
    assert cfg.playerbots_conf == root / "configs" / "modules" / "playerbots.conf"
    assert cfg.dungeonclear_conf == \
        root / "configs" / "modules" / "mod_dungeon_clear.conf"
    tdconfig.validate(cfg, check_privileges=False)
    assert not [p for p in cfg.problems
                if "playerbots_conf" in p.message or "dungeonclear" in p.message]


def test_windows_pack_finds_the_dbcs_through_data_dir(tmp_path):
    root = tmp_path / "SingleCraft"
    make_pack(root, conf_text='DataDir = "data"\n')
    (root / "data" / "dbc").mkdir(parents=True)
    assert pack_config(tmp_path, root).dbc_dir == root / "data" / "dbc"


def test_log_dir_follows_logs_dir_but_sidecars_do_not(tmp_path):
    """LogsDir moves the *.log files and nothing else. The module writes its
    sidecars by relative name, so they stay in the working directory — reading
    them out of the log directory is what left the Live view empty."""
    root = tmp_path / "SingleCraft"
    make_pack(root, conf_text='LogsDir = "logs"\n')
    (root / "logs").mkdir()
    (root / "dc_testrun_live.json").write_text("{}")
    cfg = pack_config(tmp_path, root)
    assert cfg.log_dir == root / "logs"
    assert cfg.testrun_live_file == root / "dc_testrun_live.json"


def test_a_sidecar_is_found_where_it_actually_is(tmp_path):
    """The working directory is a deduction — a service or a shortcut can set
    one this host cannot read back. A file that plainly exists in another of
    the install's own roots is better evidence than the deduction."""
    root = tmp_path / "SingleCraft"
    make_pack(root, conf_text='LogsDir = "logs"\n')
    (root / "logs").mkdir()
    (root / "logs" / "dc_test_dungeons.json").write_text('{"dungeons": []}')
    cfg = pack_config(tmp_path, root)
    assert cfg.testdungeons_file == root / "logs" / "dc_test_dungeons.json"


def test_a_missing_sidecar_still_names_the_working_directory(tmp_path):
    """Nothing written yet is the normal state of a fresh install; the path
    reported has to be where the module WILL write, so the banner and the
    docs agree with each other."""
    root = tmp_path / "SingleCraft"
    make_pack(root)
    assert pack_config(tmp_path, root).testruns_file == \
        root / "dc_testruns.jsonl"


def test_a_relocated_sidecar_can_be_named_outright(tmp_path):
    """DC_TESTRUNS_FILE can put the file anywhere; an absolute name in the
    config is how the deck is told about it."""
    root = tmp_path / "SingleCraft"
    make_pack(root)
    moved = tmp_path / "elsewhere" / "runs.jsonl"
    moved.parent.mkdir()
    moved.write_text("")
    cfg = pack_config(tmp_path, root,
                      f'\n[dungeonclear]\ntestruns_file = "{moved.as_posix()}"\n')
    assert cfg.testruns_file == moved


def test_windows_pack_detected_with_no_config_at_all(tmp_path, monkeypatch):
    """The deck unzipped inside the pack, before setup has ever run: the
    wizard's detection and the running server must name the same install."""
    monkeypatch.chdir(tmp_path)
    root = tmp_path / "SingleCraft"
    conf = make_pack(root, conf_text='LogsDir = "logs"\n')
    app_dir = root / "modules" / "mod-dungeon-clear" / "testdeck"
    app_dir.mkdir(parents=True)

    cfg = tdconfig.load(None, app_dir=app_dir)
    assert cfg.worldserver_conf == conf
    assert cfg.base == root
    assert cfg.server_root == root
    assert cfg.log_dir == root / "logs"


def test_defaults(tmp_path):
    cfg = load_toml(tmp_path, "")
    assert cfg.port == 8790
    assert cfg.bridge_type == "soap"
    assert cfg.use_sudo is False           # sudo is opt-in, never assumed
    assert cfg.min_gmlevel == 1
    assert cfg.resolved_status_check() == "process"   # no unit named


def test_status_auto_prefers_systemd_when_unit_set(tmp_path):
    cfg = load_toml(tmp_path, '[realm]\nunit = "ac-worldserver"\n')
    assert cfg.resolved_status_check() == "systemd"


def test_bad_bridge_type_refused(tmp_path):
    with pytest.raises(tdconfig.ConfigError):
        load_toml(tmp_path, '[bridge]\ntype = "telnet"\n')


def test_bad_status_check_refused(tmp_path):
    with pytest.raises(tdconfig.ConfigError):
        load_toml(tmp_path, '[realm]\nstatus_check = "psychic"\n')


def test_min_gmlevel_zero_refused(tmp_path):
    """gmlevel 0 would admit every player account — a config typo must not
    fail open."""
    with pytest.raises(tdconfig.ConfigError):
        load_toml(tmp_path, "[auth]\nmin_gmlevel = 0\n")


def test_empty_allowed_nets_refused(tmp_path):
    with pytest.raises(tdconfig.ConfigError):
        load_toml(tmp_path, "[server]\nallowed_nets = []\n")


def test_soap_pass_from_env(tmp_path, monkeypatch):
    cfg = load_toml(tmp_path, '[bridge]\ntype = "soap"\nsoap_user = "b"\n')
    monkeypatch.setenv("TESTDECK_SOAP_PASS", "sekrit")
    assert cfg.resolved_soap_pass() == "sekrit"


def test_validate_flags_missing_soap_creds(tmp_path):
    cfg = load_toml(tmp_path, '[bridge]\ntype = "soap"\n')
    tdconfig.validate(cfg, check_privileges=False)
    keys = {p.key for p in cfg.problems if p.level == "error"}
    assert "bridge" in keys


def test_validate_missing_dist_is_a_problem(tmp_path):
    cfg = load_toml(tmp_path, f'[paths]\nbase = "{tmp_path}"\n')
    tdconfig.validate(cfg, check_privileges=False)
    assert any(p.key == "frontend" for p in cfg.problems)
    assert any(p.key == "paths" for p in cfg.problems)


def test_config_search_env(tmp_path, monkeypatch):
    p = tmp_path / "elsewhere.toml"
    p.write_text("[server]\nport = 9999\n")
    monkeypatch.setenv("TESTDECK_CONFIG", str(p))
    cfg = tdconfig.load(app_dir=tmp_path / "app")
    assert cfg.port == 9999
