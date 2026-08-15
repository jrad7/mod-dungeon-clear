/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"

#include "InstanceScript.h"
#include "Player.h"
#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"

// --- Hellfire Ramparts (map 543) — approach Vazruden, CONDITIONAL + REPEATABLE
//
// The final encounter is "Vazruden the Halls". Unlike the first two bosses it
// has NO static spawn: only Vazruden the Herald (17307) sits in the world DB,
// REACT_PASSIVE on the upper ledge, and on Reset he summons two Hellfire
// Sentries (17517) down on the lower platform. Killing BOTH Sentries is the
// only trigger — the Herald then flies to the platform centre, vanishes, and
// summons the real boss Vazruden (17537) plus the dragon Nazan (17536). The
// kill credit (DungeonEncounter bit 2) is on Vazruden (17537).
//
// Two facts make this exactly the Razorfen Downs / Tuten'kash summon-boss case:
//   1. The real boss (17537) is a TempSummon with no spawnId, so the boss
//      navigation's live-boss tracking (FindLiveCreatureOnMap scans the map's
//      DB-spawn store) never sees him — without help the tank reads "boss not
//      present" and holds at the static anchor forever (live=0 deadlock).
//   2. He is summoned ~28yd from the trigger and does not exist at all until the
//      Sentries die, so there is nothing for boss-nav to path toward.
//
// The roster patch (BossRosterRegistry, map 543) gives the final boss a static
// anchor on the lower platform, dead between the two Sentries, so boss-nav walks
// the tank onto the platform and into Sentry aggro range. This event then closes
// the loop: REPEATABLE so the completed MoveTo holds the tank planted on the
// trigger spot (DcRunEventAction returns true on a repeatable completion) until
// the Sentries aggro and the combat engine takes over — and so it self-heals if
// the party drifts out before the encounter starts. Anchor and MoveTo target are
// the SAME point (the trigger), so there is no tug-of-war between boss-nav and
// the event (same pattern as RFD's gong). The condition reads false once
// Vazruden's encounter bit is set (killed), ending the loop. Folded under
// Vazruden in the panel.

namespace
{
    bool HfrApproach(Player* bot, AiObjectContext* context);
}

void RegisterHellfireRampartsEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(543, 1, "Approach Vazruden")
                      .Conditional(&HfrApproach)
                      .Repeatable()
                      .PanelBeforeBoss(/*Vazruden*/ 17537)
                      // Dead centre between the two Hellfire Sentries
                      // (-1372.56, 1724.31) and (-1383.39, 1711.82) — ~9yd from
                      // each, so a tank parked here aggros both and the kill
                      // flies Vazruden down. A tight arrival radius walks the
                      // tank essentially onto the spot so the trigger is
                      // guaranteed even if boss-nav stops a few yards short.
                      .MoveTo(/*trigger*/ -1378.0f, 1718.0f, 82.9f, /*radius*/ 6.0f)
                      .Build());
}

// --- the approach activation condition (condition 16, repeatable) ---------
// DUE while the party is at the final platform and the encounter is unfinished:
// drive the tank onto the trigger spot so the Sentries aggro (and, once it is
// up, keep him on Vazruden). The two grid scans naturally also act as the
// proximity gate — the Sentries exist from instance load but a grid
// FindNearestCreature only sees them once the party is within range of the
// platform, so this never preempts the long boss-nav approach. Reads false once
// Vazruden's kill-bit (encounter bit 2) is set, which survives his corpse
// despawning.
namespace
{
    constexpr uint32 HFR_HELLFIRE_SENTRY = 17517;
    constexpr uint32 HFR_VAZRUDEN = 17537;
    // Vazruden is the map's third DungeonEncounter (DungeonEncounter.dbc
    // encounterIndex 2 for both normal/heroic), so his kill flips bit 2 of the
    // completed-encounter mask — the authoritative "the finale is done" signal.
    constexpr uint32 HFR_VAZRUDEN_BIT = 2;
    // How far to scan for the Sentries / Vazruden. They sit at the destination,
    // so this only needs to reach the platform once the party arrives; it is the
    // de-facto proximity gate that keeps the event off the long approach.
    constexpr float HFR_PLATFORM_SCAN = 70.0f;

    bool HfrApproach(Player* bot, AiObjectContext* /*context*/)
    {
        // Done for good once Vazruden is killed — bit 2 is set.
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        if (inst && (inst->GetCompletedEncounterMask() & (1u << HFR_VAZRUDEN_BIT)))
            return false;

        // Fire only once the party has reached the platform: either a Hellfire
        // Sentry (present from instance load, the trigger to kill) or the
        // summoned Vazruden is within grid range. A grid FindNearestCreature
        // DOES see a TempSummon, unlike the map-wide spawn-store scan, so this
        // also covers the post-Sentry phase when Vazruden is up.
        if (bot->FindNearestCreature(HFR_HELLFIRE_SENTRY, HFR_PLATFORM_SCAN,
                                     /*alive*/ true))
            return true;
        return bot->FindNearestCreature(HFR_VAZRUDEN, HFR_PLATFORM_SCAN,
                                        /*alive*/ true) != nullptr;
    }
}


// --- roster patch (relocated from BossRosterRegistry) --------------------
void RegisterHellfireRampartsRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // --- Hellfire Ramparts (map 543) -----------------------------
    // The derived list ends at Omor (17308): BossSpawnIndex walks DB
    // spawns, but the final encounter's credit creature, Vazruden
    // (17537), is a TempSummon with no spawn, so it never appears and
    // the run would stop one boss short. The Herald (17307) — the only
    // creature actually spawned — sits REACT_PASSIVE on the UPPER ledge
    // (z~102), which is NOT where the fight happens, so it is the wrong
    // anchor.
    //
    // Add the final boss explicitly at the LOWER platform, dead between
    // the two Hellfire Sentries his Reset summons (-1372.56, 1724.31 and
    // -1383.39, 1711.82, z~82.8) — killing both is the only trigger, and
    // it flies Vazruden + Nazan down. Boss-nav drives the tank here; the
    // "Approach Vazruden" event (eventId 1, condition 16) holds him on
    // the trigger and engages the summoned boss (invisible to the
    // live-boss spawn-store scan). Vazruden carries DungeonEncounter.dbc
    // encounterIndex 2 (bit 2), set directly because there is no derived
    // base entry for inheritCompletionFrom to borrow from.
    {
        BossRosterPatch p;
        p.mapId = 543;
        DungeonBossInfo vazruden =
            MakeBoss(/*Vazruden*/ 17537, 543, "Vazruden the Herald",
                     -1378.0f, 1718.0f, 82.9f, /*completionFrom*/ 0);
        vazruden.encounterIndex = 2;  // DBC bit 2 (final encounter)
        p.add = { vazruden };
        t.push_back(std::move(p));
    }
}
