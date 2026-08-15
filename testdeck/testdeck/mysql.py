"""Database access: credentials out of worldserver.conf, queries via the
`mysql` client.

There is no Python MySQL driver here on purpose — that would be a third
dependency to install, and every AzerothCore host already has the client
binary the server itself was configured against.
"""

import os
import re

from .context import ctx
from .util import spawn

_creds = None


# ---------------------------------------------------------------------------
# Building SQL
# ---------------------------------------------------------------------------
#
# The `mysql` CLI transport has no bind parameters, so values are spliced into
# the statement. Callers still validate their inputs against strict allowlists
# — that is the real guard and it stays — but the escaping itself belongs in
# one place rather than at seven call sites, because an allowlist is only ever
# one careless edit from not being there. Anything that reaches SQL now goes
# through sql_str() or sql_int(), so a new caller gets the escaping for free
# instead of having to remember it.

# The quote is doubled rather than backslashed because '' is the literal
# quote in either mode; the rest is what mysql_real_escape_string does and
# assumes the server is not in NO_BACKSLASH_ESCAPES (AzerothCore never sets
# it). Neither choice affects whether a quote can escape the literal.
_ESCAPES = {
    "\\": "\\\\",
    "'": "''",
    "\n": "\\n",
    "\r": "\\r",
    "\x00": "\\0",
    "\x1a": "\\Z",
}


def sql_str(value):
    """A str as a quoted, escaped MySQL string literal."""
    return "'" + "".join(_ESCAPES.get(ch, ch) for ch in str(value)) + "'"


def sql_int(value):
    """An int as a bare SQL numeric literal. Raises on anything that is not
    one, so a caller cannot smuggle text through the integer path."""
    return str(int(value))


def sql_in(values, cast=sql_str):
    """A comma-separated list for `IN (…)`."""
    return ",".join(cast(v) for v in values)


def sql_ident(name):
    """A database or table name as a backtick-quoted identifier.

    Only the database names out of worldserver.conf go through here — an
    operator-controlled value, not a user-typed one — but a config file is
    still a file someone can fat-finger, and an unquoted identifier with a
    space in it is a confusing parse error rather than an obvious one.
    """
    return "`" + str(name).replace("`", "``") + "`"


def parse_db_creds(cfg=None):
    """{"auth"|"characters"|"world": {host, port, user, password, db}}.

    Format in worldserver.conf: "host;port;user;password;database" — the same
    parsing create-account.sh does. Cached: the file is read once per process,
    and a config change means a restart anyway.
    """
    global _creds
    cfg = cfg or ctx.cfg
    if _creds is not None:
        return _creds
    creds = {}
    try:
        text = cfg.worldserver_conf.read_text()
    except OSError:
        _creds = {}
        return _creds
    for key, name in (("LoginDatabaseInfo", "auth"),
                      ("CharacterDatabaseInfo", "characters"),
                      ("WorldDatabaseInfo", "world")):
        m = re.findall(rf'^\s*{key}\s*=\s*"([^"]*)"', text, re.M)
        if m:
            parts = m[-1].split(";")
            if len(parts) >= 5:
                creds[name] = {"host": parts[0], "port": parts[1],
                               "user": parts[2], "password": parts[3],
                               "db": parts[4]}
    _creds = creds
    return creds


def reset_creds_cache():
    """For tests, and for a future reload-config path."""
    global _creds
    _creds = None


def bot_account_prefix(cfg=None):
    """The playerbots random-bot account prefix (AiPlayerbot.RandomBotAccountPrefix).

    Two consumers, and they must agree: the Characters card tags a logged-in
    character as a BOT by it, and the roster picker excludes those accounts
    entirely — random-bot and addclass-pool characters are what plain
    `.dc test start` already draws from, so offering them in a list whose
    whole point is real characters is just noise.

    Read from the conf rather than hard-coded so a renamed prefix keeps
    working. The value lands in SQL, so it is allowlisted; anything unexpected
    falls back to the shipped default instead of being interpolated.
    """
    cfg = cfg or ctx.cfg
    prefix = cfg.bot_account_prefix_default
    try:
        m = re.findall(r'^\s*AiPlayerbot\.RandomBotAccountPrefix\s*=\s*"?([^"\s]+)"?',
                       cfg.playerbots_conf.read_text(), re.M)
        if m and re.fullmatch(r"[A-Za-z0-9_]{1,24}", m[-1]):
            prefix = m[-1]
    except OSError:
        pass
    return prefix


def driver_character(cfg=None):
    """The headless test driver's character name
    (DungeonClear.TestRun.DriverCharacter).

    Every `.dc test` that does NOT come from an in-game GM — the worldserver
    console, the AC Command Deck, and every command Test Deck issues over any
    of the three bridges — needs a real in-world Player to anchor the run
    (DungeonClearCommand.cpp: ResolveTestIssuer falls through to
    DcTestDriver::EnsureOnline when handler->GetSession() is null). SOAP is a
    console path, so this is not optional for us: without this character
    every launch fails.

    Read from the conf so a renamed driver keeps working. The value lands in
    SQL; it is allowlisted to the character-name charset, and anything
    unexpected falls back to the shipped default rather than being
    interpolated.
    """
    cfg = cfg or ctx.cfg
    name = cfg.driver_character_default
    try:
        # The whole rest of the line, not a run of non-space characters: a
        # value with a space in it must fail the charset check below, and a
        # greedy-token regex would instead hand back its first word and check
        # a name the operator never wrote.
        m = re.findall(r'^\s*DungeonClear\.TestRun\.DriverCharacter\s*=\s*(.*)$',
                       cfg.dungeonclear_conf.read_text(), re.M)
        if m:
            value = m[-1].strip()
            if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
                value = value[1:-1]
            # A WoW character name: letters only, 2-12 of them. The value
            # lands in SQL, so anything else falls back to the default rather
            # than being interpolated.
            if re.fullmatch(r"[A-Za-z]{2,12}", value):
                name = value
    except OSError:
        pass
    return name


def driver_account(cfg=None):
    """The account mod-dungeon-clear provisions the driver on
    (DungeonClear.TestRun.DriverAccount), or "" when that is turned off.

    This is the difference between "not there yet" and "broken". The module
    creates the account and the character itself the first time a console or
    Test Deck `.dc test` needs a driver (DcTestDriver.cpp), so on a healthy
    fresh host the character is absent right up until the first launch — and
    saying so is a note, not a fault. Setting the conf key to "" opts out of
    provisioning, and only THEN is a missing character something the operator
    has to fix.

    Unlike driver_character() the value is not allowlisted: it never reaches
    SQL, and naming whatever the conf actually says beats substituting the
    default into a message about the operator's own config. It is truncated
    only so a fat-fingered line cannot run away with the output.
    """
    cfg = cfg or ctx.cfg
    try:
        m = re.findall(r'^\s*DungeonClear\.TestRun\.DriverAccount\s*=\s*(.*)$',
                       cfg.dungeonclear_conf.read_text(), re.M)
    except OSError:
        return cfg.driver_account_default
    if not m:
        return cfg.driver_account_default
    value = m[-1].strip()
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        value = value[1:-1]
    return value.strip()[:32]


async def driver_character_status(cfg=None):
    """(wanted, verdict, actual, account) for the test driver character.

    verdict is one of:
      "ok"      a character with exactly that name exists
      "case"    one exists but differs in case — `actual` is the real name.
                Not pedantry: DcTestDriver resolves through
                CharacterCache::GetCharacterGuidByName, an exact
                std::map::find, so "dcdriver" in the conf never finds
                "Dcdriver" in the world and the failure says only "not found".
      "missing" no such character under any casing
      "unknown" the question could not be answered (no characters database
                configured, or the client/DB is unreachable). Reported
                differently from "missing": "I could not check" must not read
                as "it is not there".
    """
    cfg = cfg or ctx.cfg
    wanted = driver_character(cfg)
    try:
        auth_db = parse_db_creds(cfg)["auth"]["db"]
    except (KeyError, TypeError):
        return wanted, "unknown", "", ""
    # utf8mb4_bin is the stock collation on characters.name, so an unqualified
    # comparison is case-sensitive. Overriding it here is what lets us tell
    # "missing" apart from "misspelled in the conf" — the whole point of the
    # check. It costs the index on a query that runs once, from the CLI.
    try:
        rows = await mysql_query(
            "characters",
            "SELECT c.name, IFNULL(a.username, '') "
            "FROM characters c "
            f"LEFT JOIN {sql_ident(auth_db)}.account a ON a.id = c.account "
            f"WHERE c.name COLLATE utf8mb4_general_ci = {sql_str(wanted)} "
            "LIMIT 1",
            cfg)
    except RuntimeError:
        return wanted, "unknown", "", ""
    if rows is None:
        return wanted, "unknown", "", ""
    if not rows or not rows[0] or not rows[0][0]:
        return wanted, "missing", "", ""
    actual = rows[0][0]
    account = rows[0][1] if len(rows[0]) > 1 else ""
    return wanted, ("ok" if actual == wanted else "case"), actual, account


async def mysql_query(which, sql, cfg=None):
    """Rows as lists of strings, or None if that database is not configured.

    The client is whatever `resolved_mysql()` found — an absolute path on
    Windows, where no installer puts mysql.exe on PATH.

    The password goes through MYSQL_PWD rather than -p so it never appears in
    the process table.
    """
    creds = parse_db_creds(cfg).get(which)
    if not creds:
        return None
    client = (cfg or ctx.cfg).resolved_mysql() or "mysql"
    argv = [client,
            "-h", creds["host"], "-P", creds["port"],
            "-u", creds["user"], creds["db"], "-N", "-B", "-e", sql]
    try:
        rc, out, err = await spawn(
            argv, timeout=10, merge_stderr=False,
            env={**os.environ, "MYSQL_PWD": creds["password"]})
    except FileNotFoundError:
        # The single most common first-run failure, and the one whose stock
        # message helps least: "[Errno 2] No such file or directory: 'mysql'"
        # reaches a tester's browser as the reason their login failed. Name the
        # actual fix instead — on Windows no installer puts the client on PATH,
        # so this is the expected state of a fresh host rather than an unusual
        # one.
        raise RuntimeError(
            f"the MySQL client ({client}) was not found. Install it, or set "
            "[paths] mysql_bin in testdeck.toml to the full path of "
            "mysql/mysql.exe") from None
    if rc != 0:
        raise RuntimeError(err.decode("utf-8", "replace").strip()[:300])
    rows = [line.split("\t") for line in
            out.decode("utf-8", "replace").splitlines()]
    return rows
