"""hostenv.py: the Windows/POSIX differences, exercised from either side.

These run on Linux CI and on a maintainer's Windows box, so every test forces
the branch it means to check rather than trusting the host it happens to be on.
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from testdeck import config as tdconfig    # noqa: E402
from testdeck import hostenv               # noqa: E402


def test_process_probe_posix(monkeypatch):
    monkeypatch.setattr(hostenv, "IS_WINDOWS", False)
    argv, needle = hostenv.process_probe_argv("worldserver")
    assert argv == ["pgrep", "-x", "worldserver"]
    assert needle is None
    assert hostenv.process_probe_running(0, "", None) is True
    assert hostenv.process_probe_running(1, "", None) is False


def test_process_probe_windows(monkeypatch):
    monkeypatch.setattr(hostenv, "IS_WINDOWS", True)
    argv, needle = hostenv.process_probe_argv("worldserver")
    assert argv[0] == "tasklist"
    assert "IMAGENAME eq worldserver.exe" in argv
    assert needle == "worldserver.exe"


def test_tasklist_miss_is_not_running():
    """tasklist exits 0 whether or not it matched, so the exit code alone
    would report every Windows host as ONLINE forever."""
    miss = "INFO: No tasks are running which match the specified criteria."
    assert hostenv.process_probe_running(0, miss, "worldserver.exe") is False
    hit = "worldserver.exe   1234 Console   1   900,000 K"
    assert hostenv.process_probe_running(0, hit, "worldserver.exe") is True


def test_process_name_already_has_exe(monkeypatch):
    monkeypatch.setattr(hostenv, "IS_WINDOWS", True)
    _, needle = hostenv.process_probe_argv("worldserver.exe")
    assert needle == "worldserver.exe"


def test_explicit_mysql_path_wins(tmp_path):
    exe = tmp_path / "mysql"
    exe.write_text("")
    assert hostenv.find_mysql(str(exe)) == str(exe)


def test_explicit_mysql_directory_is_accepted(tmp_path, monkeypatch):
    """Pointing [paths] mysql_bin at the bin/ directory is the mistake people
    make; take it rather than reporting "no client"."""
    monkeypatch.setattr(hostenv, "IS_WINDOWS", False)
    monkeypatch.setattr(hostenv, "MYSQL_EXE", "mysql")
    (tmp_path / "mysql").write_text("")
    assert hostenv.find_mysql(str(tmp_path)) == str(tmp_path / "mysql")


def test_explicit_mysql_path_that_is_missing_is_none(tmp_path):
    assert hostenv.find_mysql(str(tmp_path / "nope")) is None


def test_data_dir_uses_localappdata_on_windows(monkeypatch):
    monkeypatch.setattr(hostenv, "IS_WINDOWS", True)
    monkeypatch.setenv("LOCALAPPDATA", r"C:\Users\t\AppData\Local")
    assert hostenv.default_data_dir().name == "ac-testdeck"
    assert "AppData" in str(hostenv.default_data_dir())


def test_data_dir_respects_xdg(monkeypatch, tmp_path):
    monkeypatch.setattr(hostenv, "IS_WINDOWS", False)
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path))
    assert hostenv.default_data_dir() == tmp_path / "ac-testdeck"


def test_listen_urls_includes_lan_for_wildcard_bind(monkeypatch):
    monkeypatch.setattr(hostenv, "lan_address", lambda: "192.168.1.5")
    urls = hostenv.listen_urls("0.0.0.0", 8790)
    assert urls == ["http://127.0.0.1:8790", "http://192.168.1.5:8790"]


def test_listen_urls_for_explicit_bind():
    assert hostenv.listen_urls("127.0.0.1", 8790) == ["http://127.0.0.1:8790"]


# ---- how config.py consumes all this --------------------------------------


def test_config_mysql_bin_is_used(tmp_path):
    exe = tmp_path / "mysql"
    exe.write_text("")
    p = tmp_path / "testdeck.toml"
    p.write_text(f'[paths]\nmysql_bin = "{exe}"\n')
    cfg = tdconfig.load(str(p), app_dir=tmp_path / "app")
    assert cfg.resolved_mysql() == str(exe)


def test_missing_mysql_is_an_error_naming_the_knob(tmp_path, monkeypatch):
    monkeypatch.setattr(tdconfig.hostenv, "find_mysql", lambda *a, **k: None)
    p = tmp_path / "testdeck.toml"
    p.write_text("")
    cfg = tdconfig.load(str(p), app_dir=tmp_path / "app")
    tdconfig.validate(cfg, check_privileges=False)
    tools = [x for x in cfg.problems if x.key == "tools"]
    assert tools and "mysql_bin" in tools[0].message


def test_screen_bridge_on_windows_is_refused(tmp_path, monkeypatch):
    """screen and tmux cannot exist there; say so instead of reporting a
    missing binary the operator could never install."""
    monkeypatch.setattr(tdconfig.hostenv, "IS_WINDOWS", True)
    p = tmp_path / "testdeck.toml"
    p.write_text('[bridge]\ntype = "screen"\n')
    cfg = tdconfig.load(str(p), app_dir=tmp_path / "app")
    tdconfig.validate(cfg, check_privileges=False)
    msgs = [x.message for x in cfg.problems if x.key == "bridge"]
    assert any("Windows" in m and "soap" in m for m in msgs)


def test_config_found_next_to_the_app(tmp_path, monkeypatch):
    """A double-clicked launcher does not set the working directory, so the
    checkout's own testdeck.toml has to be in the search path."""
    app = tmp_path / "app"
    app.mkdir()
    (app / "testdeck.toml").write_text("[server]\nport = 8123\n")
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("TESTDECK_CONFIG", raising=False)
    cfg = tdconfig.load(app_dir=app)
    assert cfg.port == 8123


def test_cwd_config_still_wins_over_the_app_dir(tmp_path, monkeypatch):
    app = tmp_path / "app"
    app.mkdir()
    (app / "testdeck.toml").write_text("[server]\nport = 8123\n")
    work = tmp_path / "work"
    work.mkdir()
    (work / "testdeck.toml").write_text("[server]\nport = 8456\n")
    monkeypatch.chdir(work)
    monkeypatch.delenv("TESTDECK_CONFIG", raising=False)
    assert tdconfig.load(app_dir=app).port == 8456


@pytest.mark.parametrize("host,port", [("0.0.0.0", 8790), ("127.0.0.1", 1)])
def test_listen_urls_never_empty(host, port):
    assert hostenv.listen_urls(host, port)
