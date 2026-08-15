"""Roster runs: real player characters, hand-picked.

`.dc test start <d> party=Tank,Heal,D1,D2,D3` runs a hand-picked party of REAL
characters instead of a random draw from the addclass pool. The picking happens
here (Test Deck is the only side with SQL and a UI), so this file owns the
character browser, the saved rosters, and every cheap refusal that can be made
before a worldserver command is issued. The worldserver re-checks the ones that
depend on live state.

It also owns the offline-character spec lookup, because that needs Talent.dbc:
a roster character is offline by definition, so the worldserver cannot tell us
its spec (AiFactory::GetPlayerSpecName needs a live Player).
"""

import json
import re
import struct

from fastapi import APIRouter, HTTPException, Request
from pydantic import BaseModel

from .. import bridge as bridge_mod
from ..auth import request_session, require_admin
from ..context import ctx
from ..mysql import (bot_account_prefix, mysql_query, parse_db_creds, sql_ident,
                     sql_in, sql_int, sql_str)
from ..util import conf_int
from .plans import catalogue_rows, check_dungeon
from .runs import audit

router = APIRouter()

# 3.3.5 player names are letters only, 2-12 characters — no apostrophes, no
# hyphens, no digits. Everything here is interpolated into `mysql -e` SQL, so
# this allowlist is the injection guard, not a cosmetic check: one permitted
# apostrophe would be enough to break out of the string literal.
NAME_RE = re.compile(r"^[A-Za-z]{2,12}$")
ROSTER_ID_RE = re.compile(r"^[A-Za-z0-9 _-]{1,40}$")

ALLIANCE_RACES = {1, 3, 4, 7, 11}   # human dwarf nightelf gnome draenei
HORDE_RACES = {2, 5, 6, 8, 10}      # orc undead tauren troll bloodelf

# Roles are POSITIONAL and the order is the contract with the worldserver.
ROSTER_ROLES = ["tank", "heal", "dps", "dps", "dps"]

DEFAULT_INSTANCES_PER_HOUR = 5


def race_faction(race):
    if race in ALLIANCE_RACES:
        return "alliance"
    if race in HORDE_RACES:
        return "horde"
    return "unknown"


def instances_per_hour():
    return conf_int(ctx.cfg.worldserver_conf, "AccountInstancesPerHour",
                    DEFAULT_INSTANCES_PER_HOUR)


async def instance_budget():
    """{accountId: instances entered in the last hour}."""
    rows = await mysql_query(
        "characters",
        "SELECT accountId, COUNT(*) FROM account_instance_times "
        "WHERE releaseTime > UNIX_TIMESTAMP() GROUP BY accountId") or []
    return {int(r[0]): int(r[1]) for r in rows if len(r) == 2}


def require_db():
    creds = parse_db_creds()
    if "characters" not in creds or "auth" not in creds:
        raise HTTPException(503, "could not parse DB creds from worldserver.conf")
    return creds


def auth_db_ident():
    return sql_ident(require_db()["auth"]["db"])


# ---------------------------------------------------------------------------
# Who may run whose characters
# ---------------------------------------------------------------------------
#
# A roster run logs real characters in, teleports them into an instance and
# re-gears them. Doing that to somebody else's character is an action ABOUT
# somebody else, which is exactly the line [auth] admin_gmlevel already draws
# for clearing shared history and deleting other people's rosters — so it
# draws this one too, rather than inventing a knob.
#
# The practical shape: a tester runs their own alts, an admin runs anyone's.


def is_admin(request):
    s = request_session(request)
    return s is not None and s.gmlevel >= ctx.cfg.admin_gmlevel


def session_account_id(request):
    s = request_session(request)
    return s.account_id if s else 0


# ---------------------------------------------------------------------------
# Talent specs for offline characters
# ---------------------------------------------------------------------------
#
# `character_talent` has the learned spell ids but nothing about which tree they
# belong to or what rank they are; that mapping only exists in Talent.dbc. So we
# read the two DBCs directly, once, and build spell -> (tree, rank) ourselves.
#
# DBC format (WDBC): 20-byte header (magic, records, fields, recordSize,
# stringBlockSize), fixed-width uint32 records, then a string block that string
# fields index into.
#
# Field layouts are from the core's own structs (src/server/shared/DataStores/
# DBCStructure.h) rather than guessed:
#   Talent.dbc     0 id, 1 TalentTab, 2 row, 3 col, 4-12 RankID (5 used, rest 0)
#   TalentTab.dbc  0 id, 1-16 name_lang, 17 nameFlags, 18 icon, 20 ClassMask,
#                  21 petTalentMask, 22 tabpage (the 0/1/2 tree slot)

_talent_cache = None


def read_dbc(path):
    """(records, strings) — records as tuples of uint32, strings as a bytes blob."""
    blob = path.read_bytes()
    magic, n_rec, n_field, rec_size, str_size = struct.unpack_from("<4sIIII", blob, 0)
    if magic != b"WDBC":
        raise RuntimeError(f"{path.name}: not a WDBC file")
    body = 20
    strings = blob[body + n_rec * rec_size:body + n_rec * rec_size + str_size]
    recs = [struct.unpack_from(f"<{n_field}I", blob, body + i * rec_size)
            for i in range(n_rec)]
    return recs, strings


def dbc_string(strings, offset):
    end = strings.find(b"\0", offset)
    return strings[offset:end if end >= 0 else None].decode("utf-8", "replace")


def talent_tables():
    """{spellId: (tabId, points)} and {tabId: {name, page, classMask}}; ({}, {})
    if the DBCs are missing (a checkout without client data), which degrades the
    picker to "no spec shown" rather than failing.

    `points` is the spell's RANK, and it is the whole reason this mapping is not
    just spell -> tab: a talent is stored at its current rank only (Player::
    addTalent removes the rank below it), so a 3/3 talent is ONE row in
    character_talent that cost THREE points. Counting rows reports the number of
    talents taken, not the build. The core's own accounting is the same
    `talentPos->rank + 1` (Player::LearnTalent's spentPoints)."""
    global _talent_cache
    if _talent_cache is not None:
        return _talent_cache
    try:
        tal, _ = read_dbc(ctx.cfg.dbc_dir / "Talent.dbc")
        tabs, tab_strings = read_dbc(ctx.cfg.dbc_dir / "TalentTab.dbc")
    except (OSError, RuntimeError, IndexError, struct.error):
        _talent_cache = ({}, {})
        return _talent_cache

    spell_tab = {}
    for r in tal:
        tab = r[1]
        for rank, spell in enumerate(r[4:13], start=1):   # RankID[9] — high ranks are 0
            if spell:
                spell_tab[spell] = (tab, rank)
    tab_info = {}
    for r in tabs:
        tab_info[r[0]] = {
            "name": dbc_string(tab_strings, r[1]),
            "page": r[22] if len(r) > 22 else 0,
            "classMask": r[20] if len(r) > 20 else 0,
        }
    _talent_cache = (spell_tab, tab_info)
    return _talent_cache


async def character_specs(guids):
    """{guid: {"spec": "Protection", "points": [51, 10, 0]}} for the given guids.

    Uses each character's ACTIVE dual-spec group (characters.activeTalentGroup)
    so the answer matches what the character would actually log in as. The spec
    is simply the tree holding the most points, which is how everyone names a
    build and how PlayerbotAI::IsTank(bySpec) decides too."""
    spell_tab, tab_info = talent_tables()
    if not spell_tab or not guids:
        return {}
    ids = sql_in(guids, sql_int)
    try:
        rows = await mysql_query(
            "characters",
            "SELECT t.guid, t.spell FROM character_talent t "
            "JOIN characters c ON c.guid = t.guid "
            f"WHERE t.guid IN ({ids}) AND (t.specMask & (1 << c.activeTalentGroup))") or []
    except Exception:
        return {}

    per_guid = {}
    for r in rows:
        if len(r) != 2:
            continue
        hit = spell_tab.get(int(r[1]))
        if hit is None:
            continue
        tab, points = hit
        per_guid.setdefault(int(r[0]), {}).setdefault(tab, 0)
        per_guid[int(r[0])][tab] += points

    out = {}
    for guid, spent in per_guid.items():
        # Three trees in tabpage order, so the triple reads like the in-game
        # "51/10/0" everyone quotes — and sums to the character's talent point
        # budget (level - 9), which is the check that catches a bad mapping.
        pages = [0, 0, 0]
        for tab, n in spent.items():
            page = tab_info.get(tab, {}).get("page", 0)
            if 0 <= page < 3:
                pages[page] += n
        best = max(spent, key=lambda t: spent[t])
        out[guid] = {"spec": tab_info.get(best, {}).get("name", "?"), "points": pages}
    return out


# ---------------------------------------------------------------------------
# Browsing
# ---------------------------------------------------------------------------


@router.get("/api/accounts")
async def api_accounts(request: Request):
    """Accounts that own at least one character, for the roster picker's account
    filter. Returns ids as well as names so the character query can filter on the
    integer — nothing user-typed then reaches the SQL (see NAME_RE)."""
    auth_db = auth_db_ident()
    mine = session_account_id(request)
    try:
        rows = await mysql_query(
            "characters",
            "SELECT a.id, a.username, COUNT(c.guid), "
            "SUM(CASE WHEN c.online = 1 THEN 1 ELSE 0 END) "
            f"FROM {auth_db}.account a JOIN characters c ON c.account = a.id "
            "WHERE c.deleteDate IS NULL "
            f"AND a.username NOT LIKE {sql_str(bot_account_prefix() + '%')} "
            "GROUP BY a.id, a.username ORDER BY a.username") or []
        used = await instance_budget()
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(503, f"account query failed: {str(e)[:200]}")

    per_hour = instances_per_hour()
    accounts = []
    for r in rows:
        if len(r) != 4:
            continue
        acct = int(r[0])
        accounts.append({
            "id": acct, "username": r[1],
            "chars": int(r[2]), "online": int(r[3] or 0),
            "instancesLeft": max(0, per_hour - used.get(acct, 0)),
            "mine": acct == mine,
        })
    return {"accounts": accounts, "instancesPerHour": per_hour,
            "myAccountId": mine, "admin": is_admin(request)}


@router.get("/api/characters")
async def api_characters(request: Request, search: str = "", cls: int = 0,
                         minlevel: int = 1, maxlevel: int = 80,
                         faction: str = "", accountId: int = 0,
                         limit: int = 200):
    """Browse real characters across every account, for the roster builder.

    Returns each character's class/level/faction/account plus the two things that
    decide whether it can be drafted right now: `online` (a character a human is
    playing cannot be run) and its account's remaining AccountInstancesPerHour
    budget (each fresh instance costs one, and roster runs always make a fresh
    instance)."""
    auth_db = auth_db_ident()
    mine = session_account_id(request)
    admin = is_admin(request)

    # Random-bot / addclass-pool characters are not offered here at all — a plain
    # `.dc test start` or a test plan is the way to run those.
    where = ["c.deleteDate IS NULL",
             f"a.username NOT LIKE {sql_str(bot_account_prefix() + '%')}"]
    if search:
        # Letters only. The escaping below is the guard now, but the allowlist
        # stays: it is also what keeps % and _ out of the LIKE pattern.
        if not re.fullmatch(r"[A-Za-z]{1,12}", search):
            raise HTTPException(400, "search must be letters only")
        # `characters.name` is utf8mb4_BIN, so a raw LIKE is case-sensitive and
        # typing "thra" found nothing. WoW names are not case-sensitive: the
        # core normalizes every one to Capitalized at creation and rename
        # (ObjectMgr::NormalizePlayerName), so normalizing the needle the same
        # way is the case-insensitive match — and it keeps the prefix LIKE on
        # the name index, which COLLATE-ing the column would throw away.
        where.append(f"c.name LIKE {sql_str(search.capitalize() + '%')}")
    if cls:
        if not 1 <= cls <= 11:
            raise HTTPException(400, "cls must be 1..11")
        where.append(f"c.class = {sql_int(cls)}")
    if not 1 <= minlevel <= 80 or not 1 <= maxlevel <= 80 or minlevel > maxlevel:
        raise HTTPException(400, "bad level range")
    where.append(f"c.level BETWEEN {sql_int(minlevel)} AND {sql_int(maxlevel)}")
    if faction:
        if faction not in ("alliance", "horde"):
            raise HTTPException(400, "faction must be alliance or horde")
        races = ALLIANCE_RACES if faction == "alliance" else HORDE_RACES
        where.append(f"c.race IN ({sql_in(sorted(races), sql_int)})")
    if accountId:
        if accountId < 0:
            raise HTTPException(400, "bad accountId")
        where.append(f"c.account = {sql_int(accountId)}")
    if not 1 <= limit <= 500:
        raise HTTPException(400, "limit must be 1..500")

    sql = (
        "SELECT c.guid, c.name, c.level, c.class, c.race, c.online, "
        "a.id, a.username, IFNULL(g.name,'') "
        "FROM characters c "
        f"JOIN {auth_db}.account a ON a.id = c.account "
        "LEFT JOIN guild_member gm ON gm.guid = c.guid "
        "LEFT JOIN guild g ON g.guildid = gm.guildid "
        "WHERE " + " AND ".join(where) +
        f" ORDER BY c.level DESC, c.name LIMIT {sql_int(limit)}")

    try:
        rows = await mysql_query("characters", sql) or []
        used = await instance_budget()
    except Exception as e:
        raise HTTPException(503, f"character query failed: {str(e)[:200]}")

    per_hour = instances_per_hour()

    # Talent spec for exactly the characters being listed (bounded by `limit`).
    specs = await character_specs([int(r[0]) for r in rows if len(r) == 9])

    chars = []
    for r in rows:
        if len(r) != 9:
            continue
        acct = int(r[6])
        race = int(r[4])
        guid = int(r[0])
        spec = specs.get(guid) or {}
        chars.append({
            "guid": guid, "name": r[1], "level": int(r[2]),
            "cls": int(r[3]), "race": race, "faction": race_faction(race),
            "online": r[5] == "1",
            "accountId": acct, "account": r[7], "guild": r[8],
            "instancesLeft": max(0, per_hour - used.get(acct, 0)),
            # "" when the character has spent no talent points, or when the DBCs
            # are absent. The picker just shows the spec; it deliberately does not
            # turn it into a role verdict — Feral and the DK trees are genuinely
            # ambiguous, and the run record's detectedRole (computed server-side
            # from PlayerbotAI::IsTank/IsHeal on the live character) is the
            # authority on that.
            "spec": spec.get("spec", ""),
            "specPoints": spec.get("points", []),
            # Whether THIS tester may draft it. The picker greys out the rest
            # rather than hiding them, so "why can't I pick Bob's tank?" has a
            # visible answer; start-roster enforces the same rule server-side.
            "owned": admin or acct == mine,
        })
    return {"characters": chars, "instancesPerHour": per_hour,
            "myAccountId": mine, "admin": admin}


# ---------------------------------------------------------------------------
# Saved rosters
# ---------------------------------------------------------------------------


def load_rosters():
    """{name: {"members": [...], "owner": "user"}}.

    Rosters used to be stored as a bare list of names. Those load as a roster
    with no owner, which is exactly right: nobody claimed it, so the first
    person to save over it becomes its owner.
    """
    try:
        data = json.loads(
            ctx.cfg.rosters_file.read_text(encoding="utf-8", errors="replace"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return {}
    if not isinstance(data, dict):
        return {}
    out = {}
    for name, value in data.items():
        if isinstance(value, list):
            out[name] = {"members": value, "owner": ""}
        elif isinstance(value, dict) and isinstance(value.get("members"), list):
            out[name] = {"members": value["members"],
                         "owner": str(value.get("owner") or "")}
    return out


def save_rosters(data):
    path = ctx.cfg.rosters_file
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(data, indent=2), encoding="utf-8")
    tmp.replace(path)


class RosterSaveRequest(BaseModel):
    name: str
    members: list   # exactly 5, ordered tank, heal, dps, dps, dps


def validate_member_names(members):
    if len(members) != 5:
        raise HTTPException(400, "a roster is exactly 5 characters "
                                 "(tank, heal, dps, dps, dps)")
    seen = set()
    for n in members:
        if not NAME_RE.fullmatch(n or ""):
            raise HTTPException(400, f"'{n}' is not a valid character name")
        if n.lower() in seen:
            raise HTTPException(400, f"'{n}' is listed twice — one character "
                                     "cannot fill two slots")
        seen.add(n.lower())


@router.get("/api/rosters")
async def api_rosters(request: Request):
    """Saved rosters. Test Deck-side only — the worldserver has no roster
    store, it just receives five names on the command line."""
    s = request_session(request)
    me = s.username if s else ""
    admin = is_admin(request)
    return {"rosters": [
        {"name": k, "members": v["members"], "owner": v["owner"],
         "mine": bool(v["owner"]) and v["owner"] == me,
         "writable": admin or not v["owner"] or v["owner"] == me}
        for k, v in sorted(load_rosters().items())]}


@router.post("/api/rosters")
async def api_rosters_save(req: RosterSaveRequest, request: Request):
    """Save a roster. Creating one is open to any tester; overwriting SOMEBODY
    ELSE'S is not — the roster list is shared, so a silent overwrite is one
    tester editing another's saved work."""
    if not ROSTER_ID_RE.fullmatch(req.name):
        raise HTTPException(400, "roster name must be 1-40 chars of letters, "
                                 "digits, space, _ or -")
    validate_member_names(req.members)
    s = request_session(request)
    me = s.username if s else ""
    rosters = load_rosters()
    existing = rosters.get(req.name)
    if existing and existing["owner"] and existing["owner"] != me \
            and not is_admin(request):
        raise HTTPException(
            403, f"'{req.name}' belongs to {existing['owner']} — save it under "
                 f"another name, or ask a GM level {ctx.cfg.admin_gmlevel} to "
                 "overwrite it")
    rosters[req.name] = {"members": req.members, "owner": me}
    save_rosters(rosters)
    return {"ok": True, "name": req.name}


@router.delete("/api/rosters/{name}")
async def api_rosters_delete(name: str, request: Request):
    """Delete a roster. Your own needs no special level; anyone else's is an
    admin action, as it always was."""
    rosters = load_rosters()
    if name not in rosters:
        raise HTTPException(404, "no such roster")
    s = request_session(request)
    me = s.username if s else ""
    if rosters[name]["owner"] != me or not me:
        require_admin(request, "deleting another tester's saved roster")
    del rosters[name]
    save_rosters(rosters)
    return {"ok": True}


# ---------------------------------------------------------------------------
# Launching
# ---------------------------------------------------------------------------


class RosterStartRequest(BaseModel):
    dungeon: str
    members: list
    heroic: bool = False


@router.post("/api/testruns/start-roster")
async def api_testruns_start_roster(req: RosterStartRequest, request: Request):
    """Launch a hand-picked party at a dungeon.

    Every refusal that can be decided from the DB happens here, so the picker can
    explain itself before a command is issued: bad/duplicate names, a character
    that does not exist, one a human is currently playing, a cross-faction party,
    and an account that has burned its AccountInstancesPerHour budget. The
    worldserver re-checks the live-state ones (it is the authority, and `.dc test
    start` is typeable by hand)."""
    validate_member_names(req.members)

    _cat, rows = await catalogue_rows()
    check_dungeon(rows, req.dungeon, req.heroic)

    auth_db = auth_db_ident()

    names_sql = sql_in(req.members)
    try:
        rows_db = await mysql_query(
            "characters",
            "SELECT c.name, c.level, c.race, c.online, a.id, a.username "
            "FROM characters c "
            f"JOIN {auth_db}.account a ON a.id = c.account "
            f"WHERE c.name IN ({names_sql}) AND c.deleteDate IS NULL") or []
        used = await instance_budget()
    except Exception as e:
        raise HTTPException(503, f"character lookup failed: {str(e)[:200]}")

    found = {r[0].lower(): r for r in rows_db if len(r) == 6}
    missing = [n for n in req.members if n.lower() not in found]
    if missing:
        raise HTTPException(400, "no character named " +
                                 ", ".join(f"'{n}'" for n in missing))

    # The authorization check, and it is deliberately after the existence
    # check so a tester probing for other people's character names learns
    # nothing they could not learn from the picker they already have.
    if not is_admin(request):
        mine = session_account_id(request)
        theirs = sorted({r[0] for r in found.values() if int(r[4]) != mine})
        if theirs:
            raise HTTPException(
                403, "these characters belong to another account: " +
                     ", ".join(theirs) +
                     f" — running someone else's characters needs GM level "
                     f"{ctx.cfg.admin_gmlevel}")

    online = [r[0] for r in found.values() if r[3] == "1"]
    if online:
        raise HTTPException(409, "logged in (a roster character must be offline): " +
                                 ", ".join(sorted(online)))

    factions = {race_faction(int(r[2])) for r in found.values()}
    if len(factions) > 1:
        raise HTTPException(400, "a roster cannot span factions — "
                                 "a cross-faction party cannot be grouped")

    per_hour = instances_per_hour()
    # Distinct accounts: five alts on one account share a single instance entry,
    # so the cost is one slot per ACCOUNT, not one per character.
    for acct in {int(r[4]) for r in found.values()}:
        if used.get(acct, 0) >= per_hour:
            acct_name = next(r[5] for r in found.values() if int(r[4]) == acct)
            raise HTTPException(
                429, f"account '{acct_name}' has entered {per_hour} instances in the "
                     "last hour (AccountInstancesPerHour) — the core would refuse the "
                     "teleport. Wait for a slot, pick other characters, or raise the "
                     "config.")

    # Order matters: roles are positional (tank, heal, dps, dps, dps).
    cmd = f".dc test start {req.dungeon} party={','.join(req.members)}"
    if req.heroic:
        cmd += " heroic"
    audit(request, cmd)
    reply = await ctx.bridge.exec(cmd)
    result = bridge_mod.public_reply(reply, cmd)
    result["roles"] = dict(zip(req.members, ROSTER_ROLES))
    return result
