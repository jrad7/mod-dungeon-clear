#!/usr/bin/env bash
# Test Deck's whole suite: backend pytest + a frontend build/type-check when
# node_modules is present. No root, no live worldserver, no database.
set -uo pipefail
cd "$(dirname "$0")/.."

fail=0

# The launcher runs before anything is installed, so it must at least parse
# under a bare interpreter — a typo there is invisible to pytest.
echo "== launcher syntax =="
python3 -m py_compile launch.py && echo "ok  launch.py compiles" || fail=1

echo "== pytest =="
python3 -m pytest t/ -q || fail=1

if [ -d web/node_modules ]; then
    echo "== frontend build (type-check gate) =="
    (cd web && npm run --silent build) || fail=1
    if [ -f t/web_smoke.mjs ]; then
        echo "== web smoke =="
        node t/web_smoke.mjs || fail=1
    fi
else
    echo "== frontend: skipped (web/node_modules missing — run npm install) =="
fi

exit $fail
