/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonWingRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"

// Registry cross-reference + persistence lint over the authored event data
// (events-system review F1/F2/F3, plus the F9 anchored-event wiring check). These
// registries reference each other by bare integers, and every dangling reference
// fails SILENTLY at runtime (an objective latches Done instantly, a required event
// never fires, an objective vanishes from a wing). These tests turn each of those
// silent-failure classes into a red test at build time.

namespace
{
    // A step kind whose rewind-on-gap is dangerous: teleport/drop/jump are one-way,
    // escort/engage/clear span combat gaps, an instance-data MoveTo garrisons a
    // gate. A multi-step anchored event containing one of these MUST be Persistent
    // or a mid-fight combat gap rewinds it (the module's most-repeated bug class —
    // see the dc-multihop-teleport-persistent memory). A KillCreatureEngage is a
    // KillCreature step with engage set.
    bool IsRewindHazardStep(EventStep const& s)
    {
        switch (s.kind)
        {
            case EventStepKind::TeleportParty:
            case EventStepKind::DropInHole:
            case EventStepKind::Jump:
            case EventStepKind::EscortCreature:
            case EventStepKind::ClearRadius:
                return true;
            case EventStepKind::KillCreature:
                return s.engage;  // KillCreatureEngage seeks + pulls across gaps
            case EventStepKind::MoveTo:
                return s.instanceDataId >= 0;  // instance-data garrison gate
            default:
                return false;
        }
    }

    bool HasRewindHazard(DungeonEvent const& ev)
    {
        for (EventStep const& s : ev.steps)
            if (IsRewindHazardStep(s))
                return true;
        return false;
    }

    // Intentionally non-persistent multi-step anchored events (each carries an
    // in-file justification). Whitelisted so the F1 lint stays green while still
    // catching the next author who forgets .Persistent(). Keyed by {mapId, eventId}.
    //   - Stratholme (329) event 5 "Dathrohan -> Balnazzar": two idempotent
    //     KillCreatureEngage kill-gates, one continuous fight at a fixed spot, no
    //     WaitForSpawn to false-complete across a gap — a restart-from-0 re-evaluates
    //     correctly (StratholmeEvents.cpp:29-35).
    bool IsNonPersistentWhitelisted(uint32 mapId, uint32 eventId)
    {
        return mapId == 329 && eventId == 5;
    }

    // An "arrival" step forces the tank to a specific spot before it can report
    // Done: MoveTo/Jump HopTo until within radius, UseGameObject/Gossip approach
    // their target, and the escort/drop/teleport primitives all pin the leader to
    // a checkpoint. A conditional event that contains ONE of these can never
    // complete while the tank is far from the anchor. Everything else (ClearRadius,
    // KillCreature, WaitForSpawn/GOState, Wait, CastSpell, UseItem, Custom) can
    // report Done from wherever the tank stands the instant the condition turns
    // true — so an event built ONLY from those latches "done" from afar.
    bool HasArrivalStep(DungeonEvent const& ev)
    {
        for (EventStep const& s : ev.steps)
            switch (s.kind)
            {
                case EventStepKind::MoveTo:
                case EventStepKind::Jump:
                case EventStepKind::UseGameObject:
                case EventStepKind::Gossip:
                case EventStepKind::EscortCreature:
                case EventStepKind::DropInHole:
                case EventStepKind::TeleportParty:
                    return true;
                default:
                    break;
            }
        return false;
    }

    // Conditional events that legitimately complete WITHOUT any arrival step —
    // their single/all-gate step list is safe ONLY because their activation
    // predicate can't read true while the tank is far from the anchor. That
    // near-only guarantee lives in the opaque condition function (a test can't
    // inspect it), so each such event is vetted by hand and listed here with the
    // mechanism that makes it near-only. Keyed by {mapId, eventId}. See the
    // ConditionalEventsWithoutArrivalStepAreProximityVetted tripwire.
    bool IsNearGatedConditionalWhitelisted(uint32 mapId, uint32 eventId)
    {
        struct Row { uint32 mapId; uint32 eventId; };
        static constexpr Row kRows[] = {
            // Stratholme Timmy pre-clear: StrTimmyGated returns false until the
            // tank is within 60yd of Timmy's spawn (the #5 fix — a creature-
            // presence condition read true from map load and false-latched at the
            // instance entrance without this gate).
            {329, 6},
            // Stratholme ziggurat acolyte clears: gated on monotonic instance data
            // (GetData(TYPE_ZIGGURATx) == 1), which flips only when the ziggurat
            // boss dies right at the chamber — never true from afar.
            {329, 1},
            {329, 2},
            {329, 3},
            // ZulFarrak Zum'rah wake-up: ZfZumrahAsleep does a 60yd
            // FindNearestCreature for him, so it cannot read true until boss-nav
            // has parked the party at him — and the hook it drives only writes his
            // faction, which is meaningless from afar anyway.
            {209, 3},
            // Black Morass "Close the time rift": BmWaveHostilesActive requires the
            // leader within 250yd of the arena centroid AND (an open Time Rift or
            // a live wave hostile) within a 250yd grid scan OF THE BOT. Its lone
            // step (hook 12, BmDriveWave) OWNS the travel — it walks the tank to
            // the locked rift's keeper, or back to Medivh's ring to clear the
            // drainers — so there is nothing for an arrival step to add. It also
            // never reports Done while the encounter is live, so the far-tank
            // false-latch state is unreachable. Repeatable besides: a momentary
            // completion never latches, the next wave re-fires it.
            {269, 4},
            // Shattered Halls Nethekurse wake-up: ShNethekurseDormant does a 60yd
            // FindNearestCreature for him, so it cannot read true until boss-nav
            // has parked the party at him — and the hook it drives only fires his
            // intro DoAction, which is meaningless from afar anyway (the Zum'rah
            // pattern verbatim).
            {540, 5},
            // Underbog "Send Ghaz'an up to his platform": deliberately map-wide,
            // and the one case where near-gating would be WRONG. Its hook fires
            // the same DoAction areatrigger 4302 fires, and path 1383921 opens by
            // swimming AWAY into deeper water — so he has to be sent up BEFORE he
            // is the party's target, not once they have walked to him. It is also
            // Repeatable and its condition re-reads his live position, so a
            // completion with the tank far away latches nothing.
            {546, 2},
        };
        for (Row const& r : kRows)
            if (r.mapId == mapId && r.eventId == eventId)
                return true;
        return false;
    }
}

// --- sanity ---------------------------------------------------------------

TEST(DungeonEventIntegrityTest, EventTableIsNonEmpty)
{
    EXPECT_FALSE(DungeonEventRegistry::AllEvents().empty());
}

// --- F2: dangling cross-references ----------------------------------------

TEST(DungeonEventIntegrityTest, ConditionalEventsHaveBoundCondition)
{
    // A Conditional event with no bound predicate never fires, so a Required one is
    // a silent wall (the run stalls at a shut door, never naming the event). With
    // the id space replaced by a function pointer (.Conditional(&Predicate)), a
    // wrong name is now a compile error; this lint still catches the one remaining
    // authoring slip — a Conditional() call that was never given a predicate.
    for (DungeonEvent const& ev : DungeonEventRegistry::AllEvents())
    {
        if (ev.activation != EventActivation::Conditional)
            continue;
        EXPECT_TRUE(static_cast<bool>(ev.condition))
            << "conditional event map " << ev.mapId << " id " << ev.id
            << " (" << ev.name << ") has no bound condition — it can never fire";
    }
}

// Tripwire for the Stratholme #5 bug class: a Conditional event whose steps can
// ALL report Done from wherever the tank stands (no arrival step) latches
// "complete" the instant its condition turns true — even with the tank at the
// far end of the map. The executor evaluates its first ClearRadius/KillCreature
// gate from that far position, finds nothing engage-reachable, returns Done, and
// the event is marked done with nothing accomplished (Timmy's room "already
// cleared" at the instance entrance). Such an event is sound ONLY if its
// condition can't read true while the tank is far — a property that lives in the
// opaque predicate a test can't inspect. So force a conscious opt-in: any such
// event must be either a room-aggro pre-clear (condition proximity-gated inside
// RoomTrashRemaining) or hand-vetted on IsNearGatedConditionalWhitelisted. A NEW
// no-arrival conditional event trips this until its author does one of those —
// or, better, gives it an arrival step / proximity-gated condition.
TEST(DungeonEventIntegrityTest, ConditionalEventsWithoutArrivalStepAreProximityVetted)
{
    for (DungeonEvent const& ev : DungeonEventRegistry::AllEvents())
    {
        if (ev.activation != EventActivation::Conditional || ev.steps.empty())
            continue;
        if (HasArrivalStep(ev))
            continue;  // an arrival step forces the tank on-site before completion
        if (DungeonEventRegistry::IsRoomAggroPreClear(ev))
            continue;  // condition is proximity-gated inside RoomTrashRemaining

        EXPECT_TRUE(IsNearGatedConditionalWhitelisted(ev.mapId, ev.id))
            << "conditional event map " << ev.mapId << " id " << ev.id << " ("
            << ev.name << ") has no arrival step (MoveTo/UseGO/Gossip/...), so every "
            << "step can report Done from afar and it false-latches 'complete' the "
            << "instant its condition turns true with the tank far from the anchor "
            << "(the Stratholme #5 'Timmy room already cleared at the entrance' bug). "
            << "Fix: give it a proximity-gated condition (like StrTimmyGated's 60yd "
            << "check) or an arrival step; then, if verified near-only, add it to "
            << "IsNearGatedConditionalWhitelisted with the gating mechanism noted.";
    }
}

TEST(DungeonEventIntegrityTest, CustomStepsHaveRegisteredHook)
{
    // A Custom step with an unregistered hookId now Blocks (was: silently Done) —
    // catch the typo at author time before it stalls a live run.
    for (DungeonEvent const& ev : DungeonEventRegistry::AllEvents())
    {
        for (EventStep const& s : ev.steps)
        {
            if (s.kind != EventStepKind::Custom)
                continue;
            EXPECT_NE(s.hookId, 0u)
                << "Custom step in event map " << ev.mapId << " id " << ev.id
                << " (" << ev.name << ") has hookId 0";
            EXPECT_TRUE(ObjectiveHookRegistry::Has(s.hookId))
                << "Custom step in event map " << ev.mapId << " id " << ev.id
                << " (" << ev.name << ") references unregistered hookId " << s.hookId;
        }
    }
}

TEST(DungeonEventIntegrityTest, RosterObjectiveEventIdsResolveToAnchoredEvents)
{
    // Every objective's eventId must resolve; a typo falls into the legacy hook
    // path (onArriveHook 0 -> Done) and the objective latches instantly on arrival,
    // silently skipping the gate it guards.
    for (BossRosterPatch const& patch : BossRosterRegistry::AllPatches())
    {
        for (DungeonBossInfo const& e : patch.add)
        {
            if (e.kind != DungeonAnchorKind::Objective || e.eventId == 0)
                continue;
            DungeonEvent const* ev = DungeonEventRegistry::Find(patch.mapId, e.eventId);
            ASSERT_NE(ev, nullptr)
                << "roster objective entry " << e.entry << " on map " << patch.mapId
                << " (" << e.name << ") references non-existent eventId " << e.eventId;
            EXPECT_EQ(ev->activation, EventActivation::Anchored)
                << "objective-referenced event map " << patch.mapId << " id " << e.eventId
                << " must be Anchored, not Conditional";
        }
    }
}

TEST(DungeonEventIntegrityTest, DifficultyGatesNeverContradictAcrossAnchorAndEvent)
{
    // An anchored event is gated primarily through the roster patch that wires
    // its objective anchor (BossRosterPatch::gate); the event's own gate is a
    // belt-and-suspenders Drive stop. The two must never CONTRADICT — a
    // HeroicOnly event wired by a NormalOnly patch (or vice versa) can never
    // run: the anchor surfaces on one difficulty, the event refuses to drive
    // there, and the run stalls (Required) or silently skips (Optional).
    auto const contradicts = [](DcDifficultyGate a, DcDifficultyGate b)
    {
        return (a == DcDifficultyGate::NormalOnly && b == DcDifficultyGate::HeroicOnly) ||
               (a == DcDifficultyGate::HeroicOnly && b == DcDifficultyGate::NormalOnly);
    };

    for (BossRosterPatch const& patch : BossRosterRegistry::AllPatches())
    {
        for (DungeonBossInfo const& e : patch.add)
        {
            if (e.kind != DungeonAnchorKind::Objective || e.eventId == 0)
                continue;
            if (DungeonEvent const* ev = DungeonEventRegistry::Find(patch.mapId, e.eventId))
                EXPECT_FALSE(contradicts(patch.gate, ev->gate))
                    << "map " << patch.mapId << " objective " << e.name
                    << " wires event id " << e.eventId
                    << " with a contradicting difficulty gate";
        }
    }
}

TEST(DungeonEventIntegrityTest, RosterObjectiveHooksAreRegistered)
{
    for (BossRosterPatch const& patch : BossRosterRegistry::AllPatches())
    {
        for (DungeonBossInfo const& e : patch.add)
        {
            if (e.kind != DungeonAnchorKind::Objective || e.onArriveHook == 0)
                continue;
            EXPECT_TRUE(ObjectiveHookRegistry::Has(e.onArriveHook))
                << "roster objective entry " << e.entry << " on map " << patch.mapId
                << " (" << e.name << ") references unregistered onArriveHook " << e.onArriveHook;
        }
    }
}

// --- F9: every anchored event is wired by exactly one objective -----------

TEST(DungeonEventIntegrityTest, AnchoredEventsAreWiredByExactlyOneObjective)
{
    // An anchored event enters the clear only through an objective anchor's eventId.
    // An anchored event no objective references is dead data (authored, never fired).
    // NOTE: DungeonEvent::orderIndex is doc-only and has drifted from the roster's
    // real order key in a few files (DireMaul pylons reuse the eventId, Uldaman uses
    // 7/8 vs roster 8/9) — so we assert the WIRING exists, not orderIndex equality.
    for (DungeonEvent const& ev : DungeonEventRegistry::AllEvents())
    {
        if (ev.activation != EventActivation::Anchored)
            continue;
        int refs = 0;
        for (BossRosterPatch const& patch : BossRosterRegistry::AllPatches())
        {
            if (patch.mapId != ev.mapId)
                continue;
            for (DungeonBossInfo const& e : patch.add)
                if (e.kind == DungeonAnchorKind::Objective && e.eventId == ev.id)
                    ++refs;
        }
        EXPECT_EQ(refs, 1)
            << "anchored event map " << ev.mapId << " id " << ev.id << " (" << ev.name
            << ") is wired by " << refs << " objectives (expected exactly 1)";
    }
}

// --- F1: persistence lint -------------------------------------------------

TEST(DungeonEventIntegrityTest, MultiStepRewindHazardEventsArePersistent)
{
    for (DungeonEvent const& ev : DungeonEventRegistry::AllEvents())
    {
        if (ev.activation != EventActivation::Anchored)
            continue;
        if (ev.steps.size() <= 1 || !HasRewindHazard(ev))
            continue;
        if (IsNonPersistentWhitelisted(ev.mapId, ev.id))
            continue;
        EXPECT_TRUE(ev.persistent)
            << "anchored event map " << ev.mapId << " id " << ev.id << " (" << ev.name
            << ") has >1 step and a rewind-hazard step but is not .Persistent() — a "
               "combat gap will rewind it to step 0. Add .Persistent() or whitelist it.";
    }
}

// --- F3: wing/roster sync -------------------------------------------------

TEST(DungeonEventIntegrityTest, IsolatedWingObjectivesAppearInExactlyOneWing)
{
    // On a physically-isolated split map the boss list is filtered to the bot's
    // wing; a synthetic objective NOT listed in a wing is silently dropped and
    // never cleared. Assert every added objective on such a map is in exactly one
    // wing.
    for (BossRosterPatch const& patch : BossRosterRegistry::AllPatches())
    {
        DungeonWingLayout const* layout = DungeonWingRegistry::Get(patch.mapId);
        if (!layout || !layout->isolated)
            continue;  // single-wing, or label-only (Maraudon) — no filter applied
        for (DungeonBossInfo const& e : patch.add)
        {
            if (e.kind != DungeonAnchorKind::Objective)
                continue;
            int inWings = 0;
            for (DungeonWing const& w : layout->wings)
                for (uint32 entry : w.bossEntries)
                    if (entry == e.entry)
                        ++inWings;
            EXPECT_EQ(inWings, 1)
                << "objective entry " << e.entry << " (" << e.name << ") on isolated "
                << "wing-split map " << patch.mapId << " is listed in " << inWings
                << " wings (expected exactly 1) — it will be silently dropped from the clear";
        }
    }
}

// --- Sethekk Halls (556): force-summon & kill Anzu ------------------------
// A focused regression over the specific wiring of the forced-Anzu bonus boss.
// The generic integrity tests above already cover it structurally; this pins the
// exact shape so a future edit that, say, drops .Persistent() or renumbers the
// hook fails loudly with intent rather than as an anonymous parametrized row.
TEST(DungeonEventIntegrityTest, SethekkAnzuEventIsWiredForForcedSummon)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(/*map*/ 556, /*eventId*/ 1);
    ASSERT_NE(ev, nullptr) << "Sethekk Halls (556) event 1 (force-summon Anzu) is missing";

    // Bonus boss: never stall the clear to Ikiss (Optional == !required), and it
    // spans the ~40s summon + the fight across combat gaps (Persistent).
    EXPECT_FALSE(ev->required) << "Anzu event must be Optional (bonus boss)";
    EXPECT_TRUE(ev->persistent) << "Anzu event must be Persistent (summon + fight span combat gaps)";
    EXPECT_EQ(ev->activation, EventActivation::Anchored);

    // Anzu is a HEROIC-ONLY bonus boss: the event must never fire on a normal run.
    EXPECT_EQ(ev->gate, DcDifficultyGate::HeroicOnly)
        << "Anzu event must be HeroicOnly (blizzlike heroic-only bonus boss)";

    // Step order matters: clear the room BEFORE poking the summon (Anzu's
    // SetInCombatWithZone would otherwise drag surviving trash into the fight).
    int clearStep = -1;
    int pokeStep = -1;
    int killStep = -1;
    for (size_t i = 0; i < ev->steps.size(); ++i)
    {
        EventStep const& s = ev->steps[i];
        if (s.kind == EventStepKind::ClearRadius)
            clearStep = static_cast<int>(i);
        if (s.kind == EventStepKind::Custom && s.hookId == 7)
            pokeStep = static_cast<int>(i);
        // KillCreatureEngage builds a KillCreature step with engage == true.
        if (s.kind == EventStepKind::KillCreature && s.engage && s.creatureEntry == 23035)
            killStep = static_cast<int>(i);
    }
    EXPECT_GE(clearStep, 0) << "Anzu event must pre-clear the room (a ClearRadius step)";
    EXPECT_GE(pokeStep, 0) << "Anzu event must poke via Custom hook 7 (DriveAnzuSummon)";
    EXPECT_GE(killStep, 0) << "Anzu event must KillCreatureEngage Anzu (23035)";
    EXPECT_TRUE(ObjectiveHookRegistry::Has(7)) << "hook 7 (DriveAnzuSummon) must be registered";
    EXPECT_LT(clearStep, pokeStep) << "room must be cleared BEFORE the Anzu summon poke";
    EXPECT_LT(pokeStep, killStep) << "the summon poke must precede the Anzu kill step";
}

TEST(DungeonEventIntegrityTest, ArcatrazSoulEaterRoomIsAnEntryFilteredSweep)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(/*map*/ 552, /*eventId*/ 2);
    ASSERT_NE(ev, nullptr) << "Arcatraz (552) event 2 (Eredar Soul-Eater room) is missing";

    // Trash, not an encounter: conditional (its own proximity + completion gate),
    // no roster objective. Persistent because the sweep is a fight and a mid-fight
    // Drive gap would otherwise rewind the step list and restart the walk-in.
    EXPECT_EQ(ev->activation, EventActivation::Conditional);
    EXPECT_TRUE(ev->persistent) << "the sweep spans combat gaps";
    // The 45yd aura is on the spawns in BOTH difficulties — never gate this heroic-only.
    EXPECT_EQ(ev->gate, DcDifficultyGate::Any);

    // Walk into the room, THEN sweep: the executor refuses to certify "clear" from
    // further out than DC_EVENT_CLEAR_JUDGE_RADIUS (12), so judging has to happen
    // from the middle of the room rather than the doorway.
    int moveStep = -1;
    int clearStep = -1;
    for (size_t i = 0; i < ev->steps.size(); ++i)
    {
        if (ev->steps[i].kind == EventStepKind::MoveTo)
            moveStep = static_cast<int>(i);
        if (ev->steps[i].kind == EventStepKind::ClearRadius)
            clearStep = static_cast<int>(i);
    }
    ASSERT_GE(moveStep, 0) << "must walk the tank into the room first";
    ASSERT_GE(clearStep, 0) << "must sweep the room (a ClearRadius step)";
    EXPECT_LT(moveStep, clearStep) << "arrive before judging the room clear";

    // BY-ENTRY BACKSTOP (the Shattered Halls assassin lesson). A ClearRadius gate
    // can only fight what AttackersValue::IsPossibleTarget (CanSeeOrDetect) and a
    // strict IsEngageReachable let it see; anything they reject reads as "room
    // clear" and latches the event done with a live 750-per-2s caster standing.
    // KillCreatureEngage resolves by entry via FindNearestCreature — no visibility
    // filter, looser reachability — so one per multispawn entry must follow the
    // sweep and catch whatever it could not see.
    std::vector<uint32> engageEntries;
    for (size_t i = 0; i < ev->steps.size(); ++i)
    {
        EventStep const& s = ev->steps[i];
        if (s.kind != EventStepKind::KillCreature || !s.engage)
            continue;
        EXPECT_GT(static_cast<int>(i), clearStep)
            << "the by-entry backstop must follow the sweep, not precede it";
        engageEntries.push_back(s.creatureEntry);
    }
    std::sort(engageEntries.begin(), engageEntries.end());
    EXPECT_EQ(engageEntries, (std::vector<uint32>{20879u, 20880u}))
        << "one KillCreatureEngage backstop per multispawn entry";

    EventStep const& sweep = ev->steps[clearStep];

    // The filter is the whole point: the sweep is the three Soul-Eaters and
    // nothing else. Unfiltered it would also pull the Arcatraz Sentinel on the
    // room's west edge (whose death summons the unkillable 21761 pulse) and offer
    // up the three corpse props as targets.
    //
    // BOTH multispawn entries, and both must be the NORMAL ones. Every one of the
    // three spawn points rolls 20879 or 20880 independently at spawn and again at
    // each respawn, so a room can contain three Deathbringers and no Soul-Eater —
    // a 20879-only filter would certify that room "clear" instantly. And
    // Creature::InitEntry keeps GetEntry() on the normal entry in every
    // difficulty, so the heroic templates (21595 / 21594) can never match.
    ASSERT_EQ(sweep.entryFilter.size(), 2u);
    EXPECT_NE(std::find(sweep.entryFilter.begin(), sweep.entryFilter.end(), 20879u),
              sweep.entryFilter.end()) << "Eredar Soul-Eater";
    EXPECT_NE(std::find(sweep.entryFilter.begin(), sweep.entryFilter.end(), 20880u),
              sweep.entryFilter.end()) << "Eredar Deathbringer (multispawn variant)";
    EXPECT_EQ(std::find(sweep.entryFilter.begin(), sweep.entryFilter.end(), 21595u),
              sweep.entryFilter.end()) << "heroic template can never match GetEntry()";
    EXPECT_EQ(std::find(sweep.entryFilter.begin(), sweep.entryFilter.end(), 21594u),
              sweep.entryFilter.end()) << "heroic template can never match GetEntry()";

    // Every spawn (305.7,148.1,24.9) / (285.5,146.2,22.3) / (301.8,127.4,22.3)
    // must sit inside the volume, or the gate certifies with one still alive.
    struct P { float x, y, z; };
    static constexpr P kSpawns[] = {
        { 305.7f, 148.1f, 24.9f }, { 285.5f, 146.2f, 22.3f }, { 301.8f, 127.4f, 22.3f },
    };
    for (P const& p : kSpawns)
    {
        float const dx = p.x - sweep.x;
        float const dy = p.y - sweep.y;
        EXPECT_LT(std::sqrt(dx * dx + dy * dy), sweep.radius) << "spawn outside the sweep radius";
        EXPECT_LT(std::fabs(p.z - sweep.z), sweep.zBand) << "spawn outside the sweep z-band";
    }

    // ...and the tank must be able to judge from the anchor: the executor only
    // trusts a "clear" verdict taken within 12yd of the centre, so the arrival
    // radius has to land it inside that.
    EXPECT_LE(ev->steps[moveStep].radius, 12.0f)
        << "arrival radius must land the tank inside DC_EVENT_CLEAR_JUDGE_RADIUS";

    // MUST-FINISH CONTRACT. The party cannot rest in this room (the aura holds it
    // flagged, and every rest path bails on the raw combat flag), so a half-driven
    // sweep leaves it standing in up to three 750-per-2s Unholy Auras with no way
    // to recover. Two authoring mistakes would break that, and both look harmless:
    //
    //   Optional() — a Failed step would Skip instead of Stall, advancing the
    //   clear with live Deathbringers behind the party.
    EXPECT_TRUE(ev->required)
        << "the Eredar room must not be skippable — a half-cleared room is the bug";
    //
    //   A tight timeout — required means Failed => Stalled, and DcRunEventAction
    //   answers Stalled by PARKING the tank. In this room parking is the wipe, so
    //   the timeout has to sit far past a real fight (3 elites, ~30-60s each for a
    //   5-bot party) rather than close to it.
    EXPECT_GE(sweep.timeoutMs, 300000u)
        << "sweep timeout must be far past a legitimate 3-elite fight; a Stall here parks "
           "the party inside the damage aura";
}

// --- drivesInCombat containment -------------------------------------------
// The flag takes ticks away from the stock combat engine: while it is set and the
// event's condition reads true, the leader's whole combat tick is spent on
// DcRunEventCombatAction (DcRel::EventDueCombat = 61, above every stock combat
// mover). That is exactly right for a continuous wave encounter whose event IS
// the fight, and exactly wrong anywhere else — an ordinary lever/gossip event
// with the flag set would hijack every fight on its map.
//
// So this is a CONTAINMENT tripwire, not a style check: the flag stays on the
// hand-vetted list below and nowhere else. Adding a map here is a deliberate act
// that says "this encounter cannot be steered between pulls".
TEST(DungeonEventIntegrityTest, DrivesInCombatIsConfinedToVettedWaveEncounters)
{
    struct Row { uint32 mapId; uint32 eventId; };
    static constexpr Row kVetted[] = {
        // The Black Morass "Close the time rift". 18 rifts, each pumping one add
        // every 15s until its keeper dies; the party is in combat from the first
        // pull to the last, so the non-combat rung ran only in the gaps between
        // waves and stopped running entirely once two rifts were open at once —
        // at which point nothing ever walked the tank to a portal and no rift ever
        // closed. See DungeonEvent::drivesInCombat.
        {269, 4},
    };

    for (DungeonEvent const& ev : DungeonEventRegistry::AllEvents())
    {
        if (!ev.drivesInCombat)
            continue;

        bool vetted = false;
        for (Row const& r : kVetted)
            if (r.mapId == ev.mapId && r.eventId == ev.id)
                vetted = true;

        EXPECT_TRUE(vetted)
            << "event " << ev.mapId << "/" << ev.id << " '" << ev.name
            << "' sets DrivesInCombat(), which hands the leader's COMBAT tick to the"
               " event driver above every stock combat mover. If that is genuinely"
               " intended (a continuous wave encounter that cannot be steered"
               " between pulls), add it to kVetted here with the reason.";

        // The combat rung is a copy of the CONDITIONAL rung; an anchored event is
        // driven off its objective arrival and never reaches it, so the flag there
        // would be silently inert.
        EXPECT_EQ(ev.activation, EventActivation::Conditional)
            << "event " << ev.mapId << "/" << ev.id
            << " sets DrivesInCombat() but is Anchored — the flag only has an"
               " effect on Conditional events (DungeonClearEventDueCombatTrigger).";
    }
}

// The BRD Ring of Law, pinned to the two properties tr-20260808-150405-10 broke
// on. Both look like tuning and are not.
TEST(DungeonEventIntegrityTest, RingOfLawGarrisonsTheCentreAndCanRestartItself)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(/*map*/ 230, /*eventId*/ 1);
    ASSERT_NE(ev, nullptr) << "Blackrock Depths (230) event 1 (Ring of Law) is missing";

    EventStep const* hold = nullptr;
    for (EventStep const& s : ev->steps)
        if (s.kind == EventStepKind::MoveTo && s.instanceDataId >= 0)
            hold = &s;
    ASSERT_NE(hold, nullptr) << "the Ring of Law must hold on TYPE_RING_OF_LAW";

    // A garrison radius is a LEASH, not a dead band. At 10yd the tank simply kept
    // wherever the last wave died — 9.7yd out toward the mob gate, never
    // re-centring, for the rest of the run.
    EXPECT_LE(hold->radius, 5.0f)
        << "the arena garrison must actually re-centre the tank between waves;"
           " a radius this wide is a dead band the tank parks inside";

    // TYPE_RING_OF_LAW is not monotonic: npc_grimstone's 30s no-victim watchdog
    // SetData(FAIL)s it back to NOT_STARTED and despawns Grimstone and every
    // summon. Without a hook running inside the hold, nothing ever notices — the
    // Custom step that started it latched Done and the areatrigger relay is edge-
    // triggered on a volume the party is standing in.
    EXPECT_NE(hold->hookId, 0u)
        << "the hold must re-run the start hook (.WhileHolding) or a Grimstone-side"
           " reset stalls the party in an empty arena until the step times out";
    EXPECT_TRUE(ObjectiveHookRegistry::Has(hold->hookId))
        << "the Ring of Law hold references unregistered hook " << hold->hookId;
}

// The Black Morass wave driver, pinned to its exact shape. Every one of these
// properties was a live failure before it was set, so a future edit that drops
// one should fail loudly with intent rather than as a silent behaviour change.
TEST(DungeonEventIntegrityTest, BlackMorassWaveEventIsWiredForTheWaveDriver)
{
    DungeonEvent const* ev = DungeonEventRegistry::Find(/*map*/ 269, /*eventId*/ 4);
    ASSERT_NE(ev, nullptr) << "Black Morass (269) event 4 (close the time rift) is missing";

    EXPECT_EQ(ev->activation, EventActivation::Conditional);
    EXPECT_TRUE(static_cast<bool>(ev->condition)) << "the wave gate predicate must be bound";

    // 18 waves: the condition going false is the only "done".
    EXPECT_TRUE(ev->repeatable) << "wave event must be Repeatable (18 rifts)";
    // A wipe / corpse-run must never hard-stall the run for a human.
    EXPECT_FALSE(ev->required) << "wave event must be Optional (a timeout re-fires it fresh)";
    // THE fix for "they fall apart any time two portals are open": the party is in
    // combat essentially the whole encounter, so a non-combat-only driver never
    // runs once it falls behind.
    EXPECT_TRUE(ev->drivesInCombat) << "wave event must DriveInCombat (continuous wave fight)";

    // ONE step, and it must be the driver hook. The priority this encounter needs
    // ("always prefer the rift keeper, it is the only shutoff") is a standing
    // preference re-evaluated per tick, not a sequence — a step list can only
    // block in order, which is how the keeper ended up behind nine drainer gates
    // that a live rift re-blocked every 15s.
    ASSERT_EQ(ev->steps.size(), 1u)
        << "the wave event must be exactly one Custom step (the driver hook); a step"
           " list cannot express the keeper-first priority this encounter needs";
    EXPECT_EQ(ev->steps[0].kind, EventStepKind::Custom);
    EXPECT_EQ(ev->steps[0].hookId, 12u);
    EXPECT_TRUE(ObjectiveHookRegistry::Has(12)) << "hook 12 (BmDriveWave) must be registered";

    // The two entry lists the driver keys on must be non-empty and DISJOINT:
    // killing a keeper closes a rift and never drains the shield; killing a
    // drainer stops a drain and never closes a rift. Overlap would make the driver
    // chase adds as if they were the shutoff.
    ASSERT_FALSE(BlackMorassKeeperEntries().empty());
    ASSERT_FALSE(BlackMorassDrainEntries().empty());
    for (uint32 keeper : BlackMorassKeeperEntries())
        for (uint32 drainer : BlackMorassDrainEntries())
            EXPECT_NE(keeper, drainer)
                << "entry " << keeper << " is in BOTH the Black Morass keeper and"
                   " drain lists; they must stay disjoint";
}

// --- stepsOwnMovement containment ------------------------------------------
// The flag does two things, and both are dangerous on an event that does not own
// its own driving. It suppresses the per-tick position hold the driving actions
// apply (StopBot(Hold) anchored, ResolveEscortConflict conditional) — that hold is
// what stops a tank coasting past its objective on a stale glide. And it makes a
// completed repeatable event YIELD the engine tick rather than claim it, which is
// only safe when the step itself decides per tick whether it has work.
//
// Containment tripwire, same contract as the drivesInCombat one: the flag stays
// on the hand-vetted list and nowhere else.
TEST(DungeonEventIntegrityTest, StepsOwnMovementIsConfinedToVettedEvents)
{
    struct Row { uint32 mapId; uint32 eventId; };
    static constexpr Row kVetted[] = {
        // Black Morass "Defend Medivh": hook 8 walks the tank into Medivh's 20yd
        // start trigger and holds the Medivh-side hold point, both through the
        // long-haul spline funnel (the portals and the hold point are 80-102yd
        // apart, past what a bare MovePoint delivers).
        {269, 3},
        // Black Morass "Close the time rift": hook 12 walks the tank to the locked
        // rift's keeper and back to Medivh's ring, same funnel. With the hold in
        // place this glide was cancelled the tick after it was issued — 151 camp
        // attempts in one batch, none arriving, all logging a healthy spline.
        {269, 4},
    };

    for (DungeonEvent const& ev : DungeonEventRegistry::AllEvents())
    {
        if (!ev.stepsOwnMovement)
            continue;

        bool vetted = false;
        for (Row const& r : kVetted)
            if (r.mapId == ev.mapId && r.eventId == ev.id)
                vetted = true;

        EXPECT_TRUE(vetted)
            << "event " << ev.mapId << "/" << ev.id << " '" << ev.name
            << "' sets StepsOwnMovement(), which suppresses the per-tick hold that"
               " keeps a tank parked at its anchor. Only set it when the event's own"
               " steps issue the movement; then add it to kVetted here with the"
               " reason.";
    }
}

// Aeonus (17881) is a DRAINER, not a keeper — the single least obvious fact in
// the Black Morass wiring, and the party silently loses the run if it is
// mis-filed. boss_aeonus::IsSummonedBy does exactly what DoSummonAtRift does to
// the trash:
//
//     me->SetReactState(REACT_DEFENSIVE);
//     me->SetHomePosition(medivh + 14yd);
//     me->GetMotionMaster()->MoveTargetedHome();
//
// and JustReachedHome then DoCastAOE(37853) inside 20yd of Medivh, which
// spell_black_morass_corrupt_medivh drains at 2 per tick — DOUBLE the trash rate.
// So the final boss never aggros the party: it walks past them to Medivh and
// drains the shield out from under the run.
//
// It must NOT be in the keeper list, where it would fail both of that list's
// jobs: it is never at the rift to be selected on (it leaves immediately), and
// killing it closes nothing (npc_time_rift::JustSummoned sets _riftKeeperGUID
// only for the first NON-Aeonus summon, so SummonedCreatureDies never matches).
//
// Deja (17879) and Temporus (17880) are the contrast: plain BossAI with no
// IsSummonedBy override, so they stay aggressive and fight at their rift. They
// belong in the keeper list and nowhere near the drain list.
TEST(DungeonEventIntegrityTest, BlackMorassAeonusIsFiledAsADrainerNotAKeeper)
{
    constexpr uint32 kAeonus   = 17881;
    constexpr uint32 kDeja     = 17879;
    constexpr uint32 kTemporus = 17880;

    auto has = [](std::vector<uint32> const& v, uint32 e)
    { return std::find(v.begin(), v.end(), e) != v.end(); };

    EXPECT_TRUE(has(BlackMorassDrainEntries(), kAeonus))
        << "Aeonus must be in the DRAIN list: it spawns REACT_DEFENSIVE, homes to"
           " Medivh's 14yd ring and channels 37853 at double rate. Out of that list"
           " nothing force-pulls it and nothing counts it toward 'Medivh's ring is"
           " dirty', so the party never engages the final boss.";
    EXPECT_FALSE(has(BlackMorassKeeperEntries(), kAeonus))
        << "Aeonus must NOT be in the keeper list: it is never at the rift to be"
           " selected on, and killing it does not close its rift.";

    // The wave-6/12 bosses are the opposite case — keep them straight.
    EXPECT_TRUE(has(BlackMorassKeeperEntries(), kDeja));
    EXPECT_TRUE(has(BlackMorassKeeperEntries(), kTemporus));
    EXPECT_FALSE(has(BlackMorassDrainEntries(), kDeja));
    EXPECT_FALSE(has(BlackMorassDrainEntries(), kTemporus));
}

// --- objective-hook id space -------------------------------------------------
// Hook ids are ONE FLAT SPACE shared by every dungeon, and unlike event
// conditions (function pointers, so a typo is a compile error) nothing about an
// id is checked by the compiler. Now that controllers register from their own
// TUs (RegisterBlackMorassHooks) instead of one central initializer list, a
// copy-pasted id is a live hazard: emplace() keeps the first row and silently
// drops the second, so the losing dungeon's objective latches Done on arrival and
// its event never runs, with nothing in the log to say why. ObjectiveHookRegistry
// ::AddHook turns that into a LOG_ERROR; these cases turn it into a red test.

TEST(DungeonEventIntegrityTest, AddHookRejectsDuplicateIdsAndKeepsTheFirst)
{
    auto const first  = [](Player*, AiObjectContext*, DungeonBossInfo const&)
    { return ObjectiveArriveResult::Done; };
    auto const second = [](Player*, AiObjectContext*, DungeonBossInfo const&)
    { return ObjectiveArriveResult::Blocked; };

    ObjectiveHookRegistry::HookTable t;
    ObjectiveHookRegistry::AddHook(t, 42, first);
    ObjectiveHookRegistry::AddHook(t, 42, second);

    ASSERT_EQ(t.size(), 1u) << "a duplicate id must not add a second row";
    EXPECT_EQ(t.at(42)(nullptr, nullptr, DungeonBossInfo{}), ObjectiveArriveResult::Done)
        << "on a collision the FIRST registration wins; if the second silently"
           " replaced it, whichever dungeon's TU happened to register last would"
           " hijack the other's objective.";
}

TEST(DungeonEventIntegrityTest, AddHookRejectsReservedIdZeroAndEmptyCallables)
{
    ObjectiveHookRegistry::HookTable t;

    ObjectiveHookRegistry::AddHook(t, 0, [](Player*, AiObjectContext*, DungeonBossInfo const&)
                                   { return ObjectiveArriveResult::Done; });
    EXPECT_TRUE(t.empty())
        << "id 0 means 'no hook' on DungeonBossInfo::onArriveHook and EventStep::hookId;"
           " registering it would make a hookless objective run someone's handler.";

    ObjectiveHookRegistry::AddHook(t, 43, ObjectiveHookRegistry::Hook{});
    EXPECT_TRUE(t.empty())
        << "an empty callable would satisfy Has() but crash on Run()";
}

// The live table, cross-checked against what the per-dungeon appenders are
// supposed to have contributed. This is what actually catches a controller TU
// being dropped by the linker (the static-lib hazard the explicit
// Register<Dungeon>Hooks calls exist to prevent) — its ids just stop resolving.
TEST(DungeonEventIntegrityTest, EveryAuthoredObjectiveHookIdIsRegistered)
{
    struct Expected { uint32 id; char const* what; };
    constexpr Expected kHooks[] = {
        { 1, "BRD Ring of Law — EnsureRingStarted" },
        { 2, "Deadmines — FireDefiasCannon" },
        { 3, "Old Hillsbrad — GrantIncendiaryBombs" },
        { 4, "The Mechanar — GrantCacheKeyAndLoot" },
        { 5, "ZulFarrak — WakeZumrah" },
        { 6, "Arcatraz — DriveMellicharWaves" },
        { 7, "Sethekk Halls — DriveAnzuSummon" },
        { 8, "Black Morass — DriveBlackMorassEvent (BlackMorassDriver.cpp)" },
        { 9, "Shattered Halls — StartNethekurseIntro" },
        { 10, "The Underbog — SendGhazanToPlatform" },
        { 12, "Black Morass — BmDriveWave (BlackMorassDriver.cpp)" },
    };

    for (Expected const& e : kHooks)
        EXPECT_TRUE(ObjectiveHookRegistry::Has(e.id))
            << "objective hook " << e.id << " (" << e.what << ") is not registered."
               " If this is one of the Black Morass ids, the most likely cause is"
               " BlackMorassDriver.cpp losing its RegisterBlackMorassHooks call from"
               " Hooks() — the module is a static lib, so a TU nothing references"
               " gets dropped along with its hooks.";

    EXPECT_FALSE(ObjectiveHookRegistry::Has(0))
        << "id 0 is the 'no hook' sentinel and must never resolve";

    // 11 (BmPullDrainers) is half of the old Black Morass wave pair, retired into
    // hook 12 and left unused rather than recycled so an old log line naming it
    // stays legible. Its partner, 10 (BmCampActivePortal), WAS recycled in S1593
    // for the Underbog — accepted then because the Black Morass rework predates
    // any log a reader still consults.
    EXPECT_FALSE(ObjectiveHookRegistry::Has(11))
        << "hook id 11 is RETIRED (old BmPullDrainers) and must stay unused";
}
