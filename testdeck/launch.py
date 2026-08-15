#!/usr/bin/env python3
"""Start DC Test Deck with one command, on Linux, macOS or Windows.

    ./testdeck.sh          Linux / macOS
    testdeck.bat           Windows (double-click it)
    python3 launch.py      either, directly

What it does, in order, doing nothing that is already done:

  1. checks this Python is new enough
  2. finds fastapi + uvicorn — in this interpreter first, so a host that
     already has them keeps working exactly as before
  3. otherwise builds a private virtualenv in .venv/ and installs them there
  4. runs `setup` if this host has no config yet
  5. serves, and opens a browser at the deck

Standard library only: this file has to run before anything is installed.
"""

import os
import subprocess
import sys
from pathlib import Path

APP_DIR = Path(__file__).resolve().parent
VENV_DIR = APP_DIR / ".venv"
# What the server cannot run without. This is also the test for "does this
# host already work?", so nothing optional belongs in it — a host that has been
# serving happily for months must not be pushed into a virtualenv over a
# nice-to-have.
DEPS = ("fastapi", "uvicorn")

# What `pip install` is actually given. Version-bounded, because this runs on
# a tester's machine and an unbounded range installs whatever the index serves
# that day. DEPS above stays bare — it is the `import` probe, not the install
# list, and an import name has no version.
DEP_SPECS = ("fastapi>=0.100,<1.0", "uvicorn>=0.23,<1.0")

# Installed alongside DEPS whenever we are building the environment anyway.
# Python gained a TOML parser in 3.11; below that the backport beats falling
# through to our own subset reader, because a hand-edited config deserves a
# real parser.
EXTRA_DEPS = () if sys.version_info >= (3, 11) else ("tomli>=2.0,<3.0",)

MIN_PYTHON = (3, 9)


def die(*lines):
    print(file=sys.stderr)
    for line in lines:
        print(line, file=sys.stderr)
    print(file=sys.stderr)
    raise SystemExit(1)


def venv_python(venv=VENV_DIR):
    """The interpreter inside a virtualenv, per platform layout."""
    if os.name == "nt":
        return venv / "Scripts" / "python.exe"
    return venv / "bin" / "python"


def has_deps(python):
    """Can this interpreter import everything the server needs?"""
    probe = "import " + ", ".join(DEPS)
    try:
        return subprocess.run([str(python), "-c", probe],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode == 0
    except OSError:
        return False


def create_venv():
    print(f"Creating a private Python environment in {VENV_DIR} …")
    try:
        import venv
        venv.EnvBuilder(with_pip=True, clear=False).create(str(VENV_DIR))
    except BaseException as e:                     # noqa: BLE001 - report it
        die(f"Could not create the virtual environment: {e}",
            "",
            "On Debian/Ubuntu this usually means the venv module is a separate",
            "package:      sudo apt install python3-venv",
            "",
            "Alternatively install the dependencies yourself and re-run:",
            f"      {sys.executable} -m pip install {' '.join(DEP_SPECS)}")
    return venv_python()


def install_deps(python):
    wanted = DEP_SPECS + EXTRA_DEPS
    print(f"Installing {', '.join(wanted)} …")
    rc = subprocess.run([str(python), "-m", "pip", "install",
                         "--disable-pip-version-check", "--quiet",
                         *wanted]).returncode
    if rc != 0:
        die("Could not install the dependencies.",
            "",
            "If this machine has no internet access, install them from a local",
            "wheel directory instead:",
            f"      {python} -m pip install --no-index --find-links <dir> "
            f"{' '.join(wanted)}")


def resolve_interpreter(force_venv=False):
    """The Python that will actually run the server."""
    if not force_venv and has_deps(sys.executable):
        return sys.executable

    python = venv_python()
    if not python.is_file():
        python = create_venv()
    if not has_deps(python):
        install_deps(python)
    if not has_deps(python):
        die("The dependencies still cannot be imported after installing them.",
            f"Try deleting {VENV_DIR} and running this again.")
    return python


def has_config(python):
    """True if this host already has a testdeck.toml the server would find."""
    code = ("import sys; sys.path.insert(0, %r);"
            "from testdeck import config;"
            "sys.exit(0 if config.find_config(app_dir=%r) else 1)"
            % (str(APP_DIR), str(APP_DIR)))
    try:
        return subprocess.run([str(python), "-c", code],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode == 0
    except OSError:
        return False


def main(argv):
    if sys.version_info < MIN_PYTHON:
        die(f"Python {MIN_PYTHON[0]}.{MIN_PYTHON[1]} or newer is required; "
            f"this is {sys.version.split()[0]}.",
            "Install a newer Python and run this again.")

    force_venv = "--venv" in argv
    argv = [a for a in argv if a != "--venv"]

    python = resolve_interpreter(force_venv)
    env = {**os.environ, "PYTHONPATH": str(APP_DIR)}

    if not has_config(python):
        print()
        rc = subprocess.run([str(python), "-m", "testdeck", "setup"],
                            cwd=str(APP_DIR), env=env).returncode
        if rc != 0:
            die("Setup did not finish, so there is nothing to serve yet.",
                "Run it again when you have the details it asked for:",
                f"      {python} -m testdeck setup")

    cmd = [str(python), "-m", "testdeck", "serve", "--open", *argv]
    try:
        return subprocess.run(cmd, cwd=str(APP_DIR), env=env).returncode
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
