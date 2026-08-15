#!/usr/bin/env bash
# Start DC Test Deck.  ./testdeck.sh   (or double-click it in a file manager)
#
# Everything is in launch.py; this only has to find a Python to run it with,
# and run from the checkout no matter where it was invoked from.
set -euo pipefail
cd "$(dirname "$0")"

for py in python3 python python3.13 python3.12 python3.11 python3.10 python3.9; do
    if command -v "$py" >/dev/null 2>&1; then
        exec "$py" launch.py "$@"
    fi
done

cat >&2 <<'EOF'

DC Test Deck needs Python 3.9 or newer, and none was found on PATH.

    Debian / Ubuntu   sudo apt install python3 python3-venv
    Fedora            sudo dnf install python3
    Arch              sudo pacman -S python
    macOS             brew install python

EOF
exit 1
