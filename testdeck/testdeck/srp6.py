"""Pure-Python port of AzerothCore's SRP6 verifier check.

Ground truth: src/common/Cryptography/Authentication/SRP6.{h,cpp} in the core.
The auth database stores a per-account random 32-byte salt and a 32-byte
verifier; checking a password is just recomputing the verifier:

    v = g ^ SHA1(salt || SHA1(USERNAME:PASSWORD)) mod N

with g = 7, N the 256-bit constant below, and — the two classic traps —

  * username AND password are uppercased first, ASCII a-z only
    (Utf8ToUpperOnlyLatin: non-Latin bytes pass through untouched), and
  * the SHA1 digest is interpreted as a LITTLE-endian integer, and the
    resulting verifier is serialized little-endian for comparison
    (BigNumber's default byte order in the core).

No dependencies beyond hashlib; `pow()` does the modular exponentiation.
Validated against a real account row via `python3 -m testdeck check-auth`.
"""

import hashlib
import hmac

N = int("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7", 16)
g = 7

SALT_LENGTH = 32
VERIFIER_LENGTH = 32


def upper_only_latin(s):
    """Utf8ToUpperOnlyLatin: uppercase ASCII a-z, leave everything else."""
    return "".join(chr(ord(c) - 32) if "a" <= c <= "z" else c for c in s)


def calculate_verifier(username, password, salt):
    """The 32-byte verifier for these credentials and this salt."""
    if len(salt) != SALT_LENGTH:
        raise ValueError(f"salt must be {SALT_LENGTH} bytes, got {len(salt)}")
    creds = f"{upper_only_latin(username)}:{upper_only_latin(password)}"
    h1 = hashlib.sha1(creds.encode("utf-8")).digest()
    x = int.from_bytes(hashlib.sha1(salt + h1).digest(), "little")
    return pow(g, x, N).to_bytes(VERIFIER_LENGTH, "little")


def check_login(username, password, salt, verifier):
    """True when the password matches the stored salt+verifier."""
    if len(verifier) != VERIFIER_LENGTH:
        return False
    try:
        want = calculate_verifier(username, password, salt)
    except ValueError:
        return False
    return hmac.compare_digest(want, verifier)
