"""srp6.py — the pure-Python port of the core's verifier check.

The hardcoded vector pins the byte order: if a refactor flips the little-
endian interpretation of either the exponent or the verifier serialization,
`test_pinned_vector` fails even though a round trip through our own code
would still "pass". Ground truth against a real account row is
`python3 -m testdeck check-auth` (documented in the README).
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from testdeck import srp6  # noqa: E402

SALT = bytes(range(32))


def test_round_trip():
    v = srp6.calculate_verifier("Admin", "password", SALT)
    assert srp6.check_login("Admin", "password", SALT, v)


def test_wrong_password():
    v = srp6.calculate_verifier("Admin", "password", SALT)
    assert not srp6.check_login("Admin", "passw0rd", SALT, v)


def test_ascii_case_insensitive():
    """Utf8ToUpperOnlyLatin uppercases both fields, so ASCII case never
    matters — the classic login bug this module exists to avoid."""
    v = srp6.calculate_verifier("ADMIN", "PASSWORD", SALT)
    assert srp6.check_login("admin", "password", SALT, v)
    assert srp6.check_login("Admin", "PaSsWoRd", SALT, v)


def test_non_latin_untouched():
    """Only a-z is uppercased; other bytes pass through, so a password that
    differs in a non-Latin character must fail."""
    v = srp6.calculate_verifier("Admin", "pässword", SALT)
    assert srp6.check_login("Admin", "pässword", SALT, v)
    assert not srp6.check_login("Admin", "pÄssword", SALT, v)


def test_pinned_vector():
    """Byte-order pin. Regenerate ONLY after re-validating against a real
    account row with `check-auth`."""
    v = srp6.calculate_verifier("Admin", "password", SALT)
    assert v.hex() == ("8d228fd2f89a39f80b6c27cf32bbcc92"
                       "18e7026868bb5162fe6916785cb5450b")


def test_bad_lengths():
    v = srp6.calculate_verifier("A", "b", SALT)
    assert not srp6.check_login("A", "b", SALT, v[:-1])   # short verifier
    try:
        srp6.calculate_verifier("A", "b", SALT[:-1])       # short salt
        raised = False
    except ValueError:
        raised = True
    assert raised
    assert not srp6.check_login("A", "b", SALT[:-1], v)    # short salt -> False
