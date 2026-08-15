"""util.spawn: the subprocess layer both the database and the status probes
sit on, including the path taken when the event loop has no child-process
support (a Windows selector loop)."""

import asyncio
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from testdeck import util as tdutil    # noqa: E402

ECHO = [sys.executable, "-c", "import sys; print('hello'); sys.exit(3)"]


def test_spawn_returns_rc_and_output():
    rc, out, _ = asyncio.run(tdutil.spawn(ECHO))
    assert rc == 3
    assert b"hello" in out


def test_spawn_falls_back_to_a_thread(monkeypatch):
    """Same result when the loop refuses to spawn children — otherwise a
    Windows host loses login, the roster picker and the realm orb at once."""
    async def no_subprocesses(*a, **k):
        raise NotImplementedError

    monkeypatch.setattr(asyncio, "create_subprocess_exec", no_subprocesses)
    rc, out, _ = asyncio.run(tdutil.spawn(ECHO))
    assert rc == 3
    assert b"hello" in out


def test_spawn_keeps_stderr_separate_when_asked(monkeypatch):
    argv = [sys.executable, "-c",
            "import sys; sys.stdout.write('o'); sys.stderr.write('e')"]
    for fallback in (False, True):
        if fallback:
            async def boom(*a, **k):
                raise NotImplementedError
            monkeypatch.setattr(asyncio, "create_subprocess_exec", boom)
        _, out, err = asyncio.run(tdutil.spawn(argv, merge_stderr=False))
        assert out == b"o" and err == b"e"


def test_run_cmd_reports_a_missing_binary_as_data():
    """One absent tool must not raise through a route handler."""
    rc, out = asyncio.run(tdutil.run_cmd(["definitely-not-a-real-binary-xyz"]))
    assert rc == -1 and out


def test_run_cmd_times_out_quietly():
    slow = [sys.executable, "-c", "import time; time.sleep(5)"]
    rc, out = asyncio.run(tdutil.run_cmd(slow, timeout=0.3))
    assert rc == -1 and out == "<timeout>"


def test_run_cmd_times_out_quietly_on_the_thread_path(monkeypatch):
    async def boom(*a, **k):
        raise NotImplementedError

    monkeypatch.setattr(asyncio, "create_subprocess_exec", boom)
    slow = [sys.executable, "-c", "import time; time.sleep(5)"]
    rc, out = asyncio.run(tdutil.run_cmd(slow, timeout=0.3))
    assert rc == -1 and out == "<timeout>"


@pytest.mark.parametrize("exc", [FileNotFoundError("x"), PermissionError("x")])
def test_run_cmd_swallows_os_errors(monkeypatch, exc):
    async def raiser(*a, **k):
        raise exc

    monkeypatch.setattr(asyncio, "create_subprocess_exec", raiser)
    monkeypatch.setattr(subprocess, "run", lambda *a, **k: (_ for _ in ()).throw(exc))
    rc, out = asyncio.run(tdutil.run_cmd(["whatever"]))
    assert rc == -1 and out
