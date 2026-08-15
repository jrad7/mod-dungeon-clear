"""The headless test driver character check.

Every launch Test Deck makes is a console command, and a console command has
no in-game player to anchor a run — mod-dungeon-clear logs the driver
character in as the stand-in GM instead. So a driver that cannot come online
is not a degraded feature, it is every run failing, and `check` has to say so
before the first launch does.

The module provisions the driver itself, though (DcTestDriver.cpp: it creates
DriverAccount and the character on first use), so "not there yet" is the
NORMAL state of a correct fresh host and must not read as a fault. What is
still worth an error is a driver that can never resolve: a case-mismatched
conf name, or a missing character with provisioning switched off.
"""

import asyncio

import pytest

from testdeck import __main__ as tdmain
from testdeck import mysql as tdmysql


def write_dc_conf(cfg, text):
    cfg.dungeonclear_conf.write_text(text)


# ---- reading the name out of the module conf ------------------------------


def test_driver_name_comes_from_the_module_conf(cfg):
    write_dc_conf(cfg, 'DungeonClear.TestRun.DriverCharacter = "Bench"\n')
    assert tdmysql.driver_character(cfg) == "Bench"


def test_driver_name_falls_back_when_the_conf_is_absent(cfg):
    cfg.dungeonclear_conf.unlink()
    assert tdmysql.driver_character(cfg) == "Dcdriver"


def test_driver_name_last_assignment_wins(cfg):
    write_dc_conf(cfg,
                  'DungeonClear.TestRun.DriverCharacter = "First"\n'
                  'DungeonClear.TestRun.DriverCharacter = "Second"\n')
    assert tdmysql.driver_character(cfg) == "Second"


def test_commented_out_driver_name_is_ignored(cfg):
    write_dc_conf(cfg, '#DungeonClear.TestRun.DriverCharacter = "Ghost"\n')
    assert tdmysql.driver_character(cfg) == "Dcdriver"


@pytest.mark.parametrize("value", [
    "Rob'; DROP TABLE characters; --",   # the value reaches SQL
    "a" * 40,                            # longer than any WoW name
    "X",                                 # shorter than any WoW name
    "Dc driver",
    "Dc1",
])
def test_implausible_driver_names_fall_back_to_the_default(cfg, value):
    write_dc_conf(cfg,
                  f'DungeonClear.TestRun.DriverCharacter = "{value}"\n')
    assert tdmysql.driver_character(cfg) == "Dcdriver"


# ---- the provisioning account ---------------------------------------------


def test_driver_account_defaults_when_the_conf_is_silent(cfg):
    assert tdmysql.driver_account(cfg) == "dcdriver"


def test_driver_account_comes_from_the_module_conf(cfg):
    write_dc_conf(cfg, 'DungeonClear.TestRun.DriverAccount = "benchdriver"\n')
    assert tdmysql.driver_account(cfg) == "benchdriver"


def test_an_empty_driver_account_means_provisioning_is_off(cfg):
    """The module's documented opt-out — and the one case where a missing
    character is the operator's problem rather than a not-yet."""
    write_dc_conf(cfg, 'DungeonClear.TestRun.DriverAccount = ""\n')
    assert tdmysql.driver_account(cfg) == ""


def test_commented_out_driver_account_is_ignored(cfg):
    write_dc_conf(cfg, '#DungeonClear.TestRun.DriverAccount = ""\n')
    assert tdmysql.driver_account(cfg) == "dcdriver"


# ---- the database verdict -------------------------------------------------


def fake_chars(monkeypatch, rows):
    async def fake_query(which, sql, cfg=None):
        assert which == "characters"
        return rows
    monkeypatch.setattr(tdmysql, "mysql_query", fake_query)


def status(cfg):
    return asyncio.run(tdmysql.driver_character_status(cfg))


def test_existing_driver_is_ok(cfg, monkeypatch):
    fake_chars(monkeypatch, [["Dcdriver", "dcbridge"]])
    wanted, verdict, actual, account = status(cfg)
    assert (wanted, verdict, account) == ("Dcdriver", "ok", "dcbridge")


def test_missing_driver_is_reported_missing(cfg, monkeypatch):
    fake_chars(monkeypatch, [])
    assert status(cfg)[1] == "missing"


def test_case_mismatch_is_its_own_verdict(cfg, monkeypatch):
    """CharacterCache::GetCharacterGuidByName is an exact std::map::find, so
    'dcdriver' in the conf never resolves the character 'Dcdriver' — and the
    module can only report 'not found'."""
    write_dc_conf(cfg, 'DungeonClear.TestRun.DriverCharacter = "dcdriver"\n')
    fake_chars(monkeypatch, [["Dcdriver", "dcbridge"]])
    wanted, verdict, actual, _ = status(cfg)
    assert (wanted, verdict, actual) == ("dcdriver", "case", "Dcdriver")


def test_case_insensitive_lookup_is_asked_for(cfg, monkeypatch):
    """Without the collation override the mismatch above is indistinguishable
    from a missing character, which is the whole point of the check."""
    seen = {}

    async def spy(which, sql, cfg=None):
        seen["sql"] = sql
        return []

    monkeypatch.setattr(tdmysql, "mysql_query", spy)
    status(cfg)
    assert "COLLATE utf8mb4_general_ci" in seen["sql"]


def test_unreachable_database_is_unknown_not_missing(cfg, monkeypatch):
    async def boom(which, sql, cfg=None):
        raise RuntimeError("Can't connect to MySQL server")

    monkeypatch.setattr(tdmysql, "mysql_query", boom)
    assert status(cfg)[1] == "unknown"


def test_unconfigured_database_is_unknown_not_missing(cfg, monkeypatch):
    async def none(which, sql, cfg=None):
        return None

    monkeypatch.setattr(tdmysql, "mysql_query", none)
    assert status(cfg)[1] == "unknown"


# ---- what `check` prints --------------------------------------------------


def levels(findings):
    return {level for level, _ in findings}


def test_a_driver_that_does_not_exist_yet_is_only_a_note(cfg, monkeypatch):
    """The state of every correct host before its first launch. Reporting it
    as an error failed `check` on installs with nothing wrong with them."""
    fake_chars(monkeypatch, [])
    findings = tdmain._check_driver_character(cfg)
    assert levels(findings) == {"info"}
    text = findings[0][1]
    assert "Dcdriver" in text and "dcdriver" in text     # character, account


def test_a_missing_driver_is_an_error_when_provisioning_is_off(cfg, monkeypatch):
    """DriverAccount = "" opts out of the module creating it, and then nobody
    else will — so the character has to be there already."""
    write_dc_conf(cfg, 'DungeonClear.TestRun.DriverAccount = ""\n')
    fake_chars(monkeypatch, [])
    findings = tdmain._check_driver_character(cfg)
    assert levels(findings) == {"error"}
    assert "DriverAccount" in findings[0][1]


def test_check_names_the_real_character_on_a_case_mismatch(cfg, monkeypatch):
    write_dc_conf(cfg, 'DungeonClear.TestRun.DriverCharacter = "dcdriver"\n')
    fake_chars(monkeypatch, [["Dcdriver", "dcbridge"]])
    level, text = tdmain._check_driver_character(cfg)[0]
    assert level == "error"
    assert "'Dcdriver'" in text and "case-sensitive" in text


def test_check_is_quiet_when_the_driver_is_healthy(cfg, monkeypatch):
    fake_chars(monkeypatch, [["Dcdriver", "dcbridge"]])
    findings = tdmain._check_driver_character(cfg)
    assert levels(findings) == {"ok"}


def test_check_warns_when_the_driver_sits_on_a_random_bot_account(cfg, monkeypatch):
    """The random-bot rotation manages its own accounts' characters and will
    log the driver out from under a live run."""
    fake_chars(monkeypatch, [["Dcdriver", "testbot3"]])
    findings = tdmain._check_driver_character(cfg)
    assert levels(findings) == {"ok", "warn"}
    assert any("random-bot" in text for level, text in findings
               if level == "warn")


def test_check_degrades_to_a_warning_when_it_cannot_ask(cfg, monkeypatch):
    async def boom(which, sql, cfg=None):
        raise RuntimeError("nope")

    monkeypatch.setattr(tdmysql, "mysql_query", boom)
    findings = tdmain._check_driver_character(cfg)
    assert levels(findings) == {"warn"}
