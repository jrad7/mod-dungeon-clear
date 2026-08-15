"""Small helpers with no dependency on the app: subprocesses, JSONL files,
and reading numbers out of an AzerothCore .conf.
"""

import asyncio
import json
import re
import subprocess

from fastapi import HTTPException


def _blocking_run(argv, cwd, env, merge_stderr, timeout):
    p = subprocess.run(
        argv, cwd=cwd, env=env, timeout=timeout, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT if merge_stderr else subprocess.PIPE)
    return p.returncode, p.stdout or b"", p.stderr or b""


async def spawn(argv, cwd=None, env=None, timeout=20, merge_stderr=True):
    """Run a command off the event loop; (rc, stdout, stderr) as bytes.

    Prefers asyncio's child-process API and falls back to a worker thread when
    the running loop has none. That fallback is not theoretical: on Windows an
    asyncio selector loop raises NotImplementedError for every subprocess, and
    whether the server ends up on one depends on the uvicorn version. Without
    this, a Windows host would lose the database layer AND the realm probe with
    an error that names neither.
    """
    try:
        proc = await asyncio.create_subprocess_exec(
            *argv, cwd=cwd, env=env,
            stdout=asyncio.subprocess.PIPE,
            stderr=(asyncio.subprocess.STDOUT if merge_stderr
                    else asyncio.subprocess.PIPE))
    except NotImplementedError:
        return await asyncio.to_thread(
            _blocking_run, argv, cwd, env, merge_stderr, timeout)
    try:
        out, err = await asyncio.wait_for(proc.communicate(), timeout=timeout)
    except asyncio.TimeoutError:
        try:
            proc.kill()
        except (ProcessLookupError, OSError):
            pass
        # Reap it here rather than leaving it to the garbage collector: the
        # transport's finaliser touches the event loop, and by then the loop
        # may be gone.
        try:
            await asyncio.wait_for(proc.wait(), timeout=2)
        except (asyncio.TimeoutError, OSError):
            pass
        raise
    return proc.returncode, out or b"", err or b""


async def run_cmd(argv, cwd=None, timeout=20):
    """Run a command, return (rc, stdout_text). Never raises on failure.

    Every caller here is a status probe or a fire-and-forget console poke, so
    a missing binary or a hung command has to read as data, not an exception —
    one absent tool must not take a card down.
    """
    try:
        rc, out, _ = await spawn(argv, cwd=cwd, timeout=timeout)
        return rc, out.decode("utf-8", "replace")
    except (asyncio.TimeoutError, subprocess.TimeoutExpired):
        return -1, "<timeout>"
    except (FileNotFoundError, OSError) as e:
        return -1, str(e)


def tail_jsonl(path, limit):
    """Newest-first tail of a JSONL file; malformed lines are skipped so a
    partial write can't 500 the panel."""
    rows = []
    try:
        with path.open("r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()[-max(1, min(limit, 500)):]
        for line in lines:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    except FileNotFoundError:
        pass
    rows.reverse()
    return rows


async def clear_jsonl(path, label, use_sudo=False):
    """Wipe a history file, keeping one level of undo in <name>.bak.

    Truncate in place — never unlink. The worldserver holds a process-wide
    append stream on these files for the life of the process (see
    DcTestRunRecord::Append); unlinking would leave it writing into an
    orphaned inode and every later record would vanish. O_TRUNC keeps the
    inode, so the next append lands in the file we can still read.

    Truncation is tried directly first — when the same user runs both the
    worldserver and Test Deck (the common case) no privilege is needed. Only
    when that fails AND the operator opted in with [bridge] use_sudo does it
    fall back to `sudo -n truncate` (worldserver running as another user,
    files not writable by us)."""
    try:
        raw = path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return {"cleared": 0, "backup": None}
    except OSError as e:
        raise HTTPException(500, f"could not read {label}: {e}")
    kept = sum(1 for line in raw.splitlines() if line.strip())
    backup = path.with_suffix(path.suffix + ".bak")
    try:
        backup.write_text(raw, encoding="utf-8")
    except OSError as e:
        raise HTTPException(500, f"could not back up {label}: {e}")
    try:
        with path.open("r+") as f:
            f.truncate(0)
        return {"cleared": kept, "backup": backup.name}
    except OSError as direct_err:
        if not use_sudo:
            raise HTTPException(
                500, f"could not clear {label}: {direct_err} (the file is not "
                     "writable by this user; if the worldserver runs as "
                     "another user, set [bridge] use_sudo and install the "
                     "sudoers snippet)")
    rc, out = await run_cmd(["sudo", "-n", "truncate", "-s", "0", str(path)])
    if rc != 0:
        raise HTTPException(500, f"could not clear {label}: {out.strip() or f'rc={rc}'}")
    return {"cleared": kept, "backup": backup.name}


def conf_int(path, key, default):
    """Read a numeric setting out of an AzerothCore .conf (last assignment
    wins, matching the core's own parse order)."""
    try:
        text = path.read_text()
    except OSError:
        return default
    m = re.findall(rf"^\s*{key}\s*=\s*(\d+)", text, re.M)
    return int(m[-1]) if m else default
