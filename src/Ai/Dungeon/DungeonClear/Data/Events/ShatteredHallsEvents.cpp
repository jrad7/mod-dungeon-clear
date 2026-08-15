/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "Creature.h"
#include "Player.h"

// --- The Shattered Halls (map 540) — drop past the door, before the first boss -
//
// On the way to the first boss, Grand Warlock Nethekurse (16807, bit 0), the
// route crosses a BREAK in the navmesh at a door: the party stands at the door
// lip (122.78, 249.59, -15) and must DROP ~33yd down and ~40yd forward to
// (132.78, 209.68, -47.84) to continue. The drop is DIAGONAL and off-mesh — the
// two surfaces sit on disconnected mesh islands — so neither a pure-vertical
// DropInHole (it would fall in the wrong column) nor a ballistic Jump (a big
// diagonal drop clips/overshoots) is reliable. Per the user this door must NOT
// block or stop progress, so we TELEPORT the whole party across the instant the
// tank reaches the door lip, exactly like The Slave Pens' drop past Mennu.
//
// A roster OBJECTIVE anchor (RegisterShatteredHallsRoster) sits at the door lip
// and borrows encounterIndex 0 (Nethekurse's bit): the Objective-before-Boss
// tie-break in Apply() sorts it AHEAD of Nethekurse at the shared key, so on a
// fresh clear the tank visits this objective FIRST, teleports past the door, and
// only then heads to Nethekurse. Boss-nav drives the walk to the lip.
//
// Anchored (not Conditional): the drop is purely positional and on the critical
// path, so it rides the objective like Slave Pens / Wailing Caverns' drops. Not
// Persistent: TeleportParty is a single synchronous step that completes the same
// tick it fires (and is idempotent on a restart — Done immediately if the leader
// is already on the landing), so there is no multi-tick fall for a tick-gap to
// rewind.

// --- The Shattered Halls (map 540) — run the flame gauntlet (after Nethekurse)
//
// Past Nethekurse the route climbs onto a raised corridor (z~2) that runs EAST
// along y~314, from the entry (~x300) to the archers' ledge (~x515). This is the
// famous flame gauntlet, driven by boss_porung.cpp:
//
//   * A Shattered Hand Scout (17693) at (341,315) watches the corridor. The
//     instant ANY player comes within 50yd of it at corridor level (z > -3) —
//     which our own tank does just by walking in — it yells "Invaders breached!",
//     runs to the far end, and STARTS the gauntlet: the first zealots charge
//     (SetInCombatWithZone), Blood Guard Porung summons fresh zealot waves every
//     45s, and the two Shattered Hand Archers (17427, at x515) begin casting
//     Shoot Flame Arrow (30952) on a loop.
//   * Each flame-arrow cast drops one BLAZE (GO 181915, a type-6 damage trap that
//     casts 30979) on a Flame Arrow anchor (17687) WITHIN 15yd OF A PLAYER — so
//     blazes only appear along the x290..469 band, and the far end (x510+, past
//     the last anchor at x469) is fire-free.
//   * The gauntlet does NOT end on a boss kill in normal mode (the Blood Guard
//     17461 that fills the boss role there has no script). It winds down when the
//     party gets ~250yd from the Scout (i.e. proceeds on toward O'mrogg, which
//     despawns the residual waves) — and killing the two Archers stops the fire
//     outright (FireArrows() returns false with no archer alive). In HEROIC the
//     real mini-boss Porung (20923) sits at the same far spot and IS a kill.
//
// Per the user this must be a continuous PUSH, not stop-and-kill on every pack
// (far too slow while fire rains). So — exactly like the Mechanar bridge gauntlet
// — a PERSISTENT anchored event stands the pull pipeline down and drives the run:
// walk onto the corridor (arms the Scout), CLEAR the near cluster at the midpoint,
// advance, then CLEAR the far cluster (archers + far zealots + Blood Guard/Porung)
// on the fire-free ledge. Folding the far boss into the ClearRadius (user's call)
// keeps it working in both normal and heroic without a heroic-only roster row.
//
// Persistent so (a) the pull pipeline stays down through the whole corridor and
// (b) the many wave/combat gaps don't rewind the step list. Step 0 (MoveTo entry)
// bumps stepIndex so the persistence sticky-trigger engages and the tank may run
// the whole corridor from the anchor. The out-of-combat "don't stand in the fire"
// behavior is separate (Dc hazard avoidance keyed on GO_BLAZE 181915).
//
// COORDS ARE FIRST-CUT — validate the four anchors + the two cluster centres
// against the real map-540 mmaps with the DcNavHarness probe (as Mechanar did)
// before trusting them; the corridor climbs z-8 -> z2 out of Nethekurse's room.

// --- The Shattered Halls (map 540) — sweep the stealth-assassin L-hallway ------
//
// Between Warbringer O'mrogg (16809, at 375,57,-7, bit 1) and Warchief Kargath
// Bladefist (16808, at 231,-83,5, bit 2) the route runs an L-SHAPED hallway lined
// with STEALTHED Shattered Hand Assassins (17695; creature_template_addon aura
// 30991 + SmartAI "On Create - Cast Stealth"). User-probed endpoints:
//
//     start (374.83, 0.31, 1.73)  --  N-S leg down x~375-382 (assassins at
//                                     382,-32 / 383,-53 / 381,-75) ...
//     ... corner ~ (382,-85) ...  --  W leg along y~-85..-92 (assassins at
//                                     368,-88 / 325,-92 / 291,-91) ...
//     end (293.67, -83.02, 1.91)  --  mouth of Kargath's arena.
//   (A seventh static spawn sits off to the NE at 482,55 — a lone sentinel on the
//    O'MROGG APPROACH, before that fight; it is handled by its own event/objective
//    below, ordered ahead of O'mrogg, NOT by this post-O'mrogg hallway sweep.)
//
// Left to the default pull this DEADLOCKS, and — the key correction over the
// first cut — a ClearRadius push does NOT fix it. An assassin flags the party
// into combat but STAYS STEALTHED and doesn't melee; the bots can't engage it and
// the run wedges "in combat, nothing to hit". The reason a ClearRadius sweep also
// failed: it engages via DcTargeting::NearestHostileNearPoint ->
// AttackersValue::IsPossibleTarget, which HARD-GATES on `bot->CanSeeOrDetect()`.
// A stealthed assassin the tank hasn't detected fails that test, so the gate
// found "no hostile", reported the zone clear, and NEVER engaged. (The grid-scan
// FarTargets set DOES contain the stealthed unit, but IsPossibleTarget filters it
// back out — my earlier "sees through stealth" claim was wrong.)
//
// Real fix: a PERSISTENT KillCreatureEngage BY ENTRY. FindNearestCreature(17695)
// is a grid scan with NO stealth/visibility filter, and Unit::Attack has no
// visibility gate — so EngageDirect walks the tank to the assassin's exact
// position and swings; the first point of damage breaks stealth. One step sweeps
// all six in-hallway assassins ("any alive 17695 within the seek radius" keeps it
// Running, engaging the nearest each tick) down the N-S leg then the W leg.
//
// Its roster OBJECTIVE borrows encounterIndex 2 (Kargath's bit): the
// Objective-before-Boss tie-break sorts it AHEAD of Kargath, so the tank sweeps
// the assassins and only then engages him. (Map-540 DBC bit order matches the
// script enum: Nethekurse 0, O'mrogg 1, Kargath 2, Porung 3 — the heroic-only
// Porung takes the top bit, NOT Kargath.) The sweep stays east of x=293, clear of
// Kargath's 42yd room (kargathRespawnPos 231,-83), so it can't wake him.
// --- The Shattered Halls (map 540) — wake Grand Warlock Nethekurse ------------
//
// Nethekurse (16807, at 172.66/289.61/-8.11) spawns SetImmuneToAll: his AI's
// Reset() re-immunes him whenever `_canAggro` is false (boss_nethekurse.cpp),
// and the ONLY things that flip it are ACTION_START_INTRO fired from
//   (a) area trigger 4347 (at_rp_nethekurse) — which exists only as a
//       CMSG_AREATRIGGER handler, a packet a bot never sends (same class as the
//       BRD Ring of Law and ZulFarrak Zum'rah bugs), or
//   (b) his own UpdateAI door-watch: chamber door 182539 read at GoState ACTIVE
//       — i.e. LOCKPICKED open, since as a DOOR_TYPE_PASSAGE it otherwise only
//       opens on the boss's own state change.
// Our route teleports past that door's navmesh break (event 1) without ever
// touching it, so in a full-bot run neither path fires: the tank walks into the
// chamber and parks against a permanently immune, passive boss. A human in the
// party fixes it by accident (their client fires AT 4347 on the way in), which
// is why this only deadlocks `dc test` / all-bot runs.
//
// Fix = the Zum'rah pattern verbatim: a Conditional + Repeatable event whose
// predicate reads the exact deadlock signature — Nethekurse alive, within 60yd,
// and still IsImmuneToPC (the very flag ACTION_START_INTRO clears) — and whose
// Custom hook (id 9, StartNethekurseIntro) calls DoAction(ACTION_START_INTRO)
// on his AI: precisely what the area-trigger script does, reusing its own
// `_introStarted` idempotence guard. Once immunity drops the predicate reads
// false forever (fight, RP, or DONE all clear it), so the repeat can never
// spin; Repeatable covers a full despawn/respawn, which reconstructs the AI
// with `_canAggro = false` and re-arms the deadlock. Firing the intro is
// enough on its own: the RP runs, and either he finishes killing his four
// peons and AttackStarts the nearest player himself, or the tank engages him
// first and JustEngagedWith cancels the RP — both end in a normal fight.
namespace
{
    // --- wake Nethekurse (first boss immune until his client-only intro) ---
    constexpr uint32 SH_NETHEKURSE = 16807;             // Grand Warlock Nethekurse
    // Comfortably beyond boss engage range so the event fires as soon as
    // boss-nav parks the tank at him, before the engage loop settles into its
    // deadlock — but near enough that it can never fire from across the dungeon
    // (mirrors ZF_ZUMRAH_SCAN).
    constexpr float SH_NETHEKURSE_SCAN = 60.0f;
    constexpr uint32 SH_HOOK_START_NETHEKURSE_INTRO = 9;  // ObjectiveHookRegistry id

    bool ShNethekurseDormant(Player* bot, AiObjectContext* context);

    // Corridor axis is y~314, z~2, x increasing east.
    constexpr float SH_ENTRY_X = 300.0f, SH_ENTRY_Y = 314.0f, SH_ENTRY_Z = 2.0f;
    // Near cluster (3 zealots + the Scout) sits at x332..341 and charges the tank;
    // centre the clear at the midpoint so the chasers are swept up here.
    constexpr float SH_MID_X = 400.0f, SH_MID_Y = 315.0f, SH_MID_Z = 2.0f;
    constexpr float SH_MID_RADIUS = 55.0f;   // covers x345..455 (the charged near pack)
    // Advance point before the far ledge (keeps the run moving, Mechanar-style).
    constexpr float SH_ADVANCE_X = 490.0f, SH_ADVANCE_Y = 316.0f, SH_ADVANCE_Z = 2.0f;
    // Far cluster: ~12 zealots + 2 archers + Blood Guard(17461)/Porung(20923) at
    // x499..515. This ledge is PAST the last blaze anchor (x469), so it is fire-free
    // — the party's post-combat rest here is safe.
    constexpr float SH_FAR_X = 508.0f, SH_FAR_Y = 316.0f, SH_FAR_Z = 2.0f;
    constexpr float SH_FAR_RADIUS = 32.0f;   // covers x476..540 (archers + far pack)
    constexpr float SH_GAUNTLET_ZBAND = 12.0f;
    // Wave survival + the far-pack kill can run a couple of minutes; keep the long
    // holds from escalating to a stall for the human.
    constexpr uint32 SH_GAUNTLET_TIMEOUT = 300000;  // 5 min per clear

    // --- stealth-assassin L-hallway (O'mrogg -> Kargath), z~1.8 ---
    // Real endpoints (user-probed). The hall is an L: a N-S leg down x~375-382
    // (y 0.3 -> ~-85) then a W leg along y~-85..-92 (x ~382 -> 293), ending at the
    // mouth of Kargath's arena.
    constexpr uint32 SH_ASSASSIN_ENTRY = 17695;      // Shattered Hand Assassin (stealthed)
    constexpr float SH_ASSN_START_X = 374.83f, SH_ASSN_START_Y = 0.31f, SH_ASSN_START_Z = 1.73f;
    constexpr float SH_ASSN_END_X = 293.67f, SH_ASSN_END_Y = -83.02f, SH_ASSN_END_Z = 1.91f;
    // Seek radius (from the moving tank) for the KillCreatureEngage sweep. Big
    // enough that the L's longest inter-assassin gap never falsely reads "clear",
    // small enough to leave the lone (482,55) sentinel off to the NE out of scope.
    constexpr float SH_ASSN_SEEK_RADIUS = 120.0f;
    // Stealthed rogues die fast, but keep the long seek-and-sweep hold from
    // escalating to a stall for the human while the tank walks the whole L.
    constexpr uint32 SH_ASSN_TIMEOUT = 180000;   // 3 min for the full sweep

    // --- lone stealthed sentinel on the O'MROGG APPROACH (guid 151185) ---
    // Sits NE of O'mrogg, encountered BEFORE the O'mrogg fight — a separate anchor
    // ordered ahead of O'mrogg (bit 1), not part of the post-O'mrogg L-hallway.
    constexpr float SH_SENTINEL_X = 481.99f, SH_SENTINEL_Y = 55.09f, SH_SENTINEL_Z = 1.94f;
    // Tight seek so this event only claims the sentinel: the nearest OTHER assassin
    // (382,-32, the hallway's first) is ~133yd away, well outside.
    constexpr float SH_SENTINEL_SEEK_RADIUS = 60.0f;
    constexpr uint32 SH_SENTINEL_TIMEOUT = 90000;   // 90s to reach + kill the one
}

void RegisterShatteredHallsEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(540, 1, "Drop past the door")
                      .Anchored(/*orderIndex, doc-only*/ 0)
                      .TeleportParty(/*door lip*/ 122.78f, 249.59f, -15.0f,
                                     /*landing*/  132.78f, 209.68f, -47.84f)
                      .Build());

    // RUN THE FLAME GAUNTLET, THEN CLEAR THE ARCHERS' LEDGE.
    // PERSISTENT: stands down the pull pipeline (no stop-and-kill on every pack)
    // and survives the wave combat gaps. Step 0 (MoveTo entry) arms the Scout and
    // makes the persistence trigger sticky so the tank runs the corridor from the
    // anchor. The two ClearRadius zones are position-based (any hostile), so the
    // scripted wave composition never needs enumerating; the far zone folds in the
    // archers (killing them stops the fire) and the Blood Guard / Porung.
    out.push_back(
        EventBuilder(540, 2, "Run the flame gauntlet")
            .Anchored(/*orderIndex (doc)*/ 1)
            .Persistent()
            .MoveTo(SH_ENTRY_X, SH_ENTRY_Y, SH_ENTRY_Z, /*radius*/ 8.0f)
            .ClearRadius(SH_MID_X, SH_MID_Y, SH_MID_Z, SH_MID_RADIUS, SH_GAUNTLET_ZBAND)
                .Timeout(SH_GAUNTLET_TIMEOUT)
            .MoveTo(SH_ADVANCE_X, SH_ADVANCE_Y, SH_ADVANCE_Z, /*radius*/ 8.0f)
            .ClearRadius(SH_FAR_X, SH_FAR_Y, SH_FAR_Z, SH_FAR_RADIUS, SH_GAUNTLET_ZBAND)
                .Timeout(SH_GAUNTLET_TIMEOUT)
            .Build());

    // SWEEP THE STEALTH-ASSASSIN L-HALLWAY BEFORE KARGATH.
    // PERSISTENT: stands the pull pipeline down and survives the combat gaps
    // between kills as the tank walks the whole L. Step 0 (MoveTo start) arms the
    // persistence sticky. The sweep is a KillCreatureEngage BY ENTRY — NOT a
    // ClearRadius. This is the whole fix: ClearRadius engages via
    // NearestHostileNearPoint -> AttackersValue::IsPossibleTarget, which hard-gates
    // on `bot->CanSeeOrDetect()` — and a stealthed assassin the tank hasn't
    // detected fails that, so ClearRadius reported "clear" and NEVER engaged (the
    // party sat deadlocked in combat with an un-engaged stealthed rogue).
    // KillCreatureEngage instead finds the assassin by ENTRY via FindNearestCreature
    // (a grid scan with NO stealth/visibility filter) and EngageDirect walks the
    // tank to its exact position and Attack()s it (Unit::Attack has no visibility
    // gate) — the first point of damage breaks stealth. One step sweeps all seven:
    // "any alive 17695 within the seek radius" keeps it Running, engaging the
    // nearest each tick, so the tank clears the N-S leg then the W leg. A trailing
    // MoveTo lands the tank at the hall's Kargath-side mouth for a clean handoff.
    out.push_back(
        EventBuilder(540, 3, "Sweep the assassin hallway")
            .Anchored(/*orderIndex (doc)*/ 2)
            .Persistent()
            .MoveTo(SH_ASSN_START_X, SH_ASSN_START_Y, SH_ASSN_START_Z, /*radius*/ 10.0f)
            .KillCreatureEngage(SH_ASSASSIN_ENTRY, /*count (doc; "any alive")*/ 6,
                                SH_ASSN_SEEK_RADIUS)
                .Timeout(SH_ASSN_TIMEOUT)
            .MoveTo(SH_ASSN_END_X, SH_ASSN_END_Y, SH_ASSN_END_Z, /*radius*/ 10.0f)
            .Build());

    // KILL THE LONE STEALTHED SENTINEL ON THE O'MROGG APPROACH.
    // Same stealth-engage mechanism as the hallway sweep (KillCreatureEngage BY
    // ENTRY — FindNearestCreature has no stealth filter, Unit::Attack no visibility
    // gate), but a SEPARATE objective ordered BEFORE O'mrogg (bit 1). The tight
    // seek radius (60yd) keeps this event to the single sentinel; the hallway's
    // assassins are ~133yd south, so the two 17695 sweeps never overlap.
    out.push_back(
        EventBuilder(540, 4, "Kill the O'mrogg-approach assassin")
            .Anchored(/*orderIndex (doc)*/ 1)
            .Persistent()
            .MoveTo(SH_SENTINEL_X, SH_SENTINEL_Y, SH_SENTINEL_Z, /*radius*/ 10.0f)
            .KillCreatureEngage(SH_ASSASSIN_ENTRY, /*count (doc; "any alive")*/ 1,
                                SH_SENTINEL_SEEK_RADIUS)
                .Timeout(SH_SENTINEL_TIMEOUT)
            .Build());

    // Wake Nethekurse: fire his client-only intro once the party reaches him, so
    // he drops SetImmuneToAll and fights an all-bot party the same way he fights
    // a human whose client tripped area trigger 4347 on the walk in. Folded
    // under Nethekurse in the panel — it is his pull, not a step of its own.
    out.push_back(EventBuilder(540, 5, "Wake Grand Warlock Nethekurse")
                      .Conditional(&ShNethekurseDormant)
                      .Repeatable()
                      .PanelBeforeBoss(SH_NETHEKURSE)
                      .Custom(SH_HOOK_START_NETHEKURSE_INTRO)
                      .Build());
}

// --- the wake-up gate (event 5, repeatable) ------------------------------
// DUE while Nethekurse is alive, within scan range, and still carrying the
// spawn-time PC-immunity his intro clears. Reads false the instant
// ACTION_START_INTRO lands (hook 9, a human's area trigger, or a lockpicked
// door — SetImmuneToAll(false) is synchronous inside all of them) and once he
// is dead or out of range, so the repeat can never spin: the only state that
// keeps it true is the exact deadlock it fixes.
//
// Tests IsImmuneToPC DIRECTLY rather than asking IsValidAttackTarget — the
// same lesson as ZfZumrahAsleep: read the one value the whole bug is about,
// which is also the value the hook's DoAction writes.
namespace
{
    bool ShNethekurseDormant(Player* bot, AiObjectContext* /*context*/)
    {
        Creature* nethekurse = bot->FindNearestCreature(SH_NETHEKURSE, SH_NETHEKURSE_SCAN);
        if (!nethekurse || !nethekurse->IsAlive())
            return false;

        // Still immune-to-PC => nobody has started his intro.
        return nethekurse->IsImmuneToPC();
    }
}

// --- roster patch --------------------------------------------------------
void RegisterShatteredHallsRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // The auto-roster derives all bosses (Nethekurse 16807 / bit 0, Omrogg,
    // Kargath) from their static spawns, so no boss surgery is needed. But the
    // path to Nethekurse crosses a navmesh BREAK at a door: the party stands at
    // the lip (122.78, 249.59, -15) and must drop ~33yd down to
    // (132.78, 209.68, -47.84), which sits on a disconnected mesh island boss-nav
    // can't route to.
    //
    // Add a travel OBJECTIVE at the door lip, ordered BEFORE Nethekurse. It
    // borrows encounterIndex 0 (Nethekurse's bit): the Objective-before-Boss
    // tie-break in Apply() sorts it AHEAD of Nethekurse at the shared key, so the
    // tank visits this objective and only then Nethekurse. Sharing the bit is
    // safe — an objective is filtered by the cleared-anchor latch, never the
    // completion mask (NextDungeonBossValue keys the mask to Boss anchors only),
    // so Nethekurse's eventual kill can't retro-complete it. Its eventId 1
    // (ShatteredHallsEvents.cpp) teleports the whole party across the break the
    // instant the tank reaches the lip.
    //
    // Second, a travel OBJECTIVE at the flame-gauntlet ENTRY, ordered AFTER
    // Nethekurse and BEFORE O'mrogg. It borrows encounterIndex 1 — which the
    // map-540 NORMAL DBC assigns to O'MROGG (normal bits: Nethekurse 0,
    // O'mrogg 1, Kargath 2 — Porung has no normal row at all; on HEROIC the DBC
    // inserts Porung AT bit 1, shifting O'mrogg to 2 and Kargath to 3 — see the
    // HeroicOnly patch below). So the Objective-before-Boss tie-break sorts
    // this objective AHEAD of O'mrogg at the shared key — the flame gauntlet runs,
    // then O'mrogg. Its eventId 2 (the persistent "Run the flame gauntlet" event)
    // drives the whole corridor: run in, clear the midpoint, clear the archers'
    // ledge. Sharing bit 1 is safe — an objective is filtered by the cleared-anchor
    // latch, never the completion mask (NextDungeonBossValue keys the mask to Boss
    // anchors only).
    {
        BossRosterPatch p;
        p.mapId = 540;
        p.add = {
            MakeObjective(OBJ(1), /*encounterIndex*/ 0, 540,
                          "Drop past the door",
                          122.78f, 249.59f, -15.0f,
                          /*arriveRadius*/ 6.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 1),
            MakeObjective(OBJ(2), /*encounterIndex*/ 1, 540,
                          "Run the flame gauntlet",
                          /*entry anchor*/ 300.0f, 314.0f, 2.0f,
                          /*arriveRadius*/ 10.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 2),
            // Third, a travel OBJECTIVE at the stealth-assassin hallway START,
            // ordered AFTER O'mrogg (bit 1) and BEFORE Kargath (bit 2). It borrows
            // encounterIndex 2 — Kargath's own kill-bit — so the Objective-before-
            // Boss tie-break sorts it AHEAD of Kargath at that shared key: the tank
            // walks into the L-hallway and sweeps the stealthed Shattered Hand
            // Assassins (17695) BY ENTRY before engaging Kargath. (Map-540 NORMAL
            // DBC bits are Nethekurse 0, O'mrogg 1, Kargath 2 — an earlier cut used
            // 3 and the objective sorted PAST Kargath to the end of the clear. On
            // HEROIC Kargath shifts to bit 3, so the HeroicOnly patch below re-keys
            // this objective to 3 there.) Sharing
            // bit 2 is safe — an objective is filtered by the cleared-anchor latch,
            // never the completion mask (NextDungeonBossValue keys the mask to Boss
            // anchors only), so Kargath's eventual kill can't retro-complete it. Its
            // eventId 3 is the persistent "Sweep the assassin hallway" event.
            MakeObjective(OBJ(3), /*encounterIndex*/ 2, 540,
                          "Sweep the assassin hallway",
                          SH_ASSN_START_X, SH_ASSN_START_Y, SH_ASSN_START_Z,
                          /*arriveRadius*/ 10.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 3),
            // Fourth, the lone stealthed sentinel on the O'MROGG APPROACH, ordered
            // BEFORE O'mrogg. It borrows encounterIndex 1 — O'mrogg's own bit — so
            // the Objective-before-Boss tie-break sorts it AHEAD of O'mrogg. It also
            // shares bit 1 with the flame-gauntlet objective (OBJ 2); at an equal
            // key the stable_sort keeps insertion order, and OBJ(2) is added first,
            // so the sequence is gauntlet -> sentinel -> O'mrogg. Its eventId 4
            // (persistent "Kill the O'mrogg-approach assassin") engages the sentinel
            // by entry. Sharing bit 1 is safe — objectives latch on the cleared
            // anchor, never the completion mask.
            MakeObjective(OBJ(4), /*encounterIndex*/ 1, 540,
                          "Kill the O'mrogg-approach assassin",
                          SH_SENTINEL_X, SH_SENTINEL_Y, SH_SENTINEL_Z,
                          /*arriveRadius*/ 10.0f, /*gateEntry*/ 0,
                          /*hook*/ 0, /*eventId*/ 4),
        };
        t.push_back(std::move(p));
    }

    // HEROIC index-shift correction. The heroic DBC INSERTS Blood Guard Porung
    // at bit 1 (verified against DungeonEncounter.dbc difficulty-1 rows:
    // Nethekurse 0, Porung 1, O'mrogg 2, Kargath 3 — NOT Porung-on-top as the
    // script enum suggests), which shifts O'mrogg to 2 and Kargath to 3. The
    // borrowed keys above then land differently on heroic:
    //   * OBJ(1) key 0 — still before Nethekurse. Fine.
    //   * OBJ(2)/OBJ(4) key 1 — now sort before PORUNG, whose ledge spot the
    //     gauntlet's far ClearRadius already covers. Fine (gauntlet -> sentinel
    //     -> Porung-if-somehow-alive -> O'mrogg reads correctly).
    //   * OBJ(3) key 2 — now O'MROGG's bit, which would send the tank to the
    //     stealth L-hallway BEFORE O'mrogg. Wrong: the hallway sits between
    //     O'mrogg and Kargath. Re-key it to 3 (Kargath's heroic bit) so the
    //     Objective-before-Boss tie-break keeps sweep-then-Kargath.
    {
        BossRosterPatch p;
        p.mapId = 540;
        p.gate = DcDifficultyGate::HeroicOnly;
        p.reorder = {{OBJ(3), 3}};
        t.push_back(std::move(p));
    }
}
