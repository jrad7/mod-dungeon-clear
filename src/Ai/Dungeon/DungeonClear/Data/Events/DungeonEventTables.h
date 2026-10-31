/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONEVENTTABLES_H
#define _PLAYERBOT_DUNGEONEVENTTABLES_H

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonEventRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPoolExitDecision.h"

class Player;
class Creature;
class Map;
class PlayerbotAI;
class AiObjectContext;
struct BossRosterPatch;
struct DungeonWingLayout;

// Internal registration seam for the per-dungeon event tables.
//
// Each dungeon owns one .cpp in this folder that defines its event rows
// (Register<Dungeon>Events). A Conditional event's activation predicate is a
// free function defined in the SAME file, handed to the builder by pointer
// (.Conditional(&MyPredicate)) — there is no separate condition registry and no
// global id space to keep collision-free. The central DungeonEventRegistry calls
// the Register<Dungeon>Events aggregators EXPLICITLY so every per-dungeon
// translation unit stays referenced.
//
// Why explicit calls and not self-registering static initializers: the module
// compiles into a static lib, and a TU whose only output is constructor
// side-effects (with no symbol the program references) is dropped by the linker
// — its events would silently vanish. The one-big-table this replaces avoided
// that by keeping everything in a single referenced TU; the aggregator calls
// below restore the reference chain per file. (Same reason ObjectiveHookRegistry
// and friends use hardcoded tables.)
//
// Adding a dungeon:
//   1. Create <Dungeon>Events.cpp here.
//   2. Define Register<Dungeon>Events; for any Conditional event, define its
//      predicate as a static free function in that file and pass &Predicate to
//      .Conditional() (a typo is a compile error, not a silent never-fire).
//   3. Declare the appender below.
//   4. Add the call in EventTable() (DungeonEventRegistry.cpp).
//   5. If the dungeon corrects the auto-derived boss list, define its roster
//      patch as Register<Dungeon>Roster in the SAME file (using the DcRoster
//      builders in DungeonRosterBuilders.h), declare it below, and add the call
//      in PatchTable() (BossRosterRegistry.cpp). One file owns all of a
//      dungeon's clear data: event rows + conditions + roster patch.

// Shared, cross-dungeon activation predicate — external linkage so several
// dungeon files can pass &DcRoomAggroPreClearCondition to .Conditional().
// DUE while the room-trash value still has anything to clear (every
// RoomAggroRegistry boss: SM Cathedral, Scholomance Marduk & Vectus, ...).
bool DcRoomAggroPreClearCondition(Player* bot, AiObjectContext* context);

// --- event rows (one appender per dungeon) -------------------------------
void RegisterSunkenTempleEvents(std::vector<DungeonEvent>& out);
void RegisterZulFarrakEvents(std::vector<DungeonEvent>& out);
void RegisterShadowfangKeepEvents(std::vector<DungeonEvent>& out);
void RegisterScarletMonasteryEvents(std::vector<DungeonEvent>& out);
void RegisterRazorfenDownsEvents(std::vector<DungeonEvent>& out);
void RegisterBlackrockDepthsEvents(std::vector<DungeonEvent>& out);
void RegisterBlackrockSpireEvents(std::vector<DungeonEvent>& out);
void RegisterDeadminesEvents(std::vector<DungeonEvent>& out);
void RegisterWailingCavernsEvents(std::vector<DungeonEvent>& out);
void RegisterStratholmeEvents(std::vector<DungeonEvent>& out);
void RegisterUldamanEvents(std::vector<DungeonEvent>& out);
void RegisterScholomanceEvents(std::vector<DungeonEvent>& out);
void RegisterDireMaulEvents(std::vector<DungeonEvent>& out);
void RegisterBlackfathomDeepsEvents(std::vector<DungeonEvent>& out);
// Blackfathom Deeps (map 48) — the Forgotten Pool exit, shared with the gtests
// (t/TestBlackfathomDeeps). The reasoning is in BlackfathomDeepsEvents.cpp.
namespace DcBlackfathomDeeps
{
    constexpr uint32 EVENT_POOL_EXIT = 8;  // 3, 4, 7 were the removed Serra'kis legs
    // Hook ids are one flat space across every dungeon; 63 is the next free one.
    constexpr uint32 HOOK_POOL_EXIT = 63;
    // Bound on one climb out (the Custom step's timeout): the longest floor
    // route to the ramp is ~200yd, under a minute of swimming.
    constexpr uint32 POOL_EXIT_TIMEOUT_MS = 120000;
    constexpr uint32 NPC_LORGUS_JETT = 12902;

    // The pool, its depth gate and the ramp out (defined beside the event).
    DcPoolExit::Pool const& ForgottenPool();
}

// Hellfire Ramparts (map 543) final-approach gate — see HellfireRampartsEvents.cpp
// for the measurements. Exposed so t/TestRampartsLedgeProbe can assert against the
// real navmesh that the zone-in platform lies outside the gate; the numbers are
// only meaningful together with that probe.
namespace DcHellfireRamparts
{
    // How far to scan for the Hellfire Sentries / Vazruden.
    constexpr float FINAL_APPROACH_SCAN = 45.0f;
    // Floor Z the bot must be above: the upper level the final platform sits on.
    constexpr float FINAL_APPROACH_MIN_Z = 76.0f;
}

// Blackrock Depths (map 230) — the Upper City numbers BlackrockDepthsEvents.cpp's
// events, hooks and roster patch share with the gtests (t/TestDungeonEvent,
// t/TestBlackrockDepthsRouteProbe). The reasoning for each is in that file.
namespace DcBlackrockDepths
{
    constexpr uint32 MAP_ID = 230;

    // instance_blackrock_depths DataTypes / EncounterState, kept local so the
    // module needn't pull the core BRD header.
    constexpr uint32 TYPE_BAR = 3;
    constexpr uint32 STATE_NOT_STARTED = 0;
    constexpr uint32 STATE_IN_PROGRESS = 1;
    constexpr uint32 STATE_DONE = 3;
    constexpr uint32 STATE_SPECIAL = 4;

    // Event ids (map-230 latch keys) and the roster objective sequence numbers
    // (OBJ(n)) that carry them. Events 1/2 (Ring of Law, Shadowforge Lock) are
    // older and keep their inline literals.
    constexpr uint32 EVENT_HURLEY_KEGS = 3;
    constexpr uint32 EVENT_RIBBLY = 4;
    constexpr uint32 EVENT_ROCKNOT = 5;
    constexpr uint32 OBJ_HURLEY_KEGS = 3;
    constexpr uint32 OBJ_RIBBLY = 4;
    constexpr uint32 OBJ_ROCKNOT = 5;

    // ObjectiveHookRegistry ids (one flat space across every dungeon).
    constexpr uint32 HOOK_START_ROCKNOT_ALE_RUN = 47;
    constexpr uint32 HOOK_WAKE_HURLEY = 52;
    constexpr uint32 HOOK_BUY_ROCKNOT_ALE = 54;
    constexpr uint32 HOOK_PHALANX_AT_DOOR = 55;

    constexpr uint32 NPC_HURLEY = 9537;
    constexpr uint32 NPC_PHALANX = 9502;
    constexpr uint32 NPC_ROCKNOT = 9503;
    constexpr uint32 NPC_RIBBLY = 9543;
    constexpr uint32 NPC_PLUGGER = 9499;
    constexpr uint32 GO_THUNDERBREW_KEG = 164911;
    constexpr uint32 GO_BAR_DOOR = 170571;
    constexpr uint32 QUEST_ALE = 4295;   // Rocknot's "Dark Iron Ale" turn-in
    constexpr uint32 ITEM_DARK_IRON_ALE_MUG = 11325;  // Plugger sells them, 4295 takes two
    // Three turn-ins of two mugs start Rocknot's walk (BarAleCount == 3).
    constexpr uint32 ALE_TURN_INS = 3;
    constexpr uint32 ALE_MUGS_PER_TURN_IN = 2;

    // Between the three Thunderbrew Lager Kegs, on the lower bar floor.
    constexpr float KEG_X = 916.7f;
    constexpr float KEG_Y = -150.2f;
    constexpr float KEG_Z = -49.76f;

    // Rocknot's hold: on the upper bar floor between his walk to the keg
    // (script_waypoint 9503, y -186..-198) and the Bar Door / Phalanx (y -225..-229),
    // off both. Probed in t/TestBlackrockDepthsRouteProbe.
    constexpr float ROCKNOT_HOLD_X = 878.0f;
    constexpr float ROCKNOT_HOLD_Y = -211.0f;
    constexpr float ROCKNOT_HOLD_Z = -43.7f;
    constexpr float ROCKNOT_HOLD_RADIUS = 6.0f;

    // Where the ale is bought. Plugger patrols BEHIND his counter (path 477030,
    // no delays, a ~10s loop) and the customer side only comes within trading
    // range at the counter's west end, where he turns at (878.1,-179.5). Probed
    // in t/TestBlackrockDepthsRouteProbe (PluggerIsReachableAcrossHisCounter).
    constexpr float PLUGGER_STAND_X = 877.0f;
    constexpr float PLUGGER_STAND_Y = -180.0f;
    constexpr float PLUGGER_STAND_Z = -43.53f;

    // Phalanx's guard spot in the Bar Door: the end of his wake-up run (path
    // 95020), where npc_phalanx turns him hostile.
    constexpr float PHALANX_DOOR_X = 867.295f;
    constexpr float PHALANX_DOOR_Y = -222.645f;

    // Where Plugger is fought (BossPullbackRegistry): 7.6yd out past the Bar
    // Door on the upper floor, 59yd of navmesh from his counter. His death turns
    // every patron within 100yd hostile; from here the nearest patron spawn is
    // 19.4yd off (a Hammered Patron, level 48-52, so at most a 14yd aggro radius
    // against a level-58 party) and the nearest Flamelash-side mob 25.5yd. Inside
    // the bar, at his counter, three patrons stand within 8yd of him and the way
    // out runs through two more groups at 1-3yd. Probed in
    // t/TestBlackrockDepthsRouteProbe.
    constexpr float PLUGGER_CAMP_X = 871.0f;
    constexpr float PLUGGER_CAMP_Y = -235.0f;
    constexpr float PLUGGER_CAMP_Z = -43.27f;

    // --- the Tomb of the Seven (event 6) ---------------------------------
    constexpr uint32 TYPE_TOMB_OF_SEVEN = 4;
    constexpr uint32 EVENT_TOMB_OF_SEVEN = 6;
    constexpr uint32 OBJ_TOMB_OF_SEVEN = 6;
    constexpr uint32 HOOK_TOMB_MUSTER = 51;
    constexpr uint32 HOOK_TOMB_RESET_WATCH = 53;
    constexpr uint32 NPC_DOOMREL = 9039;
    constexpr uint32 GO_TOMB_ENTER = 170576;
    constexpr uint32 GO_TOMB_EXIT = 170577;

    // The garrison: Doom'rel's upper tier, a few yards in front of him and off
    // the exit door. Every ghost wakes by SelectNearestPlayer(130) from its own
    // spawn, and this spot is inside 130yd of all six; it is also 90+yd from the
    // entrance door, so a member mustered on it is inside the tomb when the
    // gossip closes both doors. Probed in t/TestBlackrockDepthsRouteProbe.
    constexpr float TOMB_HOLD_X = 1268.0f;
    constexpr float TOMB_HOLD_Y = -272.0f;
    constexpr float TOMB_HOLD_Z = -78.1f;
    constexpr float TOMB_HOLD_RADIUS = 6.0f;
    // Muster: every living member this close to the hold (and on the tomb's
    // floors) before the gossip that seals the doors.
    constexpr float TOMB_MUSTER_RADIUS = 25.0f;
    constexpr float TOMB_MUSTER_ZBAND = 12.0f;
    // The core's wake range (instance_blackrock_depths::TombOfSevenEvent).
    constexpr float TOMB_WAKE_RANGE = 130.0f;

    // The six ghosts the instance wakes before Doom'rel, in wake order.
    struct TombGhost { uint32 entry; float x, y, z; };
    constexpr TombGhost TOMB_GHOSTS[] = {
        { 9035, 1215.05f, -220.66f, -85.59f },   // Anger'rel (encounter credit, bit 16)
        { 9038, 1286.56f, -253.33f, -78.06f },   // Seeth'rel
        { 9040, 1236.55f, -291.53f, -78.14f },   // Dope'rel
        { 9037, 1226.69f, -246.12f, -85.59f },   // Gloom'rel
        { 9036, 1272.32f, -199.82f, -85.59f },   // Vile'rel
        { 9034, 1246.18f, -189.97f, -85.59f },   // Hate'rel
    };

    // --- the Lyceum (events 7 and 8) -------------------------------------
    constexpr uint32 TYPE_LYCEUM = 5;
    constexpr uint32 EVENT_LYCEUM_SOUTH = 7;
    constexpr uint32 EVENT_LYCEUM_NORTH = 8;
    constexpr uint32 OBJ_LYCEUM_SOUTH = 7;
    constexpr uint32 OBJ_LYCEUM_NORTH = 8;
    constexpr uint32 HOOK_GATHER_TORCH = 48;
    constexpr uint32 HOOK_LIGHT_BRAZIER_S = 49;
    constexpr uint32 HOOK_LIGHT_BRAZIER_N = 50;
    constexpr uint32 NPC_FLAME_KEEPER = 9956;       // Shadowforge Flame Keeper, pool 1550
    constexpr uint32 ITEM_SHADOWFORGE_TORCH = 11885;
    constexpr uint32 GO_BRAZIER_S = 174744;
    constexpr uint32 GO_BRAZIER_N = 174745;
    constexpr uint32 GO_GOLEM_DOOR_N = 170573;
    constexpr uint32 GO_GOLEM_DOOR_S = 170574;
    constexpr uint32 GO_STATE_LIT_OR_OPEN = 0;      // GOState ACTIVE: a lit brazier, an open door

    // Where the tank stands to light each brazier: in front of it, on the
    // Lyceum floor, with its Fireguard Destroyer (8911) on the far side. The
    // braziers themselves are at (1330.14, -508.86) / (1431.13, -508.94), z -88.87.
    constexpr float BRAZIER_S_X = 1330.1f;
    constexpr float BRAZIER_S_Y = -503.0f;
    constexpr float BRAZIER_S_Z = -88.9f;
    constexpr float BRAZIER_N_X = 1431.1f;
    constexpr float BRAZIER_N_Y = -503.0f;
    constexpr float BRAZIER_N_Z = -88.9f;

    // Where each brazier OBJECTIVE is anchored — not on its brazier. An anchored
    // event stands the pull system, the scout-lag and the off-line route rejoin
    // down only once the tank is inside the anchor's arriveRadius; everything
    // before that is ordinary Advance. With the anchors on the brazier stands,
    // tp-20261018-211235-1 crossed the room to each one in Advance — 1m27s-2m27s
    // to the south stand from the door, 1m11s-2m31s on to the north one — with
    // the party scout-lagging 15-25yd behind, a rest gate after every fight, and
    // every one of the tank's 68 route-rejoin backtracks (none once an event
    // ran). So the south event starts 10yd inside the Lyceum door (1311.48,
    // -343.07), and the north event where the south one leaves the tank: on the
    // south stand. Both walks to a brazier are the events' own MoveTo steps.
    constexpr float LYCEUM_ENTRY_X = 1311.5f;
    constexpr float LYCEUM_ENTRY_Y = -353.0f;
    constexpr float LYCEUM_ENTRY_Z = -92.0f;

    // The four points pool 1550 (max_limit 2, 300s respawn) spawns its Flame
    // Keepers on. Every one is inside LYCEUM_KEEPER_SCAN of both anchors and both
    // brazier stands (t/TestDungeonEvent pins it): the hunt and the torch hook
    // both scan with this ONE radius, and if they disagreed the hook could ask
    // for a hunt the hunt step reads as already done, rewinding forever.
    // ROOM-WIDE (= LYCEUM_ROOM_SCAN): the hunt now starts at the door, and keeper
    // points 2 and 3 are 186yd and 213yd from it.
    struct PoolPoint { float x, y, z; };
    constexpr PoolPoint FLAME_KEEPER_POINTS[] = {
        { 1382.62f, -397.68f, -94.59f },
        { 1385.20f, -439.44f, -94.59f },
        { 1431.38f, -462.24f, -91.93f },
        { 1382.00f, -528.69f, -91.96f },
    };
    constexpr float LYCEUM_KEEPER_SCAN = 280.0f;

    // The room itself: the never-target Reservist box (DcNeverTargetRegistry —
    // all 232 of them, and every Flame Keeper point, lie inside it) plus a floor
    // band. Every one of the room's 238 spawns stands at z -96..-88, the Lyceum
    // door included, so the band keeps the box off any other level. The pet
    // sweep (DcLyceum::Tick) holds pets passive inside it.
    constexpr float LYCEUM_BOX_X_MIN = 1275.0f;
    constexpr float LYCEUM_BOX_X_MAX = 1480.0f;
    constexpr float LYCEUM_BOX_Y_MIN = -560.0f;
    constexpr float LYCEUM_BOX_Y_MAX = -305.0f;
    constexpr float LYCEUM_BOX_Z_MIN = -102.0f;
    constexpr float LYCEUM_BOX_Z_MAX = -80.0f;

    // How far the braziers are looked for, and the radius of the north event's
    // door completion. ROOM-WIDE: the braziers are 101yd apart and each has its
    // own entry, so nothing can be mistaken for its pair, while a scan sized to
    // the stand point loses the brazier the moment a fight drags the tank off
    // it. Live tr-20261018-190617-15: the south brazier was lit, the tank chased
    // Reservists off the healer to 102yd, the 40yd completion and the light
    // hook's 40yd lookup both read "no brazier", and the step ran Running until
    // the stall watchdog killed the run. Sized to reach both braziers and the
    // north Golem door (1389.55, -555.85) from every corner of the room box —
    // the furthest pair, door to the Lyceum-door corner, is 276yd
    // (t/TestDungeonEvent pins it).
    constexpr float LYCEUM_ROOM_SCAN = 280.0f;

    // Pack leash for both brazier events (DungeonEvent::packLeash): between
    // fights the tank holds until every living member is within this of it.
    // Followers trail the tank by ~6yd and a ranged healer by 15-20; past this,
    // a member is a column, not a party.
    constexpr float LYCEUM_PACK_LEASH = 22.0f;

    // A torch lost (never looted, or expired in the bags after its 300s) costs a
    // pool respawn: 300s plus slack. Past this with no keeper back, the torch
    // step stalls visibly — the signature of the pooled-respawn bug fixed by
    // AC PR #25826, not something DC works around.
    constexpr uint32 TORCH_WAIT_MS = 330000;

    // --- the Iron Hall (Magmus) -------------------------------------------
    // mod-individual-progression replaces the six C++ Ironhand Guardians (8982)
    // with SmartAI 108982s that cast Gout of Flame (15529 -> 15538 every second,
    // TARGET_UNIT_SRC_AREA_ENEMY, 30yd CIRCLE around the guardian) for as long as
    // they exist — deliberately (IP PR #890). DC fights around it: hazard rows
    // keep camps and follower slots out of the circles, a fight-in-place zone
    // keeps the tank from dragging Magmus out of the gap he spawns in, and the
    // safe-spot hop (below) keeps every member from stopping in the fire.
    constexpr uint32 NPC_MAGMUS = 9938;
    constexpr uint32 NPC_IRONHAND_GUARDIAN = 8982;      // stock AC
    constexpr uint32 NPC_IRONHAND_GUARDIAN_IP = 108982; // mod-individual-progression
    constexpr uint32 GO_THRONE_DOOR = 170575;
    constexpr float IRONHAND_FIRE_RADIUS = 30.0f;       // spell 15538's radius
    struct HallPoint { float x, y; };
    constexpr HallPoint IRONHAND_GUARDIANS[] = {
        { 1407.29f, -587.30f }, { 1353.43f, -587.32f },
        { 1406.88f, -632.44f }, { 1353.92f, -632.75f },
        { 1407.67f, -677.72f }, { 1353.16f, -677.38f },
    };
    constexpr float MAGMUS_X = 1380.72f;
    constexpr float MAGMUS_Y = -659.31f;
    constexpr float MAGMUS_Z = -91.97f;
    // The fire-free gap Magmus spawns in, measured against the six circles: a
    // diamond ~14yd across (x) by ~17yd long (y) centred on (1380.5, -655.5).
    // These are its four tips pulled in by a yard, i.e. points the party can
    // stand on with nothing landing on them. (The plan's band rectangle, x
    // 1372-1388 by y -648..-663, does NOT fit: its corners are 23-24yd from the
    // nearest guardians.)
    constexpr HallPoint IRON_HALL_SAFE[] = {
        { 1380.5f, -647.8f }, { 1380.5f, -663.6f },   // north / south tips
        { 1374.7f, -655.5f }, { 1386.5f, -655.5f },   // west / east tips
    };
    // Fight-in-place zone: the hall between the Golem doors (y -555.9) and the
    // throne door (y -710.5), wall to wall.
    constexpr float IRON_HALL_MIN_X = 1340.0f;
    constexpr float IRON_HALL_MAX_X = 1420.0f;
    constexpr float IRON_HALL_MIN_Y = -705.0f;
    constexpr float IRON_HALL_MAX_Y = -562.0f;

    // The safe-spot hop (DcIronHall, DungeonClearIronHallSafeSpot). A member may
    // walk through the fire but never STOP in it, so the hall is a chain of four
    // stopping places on the centre line, each a fire-free gap between the
    // statue bands: north by the Golem doors, gap 1, gap 2 (Magmus), south by
    // the throne door.
    //
    // "In the fire" is 32yd, not the raw 30: AC's area search measures to the
    // target's edge (IsWithinDist adds the unit's ~1.5yd reach), and half a yard
    // more covers drift. Every slot below sits at >= 33yd from every statue, so a
    // bot that arrives is a yard clear of the test that sent it — it cannot
    // re-fire on landing.
    constexpr float IRON_HALL_FIRE_KEEPOUT = 32.0f;
    // The hop's box: the fight-in-place box widened to the doors on both ends,
    // since the north and south stops stand past its y edges.
    constexpr float IRON_HALL_HOP_MIN_Y = -712.0f;
    constexpr float IRON_HALL_HOP_MAX_Y = -554.0f;
    constexpr float IRON_HALL_HOP_MIN_Z = -100.0f;
    constexpr float IRON_HALL_HOP_MAX_Z = -84.0f;
    constexpr float IRON_HALL_FLOOR_Z = -91.97f;
    constexpr uint32 IRON_HALL_ZONE_COUNT = 4;
    constexpr HallPoint IRON_HALL_ZONES[IRON_HALL_ZONE_COUNT] = {
        { 1380.5f, -562.5f },   // north, by the Golem doors
        { 1380.3f, -609.7f },   // gap 1
        { 1380.6f, -655.5f },   // gap 2, Magmus's
        { 1380.5f, -702.0f },   // south, by the throne door
    };
    // Five slots per stop, so a party does not stack on one point. The gaps are
    // diamonds ~8yd across by ~10 long at 32yd, so the slots are a 2.5yd cross.
    constexpr uint32 IRON_HALL_SLOT_COUNT = 5;
    constexpr HallPoint IRON_HALL_SLOT_OFFSETS[IRON_HALL_SLOT_COUNT] = {
        { 0.0f, 0.0f }, { 2.5f, 0.0f }, { -2.5f, 0.0f }, { 0.0f, 2.5f }, { 0.0f, -2.5f },
    };
}

// Upper Blackrock Spire (map 229) — the numbers BlackrockSpireEvents.cpp's events,
// hooks and roster patch share with the gtests (t/TestBlackrockSpireEvents,
// t/TestDungeonEvent). The reasoning for each is in that file.
//
// mod-individual-progression re-registers the UBRS instance and area-trigger
// scripts under the core's ScriptNames, so either may be running. Nothing here
// includes an IP header: its values are literals, named as such.
namespace DcBlackrockSpire
{
    constexpr uint32 MAP_ID = 229;

    // blackrock_spire.h DataTypes (boss-state slots, NOT DungeonEncounter bits).
    constexpr uint32 DATA_PYROGUARD_EMBERSEER = 9;
    constexpr uint32 DATA_WARCHIEF_REND_BLACKHAND = 10;
    constexpr uint32 DATA_DRAGONSPIRE_ROOM = 15;
    constexpr uint32 STATE_NOT_STARTED = 0;
    constexpr uint32 STATE_IN_PROGRESS = 1;
    constexpr uint32 STATE_FAIL = 2;
    constexpr uint32 STATE_DONE = 3;
    constexpr uint32 GO_STATE_OPEN = 0;    // GOState ACTIVE: an open door, a lit rune
    constexpr uint32 GO_STATE_CLOSED = 1;  // GOState READY

    // mod-individual-progression's blackrock_spire.h DATA_UBRS_DOOR_OPEN: its
    // instance script opens the Dragonspine Door on SetData(2046, 26). The core
    // script has no case for type 2046, so the same call is a no-op there.
    constexpr uint32 IP_DATA_UBRS_DOOR_OPEN = 26;

    constexpr uint32 AT_DRAGONSPIRE_HALL = 2046;

    // Event ids (map-229 latch keys), the roster objective sequence numbers that
    // carry them, and the ObjectiveHookRegistry ids (one flat space).
    constexpr uint32 EVENT_DRAGONSPINE_DOOR = 1;
    constexpr uint32 EVENT_DRAGONSPIRE_HALL = 2;
    constexpr uint32 OBJ_DRAGONSPINE_DOOR = 1;
    constexpr uint32 OBJ_DRAGONSPIRE_HALL = 2;
    constexpr uint32 HOOK_OPEN_DRAGONSPINE_DOOR = 56;
    constexpr uint32 HOOK_HALL_RUNES_SWEEP = 57;

    constexpr uint32 NPC_BLACKHAND_DREADWEAVER = 9817;
    constexpr uint32 NPC_BLACKHAND_SUMMONER = 9818;
    constexpr uint32 NPC_BLACKHAND_VETERAN = 9819;

    constexpr uint32 GO_DRAGONSPINE_DOOR = 164725;
    constexpr uint32 GO_EMBERSEER_IN = 175244;
    constexpr uint32 GO_DOORS = 175705;

    // The door event's stand point: the ubrs test row's entrance, inside AT 2046
    // (r10 around (102.43,-319.17)). The door itself is 22yd east of it at
    // (126.9,-319.1,70.9), past the executor's default 20yd GO search, so every
    // door lookup passes DOOR_SCAN.
    constexpr float DOOR_STAND_X = 105.0f;
    constexpr float DOOR_STAND_Y = -320.0f;
    constexpr float DOOR_STAND_Z = 65.5f;
    constexpr float DOOR_SCAN = 40.0f;
    // How long IP's brazier sequence gets before the click fallback (its door
    // opens ~3s after SetData), and how long the click gets before Blocked.
    constexpr uint32 DOOR_SCRIPT_WAIT_MS = 5000;
    constexpr uint32 DOOR_CLICK_WAIT_MS = 15000;

    // The hall objective's anchor, just inside the door.
    constexpr float HALL_ANCHOR_X = 131.0f;
    constexpr float HALL_ANCHOR_Y = -318.0f;
    constexpr float HALL_ANCHOR_Z = 71.0f;

    // The seven hall runes, in the clear order (lower floor 3, 1, 4, 2, then up
    // the ramp 7, 6, 5), each with the 9817/9818/9819 pack the instance script
    // lists for it (every one within 15yd at STORE time).
    struct HallRune { uint32 goEntry; uint32 room; float x, y, z; };
    constexpr HallRune HALL_RUNES[] = {
        { 175195, 3, 124.8f, -298.0f, 70.9f },
        { 175197, 1, 125.4f, -340.5f, 70.9f },
        { 175200, 4, 155.3f, -286.1f, 70.9f },
        { 175199, 2, 155.3f, -353.0f, 70.8f },
        { 175194, 7, 192.7f, -258.4f, 76.9f },
        { 175196, 6, 228.8f, -301.5f, 76.9f },
        { 175198, 5, 215.2f, -334.7f, 76.8f },
    };
    // The script's 15yd list radius plus melee reach: the ClearRadius steps and
    // the sweep share it, so a mob the sweep finds is one a rewound step clears.
    constexpr float HALL_RUNE_CLEAR_RADIUS = 18.0f;
    constexpr float HALL_RUNE_CLEAR_ZBAND = 6.0f;
    // Every rune and Emberseer In from anywhere in the hall (rune room 1 to the
    // door is 106yd).
    constexpr float HALL_SCAN = 130.0f;
    // The script's CHECK runs every 3s; a lit rune with no pack near it for
    // longer than this means CHECK is not running (STORE never armed) or the
    // snapshot holds a mob that is not coming back — re-fire the area trigger.
    constexpr uint32 HALL_REARM_AFTER_MS = 8000;
    constexpr uint32 HALL_REARM_FLOOR_MS = 20000;
    // Floor between two sweep rewinds, so a mob nothing can reach cannot spin
    // the event.
    constexpr uint32 HALL_RESTART_FLOOR_MS = 30000;

    // --- the Blackrock Altar and Pyroguard Emberseer (event 3) -----------
    constexpr uint32 EVENT_BLACKROCK_ALTAR = 3;
    constexpr uint32 OBJ_BLACKROCK_ALTAR = 3;
    constexpr uint32 HOOK_MUSTER_HALL_OF_BINDING = 58;
    constexpr uint32 HOOK_EMBERSEER_FREED = 59;
    // Not in the plan's 56-61 block: the plan's bare UseGO step does not hold
    // the ritual's channel (see UseBlackrockAltar), so the altar got a hook.
    constexpr uint32 HOOK_USE_BLACKROCK_ALTAR = 62;

    constexpr uint32 NPC_PYROGUARD_EMBERSEER = 9816;
    constexpr uint32 NPC_BLACKHAND_INCARCERATOR = 10316;
    constexpr uint32 GO_BLACKROCK_ALTAR = 175706;
    constexpr uint32 GO_EMBERSEER_OUT = 175153;
    // The altar's ritual spell 16533 sends this event; the instance's
    // ProcessEvent starts Emberseer's release when his state is NOT_STARTED.
    constexpr uint32 EVENT_PYROGUARD_EMBERSEER = 4884;
    // The ritual's anim spell: the user channels it, and GameObject::Update
    // drops a participant who is no longer channelling it before the 5s
    // ritual completes.
    constexpr uint32 SPELL_ALTAR_CHANNEL = 16532;

    // The altar (144.4,-280.9,91.5) and the stand point on the room floor 4.9yd
    // north of it — also the objective anchor.
    constexpr float ALTAR_X = 144.445f;
    constexpr float ALTAR_Y = -280.941f;
    constexpr float ALTAR_STAND_X = 144.4f;
    constexpr float ALTAR_STAND_Y = -276.0f;
    constexpr float ALTAR_STAND_Z = 91.6f;
    constexpr float ALTAR_SCAN = 15.0f;
    // One Use() per this floor while the ritual has not taken.
    constexpr uint32 ALTAR_USE_FLOOR_MS = 3000;
    // A ritual that has not started his release in this long is not going to
    // (the channel broke, every time): enter the script's own entry point.
    constexpr uint32 ALTAR_FALLBACK_MS = 15000;
    // The seven incarcerators stand at x 126-162, y -241..-277, z 91.6; the
    // farthest is 39.5yd from the stand point.
    constexpr float INCARCERATOR_CLEAR_RADIUS = 45.0f;

    // The Hall of Binding: the incarcerators' aggro shuts Emberseer In and the
    // Doors two seconds after the altar, so everyone must be in here first. The
    // room floor is z 91.6 with his platform at 96.4; the hall below is z 71 and
    // the Rookery floor west of Emberseer Out is x < 113.
    struct Box { float minX, minY, minZ, maxX, maxY, maxZ; };
    constexpr Box HALL_OF_BINDING = { 115.0f, -284.0f, 88.0f, 174.0f, -236.0f, 100.0f };
    // The muster's cap (DC_SEALED_MUSTER_TIMEOUT_MS's 20s): then proceed and
    // name who is outside.
    constexpr uint32 MUSTER_CAP_MS = 20000;
    // Emberseer's room, for the event-level completion on Emberseer Out (35yd
    // from the stand point).
    constexpr float BINDING_SCAN = 60.0f;

    // --- the Rookery crossing (no event) ----------------------------------
    // The measured north lane from Emberseer Out to the upper floor, every egg
    // >= 3.8yd off. Each point is on a navmesh poly at that z. Probed in
    // t/TestBlackrockSpireRouteProbe.
    struct LanePoint { float x, y, z; };
    constexpr LanePoint ROOKERY_LANE[] = {
        { 113.9f, -258.9f,  91.8f },   // Emberseer Out
        {  96.0f, -258.5f,  91.6f },
        {  80.0f, -258.0f,  91.6f },   // between egg clusters 12 and 15
        {  66.5f, -257.5f,  96.3f },
        {  67.5f, -244.5f,  98.9f },   // turn north
        {  88.9f, -241.3f, 105.1f },   // the ramp, above cluster 15
        { 100.6f, -252.8f, 106.6f },
        { 108.1f, -266.0f, 106.7f },   // the upper floor
    };
    constexpr uint32 GO_ROOKERY_EGG = 175124;

    // The anchor route Emberseer's room -> the stadium muster point
    // (RegisterBlackrockSpireRoute): the lane above across the Rookery, then the
    // navmesh's own corridor over the upper floor, the Hall of Blackhand and the
    // z118 walkway, decimated to legs under 10yd from its verbatim vertices.
    // Anchors 6-21 (the Rookery, Emberseer Out to the upper floor) are walked
    // with ROOKERY_ANCHOR_ARRIVE so the tank cannot cut a corner into a cluster.
    // Probed in t/TestBlackrockSpireRouteProbe.
    constexpr LanePoint ROOKERY_ROUTE[] = {
        { 143.00f, -265.75f, 95.66f },  //  0  Emberseer's room, off his platform
        { 141.61f, -269.50f, 93.45f },  //  1
        { 134.13f, -266.64f, 92.46f },  //  2
        { 126.66f, -263.78f, 92.19f },  //  3
        { 119.19f, -260.92f, 92.39f },  //  4
        { 115.45f, -259.49f, 92.28f },  //  5
        { 113.90f, -258.90f, 91.80f },  //  6  Emberseer Out
        { 104.95f, -258.70f, 91.70f },  //  7
        { 96.00f, -258.50f, 91.60f },  //  8
        { 88.00f, -258.25f, 91.60f },  //  9
        { 80.00f, -258.00f, 91.60f },  // 10  the pinch between egg clusters 12 and 15
        { 73.25f, -257.75f, 93.95f },  // 11
        { 66.50f, -257.50f, 96.30f },  // 12  the west end, turn north
        { 67.00f, -251.00f, 97.60f },  // 13
        { 67.50f, -244.50f, 98.90f },  // 14  the ramp foot
        { 74.63f, -243.43f, 100.97f },  // 15
        { 81.77f, -242.37f, 103.03f },  // 16
        { 88.90f, -241.30f, 105.10f },  // 17  the ramp, above cluster 15
        { 94.75f, -247.05f, 105.85f },  // 18
        { 100.60f, -252.80f, 106.60f },  // 19
        { 104.35f, -259.40f, 106.65f },  // 20
        { 108.10f, -266.00f, 106.70f },  // 21  the upper floor
        { 108.27f, -274.00f, 107.20f },  // 22
        { 108.44f, -282.00f, 107.20f },  // 23
        { 108.61f, -289.99f, 107.20f },  // 24
        { 108.79f, -297.99f, 107.20f },  // 25
        { 108.99f, -305.99f, 107.20f },  // 26
        { 109.10f, -309.99f, 107.20f },  // 27
        { 116.76f, -312.29f, 108.56f },  // 28
        { 124.42f, -314.60f, 112.46f },  // 29
        { 132.08f, -316.90f, 111.58f },  // 30
        { 139.74f, -319.21f, 111.62f },  // 31  the Hall of Blackhand
        { 138.73f, -327.14f, 111.53f },  // 32
        { 137.72f, -335.08f, 111.47f },  // 33
        { 136.71f, -343.01f, 111.56f },  // 34
        { 135.70f, -350.95f, 113.12f },  // 35
        { 134.47f, -358.85f, 117.97f },  // 36
        { 133.73f, -362.78f, 118.04f },  // 37  the z118 walkway
        { 125.88f, -364.33f, 118.17f },  // 38
        { 118.04f, -365.88f, 118.44f },  // 39
        { 110.19f, -367.42f, 118.37f },  // 40
        { 102.34f, -368.97f, 117.66f },  // 41
        { 97.11f, -375.02f, 118.52f },  // 42
        { 94.49f, -378.05f, 118.28f },  // 43  the descent
        { 94.78f, -386.05f, 117.72f },  // 44
        { 95.06f, -394.04f, 115.51f },  // 45
        { 95.35f, -402.04f, 112.66f },  // 46
        { 96.23f, -409.96f, 112.80f },  // 47
        { 101.84f, -415.68f, 112.17f },  // 48
        { 104.64f, -418.53f, 112.27f },  // 49
        { 112.59f, -419.41f, 111.45f },  // 50
        { 118.00f, -420.00f, 110.87f },  // 51  the stadium muster point, inside portcullis 164726
    };
    constexpr uint32 ROOKERY_ROUTE_LANE_FIRST = 6;
    constexpr uint32 ROOKERY_ROUTE_LANE_LAST = 21;
    constexpr float ROOKERY_ANCHOR_ARRIVE = 2.0f;
    constexpr uint32 NPC_ROOKERY_WHELP = 10161;
    constexpr uint32 NPC_WARCHIEF_REND_BLACKHAND = 10429;

    // --- the Blackrock Stadium: Rend and Gyth (event 4) -------------------
    constexpr uint32 EVENT_BLACKROCK_STADIUM = 4;
    constexpr uint32 OBJ_BLACKROCK_STADIUM = 4;
    constexpr uint32 HOOK_MUSTER_IN_STADIUM = 60;
    constexpr uint32 HOOK_ENSURE_STADIUM_STARTED = 61;
    constexpr uint32 NPC_LORD_VICTOR_NEFARIUS = 10162;
    constexpr uint32 GO_GYTH_ENTRY_DOOR = 164726;   // the stadium portcullis (x 108)
    constexpr uint32 GO_GYTH_COMBAT_DOOR = 175185;  // the pen gate the waves come through
    constexpr uint32 GO_GYTH_EXIT_DOOR = 175186;    // "TOBOSSROOMS", opens on Rend's death

    // AT 2026 at_blackrock_stadium: (153.76, -419.83, 110.47), radius 20. It
    // starts the event for any living player inside it — and the portcullis
    // shuts 1s later — so the party musters well outside it first.
    constexpr uint32 AT_BLACKROCK_STADIUM = 2026;
    constexpr float AT_STADIUM_X = 153.76f;
    constexpr float AT_STADIUM_Y = -419.83f;
    constexpr float AT_STADIUM_RADIUS = 20.0f;

    // The muster point: 10yd inside the portcullis, 36yd outside AT 2026, 30yd
    // from the nearest stadium-edge egg. Also the objective anchor, and the end
    // of ROOKERY_ROUTE.
    constexpr float STADIUM_MUSTER_X = 118.0f;
    constexpr float STADIUM_MUSTER_Y = -420.0f;
    constexpr float STADIUM_MUSTER_Z = 111.0f;
    // The arena stand: inside AT 2026, 45yd from the pen (summons despawn with
    // nobody within 100yd), 25yd from Rend on the balcony (the AT script finds
    // him within 100yd of the player), 46yd from the nearest egg.
    constexpr float ARENA_STAND_X = 150.0f;
    constexpr float ARENA_STAND_Y = -420.0f;
    constexpr float ARENA_STAND_Z = 110.9f;
    constexpr float ARENA_HOLD_RADIUS = 6.0f;
    // Everyone past the portcullis (x 108) and on the arena floor — not the
    // z 106-118 floors and walkway outside it.
    constexpr Box STADIUM_ARENA = { 110.0f, -455.0f, 108.0f, 192.0f, -386.0f, 114.0f };
    // Floor between two forged AT 2026 fires while Rend reads NOT_STARTED.
    constexpr uint32 STADIUM_START_FLOOR_MS = 5000;

    // --- The Beast and General Drakkisath (no events) ---------------------
    constexpr uint32 NPC_THE_BEAST = 10430;
    constexpr uint32 NPC_GENERAL_DRAKKISATH = 10363;
    constexpr uint32 GO_DRAKKISATH_DOOR_1 = 175946;
    constexpr uint32 GO_DRAKKISATH_DOOR_2 = 175947;

    // Where The Beast is fought (BossPullbackRegistry), and his roster anchor.
    // boss_the_beast evades when his own Y goes above -505 in combat, and the
    // corridor on to Drakkisath runs north from (29,-503): a fight at its mouth,
    // or a Berserker Charge onto a bot standing in it, resets him at full health.
    // The camp is on the Furnace floor (poly z 111.0), 35yd south of that line,
    // inside AT 2066 (75.26,-547.68 r30), so arriving starts his patrol and he
    // comes to the party; the nearest hostile spawn is 51yd off (the Rage Talon
    // pack at the corridor mouth). Probed in t/TestBlackrockSpireRouteProbe.
    constexpr float BEAST_CAMP_X = 60.0f;
    constexpr float BEAST_CAMP_Y = -540.0f;
    constexpr float BEAST_CAMP_Z = 111.0f;
    constexpr float BEAST_EVADE_Y = -505.0f;

    // Drakkisath's room pre-clear (RoomAggroRegistry): every pack around his
    // room — the north pair and its neighbours (21-34yd), the south archway
    // (42-48yd), the western group (47-57yd) — and nothing past the 57->72yd gap
    // to the corridor packs.
    constexpr float DRAKKISATH_X = 36.45f;
    constexpr float DRAKKISATH_Y = -285.99f;
    constexpr float DRAKKISATH_ROOM_RADIUS = 60.0f;

    inline bool InBox(Box const& b, float x, float y, float z)
    {
        return x >= b.minX && x <= b.maxX && y >= b.minY && y <= b.maxY && z >= b.minZ && z <= b.maxZ;
    }
}

void RegisterHellfireRampartsEvents(std::vector<DungeonEvent>& out);
void RegisterBloodFurnaceEvents(std::vector<DungeonEvent>& out);
void RegisterSlavePensEvents(std::vector<DungeonEvent>& out);
void RegisterUnderbogEvents(std::vector<DungeonEvent>& out);
void RegisterOldHillsbradEvents(std::vector<DungeonEvent>& out);
void RegisterMechanarEvents(std::vector<DungeonEvent>& out);
void RegisterShatteredHallsEvents(std::vector<DungeonEvent>& out);
void RegisterSteamvaultEvents(std::vector<DungeonEvent>& out);
void RegisterArcatrazEvents(std::vector<DungeonEvent>& out);
void RegisterSethekkHallsEvents(std::vector<DungeonEvent>& out);
void RegisterBlackMorassEvents(std::vector<DungeonEvent>& out);
// Everything in the Black Morass wave that DRAINS Medivh's shield rather than
// fighting the party: the nine trash adds (smart_scripts SMART_EVENT_RESET ->
// CAST 'Corrupt Medivh' 31326 on SELF) AND AEONUS, whose boss_aeonus::
// IsSummonedBy does the same thing in C++ and whose 37853 drains at DOUBLE the
// rate. All of them spawn REACT_DEFENSIVE and park at a home 14yd from Medivh, so
// they never aggro the party and the engage pipeline's natural pull never reaches
// them — the wave driver (ObjectiveHookRegistry hook 12) force-pulls them and
// counts them to decide when Medivh's ring needs cleaning. Excludes the Rift
// Lords / Keepers and the wave-6/12 bosses, which all fight normally.
std::vector<uint32> const& BlackMorassDrainEntries();

// The Black Morass RIFT KEEPERS — the mob each Time Rift summons 6s after it
// opens, and the ONLY thing whose death closes the rift
// (npc_time_rift::SummonedCreatureDies -> DespawnOrUnsummon). Shared with the
// wave driver (ObjectiveHookRegistry hook 12), which selects and pulls by it.
// Disjoint from BlackMorassDrainEntries() on purpose: keepers fight normally and
// never channel Corrupt, so they are never sweep targets — and the drainers never
// close a rift, so they are never selection targets.
//
// AEONUS IS NOT HERE despite being the wave-18 boss: it walks off to Medivh the
// instant it spawns (so it is never at the rift to select on) and it is not its
// rift's _riftKeeperGUID (so killing it closes nothing). It is a drainer.
std::vector<uint32> const& BlackMorassKeeperEntries();

// Wrath of the Lich King.
void RegisterUtgardeKeepEvents(std::vector<DungeonEvent>& out);
void RegisterNexusEvents(std::vector<DungeonEvent>& out);
void RegisterAzjolNerubEvents(std::vector<DungeonEvent>& out);
void RegisterAhnkahetEvents(std::vector<DungeonEvent>& out);
// Drak'Tharon Keep (map 600) — Novos' camp, shared with the hold driver
// (ObjectiveHookRegistry hook 14, HoldNovosCamp) so the camp and the keep-out it
// is placed against have exactly ONE definition. Every number is measured; the
// reasoning is in DrakTharonKeepEvents.cpp.
namespace DcDrakTharonKeep
{
    constexpr uint32 NOVOS = 26631;

    // Column-probed against the live 600 mmtiles: one walkable surface at
    // z 28.39. 19.3yd from Novos (-379.27, -737.73), 14.9yd from the Fetid Troll
    // Corpses' arrival point, 56yd from the staircase spawn trigger.
    constexpr float CAMP_X = -379.0f;
    constexpr float CAMP_Y = -757.0f;
    constexpr float CAMP_Z = 28.4f;

    // Grid-scan radius for Novos, from the activation predicate and the driver
    // alike. The chamber is ~96 x 88yd; this must cover it and the approach
    // without reaching Trollgore's arena 206yd away.
    constexpr float NOVOS_SCAN = 120.0f;

    // Keep-out around Novos while the Arcane Field (47346) is up. THIS MUST TRACK
    // the map-600 47346 row's placement `radius` in DcHazardRegistry — the driver
    // and the placement solver have to agree on one cylinder, and
    // t/TestDcHazard's DrakTharonArcaneFieldKeepOutAgreesWithTheNovosCamp pins
    // the camp against it.
    constexpr float FIELD_KEEPOUT = 14.0f;

    // Re-centring leash (the tank comes home past this UNLESS it is in melee
    // contact) and the hard leash (it comes home regardless). The hard leash is
    // sized to catch the three places phase 1 can strand a party — the staircase
    // at 56yd, ROOM_LEFT at 40yd and ROOM_RIGHT at 50yd — while leaving the
    // corpse arrival point at 14.9yd comfortably inside.
    constexpr float CAMP_LEASH = 6.0f;
    constexpr float CAMP_HARD_LEASH = 25.0f;
}

void RegisterDrakTharonKeepEvents(std::vector<DungeonEvent>& out);

// The Violet Hold (map 608) — the numbers the declarative half
// (VioletHoldEvents.cpp) and the imperative half (VioletHoldDriver.cpp) must
// agree on. Every one of them is either read straight out of violet_hold.h or
// probed against the live 608 mmtile; the reasoning is in VioletHoldEvents.cpp.
// Shared here for the same reason DcDrakTharonKeep is: a camp and the keep-out
// it is placed against need exactly ONE definition, and the gtests pin them
// against each other.
namespace DcVioletHold
{
    constexpr uint32 MAP = 608;

    // Creature entries (violet_hold.h VHCreatures).
    constexpr uint32 NPC_SINCLARI             = 30658;
    constexpr uint32 NPC_PRISON_DOOR_SEAL     = 30896;
    constexpr uint32 NPC_TELEPORTATION_PORTAL = 31011;
    constexpr uint32 NPC_CYANIGOSA            = 31134;
    constexpr uint32 NPC_ICHORON              = 29313;
    constexpr uint32 NPC_ICHOR_GLOBULE        = 29321;

    // The aura every wave add parks on the Prison Door Seal
    // (violet_hold_trashAI::CreatureStartAttackDoor -> DoCastAOE). Spell.dbc:
    // effect 6, aura 23 SPELL_AURA_PERIODIC_TRIGGER_SPELL, amplitude 3000ms,
    // DurationIndex 21 = INFINITE. spell_destroy_door_seal_aura turns each tick
    // into one ACTION_DECREASE_DOOR_HEALTH, so ONE add at the door costs the
    // 100-point gate one point every three seconds: 300s to lose the run alone,
    // 150s for two, 100s for three. That arithmetic is what sizes SEAL_DIRTY_MIN.
    constexpr uint32 SPELL_DESTROY_DOOR_SEAL  = 58040;

    // GetData ids (violet_hold.h VHData). _gateHealth has NO GetData case, which
    // is why the driver reads the seal's aura instead of the drain level.
    constexpr uint32 DATA_ENCOUNTER_STATUS    = 30;
    constexpr uint32 DATA_WAVE_COUNT          = 33;

    // GetBossState slots (violet_hold.h VHBosses). NOT DungeonEncounter bits:
    // the released pair is rolled per instance, so killing Zuramat as the first
    // prisoner sets no bit that names Zuramat. Completion rides these.
    constexpr uint32 BOSS_STATE_1ST           = 0;
    constexpr uint32 BOSS_STATE_2ND           = 1;
    constexpr uint32 BOSS_STATE_CYANIGOSA     = 2;

    // ObjectiveHookRegistry ids. Three defend hooks, not one: a Custom step is
    // handed a default-constructed DungeonBossInfo, so a shared hook cannot tell
    // which objective invoked it.
    constexpr uint32 HOOK_START               = 15;
    constexpr uint32 HOOK_DEFEND_1ST          = 16;
    constexpr uint32 HOOK_DEFEND_2ND          = 17;
    constexpr uint32 HOOK_DEFEND_CYANIGOSA    = 18;
    constexpr uint32 HOOK_DRIVE_WAVE          = 19;

    // THE DOOR CAMP — the EMERGENCY position, not the default one.
    //
    // On the flat door landing at the top of the entrance ramp (the ramp runs
    // z 38.6 at x~1869.8 up to z 44.0 at x~1861.5), straddling the funnel that
    // the last two waypoints of ALL SIX trash paths run through — (1858.95,
    // 810.05), (1860.84, 806.65), (1861.54, 804.15) and (1857.81, 796.77) all
    // lie within 7yd of it. 10.4yd inside the convergence midpoint and 32.4yd
    // from the Prison Seal itself, so the party is between the adds and the door
    // without standing on the door. Column-probed against the live 608 mmtile:
    // exactly ONE walkable surface, z 44.23.
    //
    // The first cut of this dungeon made this the party's STANDING position and
    // let the siege walk to it. That reading of the chokepoint is arithmetically
    // tidy and is not how the Violet Hold is played, or won: a keeper portal
    // pumps 3-4 adds every 20 seconds FOREVER and the only off-switch is 52-86yd
    // away at the rim, so a party that waits at the door fights the pump's output
    // instead of the pump and falls further behind every cycle. The party now
    // stations at the live portal (STAGE / rule 5 in VhDriveWave) and comes BACK
    // here only when something has actually reached the seal.
    constexpr float CAMP_X = 1855.0f;
    constexpr float CAMP_Y = 803.5f;
    constexpr float CAMP_Z = 44.05f;

    // THE STAGING POINT — where the party waits when no portal is open.
    //
    // The middle of the arena floor, at the foot of the entrance ramp: the core's
    // own MiddleRoomLocation, which is where Cyanigosa MoveJumps to on wave 18 and
    // 4.4yd from the wave-6/12 saboteur portal, so it is a position the encounter
    // itself treats as the centre of the fight. Column-probed against the live 608
    // mmtile: exactly ONE walkable surface, z 38.89.
    //
    // It exists because the party's job between waves is to be CLOSE TO THE NEXT
    // PORTAL, and the six rim portals average 45.5yd from here against 69.0yd from
    // the door camp (worst case 59.0 against 85.5). That is ~3.5s off every hop, on
    // a clock where a keeper portal starts pumping 30s after it opens. Every trash
    // path also runs through this half of the room on its way to the door, so
    // waiting here still meets the leftovers of the previous wave head-on.
    constexpr float STAGE_X = 1892.29f;
    constexpr float STAGE_Y = 805.70f;
    constexpr float STAGE_Z = 38.44f;

    // Where every wave add ends up: the midpoint of the two convergence points
    // (1843.71, 805.81, 44.14) — paths 0, 1, 1-alt, 2, 5 — and (1845.58, 800.68,
    // 44.10) — paths 3, 4, and also violet_hold_trashAI::EnterEvadeMode's new
    // home. The driver measures "how dirty is the seal" from here when the aura
    // read is unavailable.
    constexpr float SEAL_X = 1844.6f;
    constexpr float SEAL_Y = 803.2f;
    constexpr float SEAL_Z = 44.12f;

    // Arena centroid (mean of the six rim portal positions), used only by the
    // wave event's proximity gate, and the grid-scan radius that covers the whole
    // hold. The room is ~110yd across and the far rim portal is 86yd from the
    // camp; 200 sees all of it from anywhere the driver roams.
    constexpr float ARENA_X = 1893.1f;
    constexpr float ARENA_Y = 804.7f;
    constexpr float ARENA_SCAN = 200.0f;
    constexpr float EVENT_DUE_RANGE = 200.0f;

    // "Released" for a caged prisoner (and for Erekem's two guards): the
    // instance's StartBossEncounter clears UNIT_FLAG_NON_ATTACKABLE and
    // SetImmuneToNPC/All(false) at the moment the cell opens, so the flags ARE
    // the release latch. Also false through Ichoron's shattered-bubble window,
    // when he carries UNIT_FLAG_NOT_SELECTABLE for 15s — which is what makes the
    // driver retarget to his Ichor Globules instead of standing in drop-target
    // limbo. Defined in VioletHoldEvents.cpp.
    bool IsReleased(Creature const* c);
}

void RegisterVioletHoldEvents(std::vector<DungeonEvent>& out);
void RegisterMoltenCoreEvents(std::vector<DungeonEvent>& out);
void RegisterZulGurubEvents(std::vector<DungeonEvent>& out);
void RegisterTempleOfAhnQirajEvents(std::vector<DungeonEvent>& out);

// --- Halls of Stone (map 599) ---------------------------------------------
//
// The numbers HallsOfStoneEvents.cpp authors, HallsOfStoneDriver.cpp steers by
// and t/TestHallsOfStone.cpp pins. Shared here for the same reason
// DcDrakTharonKeep, DcVioletHold and DcGundrak are: the hold point and the add
// spawns it is placed against need exactly ONE definition, and several of these
// are SAFETY numbers whose whole value is that a test can re-derive them.
//
// Every coordinate below was column-probed against the live 599 mmtiles
// (7 tiles: 599.mmap + 2929/2930/2931/2932/3030/3031/3032). That is not
// ceremony on this map: the columns under the Tribunal arena carry a PHANTOM
// SURFACE at z ~ -142 and the columns under the eastern half carry one at
// z ~ 0.16 — map 599 is in the flat-grid-height family
// (ac-map601-flat-gridheight-zero), so an anchor authored from a script literal
// rather than from the probed TOP surface is a sink waiting to happen.
namespace DcHallsOfStone
{
    constexpr uint32 MAP = 599;

    // --- creature entries -------------------------------------------------
    //
    // PLAIN NORMAL-MODE ENTRIES, ON BOTH DIFFICULTIES, and that is worth one
    // paragraph because creature_template says otherwise at a glance.
    //
    // Almost every creature here carries a difficulty_entry_1 twin: Brann
    // 28070 -> 31366, Sjonnir 27978 -> 31386, the three wave adds 27983/27984/
    // 27985 -> 31876/31877/31380, the two hazard triggers 28237/28265 ->
    // 31875/31878, the Earthen Dwarf 27980 -> 31391. Reading that column alone
    // suggests every entry-keyed step and scan in this dungeon is normal-only and
    // silently dead on heroic.
    //
    // IT IS NOT, because a difficulty twin is a STAT TEMPLATE, not a separate
    // spawned entry. Creature::InitEntry resolves DifficultyEntry[diff-1] into
    // `m_creatureInfo` — the stats, the flags, the damage — and then does
    // `SetEntry(Entry); // normal entry always`. So Creature::GetEntry() returns
    // the NORMAL id on heroic, and FindNearestCreature / GetCreatureListWithEntry
    // InGrid match on it exactly as they do on normal.
    //
    // The two ways that could still have gone wrong are both checked and both
    // clear: Brann is a DB spawn (guid 126801, id 28070), and every summon in
    // brann_bronzebeard.cpp passes the bare normal constant — SummonCreature(
    // NPC_KADDRAK...), SummonCreatureGroup(0..2) over creature_summon_groups rows
    // whose entry column is 27983/27984/27985. Nothing in the script ever names a
    // 318xx id; heroic differs only in the stat template and in three
    // IsHeroic() ? ... : ... REPEAT CADENCES.
    //
    // So: no difficulty-gated event rows, no heroic entries in the scan lists.
    // Recorded here because the obvious "fix" on reading difficulty_entry_1 is to
    // add them, and adding them would be dead weight that reads as a safety net.
    constexpr uint32 NPC_BRANN                   = 28070;
    constexpr uint32 NPC_SJONNIR                 = 27978;
    constexpr uint32 NPC_KRYSTALLUS              = 27977;
    constexpr uint32 NPC_MAIDEN_OF_GRIEF         = 27975;

    // The three stone faces. NullCreatureAI, faction 114, unit_flags 33554436
    // (NOT_SELECTABLE | 0x4): they cannot be targeted, damaged or interrupted,
    // and they are pure spell emitters. They exist ONLY between InitializeEvent()
    // and EndTribunalFight(), which is what makes a bare aliveness probe on them
    // a sound "the Tribunal is running" test — and the only probe that reads true
    // during the quiet first 52 seconds, before any add has spawned. These three
    // are also the only creatures on the map with NO difficulty twin at all.
    constexpr uint32 NPC_KADDRAK                 = 30898;
    constexpr uint32 NPC_MARNAK                  = 30897;
    constexpr uint32 NPC_ABEDNEUM                = 30899;

    // The wave adds (creature_summon_groups, summonerId 28070 — three rows of
    // 27983 at (943.088, 401.378, 206.161), two of 27984 at (967.000, 376.832,
    // 206.161), one of 27985 at (964.302, 381.942, 206.161)). Every one is a
    // TempSummon with spawnId 0, so the spawn store cannot see them and both the
    // predicate and the driver must grid-scan.
    constexpr uint32 NPC_DARK_RUNE_PROTECTOR     = 27983;
    constexpr uint32 NPC_DARK_RUNE_STORMCALLER   = 27984;
    constexpr uint32 NPC_IRON_GOLEM_CUSTODIAN    = 27985;

    // The two ground-hazard carriers, both NOT_SELECTABLE triggers. See the
    // DcHazardRegistry rows — these are EMITTERS (a creature carrying a pulsing
    // aura), NOT persistent-area-aura pools, and that distinction is load-bearing.
    constexpr uint32 NPC_SEARING_GAZE_TRIGGER    = 28265;
    constexpr uint32 NPC_DARK_MATTER_TARGET      = 28237;
    constexpr uint32 NPC_DARK_MATTER_VISUAL      = 28235;

    // Sjonnir's 25% adds. Summoned, then immediately handed a random PLAYER's
    // faction — they fight on your side. See the DcNeverTargetRegistry row.
    constexpr uint32 NPC_EARTHEN_DWARF           = 27980;

    // The three constructs Brann walks into while REACT_AGGRESSIVE.
    constexpr uint32 NPC_RAGING_CONSTRUCT        = 27970;
    constexpr uint32 NPC_UNRELENTING_CONSTRUCT   = 27971;
    constexpr uint32 NPC_LIGHTNING_CONSTRUCT     = 27972;

    // THE ONLY FUNCTIONAL DOOR ON THE MAP, and the whole reason this dungeon
    // needs automation. gameobject.state = 1 (closed), Data0 = 0, lockId 0. The
    // only code path that opens it is instance_halls_of_stone's
    // SetData(BRANN_DOOR, DONE), whose only caller is Brann arriving at
    // POINT_SJONNIR_DOOR — 3.2s after a player takes gossip 10012.
    //
    // The map's other five doors (191292/191293/191294/191295/191459) all spawn
    // state = 0 (open), are referenced by no C++ anywhere, and are deliberately
    // NOT in DcEventDoorRegistry::IsScriptOnly — see the note there.
    constexpr uint32 GO_SJONNIR_DOOR             = 191296;

    // --- the instance index space (halls_of_stone.h) ----------------------
    //
    // READ THE MASK OR THE BOSS STATE, NEVER GetData, for indices 0-4:
    //
    //   * Krystallus (0) and the Maiden of Grief (1) never call SetBossState at
    //     all, so GetBossState(0) / GetBossState(1) are permanently NOT_STARTED.
    //   * GetData(BRANN_BRONZEBEARD) is permanently 0: the escort writes only the
    //     boss state, and instance_halls_of_stone::GetData just returns the
    //     SetData-written Encounter[] slot. So a garrison cannot gate on the
    //     escort through instance data.
    //   * GetData(BOSS_TRIBUNAL_OF_AGES) is unusable as a gate: it stays 0 through
    //     the whole 300s fight and only starts toggling SPECIAL/DONE during the
    //     256s of post-fight lore. Completion rides mask bit 2 / GetBossState(2).
    //
    // BRANN_DOOR (5) is the exception and the ONE clean instance-data reading on
    // this map: brann_bronzebeard.cpp writes BOTH stores (SetBossState and
    // SetData) when the door opens, so GetData(BRANN_DOOR) >= DONE is truthful
    // and monotonic. That is what the escort step's data gate uses — see
    // HallsOfStoneEvents.cpp for why that gate is load-bearing for a reason
    // beyond completion.
    constexpr uint32 BOSS_KRYSTALLUS           = 0;
    constexpr uint32 BOSS_MAIDEN_OF_GRIEF      = 1;
    constexpr uint32 BOSS_TRIBUNAL_OF_AGES     = 2;
    constexpr uint32 BOSS_SJONNIR              = 3;
    constexpr uint32 BRANN_BRONZEBEARD         = 4;
    constexpr uint32 BRANN_DOOR                = 5;

    // DungeonEncounter bits (instance_encounters, both difficulties). Bit 2 is
    // creditType 1 (ENCOUNTER_CREDIT_CAST_SPELL) on spell 59046, which is exactly
    // why BossSpawnIndex cannot derive it and why this dungeon ends at 2/3 today.
    constexpr uint32 BIT_KRYSTALLUS            = 0;
    constexpr uint32 BIT_MAIDEN_OF_GRIEF       = 1;
    constexpr uint32 BIT_TRIBUNAL_OF_AGES      = 2;
    constexpr uint32 BIT_SJONNIR               = 3;

    // ObjectiveHookRegistry ids. Hook ids are one FLAT space across every
    // dungeon; 1-10 and 12-21 are taken, 11 is retired and stays retired.
    constexpr uint32 HOOK_TRIBUNAL             = 22;
    constexpr uint32 HOOK_WAVE                 = 23;

    // --- clear-path order keys --------------------------------------------
    // ONE contiguous 1..6 scale shared by the three derived bosses (reordered in
    // place, kill-bits untouched) and the three new objectives. The bosses'
    // RELATIVE order is exactly what their DBC bits already gave them; the scale
    // exists only so the objectives have somewhere to sit between the Maiden and
    // Sjonnir.
    constexpr int32 ORDER_KRYSTALLUS           = 1;
    constexpr int32 ORDER_MAIDEN               = 2;
    constexpr int32 ORDER_ESCORT               = 3;
    constexpr int32 ORDER_TRIBUNAL             = 4;
    constexpr int32 ORDER_DOOR                 = 5;
    constexpr int32 ORDER_SJONNIR              = 6;

    // --- geometry, every value column-probed ------------------------------

    // Objective 1's anchor. 6.8yd from Brann's DB spawn (1077.41, 474.16, 207.80,
    // guid 126801), which is inside the escort step's own 5yd gossip walk-in with
    // a tick of drift to spare, so simply ARRIVING all but closes the gap. Probed:
    // exactly ONE walkable surface in the column, z 208.07.
    constexpr float MEET_X = 1077.40f, MEET_Y = 481.00f, MEET_Z = 208.07f;

    // The escort pre-clear volume: the centroid of the FOUR constructs standing
    // in Brann's first forty yards, measured from the live spawn rows —
    // 27971 at (1032.59, 475.95) and (1049.09, 468.95), both within 8yd of
    // waypoint 2/3 of path 280701, plus 27971 (1036.02, 501.23) and 27970
    // (1058.29, 499.91) a further 25yd out. Radius 28 encloses all four with
    // margin and reaches nothing else. Probed: one surface, z 208.39.
    //
    // DELIBERATELY NOT the whole route. Three more Lightning Constructs sit at
    // the FAR end of the escort — (972.52, 420.20), (983.27, 390.11), (967.97,
    // 381.01) — and sweeping those would mean walking the entire 170yd corridor
    // ahead of Brann and then walking back for him. They are the escort step's
    // own threat-engage job (ESCORT_THREAT_R below), which is what that primitive
    // is for: the party is beside him by then.
    constexpr float PRECLEAR_X = 1044.00f, PRECLEAR_Y = 486.50f, PRECLEAR_Z = 208.39f;
    constexpr float PRECLEAR_R = 28.0f;

    // Where path 280701 ends and Brann re-offers gossip (menu 9670). Probed:
    // z 207.18 (plus the arena's phantom -142.03 beneath it).
    constexpr float ESCORT_END_X = 939.65f, ESCORT_END_Y = 375.49f, ESCORT_END_Z = 207.18f;

    // BRANN AT THE CONSOLE — the thing being defended for 300 seconds.
    // MovePoint target (897.1759, 331.77386, 203.70638); probed z 203.93.
    constexpr float CONSOLE_X = 897.18f, CONSOLE_Y = 331.77f, CONSOLE_Z = 203.93f;

    // THE HOLD POINT — objective 2's anchor and the party's station for the whole
    // Tribunal. Chosen from the probe, not estimated.
    //
    // It sits 25yd from Brann along the console -> add-spawn-centroid line. That
    // line is the one every add walks: all three summon groups land within a 34yd
    // cluster ((943.088, 401.378) / (967.000, 376.832) / (964.302, 381.942)) and
    // every add is Taunt-wired to Brann (51774 -> 51775), 83yd away. At 25yd out
    // the three approach lines have fanned to only ~10yd apart, so ONE camp
    // straddles all of them; at the 40yd the first sketch of this plan proposed
    // they are ~17yd apart and the party is covering a cone it cannot hold.
    //
    // Probed hard, because a hold point is where the party stands for five
    // minutes: the column gives z 203.93, and an 8yd ring around it is walkable
    // at every one of eight bearings (203.93 on the five toward Brann, 204.10 on
    // the three toward the ramp the adds descend). No lip, no step, no hole.
    //
    // It is also 3.7yd from Brann's post-fight lore stop, which is a real bonus
    // rather than a coincidence: both sit on the same line. The 10206 gossip that
    // skips the 256s of lore therefore needs no travel at all — the party is
    // already standing on it when he walks up.
    constexpr float HOLD_X = 915.75f, HOLD_Y = 348.51f, HOLD_Z = 203.93f;

    // Brann's post-fight lore stop, where he offers gossip 10206
    // (POINT_TRIBUNAL_LORE, MovePoint target (917.253, 351.925, 203.699)).
    constexpr float LORE_X = 917.25f, LORE_Y = 351.93f, LORE_Z = 203.93f;

    // The three add spawn points, verbatim from creature_summon_groups. Exposed
    // so the driver can hold the intercept line and the gtest can re-derive the
    // hold point's placement from them rather than trusting a literal.
    constexpr float SPAWN_PROTECTOR_X   = 943.088f, SPAWN_PROTECTOR_Y   = 401.378f;
    constexpr float SPAWN_STORMCALLER_X = 967.000f, SPAWN_STORMCALLER_Y = 376.832f;
    constexpr float SPAWN_GOLEM_X       = 964.302f, SPAWN_GOLEM_Y       = 381.942f;

    // Objective 3's anchor. 2.2yd from where Reset() teleports the respawned
    // Brann (1199.685, 667.155, 196.324) — comfortably inside INTERACTION_DISTANCE
    // — and 9.1yd WEST of the closed door, i.e. on the party's side of it. Probed:
    // top surface z 195.56 (with 162.29 and 0.16 beneath — see the header note).
    constexpr float DOOR_STAGE_X = 1197.50f, DOOR_STAGE_Y = 667.10f, DOOR_STAGE_Z = 195.56f;

    // --- the wave event's proximity gate ----------------------------------
    // Centroid of the Tribunal arena. The wave event is due only within
    // EVENT_DUE_RANGE of this, which is what keeps the driver from steering a bot
    // that cannot see the fight (a corpse run) — and, just as importantly, keeps
    // it not-due at Brann's DB spawn 183.5yd away, where the escort still owns
    // the party. The Sjonnir door is 403yd off, far outside.
    constexpr float ARENA_X = 930.0f, ARENA_Y = 365.0f;
    constexpr float EVENT_DUE_RANGE = 150.0f;

    // Grid-scan radius for the heads and the adds. From the hold point the
    // furthest thing worth finding is the Stormcaller spawn at 58.5yd; 120 covers
    // the whole bowl from anywhere in it without reaching out of the arena.
    constexpr float ARENA_SCAN = 120.0f;

    // --- escort tuning ----------------------------------------------------
    // Brann walks 170yd alone as REACT_AGGRESSIVE with regeneration off and no
    // immunity, and his death restarts the whole escort from his DB spawn. So the
    // threat scan around him is deliberately WIDER than the 18yd default: the
    // Lightning Construct at (967.97, 381.01) sits 22yd off his waypoint 14, and
    // meeting it before he does is the difference between a 2-minute escort and
    // two of them.
    constexpr float ESCORT_STANDOFF  = 5.0f;
    constexpr float ESCORT_THREAT_R  = 25.0f;
    constexpr float ESCORT_THREAT_Z  = 15.0f;
    constexpr float ESCORT_SEARCH_R  = 100.0f;

    // --- step budgets -----------------------------------------------------
    // The Tribunal is a FIXED 300s survival timer plus a 17s wrap-up; nothing the
    // party does shortens it. 10 minutes bounds a run that has gone wrong without
    // ever being the thing that ends a healthy one — the hook returns Done off
    // mask bit 2 the moment the credit spell lands.
    //
    // NOTE there is deliberately NO escort timeout below. The EscortCreature step
    // is watchdog-owned (DungeonEventExecutor never escalates it on elapsed time;
    // DriveEscortCreature's own dead-air watchdog owns liveness), so authoring one
    // would be a number that does nothing.
    constexpr uint32 PRECLEAR_TIMEOUT_MS   = 180000;
    constexpr uint32 TRIBUNAL_TIMEOUT_MS   = 600000;
    constexpr uint32 LORE_SKIP_TIMEOUT_MS  = 120000;
    constexpr uint32 DOOR_GOSSIP_TIMEOUT_MS = 120000;
    constexpr uint32 DOOR_STATE_TIMEOUT_MS = 60000;
    constexpr uint32 WAVE_TIMEOUT_MS       = 600000;
}

void RegisterHallsOfStoneEvents(std::vector<DungeonEvent>& out);

// --- Halls of Lightning (map 602) ------------------------------------------
//
// The numbers HallsOfLightningEvents.cpp authors, HallsOfLightningDriver.cpp
// steers by and t/TestHallsOfLightning.cpp pins. Shared here for the same reason
// DcBlackwingLair, DcVioletHold and DcHallsOfStone are: the transit's corridor
// box, its route slice and the point it ends at need exactly ONE definition, and
// the probe suite re-derives them against the real mesh.
//
// NO ROSTER PATCH, and that is worth writing down rather than leaving as an
// absence. All four of map 602's instance_encounters rows are creditType 0
// (ENCOUNTER_CREDIT_KILL_CREATURE) against real `creature` spawns, and
// DungeonEncounter.dbc's order (Bjarngrim 0, Volkhan 1000, Ionar 2000, Loken
// 3000) IS the walking order — so BossSpawnIndex::Build derives the whole list
// with correct coordinates on both difficulties. This is the first WotLK map in
// the module where that is true; the previous four (Halls of Stone, Drak'Tharon,
// Gundrak, The Nexus) each needed a patch to be runnable at all. Do not add one
// here: the transit below is a CONDITIONAL event, which needs no roster row, no
// order keys and no OBJ() anchor, and a patch could only make things worse.
//
// DIFFICULTY TWINS ARE STAT TEMPLATES, NOT ENTRIES. Almost every creature on
// this map has one (Slag 28585 -> 30970, Volkhan 28587 -> 31536, ...), and
// Creature::InitEntry resolves DifficultyEntry[diff-1] into m_creatureInfo and
// then does SetEntry(Entry) — "normal entry always" — so GetEntry() returns the
// NORMAL id on heroic. Every entry-keyed constant below is the normal id and is
// correct on both difficulties. Do not add the 30xxx/31xxx ids anywhere.
//
// EVERY COORDINATE HERE IS COLUMN-PROBED against the live 602 mmtiles
// (t/TestHallsOfLightningRouteProbe), and on this map that is not ceremony. The
// mmtiles carry flat sheets far below the dungeon — a continuous surface at
// z ~ -1.9 and another at z ~ -13.9 under essentially the whole footprint — so
// map 602 is in the flat-grid-height family ([[ac-map601-flat-gridheight-zero]])
// and an anchor authored from a script literal rather than from the probed TOP
// surface is a sink waiting to happen. The open centre of Bjarngrim's ring
// (1330, 30) has NO dungeon floor at all: probing it returns 143.02, -1.88,
// -13.88, so a hold point or camp placed there drops a bot 45-55yd.
namespace DcHallsOfLightning
{
    constexpr uint32 MAP_ID = 602;

    // The four bosses, in DungeonEncounter.dbc order — which is also the order
    // the party walks them.
    constexpr uint32 NPC_BJARNGRIM = 28586;
    constexpr uint32 NPC_VOLKHAN   = 28587;
    constexpr uint32 NPC_IONAR     = 28546;
    constexpr uint32 NPC_LOKEN     = 28923;

    // Volkhan's slot in instance_halls_of_lightning's OWN HoLBossIds enum, which
    // is what GetBossState is keyed on. It is THREE, not one: the header's order
    // is Bjarngrim 0, Ionar 1, Loken 2, Volkhan 3, and that is NOT the DBC bit
    // order. Read from halls_of_lightning.h; never reuse an encounterIndex here.
    constexpr uint32 VOLKHAN_ENCOUNTER_INDEX = 3;

    // --- the Slag Furnace (the Bjarngrim -> Volkhan transit) ---------------
    //
    // The 395yd of pit between Bjarngrim's hall and Volkhan's gallery, and the
    // one leg on this map the ordinary clear cannot cross. Three facts, and
    // between them they are the whole design:
    //
    //   1. THE PIT IS THE ONLY WAY THROUGH. An A* over the map-602 mmtiles with
    //      every poly below z 40 removed returns NO PATH from Bjarngrim's floor
    //      to Volkhan. The party must descend 17yd from the hall's south arm
    //      (z 40.7 -> z 23.9), walk ~110yd south along the pit floor, climb the
    //      far wall to the mid ledge (z 38.6) and climb again to the gallery
    //      (z 52.7) — 395yd of route for 80yd of straight line. There is no
    //      upper gallery route.
    //   2. FOURTEEN SLAG (28585) LINE THE WALKWAY ON A TWENTY-SECOND RESPAWN
    //      (`creature.spawntimesecs = 20`, MovementType 1, wander_distance 8 —
    //      the shortest respawn on any map this module runs). Twelve of the
    //      fourteen are within 20yd of the route line in 2D; with the wander all
    //      fourteen reach it, and level 79 against level 80 is inside base aggro
    //      range for the whole walk.
    //   3. SO THE CLEAR HAS NO DRIVER IN THERE. DcCombatFlag::MayDrive is false
    //      while anything in the party is engaged and Advance is registered only
    //      in the NON-combat engine, so party engagement never drops for the
    //      no-engage grace window. The leg is not slow; it is stopped. This is
    //      Blackwing Lair's Suppression Rooms one tier down.
    //
    // AND KILLING THEM IS NEGATIVE PROGRESS, which is why the answer is a
    // transit and not a clear. Each Slag is 15 828 HP with DamageModifier 1 —
    // trivial, harmless in melee — but smart_scripts 28585 id 1/3 is
    // `On Just Died -> Blast Wave` (23113 normal / 22424 heroic): 10yd, and
    // SPELL_AURA_MOD_DECREASE_SPEED -50% for 6s. Fourteen deaths inside a 110yd
    // crossing is a party that spends most of the crossing at half speed.
    //
    // HONEST NOTE ON WHAT THIS IS *NOT*. Unlike Blackwing Lair's whelps this
    // does NOT meet DcNeverTargetRegistry's class-3 arithmetic bar — 0.7
    // spawns/s against fourteen low-HP mobs is a population a high-DPS party
    // genuinely can clear, where BWL's 5.3/s across 375yd is not. The problem
    // here is the SNARE and the MISSING DRIVER, and the fix addresses those. No
    // never-target rows are authored for 28585; if the transit alone turns out
    // not to cross the pit, the row wants a new justification written into that
    // table's doc comment, not an existing class stretched to fit.

    constexpr uint32 NPC_SLAG = 28585;

    // The two things in the pit that ARE worth standing and killing: Unbound
    // Firestorm (28584), 3600s respawn, two of them at (1300.0, -214.8, 23.3)
    // and (1362.2, -215.2, 23.3) — at the foot of the climb out — plus the
    // Blistering Steamragers (28583) that share their 3600s timer on the mid
    // ledge above. A kill here is progress that stays bought; a Slag kill is
    // not. This is BWL's Taskmaster/Hatcher role, and the driver's Elite hold
    // is keyed on exactly these two entries.
    constexpr uint32 NPC_UNBOUND_FIRESTORM       = 28584;
    constexpr uint32 NPC_BLISTERING_STEAMRAGER   = 28583;

    // WHERE THE TRANSIT'S SLICE OF THE ROUTE STARTS AND ENDS.
    //
    // The registry row for Volkhan is the WHOLE Bjarngrim -> Volkhan leg
    // (29 anchors; see RegisterHallsOfLightningRoute), because the ordinary
    // clear needs a polyline to walk and a cursor to project onto for the halves
    // the transit does not own. The driver is handed the middle of it — anchors
    // TRANSIT_STAGE_ANCHOR_INDEX .. TRANSIT_END_ANCHOR_INDEX — so that its
    // cursor 0 IS the staging point and its LAST anchor IS the point the
    // crossing ends at, which is what the kernel's gather gate (cursorIndex 0)
    // and completion test (the last anchor) both mean.
    //
    // A MIDDLE slice, where Blackwing Lair takes a tail one. The difference is
    // real geometry, not taste: BWL's crossing ends AT the next boss, so its
    // route simply stops there; here the party still has a hairpin and a second
    // ramp to climb after the pit, and those are ordinary ground the clear
    // walks — and must NOT be steered by a driver whose route ends behind them.
    constexpr std::size_t TRANSIT_STAGE_ANCHOR_INDEX = 5;
    constexpr std::size_t TRANSIT_END_ANCHOR_INDEX   = 18;

    // THE STAGING POINT — the top of the descent, and the last quiet ground
    // before the gauntlet. Routed and column-probed: (1323.21, -46.56, 40.66) is
    // the corridor point at which the hall's south arm stops being flat and
    // starts dropping. Nearest Slag 78yd; nothing in the pit reaches it.
    //
    // The ordinary clear delivers the party here by itself (it is out of combat
    // for the whole hall once Bjarngrim is down), so the transit's first act is
    // a gather, not a haul.
    constexpr float TRANSIT_STAGE_X = 1323.21f;
    constexpr float TRANSIT_STAGE_Y = -46.56f;
    constexpr float TRANSIT_STAGE_Z = 40.66f;

    // THE MID LEDGE — where the crossing ends and the ordinary pipeline (pull,
    // muster, standoff, engage) takes back over. The top of the climb out of the
    // pit, z 38.55, with the four z38 trash spawns 18-30yd west and east of it.
    //
    // Those spawns are the REASON the transit stops here rather than running on
    // to Volkhan: they are 3600s respawns on open ground, which is a fight worth
    // setting up for, and NO_STOP is released at the same anchor.
    constexpr float TRANSIT_END_X = 1340.53f;
    constexpr float TRANSIT_END_Y = -233.26f;
    constexpr float TRANSIT_END_Z = 38.55f;

    // How close the leader has to get before the crossing is over. Also the
    // predicate's own OFF switch: the event is due while the leader is inside the
    // pit box and NOT yet here, so arriving simply stops it being due. There is
    // no completion latch to reset — a party shoved back into the pit re-arms it,
    // which is the correct answer, and a wipe leaves no stale flag behind.
    constexpr float TRANSIT_END_RADIUS = 10.0f;

    // --- THE CORRIDOR, and why it is TWO boxes ------------------------------
    //
    // The transit's activation predicate is gated on the leader standing inside
    // this volume, which is what keeps a rung registered on every bot's combat
    // engine inert for the other three encounters of this dungeon.
    //
    // ONE box cannot draw it, and the reason is a switchback. The climb OUT of
    // the pit runs north-to-south at x 1340.5 from z 23.9 up to z 38.6; the climb
    // on to Volkhan's gallery runs back south-to-north at x 1348-1350 from z 38.6
    // up to z 52.7. Two ramps, 7.5yd apart in x, overlapping in y and in z for
    // their whole lower halves. A single box wide enough to hold the pit also
    // holds the gallery ramp, and a leader standing on THAT would have the
    // transit re-arm behind it: ResolveCursor would project it back on to the pit
    // ramp and the driver would walk the party back down the hole.
    //
    // So: box 1 is the descent and the pit floor, cut off at y -205 — north of
    // the switchback entirely. Box 2 is the climb out, narrow in x (1330..1346)
    // so it holds the pit ramp and excludes the gallery ramp (whose nearest
    // point is x 1348.03). Both share a z ceiling of 44, which is what excludes
    // the gallery itself (floor z 52.7) and Volkhan (z 57.0) from either.
    //
    // Certified against the authored anchors in t/TestHallsOfLightning: anchors
    // 5..18 are inside, 0..4 and 19..28 are outside, and so are Bjarngrim, the
    // entrance, Volkhan, Ionar, Loken and the Hall of the Watchers.
    constexpr float PIT_BOX_MIN_X = 1298.0f;
    constexpr float PIT_BOX_MAX_X = 1358.0f;
    constexpr float PIT_BOX_MIN_Y = -205.0f;
    constexpr float PIT_BOX_MAX_Y = -44.0f;
    constexpr float PIT_BOX_MIN_Z = 18.0f;
    constexpr float PIT_BOX_MAX_Z = 44.0f;

    constexpr float CLIMB_BOX_MIN_X = 1330.0f;
    constexpr float CLIMB_BOX_MAX_X = 1346.0f;
    constexpr float CLIMB_BOX_MIN_Y = -240.0f;
    constexpr float CLIMB_BOX_MAX_Y = -205.0f;
    constexpr float CLIMB_BOX_MIN_Z = 18.0f;
    constexpr float CLIMB_BOX_MAX_Z = 44.0f;

    // Is `bot` standing inside the Slag Furnace corridor right now? Two bbox
    // tests, and the transit's cheapest real gate after the map compare.
    // Defined in HallsOfLightningEvents.cpp.
    bool InTransitCorridor(Player* bot);

    // --- the driver's knobs -------------------------------------------------
    //
    // CONSTANTS, NOT SETTINGS. Blackwing Lair's equivalents are DcSettings rows
    // because that transit was retrofitted onto a raid mid-testing and the raid
    // scale genuinely varies; nothing here does. A five-man's pit crossing has
    // one right answer for each of these and a conf row nobody will ever turn is
    // cost pushed onto every future reader of the config.

    // The gather radius, and the pack leash — how tight a ball the party enters
    // the gauntlet as, and how far off the leader's route cursor a member may be
    // before the leg stops for it.
    //
    // Both are 5-man numbers and both are tighter than Blackwing Lair's 20/25.
    // The argument for the transit is that a wide ball sweeps more of a pit whose
    // spawns are the problem, and four followers behind one tank have no
    // legitimate reason to be strung over 25yd on a walkway 41yd wide at its
    // narrowest. TRANSIT_PACK_LEASH must stay comfortably above the gather
    // radius' own floor — see DcSuppressionTransit::GatherRadiusFloor, which the
    // driver applies for exactly the [[dc-party-gate-must-read-the-rungs-ring]]
    // reason.
    constexpr float TRANSIT_GATHER_RADIUS = 15.0f;
    constexpr float TRANSIT_PACK_LEASH    = 20.0f;

    // The fraction of living followers that must be inside the leash. 0.75 is
    // three of four — one bot that cannot path in must never hold the other
    // three at the top of a ramp, and stranded recovery (relevance 42) sits above
    // this whole ladder and owns that member.
    constexpr float TRANSIT_GATHER_QUORUM = 0.75f;

    // How close a Firestorm / Steamrager has to be before the driver stands and
    // fights it instead of walking past. 20yd is a little over the aggro band of
    // the level-79/80 elites on this leg, so the hold arms as the party pulls
    // them rather than after they are already in the middle of the column.
    //
    // SLAGS ARE DELIBERATELY NOT A HOLD. The tank face-pulls what walks into it
    // and keeps moving; the party fights on the move. That is the whole point of
    // NO_STOP plus OwnsThePull, and it is the difference between a transit and a
    // clear.
    constexpr float TRANSIT_ELITE_HOLD_RADIUS = 20.0f;

    // Grid-scan radius for the driver's per-tick elite sweep. Wide enough to see
    // a hold-worthy spawn from the middle of a leg (the widest hold radius is
    // 20yd and the longest authored leg is 16), tight enough that it never
    // reaches from the pit floor up on to the gallery.
    constexpr float TRANSIT_SCAN = 40.0f;

    // How far the leader may be from the staging point when the transit ARMS and
    // still be asked to gather there. Past this the gather latches open and says
    // so: you cannot gather at a point you have already walked past, and a driver
    // that insisted would march a leader standing in the pit 200yd back up the
    // ramp to form up. The case is a re-arm after a partial wipe, which is
    // exactly when walking backwards is worst.
    constexpr float TRANSIT_STAGE_SKIP_DIST = 40.0f;

    // How far the leader may be from its own stored cursor before the projection
    // is believed over it (DcSuppressionTransit::ResolveCursor). Inside a working
    // crossing nothing is ever this far from the anchor it is walking to; a
    // leader that is has died and come back, and the stored index is a fact about
    // a previous attempt.
    constexpr float TRANSIT_CURSOR_RESYNC_DIST = 50.0f;

    // The three hold watchdogs. None of them is a target: each bounds ONE wait,
    // and on expiry the driver walks on and logs which hold gave up. The elite
    // budget is a quarter of Blackwing Lair's because the fights are — two
    // Firestorms, not six Taskmasters — and the gather budget is halved for the
    // same reason (four followers, not twenty-four, and no whelps at the staging
    // point to keep them in combat).
    constexpr uint32 TRANSIT_GATHER_TIMEOUT_MS = 30000;
    constexpr uint32 TRANSIT_PACK_HOLD_TIMEOUT_MS  = 20000;
    constexpr uint32 TRANSIT_ELITE_HOLD_TIMEOUT_MS = 30000;

    // Throttle on the driver's per-tick telemetry line. Without the line a failed
    // run says nothing about WHICH mechanism is still biting; without the
    // throttle it says it several times a second, per bot.
    constexpr uint32 TRANSIT_TELEMETRY_MS = 3000;

    // Bound on the whole crossing, as the Custom step's timeout. The walk is
    // 395yd of route; at a snared 3.5yd/s that is under two minutes, and the two
    // Firestorm fights are seconds. Five minutes is a ceiling on a genuinely
    // broken run and never the binding constraint on a working one — the event is
    // Repeatable and every yielded tick re-bases the step clock, so this can only
    // fire when the driver has held continuously for the whole budget, i.e. when
    // a hold's own watchdog has failed to release.
    constexpr uint32 TRANSIT_TIMEOUT_MS = 300000;

    // The event row and the hook that drives it. Event ids are per-map; HOOK ids
    // are ONE FLAT SPACE across every dungeon (see ObjectiveHookRegistry::AddHook,
    // which LOG_ERRORs a collision rather than silently dropping one) — 1-14 are
    // the older dungeons', 15-19 the Violet Hold's, 20-21 Blackwing Lair's and
    // 22-23 Halls of Stone's, so this is 24.
    constexpr uint32 EVENT_SLAG_FURNACE_TRANSIT = 1;
    constexpr uint32 HOOK_SLAG_FURNACE_TRANSIT  = 24;
}

void RegisterHallsOfLightningEvents(std::vector<DungeonEvent>& out);

// --- Utgarde Pinnacle (map 575) -------------------------------------------
//
// The numbers UtgardePinnacleEvents.cpp authors, UtgardePinnacleDriver.cpp
// drives on and t/TestUtgardePinnacle.cpp pins. Shared here for the reason
// DcHallsOfLightning and DcVioletHold are: several are SAFETY numbers whose
// whole value is that a test can re-derive them from live data, and a literal
// buried in a .cpp cannot be pinned.
//
// EVERY COORDINATE BELOW IS ROUTED OR COLUMN-PROBED against the live map-575
// mmtiles (t/TestUtgardePinnacleRouteProbe). Map 575 is NOT in the
// flat-grid-height family ([[ac-map601-flat-gridheight-zero]]) — every fixed
// point snaps with d2d 0.00 and dz between +0.0 and +0.5, and the only larger
// deltas are the two portcullis ORIGINS (+3.92, +1.83), which is ordinary: a
// door origin sits below its own threshold.
//
// THE ONE THING TO UNDERSTAND ABOUT THIS DUNGEON. It does not have a door
// problem; it has a ONE-ROW ROSTER problem, and the door stall is the
// second-order symptom. instance_encounters credits Svala at entry 26668, which
// has NO SPAWN ROW ANYWHERE IN THE DB — boss_svala.cpp does
// me->UpdateEntry(NPC_SVALA_SORROWGRAVE) ~34s into the intro cinematic, so the
// entry only exists at runtime. BossSpawnIndex::Build joins creditEntry ->
// creature spawn row, matches nothing, and drops the first boss SILENTLY. The
// derived roster is 3 bosses for a 4-boss dungeon.
//
// That single missing row is what walked tr-20260902-083808-1 into two shut
// portcullises: with Svala gone the clear's first target is Palehoof, and the
// navmesh — baked DOOR-BLIND — offers a 696yd shortcut to him that threads BOTH
// gates. The designed first leg to Svala is 362 + 101yd and threads NEITHER.
// See [[dc-credit-entry-is-an-updateentry-target]] and the roster patch in
// UtgardePinnacleEvents.cpp.
//
// DIFFICULTY TWINS ARE STAT TEMPLATES, NOT ENTRIES, exactly as on map 602:
// Creature::InitEntry resolves DifficultyEntry[diff-1] into m_creatureInfo and
// then SetEntry(Entry), so GetEntry() returns the NORMAL id on heroic. Every
// entry-keyed constant here is the normal id and is correct on both
// difficulties. Do not add the 307xx/308xx heroic clones anywhere.
//
// AND SVALA IS DBC BIT 0 ON BOTH DIFFICULTIES (rows 577/578), unlike Gundrak
// and the Nexus — so ONE Any-gated roster patch serves normal and heroic, and
// the DBC order (Svala 0, Palehoof 1000, Skadi 2000, Ymiron 3000) IS the travel
// order for the three bosses that already derive.
namespace DcUtgardePinnacle
{
    constexpr uint32 MAP_ID = 575;

    // --- the four encounters, in travel order ------------------------------
    //
    // NPC_SVALA is the CREDIT entry (26668) and the one to anchor, target and
    // patch on: KillRewarder reads _victim->GetEntry() at DEATH time, by which
    // point the transform has long since happened, so the DBC bit-0 credit fires
    // normally. NPC_SVALA_INTRO (29281) is the entry that actually SPAWNS, and
    // the only thing it is good for is reading her home position out of the
    // `creature` table — never target it, and never wait on it.
    constexpr uint32 NPC_SVALA        = 26668;
    constexpr uint32 NPC_SVALA_INTRO  = 29281;
    constexpr uint32 NPC_PALEHOOF     = 26687;
    constexpr uint32 NPC_SKADI        = 26693;
    constexpr uint32 NPC_GRAUF        = 26893;
    constexpr uint32 NPC_YMIRON       = 26861;

    // DungeonEncounter.dbc bits. Svala's is the one that matters — it is the
    // parameter MakeBossWithBit needs, and it is 0 on BOTH difficulties.
    constexpr uint32 BIT_SVALA    = 0;
    constexpr uint32 BIT_PALEHOOF = 1;
    constexpr uint32 BIT_SKADI    = 2;
    constexpr uint32 BIT_YMIRON   = 3;

    // instance_utgarde_pinnacle's OWN `enum Data` slots — the keys GetData is
    // switched on. Here they happen to coincide with the DBC bit order, which is
    // a coincidence and not a rule (Halls of Lightning's do not); read from
    // utgarde_pinnacle.h, never reuse an encounterIndex.
    //
    // GetData(DATA_*) IS THE ONLY SAFE INSTANCE PROBE ON THIS MAP.
    // GetGuidData(DATA_SVALA_SORROWGRAVE) reads a field OnCreatureCreate fills
    // from a switch on entry 26668 — which never spawns — so it returns
    // ObjectGuid::Empty FOREVER. Never build a predicate on it. Skadi has no
    // GetGuidData case at all and is reachable only through the ObjectData store
    // (GetCreature(DATA_SKADI)); Grauf has both.
    constexpr uint32 DATA_SVALA    = 0;
    constexpr uint32 DATA_PALEHOOF = 1;
    constexpr uint32 DATA_SKADI    = 2;
    constexpr uint32 DATA_YMIRON   = 3;
    constexpr uint32 DATA_GRAUF    = 4;

    // --- the clear order ---------------------------------------------------
    //
    // ONE contiguous 1..7 scale so the three objectives have integer slots
    // between the four bosses:
    //
    //     1  objective  OBJ(1)   Wake Svala (areatrigger 5140)  -> event 1
    //     2  boss       26668    Svala Sorrowgrave                       [patched in]
    //     3  objective  OBJ(2)   Stasis Generator               -> event 2
    //     4  boss       26687    Gortok Palehoof
    //     5  objective  OBJ(3)   Enter Skadi's gauntlet         -> event 3
    //     6  boss       26693    Skadi the Ruthless              (event 4 is Conditional)
    //     7  boss       26861    King Ymiron
    //
    // AND THIS ORDER IS ENFORCED, NOT PREFERRED. boss_ymironAI::Reset() ADDS
    // UNIT_FLAG_NOT_SELECTABLE and only the instance's
    // SetData(DATA_SKADI, DONE) removes it, so Ymiron cannot be attacked out of
    // turn — a wrong-order roster fails loudly at him rather than silently. It
    // also means a `skipByDesign` on Skadi would yield a 2/4 clear, not 3/4:
    // the harpoon driver (event 4) is load-bearing for HALF this dungeon.
    constexpr int32 ORDER_WAKE_SVALA      = 1;
    constexpr int32 ORDER_SVALA           = 2;
    constexpr int32 ORDER_STASIS          = 3;
    constexpr int32 ORDER_PALEHOOF        = 4;
    constexpr int32 ORDER_ENTER_GAUNTLET  = 5;
    constexpr int32 ORDER_SKADI           = 6;
    constexpr int32 ORDER_YMIRON          = 7;

    // --- boss anchors ------------------------------------------------------
    //
    // SVALA IS ANCHORED ON THE PLATFORM FLOOR, and this is the one anchor on the
    // map worth arguing about. THREE positions are in play during her encounter:
    // the `creature` spawn / platform floor (probe-snapped, below); the FIGHT
    // position, ~6yd up, because the intro gives her SetDisableGravity(true) +
    // UNIT_FIELD_HOVERHEIGHT 6 + SetHover(true); and the Ritual of the Sword
    // position, NearTeleportTo(296.632, -346.075, 110.0) — TWENTY YARDS UP —
    // held rooted for 25s.
    //
    // NavmeshSnap's vertical extent is a FIXED 10 regardless of the snap radius
    // ([[dc-boss-anchor-snap-vertical-extent]]), so an anchor authored at z 110
    // would not snap at ALL and the roster row would be dropped at load. Author
    // the floor and let DcTargeting::GetLiveBoss follow her real position.
    constexpr float SVALA_X = 296.60f;
    constexpr float SVALA_Y = -346.10f;
    constexpr float SVALA_Z = 90.83f;

    // --- objective anchors -------------------------------------------------
    //
    // AREATRIGGER 5140 — the ONLY entry point to Svala's encounter.
    // smart_scripts source_type 2 entry 5140: SMART_EVENT_AREATRIGGER_ONTRIGGER
    // -> SET_DATA 1,1 on creature GUID 126115, which is the gate at
    // boss_svala.cpp's Started check. The box is axis-aligned (orientation 0):
    // x [293.99, 331.29], y [-298.07, -284.28], z [87.09, 122.32], so from the
    // dead-centre anchor the HALF-extents are 18.65 / 6.90 / 17.62.
    //
    // 6.90 IS THE ONLY ONE OF THOSE THREE NUMBERS THAT MATTERS, and reading the
    // 13.79yd full width as "~15yd of margin" is what cost tp-20260902-121652-1
    // three runs. Nothing server-side notices a unit entering an areatrigger:
    // both ways this trigger can be fired for a bot — hook 25's forge and the
    // harness relay — hand a CMSG_AREATRIGGER to WorldSession::HandleAreaTrigger-
    // Opcode, which re-tests Player::IsInAreaTriggerRadius(atEntry, 0.f). For a
    // box trigger that is IsWithinBox against the half-extents with ZERO delta.
    // So a bot that is merely NEAR the anchor cannot start this encounter; it has
    // to be physically inside the box.
    //
    // WHICH MAKES THE ARRIVE RADIUS A CONTAINMENT RADIUS, not a tolerance. Both
    // the objective's arrival trigger (DungeonClearTriggers.cpp) and the event's
    // leading MoveTo step (DungeonEventExecutor::RunStep) latch on
    // GetExactDist(anchor) <= arriveRadius and then STOP the tank. An arrive
    // radius larger than the smallest half-extent therefore describes a shell
    // that pokes out through the short faces: at 8.0 the shell reached 1.1yd past
    // the north face at y -284.28, and Leg A's last hop comes in from the north.
    // 17 of 20 tanks coasted one movement sub-tick further and stopped at
    // y -284.3, a few centimetres INSIDE; 3 stopped at y -283.5..-283.6, outside,
    // and their runs died there with `areatrigger relay: 0 packet(s)`, the Custom
    // step timing out forever and 0/7 bosses.
    //
    // So: arriveRadius < min(half-extent). 5.0 leaves 1.9yd of margin on the
    // short axis, is inside the box on EVERY axis for any approach direction, and
    // matches STASIS_ARRIVE. TestUtgardePinnacle pins the invariant for both of
    // this map's areatrigger objectives against the DBC boxes below.
    //
    // The test harness relays this trigger for a bot party
    // (DcTestTestAreaTriggers::Arm picks it up: it carries an areatrigger_scripts
    // row and NO areatrigger_teleport row), which is why event 1 works today.
    // Hook 25 forges the packet anyway so the dungeon also works for an ordinary
    // non-test bot party, and as belt-and-braces if the relay's gate ever fails.
    constexpr uint32 AREATRIGGER_SVALA = 5140;
    constexpr float AT_SVALA_X = 312.65f;
    constexpr float AT_SVALA_Y = -291.17f;
    constexpr float AT_SVALA_Z = 104.90f;
    constexpr float AT_SVALA_ARRIVE = 5.0f;

    // AreaTrigger.dbc box half-extents for 5140, verbatim from the row
    // (box_length 37.290001 / box_width 13.790000 / box_height 35.230000, yaw 0),
    // halved. Here so the arrive radius is checked against the real volume rather
    // than against a number remembered from a comment.
    constexpr float AT_SVALA_HALF_L = 18.645f;  // x
    constexpr float AT_SVALA_HALF_W =  6.895f;  // y — the binding one
    constexpr float AT_SVALA_HALF_H = 17.615f;  // z

    // GO 188593 STASIS GENERATOR — the only way to start Gortok Palehoof, and
    // it stands EIGHTY-THREE YARDS WEST OF HIM at the far end of his arena. The
    // party walks PAST the frozen boss and all four frozen animals to reach it,
    // clicks, and fights back east. Leg C ends here and Leg D returns; that
    // ordering is deliberate and must not be "optimised" into arriving at the
    // boss first.
    //
    // A PLAIN UseGO WORKS. GameObject::Use() calls sScriptMgr->OnGossipHello
    // BEFORE the type switch and returns early when it returns true, so
    // go_palehoof_sphere's handler — which sets GO_FLAG_NOT_SELECTABLE,
    // SetGoState(GO_STATE_ACTIVE) and DoAction(ACTION_START_EVENT) — IS the whole
    // mechanism; the type-18 summoning-ritual machinery never runs. The GO spawns
    // state 1 (READY) and selectable, which is what the executor's idempotence
    // check and its NOT_SELECTABLE guard both need.
    constexpr uint32 GO_STASIS_GENERATOR = 188593;
    constexpr float STASIS_X = 238.52f;
    constexpr float STASIS_Y = -460.83f;
    constexpr float STASIS_Z = 106.00f;
    constexpr float STASIS_ARRIVE = 5.0f;
    // Click reach for the UseGO step. The GO's own z is 105.48 and the probed
    // floor beside it 106.00, so the search only has to cover the anchor's own
    // arrival slop plus the wolf patrol shoving the tank a few yards off it.
    constexpr float STASIS_SEARCH = 12.0f;

    // AREATRIGGER 4991 — Skadi's gauntlet, and a ONE-WAY DOOR. Tripping it mounts
    // Skadi on Grauf, spawns a 13-mob first wave and arms a World Trigger
    // carrying 59275 Summon Gauntlet Mobs Periodic — a deque of 8 summon spells,
    // 2 per tick, WITH NO END CONDITION — plus a 38667 Combat Trigger that
    // DoZoneInCombat()s the whole hall. Nothing turns that off but Grauf's death.
    //
    // Unlike Svala's, this trigger has an AGGRO FALLBACK
    // (boss_skadiAI::JustEngagedWith also funnels into ACTION_START_ENCOUNTER
    // behind the same _encounterStarted guard) — but the box is large,
    // x [315.09, 346.71] and y [-519.35, -497.51], so simply walking onto the
    // platform starts it. That is the risk to design around, not the fallback:
    // event 3 is what makes entering it a DELIBERATE act taken with the party
    // regrouped, and event 4 owns everything after.
    // 8.0 STAYS HERE, and the contrast with AT_SVALA_ARRIVE is the point: this
    // box's half-extents are 15.81 / 10.92 / 15.00, so an 8.0 shell is inside it
    // on every axis with 2.9yd to spare on the short one. Same invariant, a box
    // wide enough to satisfy it. Do not "make them match" in either direction
    // without re-reading the DBC row.
    constexpr uint32 AREATRIGGER_SKADI = 4991;
    constexpr float AT_SKADI_X = 330.90f;
    constexpr float AT_SKADI_Y = -508.43f;
    constexpr float AT_SKADI_Z = 104.66f;
    constexpr float AT_SKADI_ARRIVE = 8.0f;

    // As above, from AreaTrigger.dbc row 4991 (31.620001 / 21.840000 / 30.0).
    constexpr float AT_SKADI_HALF_L = 15.810f;  // x
    constexpr float AT_SKADI_HALF_W = 10.920f;  // y
    constexpr float AT_SKADI_HALF_H = 15.000f;  // z

    // --- the harpoon: the one genuinely new mechanism on this map ----------
    //
    // PHASE 1 CANNOT BE FOUGHT. Grauf's template carries unit_flags 320 =
    // IMMUNE_TO_PC | 0x40 and boss_skadi_graufAI::Reset() never clears it, so
    // Unit::_IsValidAttackTarget refuses every player and bot for the whole
    // phase; Skadi herself is NOT_SELECTABLE from ACTION_START_ENCOUNTER until he
    // dies. There is NO damage the party can do in phase 1 at all.
    //
    // THE ONLY DAMAGE SOURCE IS A GAMEOBJECT CLICK, and the chain is short:
    //
    //   1. a player Use()s a Harpoon Launcher (192175/6/7, type 10 GOOBER,
    //      Data10 = 48641, autoclose 1000ms). The GOOBER branch sets
    //      spellCaster = user, spellId = 48641;
    //   2. 48641 "Launch Harpoon Trigger" is SPELL_EFFECT_FORCE_CAST with
    //      implicit target 38 (nearby entry), conditions-restricted to creature
    //      19871 "World Trigger (Not Immune NPC)". Three of those sit AT the
    //      launchers, already oriented at the breach;
    //   3. the TRIGGER — a creature, not a player, which is exactly how this
    //      bypasses Grauf's IMMUNE_TO_PC — casts 48642 "Launch Harpoon", a 60yd
    //      TARGET_UNIT_CONE_ENTRY restricted to Grauf (26893) or the breach's own
    //      World Trigger (22515), whose script sets the damage to
    //      CountPctFromMaxHealth(35).
    //
    // 3 x 35% = 105%. THREE LAUNCHER USES KILL THE DRAKE, and nothing else can.
    //
    // AND THE KEY ITEM IS NOT REQUIRED. Lock 1777 is LOCK_KEY_ITEM on item 37372
    // "Harpoon", but AzerothCore does not enforce LOCK_KEY_ITEM server-side for
    // GOOBER use: HandleGameObjectUseOpcode checks only distance and mover state,
    // and GameObject::Use's GOOBER branch never consults the lock. Only the real
    // client greys the launcher out. The clear does not farm harpoons.
    constexpr uint32 GO_HARPOON_LAUNCHER = 192175;

    // THE HARPOON POCKET — where the driver parks the party for the whole of
    // phase 1, and it is the last point of the routed AT 4991 -> launcher
    // corridor rather than a spot chosen by eye.
    //
    // Three constraints meet here and only here:
    //
    //   * INTERACT RANGE of launcher 192175 at (491.49, -508.19, 105.88). The
    //     bot has to be able to press it, three times, across several laps.
    //   * THE RESET RULE (below): a living member within 40yd of the Flame Breath
    //     Trigger carpet. The easternmost 28351 stands at (483.22, -507.15), 8.4yd
    //     from here.
    //   * y > -511, the north side of the breath divider.
    //
    // That last one is worth stating precisely, because it is half a defence and
    // not a whole one. spell_freezing_cloud_area_left strips targets with
    // y < -511 and _right strips y > -511, and the side Grauf breathes down is
    // RAND(POINT_LEFT, POINT_RIGHT) per lap — so there is no permanently safe
    // side of this hall, only a side that is safe from the RIGHT breath. The
    // pocket takes it because the launcher is there anyway; surviving the LEFT
    // breath is the stock combat engine's business.
    constexpr float POCKET_X = 491.50f;
    constexpr float POCKET_Y = -508.20f;
    constexpr float POCKET_Z = 107.07f;

    // How close the driver keeps the leader to the pocket. Comfortably inside
    // DC_EVENT_GO_USE_RANGE of the launcher and wide enough that a bot shoved a
    // couple of yards by an add is not re-splined every tick.
    constexpr float POCKET_LEASH = 3.0f;

    // GO search radius for the launcher click, from inside the pocket.
    constexpr float HARPOON_SEARCH = 20.0f;

    // WHERE GRAUF HAS TO BE FOR A SHOT TO LAND. He announces his arrival with
    // Talk(EMOTE_ON_RANGE) and then hovers at the breach for TEN SECONDS
    // (EVENT_GRAUF_LEAVE_BREACH) before flying his next lap, and that window
    // repeats every lap. Position is the more robust signal than the emote, and
    // there are TWO hold points because the first hover is the end of PATH_INITIAL
    // (2689300) and every later one the end of PATH_LEFT / PATH_RIGHT
    // (2689302 / 2689301):
    //
    //     BREACH_A (523.20, -548.99, 114.87)   PATH_INITIAL's last waypoint
    //     BREACH_B (520.48, -541.56, 119.84)   PATH_LEFT / PATH_RIGHT's last
    //
    // Both are ~46-52yd from launcher 192175, inside the cone's 60yd radius, and
    // both bear within 15 degrees of the trigger's own facing (5.655 rad) — so
    // ONE launcher serves every lap and there is never a reason to walk between
    // the three. The 1000ms autoclose is what makes re-use possible.
    constexpr float BREACH_A_X = 523.20f;
    constexpr float BREACH_A_Y = -548.99f;
    constexpr float BREACH_A_Z = 114.87f;
    constexpr float BREACH_B_X = 520.48f;
    constexpr float BREACH_B_Y = -541.56f;
    constexpr float BREACH_B_Z = 119.84f;

    // How near a breach hold point Grauf must be before the driver fires. The two
    // points are 8.6yd apart, so this covers both from either with room for the
    // spline's overshoot, and is far tighter than the 60yd cone — a shot taken
    // mid-lap would simply miss (and hit the breach's own World Trigger, which is
    // harmless), so this is an economy, not a safety.
    constexpr float BREACH_RADIUS = 18.0f;

    // Re-fire floor. The launcher's own autoclose is 1000ms and GO_FLAG_IN_USE
    // blocks a click until GameObject::Update clears it, so a faster cadence than
    // this can only produce swallowed clicks and log noise.
    constexpr uint32 HARPOON_REFIRE_MS = 1500;

    // Bound on the whole of phase 1, as the Custom step's timeout. Three hits at
    // one per ~35s lap is under two minutes; four minutes is a ceiling on a
    // genuinely broken attempt and never the binding constraint on a working one.
    constexpr uint32 HARPOON_TIMEOUT_MS = 240000;

    // Throttle on the driver's per-tick telemetry line.
    constexpr uint32 HARPOON_TELEMETRY_MS = 3000;

    // --- Svala's ritual: the 25 seconds the party must NOT chase ------------
    //
    // At exactly t+25s after she engages, ONCE AND ONLY ONCE
    // (EVENT_SORROWGRAVE_RITUAL is scheduled in JustEngagedWith and never
    // rescheduled), the ritual fires: a random party member is teleported to
    // Svala's own home spot, three Ritual Channelers (27281) spawn around it and
    // stun-lock that member with Paralyze 48278 — an INFINITE-duration stun that
    // ends only when the channeler dies — and the boss NearTeleportTo()s TWENTY
    // YARDS STRAIGHT UP, SetControlled(ROOT), gravity off, for 25 seconds.
    //
    // She is NOT flagged non-attackable up there. She stays targetable and simply
    // cannot be reached, so melee bots will stand underneath doing nothing while
    // the clear re-plans a route to a boss 20yd in the air. The correct behaviour
    // is: kill the three channelers, do not chase, do not re-plan — which is
    // event 5.
    //
    // Being a ONE-SHOT rather than a repeating mechanic is what makes this cheap.
    constexpr uint32 NPC_RITUAL_CHANNELER = 27281;

    // How far above her own floor she has to be before the hold arms. The hover
    // between the intro and the ritual is ~6yd (UNIT_FIELD_HOVERHEIGHT), and the
    // ritual is 20yd; 12 sits cleanly between them so the ordinary fight is never
    // held and the ritual always is.
    constexpr float RITUAL_LIFT_Z = 12.0f;

    // Grid-scan radius for the channelers. They spawn 4-8yd from her altar and
    // the party fights within a few yards of it.
    constexpr float RITUAL_SCAN = 40.0f;

    // --- doors --------------------------------------------------------------
    //
    // Both portcullises spawn SHUT (state 1), carry lockId 0 and autoCloseTime 0,
    // and are opened by the instance script exactly once, permanently — there is
    // no auto-close, no encounter-scoped shut and therefore no IsSelfClearing
    // case. smart_scripts source_type 1 has ZERO rows for map 575; the instance
    // C++ is the sole authority and contains no AddDoor, no DoorData[], no
    // SetBossState and no DoUseDoorOrButton.
    //
    // AND NEITHER IS EVER ON A DESIGNED LEG. 192174 is Ymiron's EXIT — opened by
    // SetData(DATA_YMIRON, DONE), i.e. after the last boss dies — so nothing in a
    // correct run ever waits on it; probe-measured, the designed legs clear it by
    // 43-184yd. 192173 opens on Skadi's death and is touched only by Leg G, by
    // which point it is open. Both are DcEventDoorRegistry::IsScriptOnly and
    // deliberately NOT IsNavigationIgnored: they are real gates, the at-boss
    // stand-down needs to see them, and hiding them would mask a future roster
    // regression rather than prevent one.
    constexpr uint32 GO_SKADI_DOOR  = 192173;
    constexpr uint32 GO_YMIRON_DOOR = 192174;

    // The Svala mirror — a GAMEOBJECT_TYPE_DOOR that spawns OPEN (state 0) and is
    // toggled both ways by boss_svala as pure visual FX, 11yd south of her altar.
    //
    // IsNavigationIgnored, AND THE ROW WAS AUTHORED FROM A MEASUREMENT rather
    // than from the resemblance. It goes READY — shut, by the collision-truth
    // test — for the whole 72-second intro while the party stands beside it, and
    // it LOOKS like the Utgarde KEEP forge-flame-wall shape; but
    // DungeonClearBlockingDoorValue flags a door only when a route leg TRANSITS
    // its footprint, or is GameObject-LOS-blocked within 12yd of it on the same
    // floor. So the question was purely "how close does Leg B actually pass".
    //
    // The answer, from the authored anchors: 7.35yd, and the leg ENDS 11yd away
    // on her platform. That is inside the 12yd fallback band on the same floor,
    // which is enough to auto-pause the run on the boss's own doorstep. Hence the
    // row. t/TestUtgardePinnacle pins the 7.35yd so a re-authored leg re-opens
    // the question instead of silently leaning on it.
    constexpr uint32 GO_SVALA_MIRROR = 191745;

    // --- event and hook ids -------------------------------------------------
    //
    // Event ids are per-map. HOOK ids are ONE FLAT SPACE across every dungeon
    // (ObjectiveHookRegistry::AddHook LOG_ERRORs a collision rather than silently
    // dropping one): 1-14 are the older dungeons', 15-19 the Violet Hold's,
    // 20-21 Blackwing Lair's, 22-23 Halls of Stone's and 24 Halls of Lightning's,
    // so this map takes 25-28.
    // --- step timeouts ------------------------------------------------------
    //
    // Every one of these bounds a WAIT, and none of them is a target. Where a
    // number is derived from the script it is stated; where it is a ceiling on a
    // broken attempt it is generous, because on a REQUIRED step a Failed step
    // Stalls the run and parks the party, and parking is only ever the right
    // answer once a human is genuinely needed.

    // Forging an areatrigger packet is instantaneous when it works. Fifteen
    // seconds is "the leader is standing in the box and the core still will not
    // take it", which is a human's problem, not a longer wait's.
    constexpr uint32 AREATRIGGER_TIMEOUT_MS = 15000;

    // Svala's intro is a measured 72 seconds from the trigger to her becoming
    // attackable, and the transform this step waits for lands at ~34s. Ninety
    // seconds covers the whole cinematic with margin for a server under load.
    constexpr uint32 SVALA_INTRO_TIMEOUT_MS = 90000;

    // The generator click, and then the receipt. The click is a single Use();
    // 20s is arrival slop plus a wolf patrol shoving the tank off the anchor.
    // The state check is synchronous inside that same Use(), so 8s is already
    // several ticks of grace.
    constexpr uint32 STASIS_USE_TIMEOUT_MS   = 20000;
    constexpr uint32 STASIS_STATE_TIMEOUT_MS = 8000;

    // The gauntlet entry hook gathers before it fires, so its budget is the
    // gather's plus the trigger's. Sixty seconds is four followers walking in
    // from a fight that ended up to 60yd back, and no longer.
    constexpr uint32 GAUNTLET_ENTRY_TIMEOUT_MS = 60000;

    // How tight a ball the party enters the one-way door as, and how much of it
    // has to be there. 0.75 is three of four followers — one bot that cannot path
    // in must never hold the other three on the threshold, and stranded recovery
    // (relevance 42) sits above this ladder and owns that member.
    constexpr float  GAUNTLET_GATHER_RADIUS = 20.0f;
    constexpr float  GAUNTLET_GATHER_QUORUM = 0.75f;

    // The ritual is a hard 25 seconds of script. Forty is the ceiling, and it can
    // only be reached if the boss is somehow still airborne after it — at which
    // point the event is Optional and skipping is the correct, quiet answer.
    constexpr uint32 RITUAL_TIMEOUT_MS = 40000;

    constexpr uint32 EVENT_WAKE_SVALA       = 1;
    constexpr uint32 EVENT_START_PALEHOOF   = 2;
    constexpr uint32 EVENT_ENTER_GAUNTLET   = 3;
    constexpr uint32 EVENT_BRING_DOWN_GRAUF = 4;
    constexpr uint32 EVENT_SVALA_RITUAL     = 5;

    constexpr uint32 HOOK_SVALA_AREATRIGGER = 25;
    constexpr uint32 HOOK_SKADI_AREATRIGGER = 26;
    constexpr uint32 HOOK_GRAUF_HARPOON     = 27;
    constexpr uint32 HOOK_SVALA_RITUAL_HOLD = 28;
}

void RegisterUtgardePinnacleEvents(std::vector<DungeonEvent>& out);

// --- Gundrak (map 604) ----------------------------------------------------
// The numbers GundrakEvents.cpp authors and t/TestGundrak.cpp pins. Shared here
// for the same reason DcDrakTharonKeep and DcVioletHold are: several of them are
// SAFETY numbers whose whole value is that a test can re-derive them from the
// live spawn data (the mojo search that must exclude four trash spawns, the pool
// volume that must name exactly three of six Ruins Dwellers), and a literal
// buried in the .cpp cannot be pinned. Every coordinate was column-probed against
// the live 604 mmtiles — see GundrakEvents.cpp for why each is where it is, and
// why on THIS map a GO's own Z is never usable.
namespace DcGundrak
{
    constexpr uint32 MAP = 604;

    // Encounters. The entry numbering does NOT follow the encounter order:
    // 29305 is Moorabi and 29306 is Gal'darah, not the other way round.
    constexpr uint32 SLADRAN   = 29304;
    constexpr uint32 MOORABI   = 29305;
    constexpr uint32 GALDARAH  = 29306;
    constexpr uint32 COLOSSUS  = 29307;
    constexpr uint32 ECK       = 29932;

    // The Drakkari ELEMENTAL — the Colossus's instance_encounters credit entry
    // (it is what dies; SummonedCreatureDies then KillSelf()s the Colossus). It
    // has no `creature` row on any map, which is exactly why BossSpawnIndex drops
    // the Colossus. Never an anchor entry; recorded so the roster test can say
    // which entry it must NOT have used.
    constexpr uint32 DRAKKARI_ELEMENTAL = 29573;

    constexpr uint32 LIVING_MOJO   = 29830;
    constexpr uint32 RUINS_DWELLER = 29920;

    // DungeonEncounter.dbc bits, read off the live DBC. The Colossus is bit 1 on
    // BOTH difficulties, so one MakeBossWithBit serves both; Eck is heroic-only
    // bit 3 (and heroic Gal'darah shifts to bit 4, which the DERIVED row already
    // carries correctly).
    constexpr uint32 BIT_COLOSSUS = 1;
    constexpr uint32 BIT_ECK      = 3;

    // The three altars and the four bridge statues (gundrak.h). Each altar's
    // click drives ITS OWN statue to GO_STATE_READY via instance_gundrak::SetData
    // — 192518 -> 192564, 192520 -> 192567, 192519 -> 192565. That mapping is easy
    // to transpose (the script's own _bridgeGUIDs indices run Slad'ran / Drakkari
    // / Moorabi, a DIFFERENT order from the DATA_* enum), so it is pinned by test.
    constexpr uint32 ALTAR_SLADRAN  = 192518;
    constexpr uint32 ALTAR_MOORABI  = 192519;
    constexpr uint32 ALTAR_COLOSSUS = 192520;

    constexpr uint32 STATUE_SNAKE   = 192564;  // Slad'ran's
    constexpr uint32 STATUE_MAMMOTH = 192565;  // Moorabi's
    constexpr uint32 STATUE_RHINO   = 192566;  // Gal'darah's — the BRIDGE witness
    constexpr uint32 STATUE_TROLL   = 192567;  // the Colossus's

    // Where all four statues stand, on the bridge-chamber floor 61-89yd from the
    // altars that drive them. STATUE_SEARCH has to reach this from every altar
    // anchor or the verification step hangs forever.
    constexpr float STATUE_X = 1775.16f;
    constexpr float STATUE_Y = 743.455f;
    constexpr float STATUE_Z = 119.073f;

    // --- the three altar clicks ------------------------------------------
    //
    // Two of the three altars stand in HOLES in the navmesh (the GO's own column
    // has no walkable surface at its Z), so each of those gets a roomy objective
    // anchor plus a short MoveTo onto a measured rim pad. The third, Moorabi's,
    // has 7.25yd of continuous mesh under it and needs neither.
    constexpr float SLADRAN_ANCHOR_X = 1775.29f;
    constexpr float SLADRAN_ANCHOR_Y = 670.00f;
    constexpr float SLADRAN_ANCHOR_Z = 129.26f;
    constexpr float SLADRAN_CLICK_X  = 1775.29f;
    constexpr float SLADRAN_CLICK_Y  = 676.18f;
    constexpr float SLADRAN_CLICK_Z  = 129.34f;

    constexpr float COLOSSUS_ANCHOR_X = 1683.00f;
    constexpr float COLOSSUS_ANCHOR_Y = 743.60f;
    constexpr float COLOSSUS_ANCHOR_Z = 142.94f;
    constexpr float COLOSSUS_CLICK_X  = 1690.01f;
    constexpr float COLOSSUS_CLICK_Y  = 743.60f;
    constexpr float COLOSSUS_CLICK_Z  = 142.94f;

    constexpr float MOORABI_ANCHOR_X = 1772.22f;
    constexpr float MOORABI_ANCHOR_Y = 804.96f;
    constexpr float MOORABI_ANCHOR_Z = 129.34f;

    // The GOs' own positions, for the tests that re-derive the click geometry
    // (worst-case reach = d + radius must stay inside DC_EVENT_GO_USE_RANGE).
    constexpr float ALTAR_SLADRAN_GO_X  = 1775.29f;
    constexpr float ALTAR_SLADRAN_GO_Y  = 679.68f;
    constexpr float ALTAR_COLOSSUS_GO_X = 1693.51f;
    constexpr float ALTAR_COLOSSUS_GO_Y = 743.595f;

    // Measured pad radii at the two click points — the largest disc around each
    // where every sample is still walkable mesh. The MoveTo radius must stay
    // under these or the bot can turn off the rim into the hole.
    constexpr float SLADRAN_CLICK_PAD  = 1.40f;
    constexpr float COLOSSUS_CLICK_PAD = 1.45f;

    constexpr float ALTAR_SEARCH  = 12.0f;
    constexpr float STATUE_SEARCH = 120.0f;
    constexpr float ALTAR_ARRIVE  = 6.0f;
    constexpr float CLICK_RADIUS  = 1.25f;
    constexpr uint32 ALTAR_TIMEOUT = 60000;

    // --- the Drakkari Colossus's Living Mojo ring -------------------------
    //
    // The five mojos the boss summons around himself (boss_drakkari_colossus.cpp
    // mojoPosition[]) and the merge point they charge on ACTION_MERGE.
    constexpr float MOJO_RING_X[5] = { 1663.10f, 1669.97f, 1680.70f, 1680.70f, 1670.40f };
    constexpr float MOJO_RING_Y[5] = {  743.60f,  753.70f,  750.70f,  737.10f,  733.50f };

    constexpr float COLOSSUS_X = 1672.96f;
    constexpr float COLOSSUS_Y = 743.49f;
    constexpr float COLOSSUS_Z = 142.94f;   // mesh under the (143.34) spawn

    // The four PRE-PLACED trash mojos of the west corridor (guids 127076-127079).
    // They are not TempSummons, so they aggro, fight, and inform NOBODY — hitting
    // one does not start the encounter. MOJO_SEARCH exists to keep them out.
    constexpr float MOJO_TRASH_X[4] = { 1634.21f, 1634.25f, 1624.94f, 1580.78f };
    constexpr float MOJO_TRASH_Y[4] = {  760.22f,  750.15f,  762.23f,  726.10f };

    constexpr float MOJO_SEARCH    = 20.0f;
    constexpr float COLOSSUS_SCAN  = 40.0f;
    constexpr uint32 MOJO_TIMEOUT  = 120000;

    // --- Eck's pool (heroic) ---------------------------------------------
    //
    // Centroid of the FORMATION trio 127201/127202/127203 — the only three of the
    // six Ruins Dwellers whose deaths summon Eck. Z is the WATER SHEET above their
    // 107.28 spawns, not the spawns themselves.
    constexpr float POOL_X      = 1646.40f;
    constexpr float POOL_Y      = 938.85f;
    constexpr float POOL_Z      = 108.22f;
    constexpr float POOL_RADIUS = 15.0f;
    constexpr float POOL_ZBAND  = 6.0f;
    constexpr float POOL_ARRIVE = 18.0f;
    constexpr uint32 POOL_TIMEOUT = 300000;

    // The six Ruins Dweller spawns. The first three are the formation (leader
    // 127203, groupAI 3); the last three are ungrouped and gate NOTHING — killing
    // them is three pointless elite fights and no Eck.
    constexpr float DWELLER_GATING_X[3]   = { 1651.26f, 1643.20f, 1644.73f };
    constexpr float DWELLER_GATING_Y[3]   = {  936.455f, 943.617f, 936.472f };
    constexpr float DWELLER_GATING_Z[3]   = {  107.277f, 107.276f, 107.288f };
    constexpr float DWELLER_UNGROUPED_X[3] = { 1708.48f, 1701.66f, 1717.30f };
    constexpr float DWELLER_UNGROUPED_Y[3] = {  926.962f, 951.026f, 935.615f };
    constexpr float DWELLER_UNGROUPED_Z[3] = {  116.094f, 116.536f, 117.105f };

    // Eck's HOME (boss_eck.cpp EckHomePosition), on the mesh: the water sheet at
    // 108.00 rather than the 107.205 the script names. NEVER his summon point
    // (1624.70, 891.43, 95.08) — no poly within 5yd of it.
    constexpr float ECK_X = 1642.712f;
    constexpr float ECK_Y = 934.646f;
    constexpr float ECK_Z = 108.00f;

    // --- the bridge crossing ---------------------------------------------
    //
    // The checkpoint is on comp#0, the landing on comp#1, and there is no walkable
    // route between them at any Z. Both sit on the causeway CENTRELINES, back from
    // the tips: the tips themselves measure 0.00 and 0.25yd of pad, which five
    // bots cannot arrive on.
    constexpr float CROSS_CHECK_X = 1746.00f;
    constexpr float CROSS_CHECK_Y = 744.00f;
    constexpr float CROSS_CHECK_Z = 119.10f;
    constexpr float CROSS_LAND_X  = 1802.00f;
    constexpr float CROSS_LAND_Y  = 743.50f;
    constexpr float CROSS_LAND_Z  = 119.58f;
    constexpr float CROSS_RADIUS  = 8.0f;
    constexpr float CROSS_ARRIVE  = 5.0f;
    constexpr uint32 BRIDGE_TIMEOUT = 60000;

    // The two mesh gaps the crossing spans, for the tests: the west causeway ends
    // at x 1753.50 and the east one begins at x 1796.50, with a 7-poly DEAD
    // island (no links in any direction) between them.
    constexpr float WEST_TIP_X   = 1753.50f;
    constexpr float EAST_TIP_X   = 1796.50f;

    // --- the rhino stampede, which stands on the Gal'darah approach -------
    //
    // Both rhinos spawn ON THE CAUSEWAY between the teleport landing and the
    // boss, so the party cannot reach Gal'darah without walking through them.
    //
    // 29931 is the loaded one: vehicle_template_accessory seats THREE Drakkari
    // Raiders (29982) on it, and its SmartAI runs waypoint path 1272070, fires
    // Do Action 150/151/152 at point 3, and each raider answers with "Exit
    // vehicle + Set Home Position". Three ordinary trash mobs are then loose on
    // the approach, and — instanced creatures never leash — anything they tag
    // stays tagged from wherever they end up. That is the standoff combat that
    // pinned the tank outside engage range in tr-20260830-195435-2 / -6.
    //
    // 29838 is the charging one (Charge 55530 at 5-40yd, Deafening Roar); it
    // killed tr-20260830-195435-7 outright.
    constexpr uint32 RHINO_LOADED   = 29931;
    constexpr uint32 RHINO_CHARGING = 29838;
    constexpr uint32 RAIDER         = 29982;
    constexpr uint32 RAIDER_SEATS   = 3;

    constexpr float RHINO_LOADED_X   = 1865.06f;
    constexpr float RHINO_LOADED_Y   = 742.78f;
    constexpr float RHINO_CHARGING_X = 1887.56f;
    constexpr float RHINO_CHARGING_Y = 742.88f;

    // --- Gal'darah's sealed arena ----------------------------------------
    constexpr float MUSTER_X = 1858.00f;
    constexpr float MUSTER_Y = 743.60f;
    constexpr float MUSTER_Z = 136.23f;

    // Mojo Puddle, the only PERSISTENT_AREA_AURA on the map.
    constexpr uint32 SPELL_MOJO_PUDDLE = 55627;

    // Clear-order keys — one contiguous scale so the five hand-authored
    // objectives have integer slots between the bosses.
    constexpr int32 ORDER_SLADRAN        = 1;
    constexpr int32 ORDER_ALTAR_SLADRAN  = 2;
    constexpr int32 ORDER_COLOSSUS       = 3;
    constexpr int32 ORDER_ALTAR_COLOSSUS = 4;
    constexpr int32 ORDER_MOORABI        = 5;
    constexpr int32 ORDER_ALTAR_MOORABI  = 6;
    constexpr int32 ORDER_ECK_POOL       = 7;
    constexpr int32 ORDER_ECK            = 8;
    constexpr int32 ORDER_BRIDGE         = 9;
    constexpr int32 ORDER_GALDARAH       = 10;

    // Event ids on this map.
    constexpr uint32 EVENT_ALTAR_SLADRAN  = 1;
    constexpr uint32 EVENT_COLOSSUS_MOJO  = 2;
    constexpr uint32 EVENT_ALTAR_COLOSSUS = 3;
    constexpr uint32 EVENT_ALTAR_MOORABI  = 4;
    constexpr uint32 EVENT_ECK_POOL       = 5;
    constexpr uint32 EVENT_BRIDGE         = 6;
}

void RegisterGundrakEvents(std::vector<DungeonEvent>& out);

// --- Blackwing Lair (map 469) ---------------------------------------------
// The numbers Razorgore's two halves must agree on. The declarative half (the
// event row and its activation predicate) is BlackwingLairEvents.cpp; the
// controller is Overrides/BlackwingLairDriver.cpp; the arithmetic is
// Util/DcRazorgoreDecision.h.
namespace DcBlackwingLair
{
    constexpr uint32 MAP_ID = 469;

    constexpr uint32 NPC_RAZORGORE = 12435;

    // The Orb of Domination and the thirty Black Dragon Eggs. Both are GOOBERs
    // (type 10) and both are WORLD SPAWNS, present from map load — so the driver
    // can grid-scan for them before anything has been engaged, unlike a
    // TempSummon-based encounter.
    constexpr uint32 GO_ORB_OF_DOMINATION = 177808;
    constexpr uint32 GO_BLACK_DRAGON_EGG  = 177807;
    constexpr uint32 EGG_COUNT            = 30;

    // instance_blackwing_lair's DATA_EGG_EVENT. The ONE readable progress seam
    // this encounter offers: it returns NOT_STARTED / IN_PROGRESS / SPECIAL /
    // DONE. There is deliberately no egg COUNT accessor in the instance script,
    // so "how many are left" is answered by scanning the eggs themselves.
    constexpr uint32 DATA_EGG_EVENT = 2;

    // Mind control (19832, 90s) and the charmer's lockout (23958, 60s). The
    // driver never runs a timer against either — it reads the charm and the aura
    // straight off the units, which is authoritative through a wipe, a despawn
    // and a phase flip alike.
    constexpr uint32 SPELL_MIND_CONTROL   = 19832;
    constexpr uint32 SPELL_MIND_EXHAUSTION = 23958;
    constexpr uint32 SPELL_DESTROY_EGG    = 19873;

    // The orb, and where the runner stands to take it: on the upper ledge at
    // z 413, above and clear of all eight add-spawn positions (all z 407).
    constexpr float ORB_X = -7614.83f;
    constexpr float ORB_Y = -1026.62f;
    constexpr float ORB_Z = 413.38f;

    // "Standing at the orb" — the runner clicks from here, and holds here for
    // its whole window (possession roots the charmer anyway; this only keeps it
    // from being walked off before the click).
    constexpr float ORB_STATION_RADIUS = 4.0f;

    // THE THREE MOBS ON THE ORB PLATFORM. Grethok the Controller (a level 62
    // elite caster) and two Blackwing Guardsmen stand 6-10yd from the orb, on the
    // ledge, from map load — they are not part of the encounter and nothing in
    // the instance script removes them.
    //
    // They are the reason this block exists. The runner's walk is 78yd and ends
    // ON TOP of them; the first live run sent one DPS up alone into three elites
    // and it died on the ramp without ever reaching the orb.
    //
    // GRETHOK IS THE PULL. He is not an encounter and carries no kill credit, but
    // he is what a human raid pulls to start this fight, and DC now treats him
    // that way: the roster patch makes him boss #0 of the map, so the tank brings
    // the raid to him with the ordinary pipeline (advance, muster, standoff,
    // engage) instead of forty bots racing each other up the ramp. Nothing on the
    // platform — no election, no staging, no click — happens before that pull.
    constexpr uint32 NPC_GRETHOK_THE_CONTROLLER = 12557;
    constexpr uint32 NPC_BLACKWING_GUARDSMAN    = 14456;

    // How far from the orb to look for them. The furthest of the three spawns
    // 10.0yd out; 25 covers that with room for the pull dragging one a few yards
    // without the scan reaching down to the floor pack (the nearest floor spawn
    // is 33yd out and a tier below).
    constexpr float ORB_GUARD_RADIUS = 25.0f;

    // GRETHOK'S SPAWN — and the roster anchor DC pulls him from.
    //
    // He is not a DungeonEncounter and the DBC has never heard of him, but for a
    // clear he is the boss of this room: he is what the tank pulls, and pulling
    // him starts the encounter (his formation holds Razorgore). So the roster
    // patch adds him as boss #0 of map 469 and the ORDINARY pull pipeline —
    // advance, raid muster, boss standoff, engage — brings the whole raid up to
    // him as one body. See RegisterBlackwingLairRoster.
    //
    // Read straight off `creature` (guid 84389): the two Blackwing Guardsmen
    // (84390 / 84391) stand 5-7yd either side of him at the same height.
    constexpr float GRETHOK_X = -7618.29f;
    constexpr float GRETHOK_Y = -1021.42f;
    constexpr float GRETHOK_Z = 413.56f;

    // The leader has to be in the chamber before the raid is sent up the ramp.
    // The event's own due range (200yd) is the whole approach; this is the room.
    constexpr float GUARD_CLEAR_RANGE = 100.0f;

    // WHERE THE REST OF THE RAID FIGHTS — at the foot of the orb platform, one
    // step toward the middle of the room, on the floor at z 408.87.
    //
    // The runner is rooted on the ledge for ninety seconds at a time and cannot
    // defend itself; if the raid fights wherever the pull left it, the adds that
    // pick the runner arrive unopposed and the mind control ends with its death.
    // So the raid camps between the room and the ledge.
    //
    // NOT on the ledge itself, which is the shape the first live run had and the
    // reason this exists: the platform is small (the navmesh column 5yd out from
    // the orb already has no 413 surface over it), so a raid standing on it has
    // nowhere to spread and nothing between it and the floor the adds cross.
    //
    // Column-probed against the live 469 mmtile: exactly one walkable surface
    // under it, z 408.87. It is 11.4yd (2D) / 12.3yd (3D) from the orb — inside
    // every healer's range of the runner, a couple of steps for a melee bot that
    // has to peel something off the ledge — and 46-81yd from all eight of the
    // instance's add-spawn positions, so every wave has to cross the room to
    // reach it rather than arriving on top of it.
    constexpr float CAMP_X = -7608.30f;
    constexpr float CAMP_Y = -1036.00f;
    constexpr float CAMP_Z = 408.87f;

    // How far off the camp anyone may drift before they are walked back. ONE
    // number for the whole raid — the tank used to get a longer tier of its own
    // (12 for members, 20 for the leader) and both were too tight live: bots
    // were being walked off adds they had legitimately stepped onto, which is
    // the one thing this rung must never do.
    //
    // 30 still keeps the raid in the runner's half of the chamber — the camp is
    // 12.3yd from the orb and the nearest add spawn is 46yd from the camp, so a
    // bot at the end of its leash is still between the room and the ledge — and
    // it is still a leash: "do not chase across the room" is the whole point of
    // a camp in a fight whose adds all come to you.
    //
    // A leash this wide COVERS THE ORB LEDGE (Grethok's own spawn is 18yd from
    // the camp centre), so a raid that fought the guard pull up there reads as in
    // position and is left alone. That is intended: the first add wave pulls it
    // down anyway, and nothing HOLDS it up there.
    //
    // THE COST, deliberately accepted: a bot at the FAR edge is 42yd from the
    // runner, past a healer's 40yd range, and the runner is rooted and cannot
    // come to it. The raid is one body in practice — it fights what walks in,
    // near the camp centre — so this is the far corner of the leash, not where
    // the fight happens. If runner deaths come back with the healers alive and
    // out of range, this number is the first suspect.
    constexpr float CAMP_LEASH = 30.0f;

    // How far INSIDE the leash a drifted bot is walked back to.
    //
    // The camp is a LEASH, not a point: the walk-back aims at the near EDGE of
    // it, never at the centre. Aiming at the centre is what the first live run
    // looked like — a bot crosses the whole camp inward, the fight pushes it
    // back out, and it crosses again, forever, every bot out of phase with the
    // others. Landing just inside the boundary makes the correction a step
    // instead of a lap.
    //
    // The margin IS the hysteresis, so it is neither zero nor a hair: land
    // exactly on the boundary and the next yard of drift re-arms the rung. It
    // must stay well under CAMP_LEASH — a margin that reached the leash would
    // put the hold point back at the centre and bring the lap back with it.
    constexpr float CAMP_HOLD_MARGIN = 4.0f;

    // Covers the whole chamber from anywhere in it: the eggs span ~80yd of x by
    // ~93yd of y across two tiers, and the orb sits 78yd from the boss's spawn.
    constexpr float ROOM_SCAN = 150.0f;

    // Proximity gate for the event's activation predicate — the leader must
    // actually be at the encounter, not corpse-running the entrance ramp.
    constexpr float EVENT_DUE_RANGE = 200.0f;

    // The event row and the hook that drives it. Hook ids are ONE FLAT SPACE
    // across every dungeon (see ObjectiveHookRegistry::AddHook); 15-19 are the
    // Violet Hold's.
    constexpr uint32 EVENT_RAZORGORE_ORB = 1;
    constexpr uint32 HOOK_RAZORGORE_ORB  = 20;

    // The orb platform, as the two facts both halves of the encounter ask about:
    // is any of Grethok / the two Blackwing Guardsmen still standing, and has the
    // tank PULLED them yet.
    //
    // `engaged` is what releases the orb runner. The click used to wait on the
    // platform being empty; it now waits on the pull, because the pull is the
    // only thing that has to happen first — Grethok's formation drags Razorgore
    // into the fight on first contact (groupAI 7), so from the tag onward every
    // second without the mind control is a second the raid spends damaging a boss
    // whose phase-1 death wipes it.
    //
    // Scanned from the ORB rather than from `bot`, so the answer does not change
    // with where the asker happens to be. Shared rather than duplicated because
    // TWO rungs act on it and a disagreement between them is a runner that clicks
    // an orb the leader thinks it is holding: the leader's driver
    // (Overrides/BlackwingLairDriver.cpp) uses it to decide the step, and the
    // runner's own rung (Action/DcRazorgoreActions.cpp) uses it to decide whether
    // to click on arrival or hold station.
    struct OrbGuardState
    {
        bool alive{false};    // at least one of the three is still up
        bool engaged{false};  // ...and somebody has it in combat
    };
    OrbGuardState OrbGuards(Player* bot);

    // IS THE EGG RUN HOLDING THE RAID WHERE IT STANDS?
    //
    // True on map 469 for every member (leader included) from the tick the pull
    // on Grethok lands until a tick or two after the last egg breaks — the same
    // window the camp rung arms on, asked by the rungs that would otherwise WALK
    // the raid somewhere else.
    //
    // It exists because of what the ordinary pipeline does the moment Grethok's
    // anchor clears: the next boss becomes Razorgore, and Razorgore — possessed,
    // being driven egg to egg by our own runner — is a MOVING anchor. The advance
    // then does exactly what it does for any wandering boss: it re-paths at his
    // live position every few seconds and holds at the engage range, and the
    // followers, who follow the tank, come with it. Measured on the first live
    // run of the egg phase (23:14:41 and 23:15:47, 45yd and 42yd splines issued
    // at the possessed boss, interleaved with "within engage range of Razorgore
    // the Untamed (25yd/27yd/11yd/23yd/17yd) -> holding for at-boss"): the raid
    // toured the chamber behind the boss at a fixed standoff instead of holding
    // the camp at the foot of the ledge, which is the one thing phase 1 asks of
    // it — the runner is rooted on that ledge for ninety seconds at a time.
    //
    // So during phase 1 nothing in the approach family may drive: not the route,
    // not the boss standoff, not the muster, not the engage. The raid's position
    // for the whole egg run belongs to the camp rung
    // (DungeonClearRazorgoreCampTrigger), which holds inside the leash and walks
    // a drifted bot back to its near edge.
    //
    // Bounded by construction, so it can never wedge a run: the stamp behind it
    // goes stale within ~3s of the driver stopping, by every exit the encounter
    // has — the last egg, a wipe, the event's own 10-minute timeout (it is
    // Optional and skips), `dc pause`, a dead leader.
    bool EggRunHoldsTheRaid(Player* bot);

    // IS THIS BOT HOLDING THE POSSESSION RIGHT NOW?
    //
    // Read off the bot's OWN unit fields — UNIT_FIELD_CHARM, resolved and checked
    // against Razorgore's entry — and off nothing else. That independence is the
    // whole point of it.
    //
    // The runner's rung and the raid's camp both hang off DcRunState::
    // razorDrivingMs, a stamp the LEADER refreshes on every tick its driver runs.
    // That is right for positioning (a stamp that goes stale releases the raid,
    // which is the safe direction) and WRONG for the channel: 19832 is a channel
    // on the runner's own body, and the instant the runner's rung goes inert the
    // bot's rotation comes back — a swing, a wand shot, a step out of a cone, a
    // health potion at ACTION_EMERGENCY — and ends it. Razorgore is then freed
    // mid-run, the runner eats a 60s lockout, and the raid is left holding a boss
    // it must not kill. Every reason the leader's stamp can go stale (the leader
    // dies, walks out of EVENT_DUE_RANGE, the event stops being due for a tick,
    // `dc pause`) is a reason the possession is STILL UP and still needs guarding.
    //
    // So the possession guards itself: the charm is a fact about this bot, no
    // cross-bot signal is involved, and both the rung that owns the tick and the
    // multiplier that mutes everything else read it here.
    //
    // Free everywhere else — the map compare rejects before the field is read.
    bool HoldsThePossession(Player* bot);

    // --- Vaelastrasz the Corrupt (boss 2) ---------------------------------
    //
    // The only boss on this map — and one of the very few anywhere — that a raid
    // does not PULL. He lies at 30% health, faction 35 (friendly to everyone),
    // REACT_PASSIVE and stand-state DEAD, offering a gossip; the raid starts the
    // encounter by TALKING to him, sits through ~63s of scripted RP, and is then
    // attacked by him. Nothing DC does to a hostile boss applies: he cannot be
    // tagged, he cannot be pulled, and walking the tank into melee does nothing.
    constexpr uint32 NPC_VAELASTRASZ = 13020;

    // His spawn (creature guid 84512, map 469). Also the roster anchor the
    // auto-derived boss list already carries — repeated here only as the
    // proximity gate for the rouse predicate, so a leader corpse-running the
    // entrance never reads as "at Vaelastrasz".
    constexpr float VAEL_X = -7483.79f;
    constexpr float VAEL_Y = -1015.99f;
    constexpr float VAEL_Z = 408.652f;

    // THE GOSSIP CHAIN, and why the option index is 0 at every level.
    //
    // creature_template.gossip_menu_id is 21333; its lone option opens 21334,
    // whose lone option is the one boss_vaelastrasz::sGossipSelect answers
    // (`sender == 21334 && action == 0`, where the core hands sGossipSelect the
    // MENU id as `sender` and the selected list index as `action` — see
    // WorldSession::HandleGossipSelectOptionOpcode). 21334's option in turn opens
    // 21332, which is pure flavour and closes.
    //
    // DungeonEventExecutor::SelectGossip walks exactly that shape by itself: it
    // selects the authored option on the first menu and then keeps selecting
    // option 0 of whatever submenu opens until the menu closes. So ONE Gossip
    // step with option 0 fires BeginSpeech, and there is nothing here to author
    // per level.
    constexpr int32 VAEL_GOSSIP_OPTION = 0;

    // How far out the leader may be and still have the rouse read due. Generous
    // enough to cover the boss standoff the approach parks the tank at (the
    // gossip step walks the last yards in itself), tight enough that the event is
    // never due from the Razorgore chamber ~140yd back.
    constexpr float VAEL_DUE_RANGE = 80.0f;

    // Grid-scan radius for Vaelastrasz himself. He never moves before the pull,
    // so this only has to cover his own room from the standoff.
    constexpr float VAEL_SCAN = 100.0f;

    // The event row. Ids are per-map, so this is 2 alongside Razorgore's 1.
    constexpr uint32 EVENT_VAELASTRASZ_ROUSE = 2;

    // WHERE VAELASTRASZ IS IN HIS OWN OPENING, as the two facts both halves of
    // the rouse ask about. ONE scan for both, for the same reason OrbGuards does
    // it: the engage rung and the event predicate must never disagree about
    // whether he has turned, and a second sweep of the room every tick buys
    // nothing.
    //
    //   * `offersRouse` — he still bears UNIT_NPC_FLAG_GOSSIP, i.e. nobody has
    //     talked to him yet. BeginSpeech strips the flag as its first act, so
    //     this is a one-way latch and the ONLY safe gate on "may I gossip him":
    //     a second select after the RP has started reaches no script and simply
    //     drops.
    //   * `dormant` — he is not hostile to us. The faction flip
    //     (FACTION_FRIENDLY -> FACTION_DRAGONFLIGHT_BLACK) is the last act of the
    //     intro, on the same tick he AttackStarts the bot that talked to him, so
    //     this is exactly "the fight has not begun" and it covers BOTH the wait
    //     before the gossip and the ~63s of RP after it. Deliberately a pure
    //     faction reaction rather than IsValidAttackTarget: he is also
    //     NOT_SELECTABLE for most of the intro and briefly selectable again at
    //     the end of it, and a gate that flickers would hand the engage rung a
    //     tick in the middle of the speech.
    //
    // Free everywhere else — the map compare rejects before anything is scanned.
    struct VaelastraszState
    {
        bool present{false};     // alive, in his room
        bool offersRouse{false}; // ...and still waiting to be talked to
        bool dormant{false};     // ...and has not turned on the raid yet
    };
    VaelastraszState Vaelastrasz(Player* bot);

    // --- the Suppression Rooms (the Vaelastrasz -> Broodlord transit) ------
    //
    // The 375yd of gauntlet between Vaelastrasz's chamber and Broodlord
    // Lashlayer, and the one leg on this map that the ordinary clear cannot
    // cross AT ALL. Everything in this block exists because of one arithmetic
    // fact and one code fact:
    //
    //   * 160 Corrupted Whelps live in the two rooms on a THIRTY-SECOND
    //     respawn — a spawn rate of 5.3/s, ~3.3/s for the hundred within 20yd
    //     of the route line. No DPS closes that; the room's population is a
    //     fixed point the party cannot move.
    //   * DcCombatFlag::MayDrive is false for as long as anything in the party
    //     is engaged, and Advance is registered ONLY in the non-combat engine.
    //     So with the whelps up the clear has no driver at all: it is not slow,
    //     it is stopped.
    //
    // Plus a third that decides how long "stopped" lasts: 38 Suppression Devices
    // (GO 179784) each pulse spell 22247 in a 20yd bubble for -80% movement
    // speed, and 95% of the route lies inside at least one of them. 54 seconds
    // of walking becomes 268.
    //
    // THE ANSWER IS NOT TO CLEAR IT. This leg is a TRANSIT: one body, brakes
    // off, crossed under fire. Four parts, in the order they ship:
    //
    //   A  the authored route below (data; the cursor the other three share)
    //   B  DcNeverTargetRegistry rows on 14022-14025, so the clear's pickers
    //      stop walking the tank BACK into the room for the nearest whelp
    //   D  DungeonClearTransitPack{Trigger,Action} — a moving camp that keeps
    //      the raid inside one leash around the leader's route cursor
    //   C  the transit driver (event EVENT_SUPPRESSION_TRANSIT / hook
    //      HOOK_SUPPRESSION_TRANSIT, Overrides/BlackwingLairDriver.cpp), the
    //      only thing on this map that moves the leader while it is engaged
    //
    // What the transit does NOT skip is the ELITES. Thirteen of the twenty
    // Death Talon Hatchers / Blackwing Taskmasters are within 25yd of the route,
    // they respawn on a TEN-MINUTE timer, and six Taskmasters stand on the only
    // ramp between the two rooms. Killing those is real progress and the driver
    // holds for them.

    // Broodlord Lashlayer — boss 3, and the route's owner in
    // DungeonClearRouteRegistry.
    constexpr uint32 NPC_BROODLORD_LASHLAYER = 12017;

    // ...and his index in instance_blackwing_lair's own BWLEncounter enum
    // (DATA_BROODLORD_LASHLAYER = 2), which is what GetBossState is keyed on.
    // Read rather than derived: the instance's index space is the script's, not
    // the roster's, and the two agree here only by coincidence.
    constexpr uint32 BROODLORD_ENCOUNTER_INDEX = 2;

    // THE DRAKE HALL, and why its four bosses need naming here at all.
    //
    // Firemaw stands at (-7520.2, -1025.8, 449.1) — **24.7yd STRAIGHT ABOVE
    // approach anchor 7** at (-7520.5, -1023.3, 424.5), which is 2.5yd from him
    // in plan view. The hall the raid walks from Vaelastrasz to the staging point
    // runs directly under his room, and a level-63 boss's aggro radius is about
    // 25yd, measured in 3D. In tp-20260828-121941-1 that tripped in four of five
    // runs: one bot inside the radius (21.8yd) aggros him, `BossAI::_JustEngagedWith`
    // calls `DoZoneInCombat()` — every player in the map within 250yd, no LOS and
    // no reachability test — and all twenty-five bots enter combat with a boss two
    // floors up while he never leaves his spawn. The raid then spends the rest of
    // the run split across two floors, and some of it walks up through the ceiling
    // to reach him (a PathGenerator with no navmesh route still hands a player a
    // straight line — see the ac-pathgenerator note).
    //
    // Nothing here can stop him being flagged; the exclusion rows in
    // DcTargetExclusionRegistry stop the raid ACTING on it.
    constexpr uint32 NPC_FIREMAW    = 11983;
    constexpr uint32 NPC_EBONROC    = 14601;
    constexpr uint32 NPC_FLAMEGOR   = 11981;
    constexpr uint32 NPC_CHROMAGGUS = 14020;

    // The four Corrupted Whelps (red / green / blue / bronze). Level 60 normals,
    // 4 578 HP, no AI and no script, MovementType 1 — they aggro on proximity,
    // individually, and they are back thirty seconds after they die. The
    // DcNeverTargetRegistry rows are keyed on these; nothing else does.
    constexpr uint32 NPC_CORRUPTED_RED_WHELP    = 14022;
    constexpr uint32 NPC_CORRUPTED_GREEN_WHELP  = 14023;
    constexpr uint32 NPC_CORRUPTED_BLUE_WHELP   = 14024;
    constexpr uint32 NPC_CORRUPTED_BRONZE_WHELP = 14025;

    // The two elites the transit stands and fights: Blackwing Taskmaster (9
    // spawns, six of them stacked on the ramp at (-7711,-1070,445)) and Death
    // Talon Hatcher (11 spawns, scattered through both rooms). 600s respawn
    // each, so unlike the whelps a kill here is progress that stays bought.
    constexpr uint32 NPC_BLACKWING_TASKMASTER = 12458;
    constexpr uint32 NPC_DEATH_TALON_HATCHER  = 12468;

    // The Suppression Device: GameObject 179784, `type 6` (TRAP), 38 spawns.
    // go_suppression_device casts 22247 every 5s while it is GO_STATE_READY —
    // 20yd radius, -80% move speed, -80% cast speed, 6s duration, i.e. permanent
    // while you stand in it.
    //
    // DISARMING IS NOT OURS. mod-playerbots already owns it
    // (BwlSuppressionDeviceTrigger / BwlTurnOffSuppressionDeviceAction, Ai/Raid/
    // BWL/), turning off any READY device within 15yd at ACTION_RAID (60) — and
    // this box runs `Playerbots.BotCheats = "food,taxi,raid"`, so every bot
    // qualifies, not just rogues. A device turned off that way NEVER re-arms
    // (nothing in the bot path calls DoAction(ACTION_DISARMED), so
    // EVENT_SUPPRESSION_RESET is never scheduled, and a bare SetGoState sets no
    // cooldown for autoCloseTime to fire off). One pass is enough.
    //
    // All the transit owes that rung is a TICK: 17 of the 19 route-adjacent
    // devices are within 10yd of the route line, so walking the route reaches
    // them with no detour — the driver only has to stop walking for one tick
    // when an armed one is close enough for the disarm to fire.
    constexpr uint32 GO_SUPPRESSION_DEVICE = 179784;

    // THE STAGING POINT — the last genuinely clean ground before the gauntlet,
    // at the head of the climb into the lower room.
    //
    // Measured against map 469's spawn table: nearest whelp 40.8yd, nearest
    // device 50.1yd, ZERO whelps within 30yd. The ordinary clear delivers the
    // raid here by itself (it is out of combat up to this point), so the
    // transit's first step is a short intra-room hop, not a haul.
    constexpr float TRANSIT_STAGE_X = -7630.9f;
    constexpr float TRANSIT_STAGE_Y = -915.5f;
    constexpr float TRANSIT_STAGE_Z = 437.3f;

    // WHERE THE STAGING POINT SITS IN THE AUTHORED ROW, and why that is not zero.
    //
    // The registry row for Broodlord is two halves (see RegisterBlackwingLairRoute):
    // anchors 0-19 are the un-crossed approach from Vaelastrasz's chamber, which
    // exists so the ordinary clear has a polyline to walk and a cursor to project
    // onto, and 20-39 are the crossing itself. The transit driver must NOT see the
    // approach — its kernel keys the gather gate on `cursorIndex == 0` meaning "at
    // staging", and its arm pins the cursor to 0 — so BwlTransitRoute slices the
    // row here and hands the driver a route whose anchor 0 is the staging point,
    // exactly as it was before the approach was authored.
    //
    // Certified by t/TestBlackwingLairSuppressionRouteProbe: the row's anchor at
    // this index must BE the staging point, or the driver runs its cursor down the
    // wrong half of the route.
    constexpr std::size_t TRANSIT_STAGE_ANCHOR_INDEX = 20;

    // THE BROODLORD STANDOFF — where the transit ends and the ordinary raid
    // pipeline (muster, standoff, engage) takes back over. Nearest whelp 27.5yd,
    // nearest device 21.7yd; clean ground, on the upper room's floor.
    constexpr float TRANSIT_END_X = -7573.8f;
    constexpr float TRANSIT_END_Y = -1033.5f;
    constexpr float TRANSIT_END_Z = 449.3f;

    // How close the leader has to get before the transit is over. Also the
    // predicate's own OFF switch: the event is due while the leader is inside the
    // corridor and NOT yet here, so arriving simply stops it being due. There is
    // no completion latch to reset — a leader shoved back into the gauntlet
    // re-arms it, which is the correct answer.
    constexpr float TRANSIT_END_RADIUS = 10.0f;

    // THE CORRIDOR — the axis-aligned box that is the two suppression rooms plus
    // the approach and NOTHING else on the map. The transit's activation
    // predicate is gated on the leader being inside it, which is what keeps a
    // rung registered on every bot's combat engine inert for the other seven
    // encounters of this raid.
    //
    // Derived from the route: x spans the climb (-7631) to the standoff (-7574),
    // y spans the upper room's far wall (-1130) to the head of the climb (-905),
    // z spans the lower room's floor (437) to the upper room's (449) with a
    // couple of yards of slack either side. Vaelastrasz's chamber (-7484,-1016,
    // z 409) is outside it on x, y AND z; the Razorgore chamber (-7615,-1027,
    // z 409-413) is outside it on z.
    constexpr float TRANSIT_BOX_MIN_X = -7720.0f;
    constexpr float TRANSIT_BOX_MAX_X = -7570.0f;
    constexpr float TRANSIT_BOX_MIN_Y = -1130.0f;
    constexpr float TRANSIT_BOX_MAX_Y = -905.0f;
    constexpr float TRANSIT_BOX_MIN_Z = 430.0f;
    constexpr float TRANSIT_BOX_MAX_Z = 455.0f;

    // Is `bot` standing inside the suppression corridor right now? One bbox test,
    // and the transit's cheapest real gate after the map compare.
    bool InTransitCorridor(Player* bot);

    // How far INSIDE the pack leash a drifted member is walked back to.
    //
    // The margin IS the hysteresis, so it is neither zero nor a hair: land exactly
    // on the boundary and the next yard of drift re-arms the rung. It must stay
    // well under the leash — a margin that reached it would put the hold point
    // back at the cursor itself and bring the cross-the-whole-pack lap back with
    // it. Four yards is the Razorgore camp's number, which is authored against the
    // same shape (CAMP_HOLD_MARGIN) and survived a live raid.
    //
    // Not a setting: it is a property of the hold, not of the leg, and it only
    // means anything relative to TransitPackLeash — which IS a setting, and whose
    // clamp floor (10) keeps this comfortably inside it.
    constexpr float TRANSIT_PACK_HOLD_MARGIN = 4.0f;

    // The pack rung's ARRIVAL leash — how close to its hold point a walking member
    // has to get before the rung hands the tick back.
    //
    // Named rather than inlined at the call site because the gather gate has to
    // read it. A member parks anywhere in (leash - margin, leash - margin + this]
    // of the cursor: the trigger keeps firing while it is outside the hold radius,
    // but the action refuses to move it once it is within this of the point it
    // aims at. That band IS where the raid stands, so a gate that asks for a
    // radius under its top edge is asking for a formation the rung will never
    // produce — see TransitGatherRadius's floor in the driver, and
    // [[dc-moving-camp-rung-hysteresis]] for the first half of the same lesson.
    constexpr float TRANSIT_PACK_ARRIVE_LEASH = 2.0f;

    // How far the mesh may move the pack rung's chord hold point before the chord
    // is judged to be describing somewhere the party cannot stand, and the rung
    // rides the authored polyline instead (DcTransit::HoldPoint).
    //
    // TIGHT ON PURPOSE, and tighter than it looks. NavmeshSnap searches a FIXED
    // 10yd vertical extent ([[dc-boss-anchor-snap-vertical-extent]]), so a chord
    // that has merely sunk under a ramp still snaps — to the ramp, a yard or two
    // up — and that is a GOOD outcome: measured on the real mesh, the north-arm
    // follower's chord snaps 1.40yd to (-7629.8, -931.7, 441.1), which routes
    // 11.7yd and arrives. It is the chords the snap CANNOT rescue that need the
    // polyline. Two yards is the line between those two populations.
    constexpr float TRANSIT_HOLD_SNAP_TOLERANCE = 2.0f;

    // Horizontal search box for that snap. Deliberately smaller than the pack
    // leash: the question is "is the chord standing on the corridor", and a wide
    // box answers "is there floor SOMEWHERE near here", which on a C-shaped ramp
    // finds the OTHER arm across the void and calls the chord good.
    constexpr float TRANSIT_HOLD_SNAP_RADIUS = 3.0f;

    // NavmeshSnap's own vertical extent, restated so the certification probe can
    // search the same box the runtime does. Not a knob — it is fixed inside
    // NavmeshSnap::Snap; named here only so the probe cannot drift from it.
    constexpr float TRANSIT_HOLD_SNAP_V_EXTENT = 10.0f;

    // Grid-scan radius for the driver's per-tick elite / device sweeps. Wide
    // enough to see every hold-worthy thing from the middle of a leg (the widest
    // hold radius is 20yd and a leg is up to 33yd), tight enough that it never
    // reaches across the room divider into the other suppression room.
    constexpr float TRANSIT_SCAN = 40.0f;

    // The event row and the hook that drives it. Event ids are per-map (1
    // Razorgore, 2 Vaelastrasz); HOOK ids are ONE FLAT SPACE across every dungeon
    // (see ObjectiveHookRegistry::AddHook) — 15-19 are the Violet Hold's and 20
    // is Razorgore's, so this is 21.
    constexpr uint32 EVENT_SUPPRESSION_TRANSIT = 3;
    constexpr uint32 HOOK_SUPPRESSION_TRANSIT  = 21;

    // THE THREE HOLD WATCHDOGS, and why they are three numbers rather than one.
    //
    // The holds are not the same KIND of wait. A device disarm is a tick or two of
    // standing still while somebody else's ACTION_RAID rung fires; a straggler
    // catching up across a 24yd leg is tens of seconds; an elite fight on this leg
    // is minutes (the live budget allows ~3 of the crossing's 4). One shared
    // watchdog would either release the disarm hold before that rung had a tick,
    // or leave the leg parked behind a wedged straggler for the whole elite
    // budget.
    //
    // None of them is a target. Each bounds ONE wait, and on expiry the driver
    // walks on and logs which hold gave up — the member it leaves behind is
    // stranded recovery's problem (relevance 42), which sits above this whole
    // ladder.
    constexpr uint32 TRANSIT_PACK_HOLD_TIMEOUT_MS   = 30000;
    constexpr uint32 TRANSIT_ELITE_HOLD_TIMEOUT_MS  = 120000;
    constexpr uint32 TRANSIT_DISARM_HOLD_TIMEOUT_MS = 1500;


    // Telemetry threshold only. How far a live combat reference has to be from
    // every member holding it before the driver's `towing N` field counts it as
    // aggro the raid is CARRYING rather than a fight the raid is IN.
    //
    // NOTHING HOLDS ON THIS. A gate that did was tried and removed: it waited at
    // the staging point, and tr-20260830-152617-2 and -5 both show the raid
    // reaching staging with the count at zero and 25 of 25 formed up on merit.
    // The count only climbs at cursor 9, the foot of the Taskmaster ramp, ~200yd
    // into the crossing — so a staging gate can never see it.
    //
    // Deliberately below DC_ENGAGEMENT_RADIUS (100yd), which is the opposite
    // question: DcCombatFlag's scan discards everything past 100yd as "a
    // reference that has outlived its geometry", and that discarded set is
    // exactly what this counts. 60yd because the crossing's own fights are inside
    // TRANSIT_SCAN (40yd) and the far packs sit 100-200yd off it.
    constexpr float TRANSIT_AGGRO_SHED_DIST = 60.0f;

    // How far from the staging point the leader may be when the transit ARMS and
    // still be asked to gather there.
    //
    // Past this the gather gate latches open immediately, and is logged: you
    // cannot gather at a point you have already walked past, and a driver that
    // insisted would march a leader standing in the upper room 300yd back through
    // the gauntlet to form up. The case is a re-arm after a partial wipe, which is
    // exactly when walking backwards is worst.
    constexpr float TRANSIT_STAGE_SKIP_DIST = 60.0f;

    // How far the leader may be from its own stored cursor before the projection
    // is believed over it (DcSuppressionTransit::ResolveCursor). Inside a working
    // crossing nothing is ever this far from the anchor it is walking to; a leader
    // that is has died and come back, and the stored index is a fact about a
    // previous attempt.
    constexpr float TRANSIT_CURSOR_RESYNC_DIST = 60.0f;

    // Throttle on the driver's per-tick telemetry line. Without the line a failed
    // run says nothing about WHICH of the four mechanisms is still biting; without
    // the throttle it says it several times a second, per bot.
    constexpr uint32 TRANSIT_TELEMETRY_MS = 3000;

    // Bound on the whole crossing, as the step's timeout.
    //
    // §6.2's live budget is FOUR MINUTES from the staging point (54s of
    // unsuppressed walking plus ~3min of elite fights), so ten minutes is a
    // ceiling on a genuinely broken run and never the binding constraint on a
    // working one. The event is Repeatable and every yielded tick re-bases the
    // step clock, so this only fires when the driver has held (returned Running)
    // continuously for the whole budget — i.e. when a hold's own watchdog has
    // failed to release, which is the one shape nothing else here can see.
    constexpr uint32 TRANSIT_TIMEOUT_MS = 600000;

    // --- Chromaggus (boss 7) — the cage, and the lever that opens it -------
    //
    // The second boss on this map a raid does not pull. Chromaggus stands behind
    // a shut portcullis (GO 179116) and boss_chromaggus's constructor holds him
    // with `SetImmuneToAll(true)` — a core hack-fix that stops him being pulled
    // through the floor from the corridor below. NOTHING clears that immunity but
    // the lever: go_chromaggus_lever's GossipHello opens the portcullis, walks him
    // out on waypoint path 140200, and hands his AI the clicker's GUID via
    // SetGUID(GUID_LEVER_USER), which is what calls SetImmuneToAll(false) and,
    // when the path ends, SetInCombatWith(the clicker).
    //
    // So the lever is the pull, and the bot that pulls it is the bot Chromaggus
    // engages. That is the tank, because a conditional event is performed by the
    // leader.
    constexpr uint32 NPC_CHROMAGGUS_CAGE_DOOR = 179116;   // GO, guid 75161
    constexpr uint32 GO_CHROMAGGUS_LEVER      = 179148;   // GO, guid 56161

    // The lever's spawn (gameobject guid 56161, map 469), on the south wall of
    // the chamber. Also the event's proximity gate.
    constexpr float CHROMA_LEVER_X = -7510.98f;
    constexpr float CHROMA_LEVER_Y = -1094.69f;
    constexpr float CHROMA_LEVER_Z = 476.555f;

    // WHERE CHROMAGGUS FIGHTS, and the roster anchor DC replaces his spawn with.
    //
    // boss_chromaggus::homePos: the script SetHomePosition()s him here the moment
    // the lever is pulled and runs waypoint path 140200 (four points, all
    // (-7488.41, -1074.58, 476.544)) to walk him out of the cage into the chamber.
    // That is the ground the encounter is actually fought on; his DB spawn
    // (-7515.34, -1029.62, 476.73) is a holding pen behind a shut portcullis.
    //
    // WHAT THIS DOES AND DOES NOT BUY. Once his grid is loaded the advance routes
    // at the LIVE creature, not at the anchor (DcAdvanceAction takes bossX/Y/Z off
    // GetLiveBoss), so this does NOT move where the tank ends up standing off —
    // that is one BossEngageRange short of the cage, which is fine: nothing shares
    // his floor within 60yd. What it does buy is the FAR approach, which routes to
    // the anchor while the grid is still streaming in, and every distance readout
    // in the panel and the diag roster. Aiming the long walk at a point sealed
    // inside a cage, directly above a corridor of z-449 trash, is the wrong-floor
    // path-cursor shape this map has already produced once (see the Firemaw note
    // above); aiming it at the open chamber the party walks through is not.
    constexpr float CHROMA_HOME_X = -7491.1587f;
    constexpr float CHROMA_HOME_Y = -1069.718f;
    constexpr float CHROMA_HOME_Z = 476.59094f;

    // How far from the LEVER the leader may be and still read the cage as due.
    // Wide enough to cover the whole chamber — the standoff the approach parks
    // the tank at is ~47yd out, by the cage door, the muster spread adds to that,
    // and the UseGO step walks the last yards in itself — and short of the drake
    // hall behind it (Flamegor is 130yd away).
    constexpr float CHROMA_DUE_RANGE = 90.0f;

    // ...and the FLOOR the leader has to be standing on to be "at the lever".
    //
    // The range gate is 2D, and 2D is a lie on this part of map 469: the whole
    // Broodlord floor sits 27yd DIRECTLY BELOW the chamber, and the Suppression
    // Rooms transit corridor passes within 36yd (2D) of the lever at z 449. The
    // muster gate already makes it impossible for the cage event to be due down
    // there — the next boss during the crossing is Broodlord, not Chromaggus —
    // but this map has produced the wrong-floor bug twice already (the Firemaw
    // approach, the drake-hall/Broodlord overlap), and a 2D radius that spans two
    // floors is exactly how. Half-band, applied around the chamber's floor.
    constexpr float CHROMA_FLOOR_Z    = 476.6f;
    constexpr float CHROMA_FLOOR_BAND = 15.0f;

    // Search radius handed to the UseGO step. The step's own default is 20yd,
    // which would never see the lever from the standoff; this has to reach from
    // anywhere the due range admits.
    constexpr float CHROMA_LEVER_SEARCH = 100.0f;

    // Grid-scan radius for Chromaggus himself, from the bot. Only has to cover
    // the cage from the chamber — his spawn is 47yd from the home anchor and
    // 66yd from the lever, and the bot may be anywhere between them.
    constexpr float CHROMA_SCAN = 100.0f;

    // instance_blackwing_lair's DATA_CHROMAGGUS. Same index space as
    // BROODLORD_ENCOUNTER_INDEX above (the script's DATA_ enum, which on this map
    // happens to match the DBC bits).
    constexpr uint32 CHROMAGGUS_ENCOUNTER_INDEX = 6;

    // The event row. Ids are per-map: 1 Razorgore, 2 Vaelastrasz, 3 the transit.
    constexpr uint32 EVENT_CHROMAGGUS_CAGE = 4;

    // WHERE CHROMAGGUS IS IN HIS OWN OPENING — the two facts the event predicate
    // and the boss-engage hold must never disagree about, read in ONE scan for
    // the same reason OrbGuards and Vaelastrasz do it.
    //
    //   * `caged` — he is alive and still carries UNIT_FLAG_IMMUNE_TO_PC. Only
    //     the lever clears it (SetGUID -> SetImmuneToAll(false)), and nothing
    //     re-applies it short of a full respawn, so this is exactly "the cage has
    //     never been opened in this instance" and it survives a wipe correctly:
    //     after a failed attempt he evades home ATTACKABLE, and the raid re-pulls
    //     him the ordinary way with no lever involved.
    //   * `leverReady` — the lever is spawned, GO_STATE_READY and still
    //     selectable. go_chromaggus_lever's GossipHello stamps
    //     GO_FLAG_NOT_SELECTABLE | GO_FLAG_IN_USE and GO_STATE_ACTIVE on itself,
    //     unconditionally and permanently (nothing in the script ever resets it),
    //     so this is the one-way latch the click cannot double-fire through.
    //
    // Free everywhere else — the map compare rejects before anything is scanned.
    struct ChromaggusState
    {
        bool present{false};    // alive, in or out of the cage
        bool caged{false};      // ...and still immune, i.e. the lever is unpulled
        bool leverReady{false}; // ...and the lever is still there to pull
    };
    ChromaggusState Chromaggus(Player* bot);

    // --- Nefarian (boss 8) — starting the encounter ------------------------
    //
    // The third boss here a raid does not pull, and the only one that does not
    // EXIST until the raid asks for him. Lord Victor Nefarius (10162) sits
    // friendly and passive on his balcony offering a gossip; answering it runs
    // boss_victor_nefarius::sGossipSelect -> BeginEvent, which flips him hostile,
    // engages the raid and starts the drakonid waves. Nefarian himself (11583) is
    // summoned only after MAX_DRAKONID_KILLED (42) adds die, flies in on waypoint
    // path 11583 and lands at the far end of the room.
    //
    // DC's entire job here is the gossip. Everything after it — the wave fight,
    // the transformation, the class calls — belongs to mod-playerbots' raid
    // strategy, exactly as Razorgore's adds and Vaelastrasz's burn do.
    constexpr uint32 NPC_VICTOR_NEFARIUS = 10162;
    constexpr uint32 NPC_NEFARIAN        = 11583;

    // Victor's spawn (creature guid 85785, map 469) — the gossip target and the
    // event's proximity gate.
    constexpr float NEFARIUS_X = -7587.76f;
    constexpr float NEFARIUS_Y = -1261.43f;
    constexpr float NEFARIUS_Z = 482.21f;

    // WHERE NEFARIAN LANDS, and the roster anchor for him.
    //
    // He has NO creature spawn row at all, so BossSpawnIndex cannot derive him
    // and the auto-roster ends at Chromaggus — the run would report itself
    // finished one boss short. This is the last point of waypoint path 11583
    // (the intro flight from (-7348.85, -1495.13, 552.52) down into the lair),
    // i.e. the ground he is standing on the moment he becomes a boss anybody can
    // fight. 86yd from Victor, which the event's due range has to span.
    constexpr float NEFARIAN_X = -7502.0f;
    constexpr float NEFARIAN_Y = -1256.5f;
    constexpr float NEFARIAN_Z = 476.758f;

    // His real DungeonEncounter bit (DBC row 617, encounterIndex 7) — read off
    // DungeonEncounter.dbc, not guessed from the instance script's DATA_ enum.
    // MakeBossWithBit takes it directly because there is no derived row to
    // inherit a bit from.
    constexpr uint32 NEFARIAN_ENCOUNTER_INDEX = 7;

    // THE GOSSIP CHAIN. creature_template.gossip_menu_id is 21330; its lone
    // option opens 21331, whose lone option opens 21332, whose lone option
    // ("Please do.") is the one boss_victor_nefarius::sGossipSelect answers
    // (`sender == 21332 && action == 0`). Every level offers exactly one option
    // at OptionID 0 and none of the three carries a `conditions` row, so
    // DungeonEventExecutor::SelectGossip — which selects the authored option and
    // then keeps selecting option 0 of whatever submenu opens until the menu
    // closes — walks the whole chain from a single authored 0.
    constexpr int32 NEFARIUS_GOSSIP_OPTION = 0;

    // How far from VICTOR the leader may be and still read the start as due, and
    // how far out the Gossip step may acquire him.
    //
    // The due range is a WINDOW, not just a floor, and both ends are load-bearing.
    // It must clear the 86yd from Nefarian's landing (the raid anchor) to Victor
    // with slack for wherever the at-boss hold actually parks the tank — or the
    // event could never arm from the place the advance delivers the raid to. And
    // it must NOT reach back to the lair's entrance portcullis, which is 149yd
    // from Victor: an event due at the door would preempt the advance there and
    // walk the tank the length of the room while the raid was still filing in
    // behind it. 120 sits between the two with room either side.
    //
    // The scan is deliberately much wider — it only has to RESOLVE him once the
    // range gate has already said yes.
    constexpr float NEFARIUS_DUE_RANGE = 120.0f;
    constexpr float NEFARIUS_SCAN      = 200.0f;

    // The event row. Ids are per-map: 1 Razorgore, 2 Vaelastrasz, 3 the transit,
    // 4 Chromaggus' cage.
    constexpr uint32 EVENT_NEFARIAN_START = 5;

    // WHETHER THE ENCOUNTER HAS BEEN STARTED, read off Victor himself.
    //
    // `offersStart` is UNIT_NPC_FLAG_GOSSIP, and it is the same one-way latch the
    // Vaelastrasz rouse uses: sGossipSelect removes the flag before anything
    // else, so the predicate goes false on the click and a second select would
    // reach no script anyway. It also comes BACK on its own — a failed attempt
    // schedules EVENT_RESPAWN_NEFARIUS 15min out and Reset() re-adds the flag —
    // which is why the event is Repeatable: the raid gets to start him again.
    struct NefariusState
    {
        bool present{false};     // alive, on his balcony
        bool offersStart{false}; // ...and still waiting to be talked to
    };
    NefariusState Nefarius(Player* bot);
}

void RegisterBlackwingLairEvents(std::vector<DungeonEvent>& out);

// --- Pit of Saron (map 658) ------------------------------------------------
// The numbers PitOfSaronEvents.cpp and Overrides/PitOfSaronDriver.cpp both
// author against, and t/TestPitOfSaron.cpp pins. Shared here for the reason every
// other dungeon's block is: a coordinate that lives in two files diverges.
//
// READ THE ROSTER PATCH FIRST (PitOfSaronEvents.cpp). The derived roster on this
// map is TWO bosses for a three-boss dungeon, and the observable consequence is
// not "the last boss is skipped" — it is that a run REPORTS SUCCESS at 2/2 with
// the third encounter never attempted. Scourgelord Tyrannus (36658) has a real
// kill-credit DungeonEncounter row on both difficulties and NO `creature` spawn:
// he is a vehicle accessory on Rimefang (36661, vehicle_template_accessory
// 36661 -> 36658), so BossSpawnIndex::Build's spawn join drops him. The Gundrak
// shape, and MakeBossWithBit is the escape hatch.
//
// EVERYTHING ELSE ON THIS MAP IS DOWNSTREAM OF THAT ROW, because what stands
// between Krick and Tyrannus is a FOUR-AREATRIGGER, TWO-WAVE, STRICTLY-ORDERED
// GAUNTLET in which every gate is refused silently if crossed out of turn.
namespace DcPitOfSaron
{
    constexpr uint32 MAP_ID = 658;

    // --- the three encounters, in travel order ------------------------------
    //
    // NPC_ICK (36476) is the CREDIT entry for the second encounter, not Krick
    // (36477) — Krick rides Ick and dies with him, and instance_encounters rows
    // 835/836 name 36476. Anchor, target and reorder on Ick.
    constexpr uint32 NPC_GARFROST = 36494;
    constexpr uint32 NPC_ICK      = 36476;
    constexpr uint32 NPC_KRICK    = 36477;
    constexpr uint32 NPC_TYRANNUS = 36658;

    // Rimefang. He DOES have a spawn row (1017.3, 168.97, 642.93) and he is what
    // Tyrannus rides, 14.7yd in the air, until Gorkun's intro ends. He is
    // unit_flags 66 (NON_ATTACKABLE | IMMUNE_TO_NPC) and never a target; he is
    // here because DATA_RIMEFANG_GUID is the only handle on the vehicle whose
    // DespawnOnEvade decides whether a failed attempt is recoverable.
    constexpr uint32 NPC_RIMEFANG = 36661;

    // The ORCHESTRATOR — npc_pos_tyrannus_events, the RP Tyrannus that flies the
    // pre-boss scenes and owns the gauntlet's kill counter. It is the creature the
    // three SmartTrigger areatriggers SetData, and its POSITION is gate 1's real
    // precondition (see RP_WAIT_* below). Reachable from any bot through
    // GetGuidData(DATA_TYRANNUS_EVENT_GUID).
    constexpr uint32 NPC_TYRANNUS_EVENT = 36794;

    // Gorkun / Martin, second incarnation — the Horde and Alliance faces of the
    // same NPC, summoned by areatrigger 5633 and the thing whose EXISTENCE is the
    // receipt that the trigger took (the instance stores it under
    // DATA_MARTIN_OR_GORKUN_GUID, and at_tyrannus_event_starter refuses to fire
    // again while it is set).
    constexpr uint32 NPC_MARTIN_2 = 37580;
    constexpr uint32 NPC_GORKUN_2 = 37581;

    // WAVE 1 — ten mobs, killsLeft 10 on both difficulties. NONE of these three
    // entries has a static spawn anywhere on map 658, so "any live one within the
    // scan" is an unambiguous read of "wave 1 is still up" and needs no volume.
    constexpr uint32 NPC_YMIRJAR_DEATHBRINGER = 36892;
    constexpr uint32 NPC_YMIRJAR_WRATHBRINGER = 36840;
    constexpr uint32 NPC_YMIRJAR_FLAMEBEARER  = 36893;

    // WAVE 2 — six mobs on normal, TWELVE on heroic (a second group of 4+2 that
    // ends 68yd further north and 17yd lower).
    //
    // 36841 IS NOT UNAMBIGUOUS AND THAT IS THE WHOLE PROBLEM. It has EIGHT static
    // spawns on this map, every one of them in the icicle tunnel at x 997-1074,
    // and Gorkun's own add pump summons more of it behind the boss for the entire
    // Tyrannus fight. A bare aliveness probe on this entry would read "wave 2 is
    // still up" from the moment the party can see the tunnel. Hence WAVE2_* below.
    constexpr uint32 NPC_FALLEN_WARRIOR       = 36841;
    constexpr uint32 NPC_WRATHBONE_COLDWRAITH = 36842;

    // DungeonEncounter.dbc bits, verbatim from rows 833-838. Identical on normal
    // and heroic (833/834 -> 0, 835/836 -> 1, 837/838 -> 2), so ONE Any-gated
    // roster patch serves both difficulties — unlike Gundrak and the Nexus, whose
    // heroic-only additions needed their own gated patch.
    constexpr uint32 BIT_GARFROST = 0;
    constexpr uint32 BIT_KRICK    = 1;
    constexpr uint32 BIT_TYRANNUS = 2;

    // instance_pit_of_saron's OWN `enum DataTypes` slots — the keys GetData and
    // GetGuidData are switched on, counted from pit_of_saron.h. MAX_ENCOUNTER
    // occupies slot 3, which is why the progress counter is FOUR and not three;
    // never hand-count this enum again, and never reuse an encounterIndex for it.
    constexpr uint32 DATA_GARFROST              = 0;
    constexpr uint32 DATA_ICK                   = 1;
    constexpr uint32 DATA_TYRANNUS              = 2;
    constexpr uint32 DATA_INSTANCE_PROGRESS     = 4;
    constexpr uint32 DATA_TYRANNUS_EVENT_GUID   = 6;
    constexpr uint32 DATA_MARTIN_OR_GORKUN_GUID = 13;
    constexpr uint32 DATA_RIMEFANG_GUID         = 14;
    constexpr uint32 DATA_TYRANNUS_GUID         = 15;

    // --- THE INSTANCE PROGRESS LADDER — this map's whole state machine -------
    //
    // GetData(DATA_INSTANCE_PROGRESS) is MONOTONIC (SetData raises it and never
    // lowers it), readable from any bot, and advanced ONLY by killing Ick and by
    // standing inside one of three areatrigger spheres. That makes it both the
    // gauntlet driver's FSM key AND the near-gate justification for the
    // conditional event's allow-list row in t/TestEventRegistry.cpp — a stronger
    // guarantee than any distance check, because the counter cannot reach 3 or 4
    // with the party anywhere but inside a specific 34-38yd sphere.
    //
    //   0 NONE
    //   1 FINISHED_INTRO        the Sylvanas/Jaina entrance RP ended; gates Ick
    //   2 FINISHED_KRICK_SCENE  set the INSTANT Ick dies (boss_krickAI event 20)
    //   3 AFTER_WARN_1          areatrigger 5578 accepted -> wave 1 (10 mobs)
    //   4 AFTER_WARN_2          areatrigger 5579 accepted -> wave 2 (6 / 12 heroic)
    //   5 AFTER_TUNNEL_WARN     areatrigger 5580 accepted -> the icicles arm
    //   6 TYRANNUS_INTRO        areatrigger 5633 accepted -> Gorkun summoned
    constexpr uint32 PROGRESS_NONE                 = 0;
    constexpr uint32 PROGRESS_FINISHED_INTRO       = 1;
    constexpr uint32 PROGRESS_FINISHED_KRICK_SCENE = 2;
    constexpr uint32 PROGRESS_AFTER_WARN_1         = 3;
    constexpr uint32 PROGRESS_AFTER_WARN_2         = 4;
    constexpr uint32 PROGRESS_AFTER_TUNNEL_WARN    = 5;
    constexpr uint32 PROGRESS_TYRANNUS_INTRO       = 6;

    // --- the clear order ----------------------------------------------------
    //
    // ONE contiguous 1..4 scale so the single objective has an integer slot
    // between Krick and the boss the roster patch adds:
    //
    //     1  boss       36494    Forgemaster Garfrost           (reorder, bit 0)
    //     2  boss       36476    Krick and Ick                  (reorder, bit 1)
    //     3  objective  OBJ(1)   Tyrannus's ledge     -> event 2
    //     4  boss       36658    Scourgelord Tyrannus  [PATCHED IN, bit 2]
    //
    // The GAUNTLET between 2 and 3 is deliberately NOT an objective. The party is
    // in continuous combat across it, and an anchored event has no combat-side
    // rung — DcRel::AtObjective (30) is non-combat only, and DcRel::EventDueCombat
    // (61) exists only for a Conditional event carrying DrivesInCombat. So the
    // gauntlet is event 1, a conditional driver, exactly as Halls of Lightning's
    // Slag Furnace crossing is.
    constexpr int32 ORDER_GARFROST = 1;
    constexpr int32 ORDER_KRICK    = 2;
    constexpr int32 ORDER_LEDGE    = 3;
    constexpr int32 ORDER_TYRANNUS = 4;

    // --- boss anchors -------------------------------------------------------
    //
    // Garfrost and Ick are their live `creature` rows and are here only so the
    // route table and the probe can name them; the derivation already places both.
    constexpr float GARFROST_X = 712.14f;
    constexpr float GARFROST_Y = -215.70f;
    constexpr float GARFROST_Z = 527.07f;
    constexpr float ICK_X      = 852.85f;
    constexpr float ICK_Y      = 123.53f;
    constexpr float ICK_Z      = 510.11f;

    // TYRANNUS IS ANCHORED ON HIS EXIT POSITION, NOT WHERE HE SPAWNS, and this is
    // the one anchor on the map worth arguing about.
    //
    // He has no spawn row at all: he is created as Rimefang's vehicle accessory
    // at (1017.3, 168.97, 642.93) — FOURTEEN YARDS IN THE AIR — and stays there,
    // NON_ATTACKABLE, for the whole intro. boss_tyrannusAI::DoAction(1) then
    // MoveJump()s him to exitPos (1023.46, 159.12, 628.2), SetHomePosition()s him
    // there, and that is where the fight is actually fought.
    //
    // NavmeshSnap's vertical extent is a FIXED 10 regardless of snap radius
    // ([[dc-boss-anchor-snap-vertical-extent]]), so an anchor authored at his
    // riding position would not snap at all and the roster row would be DROPPED AT
    // LOAD with a single LOG_ERROR — the same silent two-boss roster this patch
    // exists to repair, one layer down.
    constexpr float TYRANNUS_X = 1023.46f;
    constexpr float TYRANNUS_Y = 159.12f;
    constexpr float TYRANNUS_Z = 628.20f;

    // --- the four gates (AreaTrigger.dbc, map 658) --------------------------
    //
    // All four carry a RADIUS, so Player::IsInAreaTriggerRadius takes its
    // `radius > 0` branch: a 3D SPHERE around the centre, not a box. That is the
    // opposite of Utgarde Pinnacle's two, and it is why the arrive radii below are
    // ordinary tolerances rather than containment radii.
    //
    // 5578 / 5579 / 5580 are `SmartTrigger` rows whose smart_scripts source_type 2
    // entries SET_DATA (1, 1|2|3) on the orchestrator; 5633 is the C++
    // at_tyrannus_event_starter. All four have an areatrigger_scripts row and NO
    // areatrigger_teleport row, so DcTestAreaTriggers relays every one of them in
    // a `.dc test` run — and the driver forges them anyway, which is what makes
    // this map work for an ORDINARY bot party.
    constexpr uint32 AREATRIGGER_WARN_1   = 5578;
    constexpr uint32 AREATRIGGER_WARN_2   = 5579;
    constexpr uint32 AREATRIGGER_TUNNEL   = 5580;
    constexpr uint32 AREATRIGGER_TYRANNUS = 5633;

    constexpr float AT_WARN_1_X = 859.28f;
    constexpr float AT_WARN_1_Y = 44.40f;
    constexpr float AT_WARN_1_Z = 515.03f;
    constexpr float AT_WARN_1_R = 34.54f;

    constexpr float AT_WARN_2_X = 960.06f;
    constexpr float AT_WARN_2_Y = 75.25f;
    constexpr float AT_WARN_2_Z = 566.17f;
    constexpr float AT_WARN_2_R = 38.14f;

    constexpr float AT_TUNNEL_X = 969.91f;
    constexpr float AT_TUNNEL_Y = -117.44f;
    constexpr float AT_TUNNEL_Z = 597.94f;
    constexpr float AT_TUNNEL_R = 20.51f;

    constexpr float AT_TYRANNUS_X = 1006.86f;
    constexpr float AT_TYRANNUS_Y = 178.96f;
    constexpr float AT_TYRANNUS_Z = 628.16f;
    constexpr float AT_TYRANNUS_R = 51.50f;

    // --- where the driver actually STANDS to fire each gate -----------------
    //
    // NOT the DBC centres, and the distinction is load-bearing. A trigger centre
    // is a point in a sphere, not a point on the floor: 5578's is 34.5yd across
    // and its centre sits over the ramp the party climbs, 5580's is on the tunnel
    // mouth's slope. The driver walks the leader to a PROBED, on-mesh point
    // comfortably inside each sphere and forges from there, so the walk-in can
    // never end on a surface the navmesh does not have
    // ([[dc-navharness-prints-the-route]]).
    //
    // Every one is a probed point on the corridor LongRangePathfinder returns for
    // the leg that reaches it, and gate 1's is worth its own note: the corridor A*
    // prefers between Krick and the ledge DOES NOT GO THERE. It climbs the ramp at
    // y ~ 76 and its closest approach to areatrigger 5578's centre is 33.2yd — 1.3
    // yards inside a 34.54yd sphere, which is less margin than the arrival leash.
    // So the authored route detours ~60yd south to stand at the trigger properly
    // (3.6yd from its centre) rather than grazing it.
    //
    // Measured margins, stand point to its own sphere's centre:
    //
    //     gate 1   3.6yd of 34.54    gate 2  16.2yd of 38.14
    //     gate 3   6.1yd of 20.51    5633   38.7yd of 51.50
    //
    // t/TestPitOfSaronRouteProbe asserts each is on-mesh AND — with room for the
    // arrival leash — inside its own sphere, so a mesh regen that moves a corridor
    // trips red instead of quietly leaving the driver forging from outside.
    constexpr float GATE_1_X = 861.03f;
    constexpr float GATE_1_Y = 47.09f;
    constexpr float GATE_1_Z = 516.74f;
    constexpr float GATE_2_X = 949.28f;
    constexpr float GATE_2_Y = 63.16f;
    constexpr float GATE_2_Z = 566.80f;
    constexpr float GATE_3_X = 966.29f;
    constexpr float GATE_3_Y = -112.68f;
    constexpr float GATE_3_Z = 596.94f;

    // How close to a gate stand point counts as "there". Ordinary arrival slop:
    // the spheres are 20-38yd across, so nothing here is a containment radius.
    constexpr float GATE_LEASH = 4.0f;

    // --- the ARM state: where the party waits out the Krick outro ------------
    //
    // THE KRICK OUTRO IS 85-100 SECONDS LONG AND GATE 1 IS INSIDE IT. Ick's death
    // sets FINISHED_KRICK_SCENE immediately, then boss_krickAI::DoAction(1) runs an
    // eleven-step RP chain summing to ~68s, at which point the orchestrator is sent
    // to (809.39, 74.69, 541.54) and only THEN to RP_WAIT_* 121yd further.
    //
    // DC advances the moment the encounter bit flips. The Krick arena is ~88yd
    // from gate 1's centre and the tank walks that in well under 85 seconds, so
    // without an explicit hold the party stands in the sphere for a minute and a
    // half with nothing to show for it.
    //
    // THE HOLD COSTS THE TANK ITS REST TICKS AND NOT THE PARTY'S, which is why it
    // is affordable. The event rung is LEADER-ONLY: while it returns Running the
    // leader's NeedsRest (26.5) is starved by EventDue (31), but every follower is
    // running its own ladder and drinks normally. A tank that does not eat for
    // ninety seconds after a boss kill is a much smaller cost than a healer that
    // does not.
    //
    // STAGE_* USED TO BE THE SCRIPT'S OWN KrickCenterPos (836.65, 115.08, 509.81)
    // — the middle of the arena, 74.4yd from gate 1's centre. IT IS NOW SOUTH OF
    // THE GAUNTLET INSTEAD, and the move is about geometry, not about the hold.
    //
    // Holding in the arena meant resuming from the arena, and the resume line ran
    // south-east over the shoulder at (848, 79) on its way to the ramp's foot.
    // The escort spline between anchors is LINEAR (see CREST ANCHORS in
    // PitOfSaronEvents), so that chord cut the shoulder and dragged the party
    // through the rock. Holding down on the flat southern plain instead means the
    // party is already lined up with the ramp when the outro ends: measured
    // against the map's own mmtiles, every line out of here is clean —
    //     hold -> gate 1            36.6yd, 0.00yd of penetration
    //     hold -> the ramp foot     48.1yd, 0.00
    //     hold -> up the ramp       59.7yd, 0.00
    // and the whole Krick -> gate 1 walk now has a worst penetration of 0.09yd,
    // against 3.14yd over the shoulder.
    //
    // THE TRADE IS THAT THIS POINT IS INSIDE AREATRIGGER 5578, and that is a
    // deliberate, checked relaxation of what the paragraph above was protecting.
    // It is 33.81yd from the trigger's centre, whose radius is 34.54 — inside by
    // 0.73yd, i.e. on the rim rather than in the middle. That is safe here for a
    // reason that did not hold when this was written: the driver FORGES the
    // trigger (HandleAreaTriggerOpcode from a bot standing on the gate point) and
    // bots have no client to fire it by walking in, so being inside the sphere
    // early consumes nothing. A refused forge is silent and simply retried. The
    // property that still matters is distance from the ground the AMBUSH lands on,
    // and that is unchanged: 49.7yd from the first ramp anchor.
    //
    // If a run ever shows wave 1 reaching the hold, move the point ~27yd further
    // south-west along the same bearing to restore the old 40yd-clear-of-sphere
    // margin; the bearing is what keeps the party lined up, not the distance.
    constexpr float STAGE_X = 835.59f;
    constexpr float STAGE_Y = 20.74f;
    constexpr float STAGE_Z = 510.32f;
    constexpr float STAGE_LEASH = 6.0f;

    // PTSTyrannusWaitPos1 — gate 1's REAL precondition, and the reason the ARM
    // state tests a position rather than a timer.
    //
    // npc_pos_tyrannus_eventsAI::SetData(1, 1) refuses unless
    // `me->GetExactDist(&PTSTyrannusWaitPos1) <= 3.0`, so the orchestrator has to
    // have physically flown here. The 85-100s figure above is summed from EventMap
    // schedules plus a flight-speed estimate and is NOT what the driver waits on:
    // it waits on the creature actually being here, which cannot be wrong.
    //
    // RP_ARRIVE_DIST is the core's own 3.0 verbatim. Do not widen it "for slop" —
    // a wider test would arm the gate before the SetData would accept it, and the
    // refusal is silent.
    constexpr float RP_WAIT_X = 923.45f;
    constexpr float RP_WAIT_Y = 82.65f;
    constexpr float RP_WAIT_Z = 582.44f;
    constexpr float RP_ARRIVE_DIST = 3.0f;

    // --- the waves ----------------------------------------------------------
    //
    // Wave 1 is scanned by ENTRY ALONE (none of its three entries spawns
    // statically on this map). Wave 2 needs a VOLUME, because 36841 has eight
    // static spawns in the tunnel and Gorkun pumps more of it during the boss
    // fight.
    //
    // The volume is a cylinder: 2D radius WAVE2_RADIUS about (WAVE2_X, WAVE2_Y)
    // with |z - WAVE2_Z| <= WAVE2_ZBAND. Measured against every position the
    // script actually uses:
    //
    //     normal  spawn (927, -72, 592.2)      d 47.3   dz  7.2
    //     normal  home  (926.1, -46.6, 591.2)  d 22.4   dz  6.2
    //     heroic  mid   (928.4, -29.3, 589.0)  d  4.7   dz  4.0
    //     heroic  home  (937.8,  21.2, 574.6)  d 46.6   dz 10.4
    //
    // ...and against every 36841 it must EXCLUDE:
    //
    //     nearest static (997.3, -139.3, 615.9)   d 131.6   dz 30.9
    //     Gorkun's pump  (1061.0, 102.8, 630.2)   d 181.6   dz 45.2
    //
    // 80yd is therefore a 51yd margin on the near side and a 33yd margin on the
    // far side, and one volume covers BOTH difficulties. If a probe ever says
    // otherwise, split wave 2 into two volumes rather than widening this one until
    // the tunnel's statics leak in.
    constexpr float WAVE2_X      = 932.0f;
    constexpr float WAVE2_Y      = -25.0f;
    constexpr float WAVE2_Z      = 585.0f;
    constexpr float WAVE2_RADIUS = 80.0f;
    constexpr float WAVE2_ZBAND  = 30.0f;

    // Grid-scan radius for the wave probes, from the leader. Wave 1's furthest
    // summon point is 105yd from gate 1's stand point and its two Deathbringers
    // spline 67yd west of that, so the scan has to be generous; the entry filter
    // is what keeps it honest.
    constexpr float WAVE_SCAN = 130.0f;

    // How near a live wave mob has to be before the driver stops steering and
    // hands the tick to the combat engine. Beyond it the driver walks the leader
    // at the nearest one — a wave mob that never aggros is a wave that never
    // clears, and killsLeft only moves on a DEATH.
    constexpr float WAVE_ENGAGE_RANGE = 25.0f;

    // How long a wave state must have been live before "nothing alive" is allowed
    // to mean "the wave is dead".
    //
    // THE SUMMONS ARE NOT SYNCHRONOUS WITH THE PROGRESS BUMP. SetData(1,1) raises
    // the counter and schedules event 30 at 0ms; the two Deathbringers land on the
    // next UpdateAI, the eight others at +3.0s and +3.5s behind a spline-arrival
    // poll. So there is a real window in which progress reads 3 and the scan reads
    // empty, and a driver without this grace walks straight into gate 2 — which
    // refuses (killsLeft != 0) and leaves the party standing in the ambush.
    // Six seconds covers the whole summon chain with margin.
    constexpr uint32 WAVE_SPAWN_GRACE_MS = 6000;

    // How long a LIVE wave mob within WAVE_ENGAGE_RANGE is allowed to say nothing
    // before the driver stops waiting for aggro and pulls it itself.
    //
    // THE WAVE DOES NOT RELIABLY AGGRO, AND WAITING CANNOT FIX IT. Wave 1's eight
    // escorts (36840 / 36893) carry `smart_scripts` AI_INIT -> REACT_PASSIVE plus a
    // ONE-SHOT 3500ms update -> REACT_AGGRESSIVE, and the two Deathbringers (36892)
    // get the same window from pit_of_saron.cpp cases 30/35/36 (REACT_PASSIVE +
    // UNIT_FLAG_NON_ATTACKABLE, cleared 3.5s behind their escorts). SetReactState
    // runs no aggro scan, and AzerothCore's proximity aggro fires ONLY from a
    // relocation notifier, so the mob's one look at the party is spent while it is
    // still passive; after the flip somebody has to MOVE. A party the driver has
    // parked never does. Measured on tp-20260907-113722-1: 2 of 5 runs stood 8yd
    // from ten live Ymirjar, full health, for 6m23s.
    //
    // Five seconds is the 3500ms emerge window plus margin for a summon the scan
    // picks up a tick or two late — long enough that an ordinary aggro is never
    // pre-empted, short enough that the recovery costs a fraction of the leg.
    constexpr uint32 WAVE_STANDOFF_MS = 5000;

    // --- WAVE 1 HAS TO BE ARMED BEFORE THE PARTY CLIMBS THE RAMP ------------
    //
    // THE BUG THIS PINS: the party waits out the whole Krick outro at the ARM
    // hold, forges gate 1 — and then walks up the ramp into a wave that does not
    // exist yet. It looks like the driver leaving the hold too early. It is not:
    // the hold ended correctly, and the very next state walked away from it.
    //
    // pit_of_saron.cpp's summon chain is a CASCADE, not an event, and the progress
    // bump is only its first link:
    //
    //     SetData(1,1)   progress -> 3, killsLeft = 10, event 30 at 0ms
    //     event 30       next UpdateAI: the TWO DEATHBRINGERS (36892) are summoned
    //                    at (950.6, 50.9, 567.9) and (949.1, 61.2, 566.6) — the far
    //                    top of the ramp, ~100yd from gate 1's stand point — both
    //                    REACT_PASSIVE + UNIT_FLAG_NON_ATTACKABLE, each on a
    //                    MoveSplinePath down to a home 56yd (west pack) and 93yd
    //                    (lower pack) away
    //     events 31/32   poll every 500ms until that spline ENDS, then +3s
    //     event 33       THE UPPER 4-PACK: 2x 36840 + 2x 36893 around
    //                    (909-921, 63-89, 548-559)
    //     event 34       THE LOWER 4-PACK: the same four around
    //                    (877-889, 41-66, 521-534) — 20yd up-slope of gate 1
    //     events 35/36   +3500ms after each pack: that pack's Deathbringer goes
    //                    REACT_AGGRESSIVE and loses NON_ATTACKABLE, while the pack
    //                    itself flips on its own `smart_scripts` one-shot
    //                    (AI Init -> passive, 3500ms update -> aggressive)
    //
    // So the ambush is real ~15-20s after the progress bump, and for that whole
    // window the only thing a wave scan can see is two passive Deathbringers at the
    // TOP OF THE RAMP. A driver that steers at the nearest live wave mob steers at
    // exactly those: it marches the party up a ramp, past the ground both 4-packs
    // are about to spawn on, at a mob it cannot attack.
    //
    // The fix is to hold until the wave is ARMED — alive, attackable and
    // REACT_AGGRESSIVE — and let the ambush come to the party at the bottom of the
    // ramp, which is what the encounter is shaped for.
    //
    // WAVE1_ARMED_QUORUM IS 8 BECAUSE THAT IS "BOTH 4-PACKS", and the count says so
    // unambiguously without the driver having to know which mob belongs to which
    // pack. The arming order fixes the arithmetic: the upper pack arms together
    // with its Deathbringer (4 + 1 = 5), the lower pack with its own (another 5).
    // Five is one pack; eight cannot be reached with either pack missing, and is
    // still met if a single Deathbringer fails to summon.
    constexpr uint32 WAVE1_ARMED_QUORUM = 8;

    // ...and the cascade above is ~15-20s long, so this is the budget after which
    // the driver stops waiting for it and closes anyway. It is a DEADLOCK BREAKER,
    // not a schedule: a Deathbringer that never reaches its home leaves events
    // 31/32 polling for ever and NEITHER pack is ever summoned, and a party held on
    // a quorum that can no longer be met would stand at gate 1 until the run's own
    // timeout. Measured from the phase clock — which is stamped on the progress
    // bump, the first link of the chain — so it covers the whole cascade and not
    // just the part after the first summon lands.
    constexpr uint32 WAVE1_ARM_MS = 45000;

    // --- the gate re-arm watchdog -------------------------------------------
    //
    // The driver's own forge is LEVEL-triggered — it calls
    // HandleAreaTriggerOpcode every tick — so, unlike the test harness's relay, it
    // cannot burn an edge and cannot need re-arming for the ordinary refusals
    // (the orchestrator not yet in place, killsLeft not yet zero). Those simply
    // keep being refused until they are not.
    //
    // What this budget catches is the OTHER shape: standing inside the sphere,
    // forging every tick, and the counter never moving — an orchestrator that is
    // dead or wedged, or a wave mob that despawned instead of dying and left
    // killsLeft stuck forever. There is no way to distinguish those from here, so
    // the driver says so ONCE, loudly, and keeps forging; the event's own timeout
    // is what eventually stalls the run and puts a human on it. A silent
    // forever-loop is the failure this exists to make visible.
    constexpr uint32 GATE_STALL_MS = 45000;

    // --- Tyrannus's ledge (event 2 / OBJ(1)) --------------------------------
    //
    // TWO POINTS, and the split is the whole design of this event.
    //
    // LEDGE_* is the objective anchor and the GATHER point: on the walkable strip
    // the freed slaves are placed on (TSData spans x 1044-1059, y 118-131), 73yd
    // from areatrigger 5633's centre — i.e. 22yd OUTSIDE its 51.5yd sphere. The
    // party can therefore form up here without tripping anything.
    //
    // ARENA_* is where the trigger is actually FIRED from, and it has to exist
    // because 5633 is a sphere and a forge from outside it is a no-op: the core
    // re-tests Player::IsInAreaTriggerRadius on the packet, so the leader must be
    // physically within 51.5yd of the centre. It is a probed on-mesh point on the
    // approach line from the ledge to the arena, inside the sphere and well inside
    // the boss's own leash box.
    constexpr float LEDGE_X = 1050.00f;
    constexpr float LEDGE_Y = 120.00f;
    constexpr float LEDGE_Z = 628.46f;
    constexpr float LEDGE_ARRIVE = 6.0f;

    constexpr float ARENA_X = 1030.00f;
    constexpr float ARENA_Y = 148.00f;
    constexpr float ARENA_Z = 628.20f;
    constexpr float ARENA_LEASH = 4.0f;

    // THE HARD LEASH, and the run-ender it describes.
    //
    // boss_tyrannusAI::UpdateAI checks, EVERY TICK, that its VICTIM is within
    // 100yd of TSDistCheckPos and within +/-20yd of its z. Outside either, the boss
    // full-heals and EnterEvadeMode()s — which despawns Gorkun, clears
    // DATA_MARTIN_OR_GORKUN_GUID, calls DespawnOnEvade() on RIMEFANG and despawns
    // Tyrannus himself. Recovery then needs Rimefang to respawn AND a fresh trip
    // of areatrigger 5633.
    //
    // The gather quorum in hook 30 is the primary defence: the encounter starts by
    // AttackStart()ing the NEAREST PLAYER within 100yd, so a straggler that is
    // that player and then walks back down the tunnel evades the whole fight.
    constexpr float LEASH_X      = 1009.29f;
    constexpr float LEASH_Y      = 163.15f;
    constexpr float LEASH_Z      = 628.157f;
    constexpr float LEASH_RADIUS = 100.0f;
    constexpr float LEASH_ZBAND  = 20.0f;

    // --- gather --------------------------------------------------------------
    //
    // THREE OF FOUR FOLLOWERS, not four of four, and the difference is deliberate:
    // one bot that cannot path in must never hold the other three, and stranded
    // recovery (DcRel::StrandedRecovery, 42) sits above this whole ladder and owns
    // that member. Dead members do not count against it either — the party view
    // counts only the LIVING — so the gate opens as soon as the party is up, and
    // the rez ladder, not this hook, is what gets it there.
    constexpr float GATHER_RADIUS = 20.0f;
    constexpr float GATHER_QUORUM = 0.75f;

    // --- doors ---------------------------------------------------------------
    //
    // GO 201885 ICE WALL, at (932.27, -80.67, 591.68) — a real
    // GAMEOBJECT_TYPE_DOOR with lockId 0 and autoCloseTime 0, spawned state 1
    // (SHUT), opened ONCE by instance_pit_of_saron the moment Garfrost and Ick are
    // both DONE. It stands on the wave-2 ground the party fights on, ~9yd from the
    // near wave-2 spawn line, so a designed leg passes well inside
    // DungeonClearBlockingDoorValue's 12yd same-floor band.
    //
    // By the time any of this runs it is already open — the gauntlet's own
    // predicate requires both bosses DONE — so the row is belt-and-braces, and it
    // is IsScriptOnly rather than IsNavigationIgnored on purpose: a run that DOES
    // pause here has regressed somewhere the module should be told about, and
    // hiding the door from navigation would mask that rather than prevent it.
    constexpr uint32 GO_ICE_WALL = 201885;

    // The Halls of Reflection portcullis, opened by the Tyrannus outro. Named only
    // so nobody authors it as a gate: the run ends before it matters.
    constexpr uint32 GO_HOR_PORTCULLIS = 201848;

    // --- event and hook ids ---------------------------------------------------
    //
    // Event ids are per-map. HOOK ids are ONE FLAT SPACE across every dungeon
    // (ObjectiveHookRegistry::AddHook LOG_ERRORs a collision rather than silently
    // dropping one): 1-14 are the older dungeons', 15-19 the Violet Hold's, 20-21
    // Blackwing Lair's, 22-23 Halls of Stone's, 24 Halls of Lightning's and 25-28
    // Utgarde Pinnacle's, so this map takes 29-30.
    constexpr uint32 EVENT_GAUNTLET       = 1;
    constexpr uint32 EVENT_TYRANNUS_LEDGE = 2;

    constexpr uint32 HOOK_POS_GAUNTLET       = 29;
    constexpr uint32 HOOK_POS_TYRANNUS_LEDGE = 30;

    // --- step timeouts --------------------------------------------------------
    //
    // Both bound a WAIT, and neither is a target.
    //
    // The gauntlet's budget has to cover the Krick outro (85-100s, DERIVED from
    // EventMap schedules plus a flight-speed estimate and therefore the least
    // trustworthy number on this page), two fights, three walks and three forges.
    // Six minutes is a ceiling on a genuinely broken attempt; the ARM state does
    // not depend on the estimate at all (it watches the orchestrator's real
    // position), so a slow outro costs seconds of the budget, not the run.
    constexpr uint32 GAUNTLET_TIMEOUT_MS = 360000;

    // The ledge is a gather, a 30yd walk-in and a forge. Ninety seconds is the
    // gather's own worst case — four followers walking in from a fight that ended
    // up the tunnel — plus the trigger.
    constexpr uint32 LEDGE_TIMEOUT_MS = 90000;

    // Throttle on the two drivers' per-tick telemetry lines.
    constexpr uint32 TELEMETRY_MS = 3000;
}

void RegisterPitOfSaronEvents(std::vector<DungeonEvent>& out);

// --- Halls of Reflection (map 668) ---------------------------------------
//
// THE MOST SCRIPT-DRIVEN 5-MAN IN WOTLK, and the first map in this module where
// DC's pull/advance pipeline is the WRONG TOOL for most of the dungeon rather
// than merely a tool that needs help.
//
// Three properties shape everything below.
//
//   1. NOTHING IS PULLABLE. Falric, Marwyn and all 34 wave mobs are PRE-SPAWNED
//      invisible, UNIT_FLAG_NOT_SELECTABLE and SetImmuneToAll. The instance
//      activates them one wave at a time and THEY attack — on the FARTHEST
//      player. There is no trash to clear and no boss to pull between the first
//      gossip and Marwyn's death, so a conditional driver owns the party for
//      that whole stretch (event 2) and the derived Falric/Marwyn anchors are
//      outranked while it does. tr-20260907-100815-2 is what happens without
//      it: the tank routed past Jaina to Marwyn's invisible, immune spawn and
//      stood on him for five minutes.
//
//   2. THE MAP HAS A HARD LEASH WITH A SILENT, EXPENSIVE FAILURE MODE. While a
//      wave is live, ANY non-GM player more than MAX_DIST_FROM_CENTER_IN_COMBAT
//      (70.5yd, 2D) from CenterPos wipes the event and replays waves 1-4 with
//      every dead mob respawned. The front door is 65yd from centre and the
//      entrance is 103. A bot that RELEASES during waves is locked out
//      (Map::CannotEnter special-cases 668 through IsEncounterInProgress) until
//      the survivors wipe or kill Marwyn.
//
//   3. THE LICH KING (36954) IS A PLAIN HOSTILE CREATURE WITH unit_flags 0. No
//      immunity flag protects him, he force-flags every player into combat every
//      second, and stock playerbots' assist triggers WILL attack him. He heals
//      himself to 75% below 70%, so the damage is pure waste — but the real cost
//      is movement: one bot holding him as a victim makes DcCombatFlag::IsEngaged
//      true for the party and freezes every MayDrive rung with a Lich King
//      walking 1.4yd/s behind. See NPC_LICH_KING and the three registry rows
//      that keep the party off him (DcTargetExclusionRegistry with alsoTank,
//      DcNeverTargetRegistry, and the widened hold-fire rung).
namespace DcHallsOfReflection
{
    constexpr uint32 MAP_ID = 668;

    // --- the roster ---------------------------------------------------------
    //
    // TWO derived bosses and one patched-in row. Falric and Marwyn have
    // instance_encounters rows (839/840 and 841/842) AND `creature` spawns, so
    // BossSpawnIndex derives both correctly — this map does NOT have Pit of
    // Saron's silent-short-roster defect.
    //
    // The escape does not derive, and cannot: "Escaped from Arthas" has
    // DungeonEncounter.dbc rows 843/844 but NO instance_encounters row at all, so
    // there is no credit entry to join on and no completion BIT to read. Its only
    // observable completion is GetBossState(DATA_LICH_KING) == DONE, which
    // npc_hor_lich_kingAI sets when he reaches the last waypoint with all four
    // walls down. That is the Nexus Frozen Commander shape: MakeBoss(...,
    // doneBossStateIndex = DATA_LICH_KING), which parks encounterIndex at 64 so
    // the completed-mask check never consults a bit that does not exist.
    //
    // SO encounterMask 0x3 IS THE EXPECTED FINAL MASK on a fully cleared run.
    // There is no third bit and a run that reports one has read something else.
    constexpr uint32 NPC_FALRIC = 38112;
    constexpr uint32 NPC_MARWYN = 38113;

    constexpr uint32 BIT_FALRIC = 0;
    constexpr uint32 BIT_MARWYN = 1;

    // The escape Lich King — a REAL creature with a spawn row at
    // (5552.77, 2262.57, 733.01), faction 2102, unit_flags 0, speed_run 0.86,
    // HealthModifier 2000, NullCreatureAI. See property 3 above.
    constexpr uint32 NPC_LICH_KING = 36954;

    // The INTRO Lich King (37226), the one that walks into the throne room during
    // the gossip cutscene and later stands frozen. Immune and passive throughout,
    // so AttackersValue already rejects him — he is named here only so the
    // never-target rows can be complete, and so nobody confuses the two entries.
    constexpr uint32 NPC_LICH_KING_INTRO = 37226;

    // Uther (37225) — the RP ghost at the altar during the intro. Never hostile;
    // a never-target row so no DC scan ever proposes him.
    constexpr uint32 NPC_UTHER = 37225;

    // THE PART-1 LEADER'S INSTANCE GUID KEY IS ALWAYS 37223, ON BOTH FACTIONS,
    // and this is the single most useful fact in this block. instance_halls_of
    // _reflection::OnCreatureCreate runs InstanceScript::OnCreatureCreate (which
    // stores the object under its CURRENT entry, 37223) BEFORE it UpdateEntry()s
    // the creature to 37221 for an Alliance instance. So GetGuidData(37223)
    // resolves the leader whichever faction is inside, and a lookup on 37221
    // resolves nothing at all. Same shape for part 2: the key is 37554.
    constexpr uint32 NPC_LEADER_PART1     = 37223;  // Sylvanas; Jaina 37221 on Alliance
    constexpr uint32 NPC_LEADER_PART1_ALT = 37221;
    constexpr uint32 NPC_LEADER_PART2     = 37554;  // Sylvanas; Jaina 36955 on Alliance
    constexpr uint32 NPC_LEADER_PART2_ALT = 36955;

    // The Frostsworn General and his Spiritual Reflections. NOT an encounter —
    // he has no DungeonEncounter row and no kill bit; his death sets the
    // persistent flag that makes the throne-room areatrigger live. Mandatory
    // regardless: at_hor_shadow_throne refuses while that flag is clear.
    constexpr uint32 NPC_FROSTSWORN_GENERAL   = 36723;
    constexpr uint32 NPC_SPIRITUAL_REFLECTION = 37068;

    // The four Ice Wall Targets. The Ice Wall GO (201385) is SUMMONED at these
    // and never DB-spawned, so the targets are the only author-time handle on
    // where a wall will stand. Never-targeted: they are invisible world triggers.
    constexpr uint32 NPC_ICE_WALL_TARGET = 37014;

    // THE 34 PRE-SPAWNED WAVE MOBS, by entry and DB count:
    //   38172 Phantom Mage        x6     38173 Spectral Footman   x10
    //   38175 Ghostly Priest      x6     38176 Tortured Rifleman  x6
    //   38177 Shadowy Mercenary   x6
    // Every one of them exists from map load, hidden and immune, and is activated
    // exactly once across the ten waves. That is why the wave driver counts only
    // SELECTABLE, non-IMMUNE_TO_PC members of this list: a bare aliveness probe
    // would read "34 wave mobs are up" before the intro has even started.
    constexpr uint32 NPC_WAVE_MAGE      = 38172;
    constexpr uint32 NPC_WAVE_FOOTMAN   = 38173;
    constexpr uint32 NPC_WAVE_PRIEST    = 38175;
    constexpr uint32 NPC_WAVE_RIFLEMAN  = 38176;
    constexpr uint32 NPC_WAVE_MERCENARY = 38177;

    // The Phantom Mage's 40s summon. It does NOT count toward the wave clear and
    // despawns on its own, so it is deliberately absent from the wave probe list.
    constexpr uint32 NPC_PHANTOM_HALLUCINATION = 38567;

    // The escape adds, summoned AT THE LICH KING in four batches and sent to the
    // leader's current stop. JustSummoned puts 1000 threat on — and AttackStart()s
    // — the player NEAREST THE LICH KING, so the rearmost bot takes every opener.
    constexpr uint32 NPC_RAGING_GHOUL         = 36940;
    constexpr uint32 NPC_RISEN_WITCH_DOCTOR   = 36941;
    constexpr uint32 NPC_LUMBERING_ABOMINATION = 37069;

    // --- gameobjects --------------------------------------------------------
    //
    // All four are script-only: the instance opens and shuts them and a bot
    // clicking one would fight the script for the state. The front door in
    // particular closes at the END OF THE INTRO and again for every wave, which
    // is exactly when the party is standing behind it by design.
    constexpr uint32 GO_FRONT_DOOR         = 201976;  // (5264.61, 1959.44)
    constexpr uint32 GO_ARTHAS_DOOR        = 197341;  // (5358.96, 2058.75)
    constexpr uint32 GO_DOOR_BEFORE_THRONE = 197342;  // (5520.77, 2229.04)
    constexpr uint32 GO_DOOR_AFTER_THRONE  = 197343;  // (5582.81, 2230.62)
    constexpr uint32 GO_ICE_WALL           = 201385;  // summoned at the wall targets

    // NOT a door, despite a GAMEOBJECT_TYPE_DOOR template: the Frostmourne
    // altar dais, spawned shut and never scripted, standing 0.13yd from the
    // CenterPos that Leg A of the route begins at. Navigation-ignored in
    // DcEventDoorRegistry; see the row there for what leaving it unlisted cost.
    constexpr uint32 GO_FROSTMOURNE_ALTAR  = 202236;  // (5309.34, 2006.52)

    // And the SWORD standing in it — a second GAMEOBJECT_TYPE_DOOR 0.03yd from
    // the altar's origin. This one IS scripted, but only ever shut: the instance
    // closes it on create, the intro opens it for the cutscene, and the end of
    // the Lich King intro closes it and phases it to mask 2. Left unlisted it
    // simply inherits the altar's old job of auto-pausing every run the moment
    // Leg A is seeded. Navigation-ignored alongside the altar.
    constexpr uint32 GO_FROSTMOURNE        = 202302;  // (5309.36, 2006.55)

    // The two doors an authored leg passes, so the route rows can declare them
    // rather than meet them. Both are opened by the instance well before the
    // party arrives (the Arthas door on Marwyn's death; the west throne door is
    // spawned open and never scripted), so the DOOR_AHEAD flags are belt-and-
    // braces — but a run that DOES pause at one has regressed, and a declared
    // door is what makes that legible instead of a silent auto-pause.
    constexpr float GO_ARTHAS_DOOR_X = 5358.96f;
    constexpr float GO_ARTHAS_DOOR_Y = 2058.75f;
    constexpr float GO_ARTHAS_DOOR_Z = 707.724f;

    // --- instance data slots, HAND-COPIED from halls_of_reflection.h ---------
    //
    // `enum Data` is one enum holding THREE unrelated key spaces — boss-state
    // indices, GetData/SetData slots and DoAction ids — with MAX_ENCOUNTER
    // occupying slot 3 in the middle of it. Never hand-count it; these are the
    // values the compiler assigns, transcribed.
    //
    //   DATA_FALRIC 0 · DATA_MARWYN 1 · DATA_LICH_KING 2 · MAX_ENCOUNTER 3
    //   DATA_INTRO 4 · DATA_FROSTSWORN_GENERAL 5 · DATA_BATTERED_HILT 6 (= for SAI)
    //   DATA_LK_INTRO 7 · DATA_WAVE_NUMBER 8 · DATA_LK_BATTLE 9 · DATA_SHIP_CAPTAIN 10
    //   ACTION_SHOW_TRASH 11 ... ACTION_DELETE_ICE_WALL 17
    //
    // AND THESE FOUR ARE ALSO GetGuidData KEYS FOR FALRIC AND MARWYN, which is the
    // one place this map's two lookup conventions differ and is worth stating
    // where both are visible.
    //
    // InstanceScript::LoadObjectData stores `objectInfo[entry] = type` and
    // OnCreatureCreate files the guid under the TYPE — so GetGuidData()'s key is
    // the SECOND column of instance_halls_of_reflection's creatureData table. Most
    // of this map's rows repeat the entry there ({ NPC_SYLVANAS_PART1,
    // NPC_SYLVANAS_PART1 }, and likewise the part-2 leader, the Lich King and the
    // Frostsworn General), which is why every other lookup in this module passes
    // an entry. Falric and Marwyn are the exceptions — { NPC_FALRIC, DATA_FALRIC }
    // and { NPC_MARWYN, DATA_MARWYN } — so they are filed under 0 and 1, and a
    // lookup on 38112 returns ObjectGuid::Empty for ever.
    constexpr uint32 DATA_FALRIC      = 0;
    constexpr uint32 DATA_MARWYN      = 1;
    constexpr uint32 DATA_LICH_KING   = 2;
    constexpr uint32 DATA_WAVE_NUMBER = 8;

    // GetData(DATA_BATTERED_HILT) returns _batteredHiltStatus, which the leader
    // gossip does NOT touch — SetData(DATA_BATTERED_HILT, 1) only stores the
    // PERSISTENT flag. So this is NOT a usable "somebody already gossiped" probe;
    // PERSISTENT_DATA_BATTERED_HILT below is. Named so nobody reaches for it.
    constexpr uint32 DATA_BATTERED_HILT = 6;

    // The persistent-data vector (SetPersistentDataCount(4)). All four survive a
    // map reload, which is what makes them safe to read as one-way progress.
    constexpr uint32 PERSISTENT_DATA_INTRO              = 0;
    constexpr uint32 PERSISTENT_DATA_FROSTSWORN_GENERAL = 1;
    constexpr uint32 PERSISTENT_DATA_LK_INTRO           = 2;
    constexpr uint32 PERSISTENT_DATA_BATTERED_HILT      = 3;

    // --- the intro gossip ---------------------------------------------------
    //
    // Menu 11031 (Jaina) / 10950 (Sylvanas), two options, and the requirement is
    // on the SELECTING BOT rather than on the instance:
    //
    //   position 0  "Can you remove the sword?"   quest 24710 (A) / 24712 (H)
    //               -> ACTION_START_INTRO, the full 224.5s / 209s cutscene
    //   position 1  "...I think I hear Arthas"    quest 24500 (A) / 24802 (H)
    //               -> ACTION_SKIP_INTRO, the 75.5s version
    //
    // 24710/24712 is ALSO the dungeon_access_requirements row, so
    // DcDungeonAccess::GrantEntry already rewards it for every bot it teleports —
    // which is what makes option 0 always available in a harness run. It
    // additionally rewards 24500/24802 for this map so the SKIP is available too;
    // see DcDungeonAccess.cpp for why that is a test-harness convenience and not
    // a behaviour change for an ordinary party.
    //
    // Option positions, not OptionIDs: DungeonEventExecutor::ResolveGossipListId
    // translates. The two happen to coincide here (the DB rows are OptionID 0 and
    // 1) but the skip option is ABSENT for a bot without the second quest, so
    // position 1 must only ever be asked for when that quest is rewarded.
    constexpr int32 GOSSIP_OPTION_INTRO_FULL = 0;
    constexpr int32 GOSSIP_OPTION_INTRO_SKIP = 1;

    constexpr uint32 QUEST_ACCESS_ALLIANCE = 24710;  // Deliverance from the Pit
    constexpr uint32 QUEST_ACCESS_HORDE    = 24712;
    constexpr uint32 QUEST_SKIP_ALLIANCE   = 24500;  // Wrath of the Lich King
    constexpr uint32 QUEST_SKIP_HORDE      = 24802;

    // --- geometry: the wave phase -------------------------------------------
    //
    // CenterPos, verbatim from halls_of_reflection.h. Every leash on this map is
    // measured from it in 2D.
    constexpr float CENTER_X = 5309.459473f;
    constexpr float CENTER_Y = 2006.478516f;
    constexpr float CENTER_Z = 711.595459f;

    // MAX_DIST_FROM_CENTER_IN_COMBAT / MAX_DIST_FROM_CENTER_TO_START, verbatim.
    // The first wipes the event; the second is what the AUTOMATIC restart waits
    // for (every non-GM player alive and inside it). There is no gossip to
    // re-start the waves — standing in the right place is the whole mechanism.
    constexpr float LEASH_COMBAT  = 70.5f;
    constexpr float LEASH_RESTART = 40.0f;

    // WHERE THE PARTY HOLDS THE ALTAR, and it is deliberately not CenterPos.
    //
    // Three constraints meet here. The RP actors (the leader, Uther and the intro
    // Lich King) stand ON the altar for the whole intro and the party must not be
    // inside that. Marwyn's Well of Corruption (72362) drops a 3yd void zone under
    // a random member and somebody has to be able to step out of it. And the point
    // has to sit far enough inside the 70.5yd leash that a 4s Defiling Horror fear
    // (~28yd) plus a Circle of Destruction knockback (10yd) cannot carry a feared
    // bot past it.
    //
    // 19.8yd from CenterPos on the ENTRANCE side satisfies all three: 50.7yd of
    // leash margin, off the altar, and inside LEASH_RESTART so the automatic
    // wave restart is satisfied by the party simply being here.
    constexpr float CAMP_X = 5296.00f;
    constexpr float CAMP_Y = 1992.00f;
    constexpr float CAMP_Z = 709.30f;
    constexpr float CAMP_LEASH = 6.0f;

    // Grid-scan radius for the wave probe, from the leader standing at the camp.
    // The pre-spawn ring runs x 5275..5344, y 1972..2043 — 45yd out from centre
    // at its widest, so ~65yd from the camp. 100 covers it with room for a mob
    // that has walked at a straggler.
    constexpr float WAVE_SCAN = 100.0f;

    // --- geometry: the anchors ----------------------------------------------
    //
    // OBJ(1), the intro. SpawnPos — where the leader walks to and offers the
    // gossip — is (5263.22, 1950.96, 707.70); this anchor is 7.2yd short of it on
    // the entrance side, so ARRIVING puts the tank in interact range without
    // standing inside the NPC.
    constexpr float INTRO_X = 5258.00f;
    constexpr float INTRO_Y = 1946.00f;
    constexpr float INTRO_Z = 707.70f;
    constexpr float INTRO_ARRIVE = 6.0f;

    // OBJ(2), the Frostsworn General. His spawn is (5413.92, 2116.50, 707.70) and
    // he EVADES IF DRAGGED MORE THAN 30YD FROM HOME, so the anchor sits 28.6yd
    // short of him in the corridor — close enough that arriving is the approach,
    // far enough that the arrival itself is not already inside the fight.
    constexpr float GENERAL_X = 5395.00f;
    constexpr float GENERAL_Y = 2095.00f;
    constexpr float GENERAL_Z = 707.70f;
    constexpr float GENERAL_ARRIVE = 8.0f;

    // His home and his evade radius, for the FightInPlaceRegistry box.
    constexpr float GENERAL_HOME_X = 5413.92f;
    constexpr float GENERAL_HOME_Y = 2116.50f;
    constexpr float GENERAL_HOME_Z = 707.695f;
    constexpr float GENERAL_EVADE_RADIUS = 30.0f;

    // OBJ(3), the throne room. The anchor is the WEST door (197342), which is
    // spawned open and never scripted — and, crucially, OUTSIDE areatrigger
    // 5605's box, so arriving here cannot fire the cutscene before the party has
    // formed up.
    constexpr float THRONE_X = 5520.00f;
    constexpr float THRONE_Y = 2229.00f;
    constexpr float THRONE_Z = 733.00f;
    constexpr float THRONE_ARRIVE = 8.0f;

    // --- areatrigger 5605, at_hor_shadow_throne -----------------------------
    //
    // A BOX, not a sphere (AreaTrigger.dbc box_length / box_width / box_height /
    // box_yaw are set and radius is 0), 20.5 x 94.5 x 72.1 at yaw 3.942 across
    // the throne room's mouth. The hook forges from a probed on-mesh point at its
    // centre.
    //
    // IT IS SAFE TO CROSS EARLY. at_hor_shadow_throne no-ops unless
    // PERSISTENT_DATA_FROSTSWORN_GENERAL is set and PERSISTENT_DATA_LK_INTRO is
    // not, and crossing it while either is wrong consumes NOTHING — re-crossing
    // later works. That is the opposite of Pit of Saron's gate spheres and it is
    // why this hook needs no arming state: the forge is level-triggered and the
    // server side is level-safe too.
    constexpr uint32 AREATRIGGER_THRONE = 5605;
    constexpr float  AT_THRONE_X = 5539.65f;
    constexpr float  AT_THRONE_Y = 2247.05f;
    constexpr float  AT_THRONE_Z = 733.01f;
    constexpr float  AT_THRONE_LENGTH = 20.5f;
    constexpr float  AT_THRONE_WIDTH  = 94.5f;
    constexpr float  AT_THRONE_HEIGHT = 72.1f;
    constexpr float  AT_THRONE_YAW    = 3.942f;

    // The probed stand point the hook forges from — the box centre, snapped.
    constexpr float FORGE_X = 5539.65f;
    constexpr float FORGE_Y = 2247.05f;
    constexpr float FORGE_Z = 733.01f;
    constexpr float FORGE_LEASH = 5.0f;

    // --- geometry: the escape ------------------------------------------------
    //
    // LeaderEscapePos — where Jaina/Sylvanas stands after the freeze cutscene and
    // where her escape gossip is offered. The party walks PAST the frozen Lich
    // King to reach her; he sits 36yd south-west of it.
    constexpr float LEADER_ESCAPE_X = 5576.80566f;
    constexpr float LEADER_ESCAPE_Y = 2235.55004f;
    constexpr float LEADER_ESCAPE_Z = 733.012268f;

    // The frozen Lich King's spawn, i.e. where he stands until the gossip.
    constexpr float LK_SPAWN_X = 5552.77f;
    constexpr float LK_SPAWN_Y = 2262.57f;
    constexpr float LK_SPAWN_Z = 733.012f;

    // Where the party musters before the point of no return. Three yards short of
    // the leader on the approach side, so the gather does not stand on her.
    constexpr float MUSTER_X = 5573.80f;
    constexpr float MUSTER_Y = 2235.55f;
    constexpr float MUSTER_Z = 733.01f;
    constexpr float MUSTER_LEASH = 4.0f;

    // THE ESCAPE PATH, verbatim from PathWaypoints[19] in halls_of_reflection.h,
    // and the six WP_STOP indices the leader actually stops at. The party's own
    // stand points are derived from these (STAND_* below), never from them
    // directly: the leader stops ~24yd short of each wall and the party must be
    // AHEAD of her, not on her.
    //
    // Direction of travel is -x AND -y for the whole run, which is what makes the
    // Lich King's own "is this player behind me" test — (p.x - lk.x) + (p.y -
    // lk.y) > 20 — a single scalar rather than a bearing.
    constexpr uint32 PATH_WP_COUNT = 19;
    constexpr uint32 STOP_COUNT = 6;
    inline constexpr uint8 WP_STOP[STOP_COUNT] = { 0, 5, 8, 10, 14, 18 };

    struct HorPoint { float x, y, z; };

    inline constexpr HorPoint PATH_WAYPOINTS[PATH_WP_COUNT] = {
        { 5588.055664f, 2229.327393f, 733.011353f },  //  0  start (WP_STOP 0)
        { 5605.567383f, 2203.448486f, 731.304626f },  //  1
        { 5607.415039f, 2189.225098f, 731.022217f },  //  2
        { 5598.958984f, 2169.660156f, 730.919800f },  //  3
        { 5586.018066f, 2149.685303f, 731.090759f },  //  4
        { 5558.182617f, 2103.950928f, 731.263000f },  //  5  stop 1 (ice wall 1)
        { 5534.202637f, 2054.254150f, 731.131165f },  //  6
        { 5526.244629f, 2023.878540f, 732.408264f },  //  7
        { 5513.573242f, 1996.611206f, 735.115723f },  //  8  stop 2 (ice wall 2)
        { 5478.590820f, 1938.773315f, 741.926697f },  //  9
        { 5456.632324f, 1902.801025f, 747.220886f },  // 10  stop 3 (ice wall 3)
        { 5423.630371f, 1858.672363f, 754.901367f },  // 11
        { 5402.314453f, 1829.705811f, 758.029907f },  // 12
        { 5374.380371f, 1802.807007f, 760.831238f },  // 13
        { 5340.560059f, 1772.791016f, 766.478149f },  // 14  stop 4 (ice wall 4)
        { 5318.707031f, 1750.379395f, 771.635132f },  // 15
        { 5297.951660f, 1725.419067f, 778.211548f },  // 16
        { 5279.251953f, 1697.474365f, 785.700256f },  // 17
        { 5262.773926f, 1669.980103f, 784.301697f },  // 18  the end (WP_STOP 5)
    };

    // The four Ice Wall Targets, in wall order — their live `creature` rows. The
    // wall GO is summoned ON these, so they are also where the driver looks for
    // the GO when it asks whether a wall has opened.
    inline constexpr HorPoint ICE_WALL_TARGETS[4] = {
        { 5550.62f, 2079.75f, 731.715f },
        { 5504.20f, 1974.70f, 737.318f },
        { 5445.09f, 1881.48f, 752.654f },
        { 5321.39f, 1758.07f, 770.419f },
    };

    // WHERE THE PARTY STANDS AT EACH WALL, and this is the one piece of authored
    // geometry on this map that the whole escape rides on.
    //
    // Each point is its WP_STOP, offset STAND_AHEAD_YD further along the bearing
    // to that wall's Ice Wall Target — i.e. a few yards AHEAD of the leader, on
    // the far side of her from the Lich King. Three things fall out of that:
    //
    //   * the party is never inside the 12.5yd circle the CATCH is measured in
    //     (that check is on the LEADER, not on players, so the only thing a
    //     player can do about it is clear the wall in time);
    //   * the party is never BEHIND him in the (x + y) sense, so the 2-second
    //     10 000-damage Zap (70653) never fires;
    //   * the adds, which spawn AT the Lich King and run to the LEADER'S stop,
    //     arrive from behind the party and are fought facing back down the path.
    //
    // The offset is small on purpose. The leader stops 24-25yd short of each wall
    // and the wall itself blocks further movement, so there is room for ~5yd and
    // no more; and standing further forward would put the witch doctors (20yd
    // shadow bolt, cast from the Lich King side) further from the melee.
    constexpr float STAND_AHEAD_YD = 5.0f;

    // Derived offline from WP_STOP + STAND_AHEAD_YD along the bearing to each
    // wall target, then snapped to the mesh by t/TestHallsOfReflectionRouteProbe
    // (which re-asserts both the offset and the on-mesh property, so an mmaps
    // regen that moves this corridor trips red rather than parking the party
    // inside a wall).
    inline constexpr HorPoint STAND_POINTS[4] = {
        { 5556.69f, 2099.18f, 731.35f },  // wall 1
        { 5511.61f, 1992.01f, 735.58f },  // wall 2
        { 5454.25f, 1898.40f, 748.34f },  // wall 3
        { 5336.59f, 1769.75f, 767.29f },  // wall 4
    };

    // How close to a stand point counts as "there". Generous: this is a party
    // holding ground, not a trigger to be stood inside.
    constexpr float STAND_LEASH = 6.0f;

    // ...AND HOW FAR OFF IT COUNTS AS "GONE". The pair is a SCHMITT TRIGGER and
    // the gap between them is the whole point: arriving takes STAND_LEASH,
    // leaving takes this.
    //
    // Measured on tp-20260907-221612-1, where the escape driver had only the one
    // radius. At wall 2 the tank sat on the boundary and flipped Advance <->
    // Fight every one to two seconds for fifty-six seconds — distToStand
    // 6.7 -> 5.0 -> 6.7 -> 5.0, fifty-seven transitions in one run, nine to
    // fifty-seven in each of the other nine. Each Advance re-issued a TravelTo
    // that cancelled the bear's melee approach; each Fight handed the tick back
    // to the combat engine, which walked it 1.7yd off the stand point and
    // re-armed Advance. Three summons stayed alive for the entire 105 seconds
    // that took, the wall never opened, and the leader was caught at stop 2.
    //
    // TWELVE YARDS WAS NOT ENOUGH, and tp-20260907-232556-1 measured by how much.
    // The hysteresis did kill the one-to-two-second boundary flutter, but the
    // thing MoveChase is chasing is a Risen Witch Doctor, and a witch doctor
    // stops at 20yd to cast — on the Lich King's side of the party, by design.
    // So the tank's melee approach is a TWENTY-FIVE YARD walk, not a chase step:
    // at stand 3 it ran 0.1 -> past 12 -> Advance -> travel back to 0.1 and round
    // again, fifteen times per run, with distToStand peaking at 24.7yd. Each
    // Advance claims the tick and re-plots the spline, so roughly half of every
    // wall fight was spent NOT fighting — which is why three summons survived a
    // hundred seconds and the wall never opened.
    //
    // Thirty yards is that measured approach plus margin. It is safe on both
    // sides: a real leg to the next wall is 110-180yd and is outside this within
    // one tick of setting off, drift FORWARD into the slab is caught by Recenter
    // (which fires only while atStand, so a wider radius covers MORE of it), and
    // drift BACKWARD toward him is caught by Pressure at LK_PRESSURE_DIST, which
    // outranks every other state.
    constexpr float STAND_LEAVE_LEASH = 30.0f;

    // HOW FAR FORWARD OF THE STAND POINT THE TANK MAY DRIFT while the wall in
    // front of it is still shut.
    //
    // The stand points sit 18.8-20.4yd out from the wall gameobject's origin and
    // are clear of it. The tank does not stay on them: yielding the whole of a
    // wall fight to the combat engine lets MoveChase walk it forward, and on the
    // same plan bots were logged 13.4yd from a SHUT wall's origin — five yards
    // past the stand point, inside the 2.5x-scaled slab, which is the clipping
    // the walls were reported for.
    //
    // Nothing is gained by that drift and the correction is free: the summons
    // spawn AT the Lich King and run at the party from BEHIND, so every yard back
    // toward the stand point is a yard TOWARD what the tank is meant to be
    // holding. Three yards of slack keeps the correction off a bot that is merely
    // strafing.
    constexpr float WALL_STANDOFF_SLACK = 3.0f;

    // HOW LONG THE TANK MAY SPEND PICKING UP A BATCH before it is made to walk
    // on, once the leader has moved to the next stop.
    //
    // The escape driver used to travel the instant her stop index changed, with
    // no regard for what was already on the party. On tp-20260907-221612-1 that
    // ran the tank 88 yards to stand 2 while eight summons were alive and on the
    // group — it arrived 153yd ahead of the Lich King, which is where they spawn,
    // and the three non-tanks died 12 to 26 seconds later to a Lumbering
    // Abomination and two Risen Witch Doctors that were never tanked.
    //
    // The fix is NOT to wait for the batch to die (the header on
    // DcHorEscapeDecision.h is right that the clock is absolute and finishing the
    // last ghoul at the old stop is wasted time). It is to take THREAT and then
    // walk: the summons chase whoever holds them, so a tank that leaves with the
    // batch on it drags them to the next wall, which is exactly where they were
    // going anyway. Six seconds is a taunt plus an AoE swing and cannot spend a
    // wall's margin even if the pickup never completes.
    constexpr uint32 GRAB_THREAT_MS = 6000;

    // HOW FAR INSIDE STAND_LEAVE_LEASH A DRIFTED TANK IS PULLED BACK — and the
    // reason it is not pulled back to the stand point at all.
    //
    // The Schmitt pair says a bot that has crossed STAND_LEAVE_LEASH is "gone".
    // What the driver then did with that was travel to the STAND POINT, whose
    // arrival leash is STAND_LEASH — so a tank that chased a witch doctor to
    // 30yd was walked the whole 24yd home, dropping the mob it was on, and then
    // walked back out to it. tp-20260908-225156-2, tr-...-15, wall 3: Fight at
    // 7.1yd -> 28.2 -> Advance -> back to 6 -> Fight -> 28.8 -> Advance, twice in
    // twenty seconds, each Advance re-plotting a spline over the melee approach.
    //
    // THE STAND POINT IS A LEASH, NOT A PARKING SPACE. Nothing about this fight
    // wants the tank on the exact point — the summons come to the party and the
    // only geometry that matters is "not behind him, not in his ring, not inside
    // the slab", all of which hold anywhere in the band. So a drifted tank is
    // returned to the NEAR EDGE of the band (DcTransit::HoldPoint, the same
    // near-edge rule Blackwing Lair's camp uses and for the same reason) and goes
    // straight back to swinging.
    //
    // Six yards inside 30 leaves a 24-to-30yd working band: wide enough that the
    // recall is not re-armed by the next chase step, narrow enough that the tank
    // is never the rearmost thing on the path. Both edges stay covered by rules
    // that outrank this one — Pressure at LK_PRESSURE_DIST going backward,
    // Recenter against the slab going forward.
    constexpr float STAND_EDGE_MARGIN = 6.0f;

    // The snap box for that edge point and the leash it arrives on. Same numbers
    // Blackwing Lair's pack hold uses, and for the same reasons: the chord has to
    // land on a polygon the pathfinder can name, and the arrival leash has to be
    // tighter than the margin or the rung hands the tick back while the bot is
    // still outside the band it was recalled from.
    constexpr float STAND_EDGE_SNAP_RADIUS = 3.0f;
    constexpr float STAND_EDGE_SNAP_TOLERANCE = 2.0f;
    constexpr float STAND_EDGE_ARRIVE_LEASH = 2.0f;

    // THE ARRIVAL LEASH FOR THE RECENTER STEP, and it has to be tight for a
    // reason that made the whole state a no-op.
    //
    // Recenter fires while the tank is AT the stand point (it is a forward drift
    // of a few yards inside the leash), and the driver issued it as
    // TravelTo(stand, STAND_LEASH). TravelTo returns without moving when the bot
    // is already inside the leash it is handed — and 6yd is wider than every
    // clipping episode there is, so the correction never once landed. On
    // tr-20260908-225156-15 the state fired 27 times and moved the tank nowhere:
    // logged at 1.4yd and 4.3yd from the stand, both comfortably inside 6.
    //
    // The step back is 3-6 yards, so the leash has to be smaller than that.
    constexpr float WALL_RECENTER_LEASH = 1.5f;

    // ...AND THE HYSTERESIS THAT STOPS IT CHATTERING. Arming at
    // standWallDist - WALL_STANDOFF_SLACK and disarming at the same number is one
    // threshold, so the state flipped Fight -> Recenter -> Fight every two to
    // three seconds for the whole of a wall. Recenter now HOLDS until the tank is
    // back within this of the stand point's own standoff, so the correction is
    // one step rather than twenty-seven.
    constexpr float WALL_STANDOFF_CLEAR = 1.0f;

    // --- THE IDLE TANK -------------------------------------------------------
    //
    // ENGAGE_REACH_YD / ENGAGE_MELEE_YD / ENGAGE_IDLE_MS. The escape driver's
    // header says this file never walks the tank at a hostile, on the grounds
    // that every activation here ends in SetInCombatWithZone plus an explicit
    // AttackStart so the fight always starts itself. That is true about STARTING
    // a fight and it is silent about this: the fight has started, the tank is
    // flagged, and the summon it has targeted is out of reach and beating on
    // somebody else.
    //
    // tr-20260908-225156-15, wall 4, 23:04:41 to 23:06:08 — EIGHTY-SEVEN SECONDS
    // with distToStand pinned at exactly 1.0yd and 9 adds falling to 2 without
    // the tank contributing a swing. The engine trace is unambiguous: 448 ticks
    // of "no actions executed", 399 melee prereq failures, 358 combat-formation
    // -move failures, and every rung IMPOSSIBLE with result 67 (out of range)
    // against a Risen Witch Doctor. Fight had yielded the tick, and there was
    // nothing left in the stock engine that closes 20 yards. The tank died at
    // 23:06:28 and the party wiped to the Lumbering Abomination it never held.
    //
    // So the driver takes the one step the engine cannot: when a summon is alive,
    // none of them is on this bot, and the nearest is between melee reach and
    // ENGAGE_REACH_YD, walk at it. Bounded on BOTH sides on purpose —
    //
    //   * inside ENGAGE_MELEE_YD the rotation can reach it, so steering there
    //     would only tear down the approach the engine is already running;
    //   * past ENGAGE_REACH_YD the add is not a straggler, it is still running in
    //     from the Lich King 200yd back, and walking at it is walking at him.
    //
    // TWENTY-FIVE AND NOT THIRTY, which is the number the geometry asks for
    // twice over. It is the witch doctor's 20yd cast range plus margin — the one
    // add this exists for — and it is STRICTLY inside STAND_LEAVE_LEASH, so a
    // pickup begun from the stand point can never end outside the band and hand
    // itself straight to the recall. (A pickup begun from mid-band still can,
    // and that is fine: the recall is then a six-yard correction to the edge,
    // not the twenty-four-yard round trip to the centre this change removed.)
    //
    // The three-second arm keeps it off the ordinary gap between one summon dying
    // and the next arriving, which is under a second.
    constexpr float  ENGAGE_REACH_YD = 25.0f;
    constexpr float  ENGAGE_MELEE_YD = 5.0f;
    constexpr uint32 ENGAGE_IDLE_MS  = 3000;

    // How near a WP_STOP the leader has to be before she counts as standing AT
    // it. Wider than a leash because the number it feeds is an INDEX, not a
    // position: she is either waiting at a stop or running between two of them,
    // and eight yards separates those cleanly without ever resolving to the wrong
    // one (the stops are 100-176yd apart).
    constexpr float LEADER_STOP_SNAP = 8.0f;

    // WHICH WP_STOP a point at (x, y) is at or heading for — the leader's own
    // position turned into the escape's state variable.
    //
    // currentWall is private to npc_hor_lich_kingAI and nothing exposes it, so
    // this is how many walls are down. It is also the number the party actually
    // needs, because the party stands ahead of HER and not ahead of him: she
    // waits at a stop until WallCompleted fires and then runs to the next one
    // immediately, so her position tracks the wall count with no lag worth
    // measuring.
    //
    // "The nearest stop within LEADER_STOP_SNAP, else the next one along the
    // path" — and because the path runs strictly -x AND -y, "the next one along"
    // is simply the first stop whose (x + y) is below hers. That resolves the
    // in-between case (running from stop 2 to stop 3) to the stop she is running
    // TO, which is where the party should be walking as well.
    //
    // Pure, so t/TestHallsOfReflection.cpp can pin it against the real waypoints.
    inline uint8 StopIndexNear(float x, float y)
    {
        uint8 best = 0;
        float bestDist = -1.0f;
        for (uint8 i = 0; i < STOP_COUNT; ++i)
        {
            HorPoint const& p = PATH_WAYPOINTS[WP_STOP[i]];
            float const dx = p.x - x;
            float const dy = p.y - y;
            float const d = dx * dx + dy * dy;
            if (bestDist < 0.0f || d < bestDist)
            {
                bestDist = d;
                best = i;
            }
        }

        if (bestDist >= 0.0f && bestDist <= LEADER_STOP_SNAP * LEADER_STOP_SNAP)
            return best;

        float const sum = x + y;
        for (uint8 i = 0; i < STOP_COUNT; ++i)
        {
            HorPoint const& p = PATH_WAYPOINTS[WP_STOP[i]];
            if (p.x + p.y < sum)
                return i;
        }
        return static_cast<uint8>(STOP_COUNT - 1);
    }

    // Where the party stands for a given target stop. 1..4 are the authored
    // points a few yards past each of the leader's wall stops; 5 is WP18 itself,
    // where the wall geometry is over and there is nothing left to be ahead of.
    inline HorPoint StandPointFor(uint8 targetStop)
    {
        if (targetStop >= 5)
            return PATH_WAYPOINTS[18];
        uint8 const idx = targetStop >= 1 ? static_cast<uint8>(targetStop - 1) : 0;
        return STAND_POINTS[idx];
    }

    // --- the two escape distances the driver acts on ------------------------
    //
    // LK_PRESSURE_DIST — Remorseless Winter (69780 -> 69781) deals 7068 +/- 863
    // frost EVERY SECOND to everything within 10yd of him. 16 is that radius plus
    // enough margin that a bot which is merely drifting is corrected before it is
    // taking damage, and it is well inside the 20yd caster range the witch doctors
    // fight from, so pulling a bot off one is a step and not an abandonment.
    //
    // LK_BEHIND_SUM — the core's own rule is (p.x - lk.x) + (p.y - lk.y) > 20.0,
    // checked every 2 seconds while Winter is up, and failing it costs 10 000
    // damage AND a knockback that throws the victim FURTHER BEHIND. 6 is a third
    // of the way to that line: the driver corrects long before the rule bites,
    // because the rule's own failure mode is self-reinforcing.
    constexpr float LK_PRESSURE_DIST = 16.0f;
    constexpr float LK_BEHIND_SUM    = 6.0f;

    // Remorseless Winter's keep-out for the hazard registry, and the aura the
    // driver reads to know the ring is live. 12 = the 10yd pulse plus 2 of slop.
    constexpr uint32 SPELL_REMORSELESS_WINTER = 69780;
    constexpr float  LK_RING_KEEPOUT = 12.0f;

    // Harvest Soul — cast on the LEADER when the Lich King catches her. Three
    // seconds later she dies and Fury of Frostmourne wipes the party. Nothing can
    // be done about it by then; the driver logs it once and stops pretending.
    constexpr uint32 SPELL_HARVEST_SOUL = 70070;

    // Marwyn's Well of Corruption — a 3yd persistent area aura under a random
    // player within 40yd, 8 seconds, applying +30% shadow damage taken. Exactly
    // the DcGroundHazard shape.
    constexpr uint32 SPELL_WELL_OF_CORRUPTION = 72362;

    // --- the clear order ----------------------------------------------------
    //
    //   1  objective  OBJ(1)  Start the intro       -> event 1
    //   2  boss       38112   Falric                (reorder, bit 0)
    //   3  boss       38113   Marwyn                (reorder, bit 1)
    //   4  objective  OBJ(2)  Frostsworn General    -> event 3
    //   5  objective  OBJ(3)  The throne room       -> event 4
    //   6  boss       36954   The Lich King         (doneBossStateIndex)
    //
    // The WAVES between 1 and 2 are deliberately not an objective, for the Pit of
    // Saron reason: the party is in continuous combat across them and an anchored
    // event has no combat-side rung (DcRel::AtObjective is non-combat only). They
    // are event 2, a conditional driver carrying DrivesInCombat. The ESCAPE is
    // event 5 for the same reason.
    constexpr int32 ORDER_INTRO   = 1;
    constexpr int32 ORDER_FALRIC  = 2;
    constexpr int32 ORDER_MARWYN  = 3;
    constexpr int32 ORDER_GENERAL = 4;
    constexpr int32 ORDER_THRONE  = 5;
    constexpr int32 ORDER_LICH_KING = 6;

    // --- event and hook ids --------------------------------------------------
    //
    // Event ids are per-map. HOOK ids are ONE FLAT SPACE across every dungeon
    // (ObjectiveHookRegistry::AddHook LOG_ERRORs a collision rather than silently
    // dropping one): 1-14 the older dungeons', 15-19 Violet Hold, 20-21 Blackwing
    // Lair, 22-23 Halls of Stone, 24 Halls of Lightning, 25-28 Utgarde Pinnacle,
    // 29-30 Pit of Saron. This map takes 31-35.
    constexpr uint32 EVENT_INTRO   = 1;
    constexpr uint32 EVENT_WAVES   = 2;
    constexpr uint32 EVENT_GENERAL = 3;
    constexpr uint32 EVENT_THRONE  = 4;
    constexpr uint32 EVENT_ESCAPE  = 5;

    constexpr uint32 HOOK_HOR_INTRO_GOSSIP = 31;
    constexpr uint32 HOOK_HOR_WAVES        = 32;
    constexpr uint32 HOOK_HOR_THRONE       = 33;
    constexpr uint32 HOOK_HOR_ESCAPE_GO    = 34;
    constexpr uint32 HOOK_HOR_ESCAPE       = 35;

    // --- gather quorums -----------------------------------------------------
    //
    // TWO DIFFERENT ANSWERS, and the difference is not an oversight.
    //
    // The throne-room gather (hook 33) is the ordinary 3-of-4: one bot that
    // cannot path in must never hold the other three, and stranded recovery (42)
    // owns that member.
    //
    // The ESCAPE gossip (hook 34) demands 5 OF 5 ALIVE. It is the point of no
    // return, and past it there is no out-of-combat resurrect for four to six
    // minutes — the Lich King SetInCombatWithZone()s every player once a second
    // for the whole escape — so a bot that is dead at the gossip stays dead for
    // the rest of the run, and a healer that is dead at the gossip is a wipe.
    constexpr float GATHER_RADIUS = 20.0f;
    constexpr float GATHER_QUORUM = 0.75f;

    // Mana floor for the escape gossip. There is no drinking after it, and the
    // healer has to cover four wall fights.
    constexpr float ESCAPE_MANA_PCT = 80.0f;

    // --- clocks --------------------------------------------------------------
    //
    // The wave event's budget covers the WHOLE first half: ten waves, two boss
    // fights and any number of automatic leash restarts. The plan's own estimate
    // is 6-9 minutes of waves; ten is a ceiling on a genuinely broken attempt.
    constexpr uint32 WAVES_TIMEOUT_MS = 600000;

    // The intro gossip step: the leader is not even VISIBLE for the first 10s
    // after she loads and has no gossip flag for 19s. Ninety seconds is that plus
    // a walk-in with room to spare.
    constexpr uint32 INTRO_GOSSIP_TIMEOUT_MS = 90000;

    // The garrison that walks the party in and holds it at the camp until wave 1
    // starts. It has to cover the FULL intro (224.5s Alliance, 209s Horde) plus
    // the walk, because a harness run that did not get the skip quest takes it.
    constexpr uint32 INTRO_HOLD_TIMEOUT_MS = 300000;

    // The General: a walk plus one fight with five reflections.
    constexpr uint32 GENERAL_TIMEOUT_MS = 240000;

    // The throne room: a gather, a forge and ~21 seconds of cutscene.
    constexpr uint32 THRONE_TIMEOUT_MS = 120000;

    // The escape gossip: a gather plus drinking to ESCAPE_MANA_PCT.
    constexpr uint32 ESCAPE_GO_TIMEOUT_MS = 180000;

    // The escape itself. The measured deadline for the LAST wall is T0+355s and
    // the outro adds ~40 more; eight minutes is that with a wipe's worth of slack.
    constexpr uint32 ESCAPE_TIMEOUT_MS = 480000;

    // How long the Lich King may stand within LK_STALL_DIST of the leader at a
    // shut wall before the driver says so. This is the escape's equivalent of Pit
    // of Saron's gate-stall WARN: unrecoverable from inside the driver (an add
    // parked inside the 10yd ring that the melee cannot reach), so the honest act
    // is to name it once with the adds listed and let the timeout stall the run.
    //
    // THE FIRST PAIR (30yd / 20s) COULD NEVER FIRE, and that is arithmetic rather
    // than bad luck. He closes on a leader who is standing still at 1.445 yd/s,
    // so the window between crossing LK_STALL_DIST and catching her at 12.5yd is
    // (30 - 12.5) / 1.445 = 12.1 SECONDS — and the latch needs 20 continuous
    // seconds inside it before it reports. Ten runs of tp-20260907-221612-1
    // stalled at a wall and produced zero STALLED lines between them; every one
    // of them went straight to the Doomed WARN, which names nothing about WHY.
    //
    // 60yd gives (60 - 12.5) / 1.445 = 32.9 seconds inside the window, so a 12s
    // hold reports with ~21 seconds still on the clock — early enough that the
    // line is a diagnosis and not an obituary.
    constexpr float  LK_STALL_DIST = 60.0f;
    constexpr uint32 ESCAPE_STALL_MS = 12000;

    // HOW FAR OUT THE DRIVER MAY BELIEVE ITS OWN GRID SCANS. Both numbers exist
    // because a grid searcher answers "nothing within R" identically to "nothing
    // at all", and on this map the two are routinely different things: the legs
    // between stand points run 110-180yd and every summon batch is cast AT the
    // Lich King, who is at the far end of the leg the party is crossing.
    //
    // ESCAPE_ADD_SCAN_YD — the add census. The old 120yd read zero adds for the
    // whole of every transit. On tr-20260907-232601-9 the tank logged
    // "0 add(s) up, wall shut" for twenty-one seconds at stand 3 while the batch
    // that held that wall was alive 155-172yd back at him, and it reappeared as
    // 2, then 3, then 4 as he walked it into range. A false zero mutes the stall
    // watchdog (which needs addsAlive > 0), and it makes `batchLoose` false, so
    // the Threat pickup cannot fire in the one window it exists for. The longest
    // stand-to-previous-stop is stand 4's 180.3yd; 200 covers it with margin.
    constexpr float ESCAPE_ADD_SCAN_YD = 200.0f;

    // WALL_READ_RANGE — how near the ice wall target the bot must be before an
    // EMPTY gameobject scan may be read as "the wall is open".
    //
    // The wall GO is summoned, never DB-spawned, so absent genuinely means open —
    // but only if the scan could have seen it. Out at 145yd the scan cannot, and
    // the driver logged "wall OPEN" at 23:36:26 and 23:36:32 on
    // tr-20260907-232601-10 and "wall shut" at 23:36:39 with nothing having
    // changed in the world except the tank's distance to it. Out of range is
    // UNKNOWN, and unknown must read shut: both consumers (Recenter, and the
    // stall watchdog's `wallShut`) only act when the party is standing at the
    // wall, so a conservative answer at range costs nothing and a false "open"
    // silences the one line that explains a stalled run.
    //
    // 100 rather than the scan's own 120 because the GO is matched within 20yd of
    // the target: at 100yd from the target the GO is inside 120yd of the bot for
    // certain.
    constexpr float WALL_READ_RANGE = 100.0f;
    constexpr float WALL_SCAN_YD = 120.0f;

    // Throttle on the two drivers' per-tick telemetry lines.
    constexpr uint32 TELEMETRY_MS = 3000;
}

// Halls of Reflection (668) — five events: the intro gossip + garrison, the
// conditional wave driver that owns the party from wave 1 to Marwyn's death, the
// Frostsworn General, the throne-room cutscene + the point-of-no-return gossip,
// and the conditional escape driver. See HallsOfReflectionEvents.cpp.
void RegisterHallsOfReflectionEvents(std::vector<DungeonEvent>& out);

// --- The Culling of Stratholme (map 595) ----------------------------------
//
// Greenfield before this block: map 595 had no roster, no events and no route,
// and every run died at setup with "no boss roster for this map". The reason is
// worth stating once, because it is also why the objectives below are objectives
// and not bosses: ALL FOUR of this dungeon's encounters are script TempSummons
// with NO `creature` spawn row anywhere on the map, so BossSpawnIndex's
// instance_encounters -> spawn join emits nothing at all.
//
// The DBC data itself is complete and correct — DungeonEncounter rows 293-296
// (normal) / 297-300 (heroic) give bits 0 Meathook, 1 Salramm, 2 Epoch,
// 3 Mal'ganis, and Mal'ganis's cast-spell credit (58630) exists as a spell_dbc
// row so ObjectMgr really does stamp it SPELL_ATTR0_CU_ENCOUNTER_REWARD. So
// 0xF IS the expected final encounterMask on a fully cleared run even though
// the roster carries no boss rows: the bits are flipped by the kills the
// objectives' events perform.
//
// THE WHOLE DUNGEON IS ONE MONOTONIC COUNTER. GetData(DATA_ARTHAS_EVENT) climbs
// 0 -> 11 and never regresses (an Arthas death repositions him to the current
// value's checkpoint and re-arms his gossip; it does not roll the counter back),
// which is what makes every gate here a >= test — the safe shape, because the
// event engine is dormant in combat and a transient "boss is up" window can be
// missed outright.
//
// AND EVERY PLAYER ACTION IS ONE OF THREE THINGS: a gossip click gated on that
// counter, five item uses, or a kill. Nothing in this instance keys on player
// position and there is not one areatrigger on the critical path — so, unlike
// Pit of Saron or Utgarde Pinnacle, no hook here has to forge a packet.
namespace DcCullingOfStratholme
{
    constexpr uint32 MAP_ID = 595;

    // --- instance data slots, HAND-COPIED from culling_of_stratholme.h -------
    //
    // `enum Data` is a plain unnumbered enum, so these are the values the compiler
    // assigns, transcribed. Never hand-count it again:
    //
    //   DATA_ARTHAS_EVENT 0 · DATA_GUARDIANTIME_EVENT 1 · DATA_SHOW_CRATES 2
    //   DATA_CRATE_COUNT 3 · DATA_START_WAVES 4 · DATA_SHOW_INFINITE_TIMER 5
    //   DATA_ARTHAS_REPOSITION 6 · DATA_INTRO_EVENT_FINISHED 7
    //
    // Only the first two are READ here. The rest are setters the instance script
    // owns and a bot must never touch — DATA_CRATE_COUNT in particular is a bare
    // increment with no idempotence (`_crateCount++`), so a second SetData from
    // outside the helper's SpellHit would miscount the crates.
    constexpr uint32 DATA_ARTHAS_EVENT = 0;
    // The heroic bonus timer, in MILLISECONDS REMAINING — not a state id. Set to
    // 26 minutes when DATA_START_WAVES fires on a heroic run, counted down in
    // InstanceScript::Update, and zeroed both when the Infinite Corruptor dies and
    // when it expires. So `> 0` is exactly "the bonus is still winnable", which is
    // the only probe the heroic objective needs.
    constexpr uint32 DATA_GUARDIANTIME_EVENT = 1;

    // --- DATA_ARTHAS_EVENT values (enum ArthasPhase) ------------------------
    constexpr uint32 PROGRESS_NOT_STARTED         = 0;
    constexpr uint32 PROGRESS_CRATES_FOUND        = 1;   // 5th crate hit
    constexpr uint32 PROGRESS_START_INTRO         = 2;   // Chromie-middle gossip
    constexpr uint32 PROGRESS_FINISHED_INTRO      = 3;   // Arthas at WP8, gossip up
    constexpr uint32 PROGRESS_FINISHED_CITY_INTRO = 4;   // city RP done; waves in 20s
    constexpr uint32 PROGRESS_KILLED_MEATHOOK     = 5;   // wave 5 dead
    constexpr uint32 PROGRESS_KILLED_SALRAMM      = 6;   // wave 10 dead
    constexpr uint32 PROGRESS_REACHED_TOWN_HALL   = 7;   // Arthas at WP20, gossip up
    constexpr uint32 PROGRESS_KILLED_EPOCH        = 8;   // Epoch dead, Arthas idle
    constexpr uint32 PROGRESS_LAST_CITY           = 9;   // Arthas at WP45, gossip up
    constexpr uint32 PROGRESS_BEFORE_MALGANIS     = 10;  // Arthas at WP54, gossip up
    constexpr uint32 PROGRESS_FINISHED            = 11;  // Mal'ganis done, exit open

    // --- creatures ----------------------------------------------------------
    //
    // ARTHAS IS THE ESCORT AND HE IS NOT THE WAILING CAVERNS SHAPE. Faction 2076
    // (friendly to players; the Scourge attacks him), no immunities, HealthModifier
    // 3.5, self-heals below 40%, and — this is the load-bearing difference — his
    // npc_escortAI is started WITHOUT a player GUID, so there is no 100yd
    // player-distance despawn the way Thrall has one.
    //
    // He therefore never takes the escort driver's START branch, which requires the
    // idle faction 35. Everything on this map runs through the RESUME branch
    // instead: he raises UNIT_NPC_FLAG_GOSSIP at five checkpoints (WP8, WP20, WP31,
    // WP45, WP54), the driver walks to 5yd and selects option 0, and his script
    // advances the counter and clears the flag. Exactly the Thrall model, minus the
    // opening release.
    constexpr uint32 NPC_ARTHAS = 26499;
    // The heroic Arthas template (HealthModifier 5.0). Named so nobody "fixes" the
    // escort by adding it: GetEntry() stays 26499 on both difficulties — 31210 is a
    // separate template the map does not spawn — so every scan here is 26499 only.
    constexpr uint32 NPC_ARTHAS_HEROIC = 31210;

    // The two Chromies, and they are DIFFERENT CREATURES with different scripts.
    // 26527 stands at the entrance from map load (gossip menu 9586) and hands out
    // the Arcane Disruptor; 27915 does not exist until the instance SUMMONS her
    // 20 seconds after the fifth crate (menu 9610), and her gossip is the only
    // thing that starts the escort.
    constexpr uint32 NPC_CHROMIE_START  = 26527;
    constexpr uint32 NPC_CHROMIE_MIDDLE = 27915;

    // The invisible trigger the crate spell actually hits: NOT_SELECTABLE, faction
    // 35, flags_extra TRIGGER, one standing on each of the five crates. Its
    // SpellHit is what counts the crate, so the item use has to land on IT and not
    // on the gameobject. See EventStepKind::UseItemAt.
    constexpr uint32 NPC_CRATE_HELPER = 27827;

    // The four encounters. None has a spawn row; all are TempSummons (spawnId 0),
    // which is both why the roster is objectives-only and what makes the wave
    // census below safe — see COS_WAVE_* .
    constexpr uint32 NPC_MEATHOOK = 26529;
    constexpr uint32 NPC_SALRAMM  = 26530;
    constexpr uint32 NPC_EPOCH    = 26532;
    constexpr uint32 NPC_MALGANIS = 26533;

    constexpr uint32 BIT_MEATHOOK = 0;
    constexpr uint32 BIT_SALRAMM  = 1;
    constexpr uint32 BIT_EPOCH    = 2;
    constexpr uint32 BIT_MALGANIS = 3;

    // The HEROIC bonus boss and his two props. 32273 is summoned at
    // EventPos[EVENT_SRC_CORRUPTOR] the moment DATA_START_WAVES fires on a heroic
    // run — the same tick wave 1 spawns — and is attackable immediately (faction
    // 1720, unit_flags 0x40 only: no immunity, no NON_ATTACKABLE, no arming
    // window, unlike Epoch and Mal'ganis).
    //
    // WHERE HE STANDS IS THE WHOLE REASON THE LAST ESCORT LEG IS SPLIT IN TWO, and
    // the straight-line distance is a trap. He is 81 yards from Market Row — and
    // 927 yards from it ON FOOT. Measured on the real mmtiles
    // (t/TestCullingOfStratholmeRouteProbe.cpp):
    //
    //     Arthas WP54 (pre-Mal'ganis)  ->  143 yd
    //     Fire Street WP48             ->  379 yd
    //     Market Row                   ->  927 yd
    //     King's Square                -> 1139 yd
    //     Arthas WP11 (waves start)    -> 1223 yd
    //     Elders' Square               -> 1255 yd
    //
    // He is in the MARKET DISTRICT, which the city does not connect to from the
    // wave streets at all: the only route is the one the dungeon intends — through
    // the Town Hall, down the secret passage, along Fire Street and round the
    // Market. So the only place he can be killed without walking the whole dungeon
    // twice is the PROGRESS_BEFORE_MALGANIS pause at waypoint 54, where Arthas
    // stands with his gossip up and waits indefinitely. That is where objective 8
    // sits, and it is why objective 7 ends at BEFORE_MALGANIS instead of running
    // straight on into Mal'ganis.
    //
    // THE CONSEQUENCE FOR THE CLOCK, stated plainly so nobody triages it as a bug:
    // the 26-minute timer starts at wave 1, and reaching waypoint 54 takes 21-28
    // minutes (10-15 of waves, ~1 to the Town Hall, 5-7 for the Town Hall and
    // Epoch, ~5 for the passage and Fire Street). So the bonus is winnable on a
    // fast run and gone on a slow one, which is what a time-attack bonus is. When
    // it is gone he has despawned, and the event's Optional gate turns that into a
    // skipped objective rather than a stalled run.
    //
    // His Time Rift (28409) is NOT_SELECTABLE + IMMUNE_TO_PC and his Guardian of
    // Time (32281) is faction 35, so neither needs a never-target row: the
    // attacker scans reject both on their own.
    constexpr uint32 NPC_INFINITE_CORRUPTOR = 32273;
    constexpr uint32 NPC_GUARDIAN_OF_TIME   = 32281;
    constexpr uint32 NPC_TIME_RIFT          = 28409;

    // --- the ten waves ------------------------------------------------------
    //
    // Eight spawn tables of four, cycled by npc_arthasAI::SummonNextWave with
    // `tableId = waveGroupId > 4 ? waveGroupId - 1 : waveGroupId` — so the ten
    // waves are tables 0,1,2,3, MEATHOOK, 4,5,6,7, SALRAMM, and the two bosses
    // spawn at Market Row where tables 2 and 5 also are.
    //
    // THERE IS NO INTER-WAVE TIMER. SummonedCreatureDies -> SendNextWave summons
    // the next wave in the SAME CALL as the fourth death, so the census below is
    // essentially never empty between waves and the party is in combat
    // continuously from wave 1 to Salramm. That is the single most important fact
    // about this phase: there is no breathing space in it anywhere, which is why
    // the driver's rest rung has to fire with a wave standing alive (they never
    // come to you) and why the two wave objectives are bookkeeping rather than
    // navigation.
    constexpr uint32 NPC_RISEN_ZOMBIE        = 27737;
    constexpr uint32 NPC_DEVOURING_GHOUL     = 28249;
    constexpr uint32 NPC_DARK_NECROMANCER    = 28200;
    constexpr uint32 NPC_TOMB_STALKER        = 28199;
    constexpr uint32 NPC_CRYPT_FIEND         = 27734;
    constexpr uint32 NPC_BILE_GOLEM          = 28201;
    constexpr uint32 NPC_ENRAGING_GHOUL      = 27729;
    constexpr uint32 NPC_PATCHWORK_CONSTRUCT = 27736;

    // The four spawn clusters, as centroids of their tables. Documentation and
    // test anchors only — the driver never walks to one of these. It walks to a
    // LIVE MOB's own position, for a reason the probe measured: the geometric
    // centre of the four clusters (2245, 1270) has NO navmesh at city height, only
    // map 595's flat z=0.14 sheet, and this map is one of the twelve where a
    // destination with no navmesh poly resolves to that sheet — a ~130yd sink.
    // A live summon's position is standable by construction.
    constexpr float CLUSTER_KS_X = 2174.4f, CLUSTER_KS_Y = 1253.1f, CLUSTER_KS_Z = 135.4f;
    constexpr float CLUSTER_FL_X = 2256.3f, CLUSTER_FL_Y = 1160.5f, CLUSTER_FL_Z = 138.2f;
    constexpr float CLUSTER_MR_X = 2350.0f, CLUSTER_MR_Y = 1199.0f, CLUSTER_MR_Z = 130.5f;
    constexpr float CLUSTER_ES_X = 2138.5f, CLUSTER_ES_Y = 1357.3f, CLUSTER_ES_Z = 132.1f;

    // --- gameobjects --------------------------------------------------------
    //
    // The crate PAIR is the UseItemAt mechanic: the helper's SpellHit Delete()s the
    // Suspicious crate and SummonGameObject()s a Plagued one in its place for a
    // DAY, carrying the sniffed rotation over. So the Plagued crate is a stable
    // per-crate receipt and the Suspicious one is gone — which is why the step
    // latches on the former's presence rather than on the latter's state.
    constexpr uint32 GO_SUSPICIOUS_CRATE = 190094;
    constexpr uint32 GO_PLAGUED_CRATE    = 190095;

    // The Town Hall bookcase (the secret passage) and the exit gate. Both are in
    // DcEventDoorRegistry — see the rows there for why the bookcase needs BOTH
    // IsScriptOnly and IsNavigationIgnored.
    constexpr uint32 GO_SHKAF_GATE = 188686;
    constexpr uint32 GO_EXIT_GATE  = 191788;

    // --- the Arcane Disruptor ----------------------------------------------
    //
    // Item 37888 casts 49590 on use. Three properties the crate step depends on:
    //
    //   * Map 595 in item_template, so granting it is scoped to this instance and
    //     CheckItems refuses it anywhere else;
    //   * spellcharges 0 and maxcount 1 — it is NOT consumed, so one grant serves
    //     all five crates;
    //   * spellcooldown 10000. The step waits that out rather than spam-casting
    //     through it, because a cooldown refusal is silent.
    //
    // And 49590's own shape: effect 0 is an APPLY_AURA whose implicit target is
    // TARGET_UNIT_NEARBY_ENTRY with SpellRange 137 (8 yards), narrowed by a
    // CONDITION_SOURCE_TYPE_SPELL_IMPLICIT_TARGET row to an ALIVE creature 27827.
    // So the engine finds the crate helper itself and the cast needs no target of
    // ours at all.
    constexpr uint32 ITEM_ARCANE_DISRUPTOR  = 37888;
    constexpr uint32 SPELL_ARCANE_DISRUPTION = 49590;

    // --- event / objective ids ----------------------------------------------
    //
    // One scale: event N is anchored on OBJ(N) with orderOverride N, for N in
    // 1..9. Event 10 is the conditional wave driver and has no objective.
    //
    // EIGHT IS A GAP ON A NORMAL RUN. The Infinite Corruptor's objective and event
    // are both HeroicOnly (he does not exist on normal), so a normal roster sorts
    // 1..7 then 9 — which is fine, because orderOverride is a sort key and nothing
    // requires it to be dense.
    constexpr uint32 EVENT_CRATES      = 1;
    constexpr uint32 EVENT_START_RP    = 2;
    constexpr uint32 EVENT_CITY_GATE   = 3;
    constexpr uint32 EVENT_MEATHOOK    = 4;
    constexpr uint32 EVENT_SALRAMM     = 5;
    constexpr uint32 EVENT_TOWN_HALL   = 6;
    constexpr uint32 EVENT_FIRE_STREET = 7;
    constexpr uint32 EVENT_CORRUPTOR   = 8;   // heroic only
    constexpr uint32 EVENT_MALGANIS    = 9;
    constexpr uint32 EVENT_WAVES       = 10;  // conditional driver, no objective

    constexpr uint32 ORDER_CRATES      = 1;
    constexpr uint32 ORDER_START_RP    = 2;
    constexpr uint32 ORDER_CITY_GATE   = 3;
    constexpr uint32 ORDER_MEATHOOK    = 4;
    constexpr uint32 ORDER_SALRAMM     = 5;
    constexpr uint32 ORDER_TOWN_HALL   = 6;
    constexpr uint32 ORDER_FIRE_STREET = 7;
    constexpr uint32 ORDER_CORRUPTOR   = 8;
    constexpr uint32 ORDER_MALGANIS    = 9;

    // ObjectiveHookRegistry id for the wave controller. Ids are ONE FLAT SPACE
    // across every dungeon; 36 follows Halls of Reflection's 31-35.
    constexpr uint32 HOOK_COS_WAVES = 36;

    // --- anchors ------------------------------------------------------------
    //
    // Every one of these is probed on the real mmtiles by
    // t/TestCullingOfStratholmeRouteProbe.cpp, and on this map that is not a
    // formality: map 595 carries a flat navmesh sheet at z = 0.14 under most of
    // the city, so an anchor written a few yards off a walkable surface does not
    // fail loudly — it resolves 130 yards down.

    // Chromie at the entrance. Her own spawn; the gossip reach is 20yd so the
    // arrive radius only has to put the tank in the same part of the road.
    constexpr float CHROMIE_X = 1550.08f, CHROMIE_Y = 574.41f, CHROMIE_Z = 92.79f;
    constexpr float CHROMIE_ARRIVE = 8.0f;

    // Chromie-middle's SUMMON position (EventPos[EVENT_POS_CHROMIE]). The anchor
    // sits on top of it, which is what makes WaitForSpawn's 250yd scan trivially
    // correct here.
    constexpr float CHROMIE_MID_X = 1813.30f, CHROMIE_MID_Y = 1283.58f, CHROMIE_MID_Z = 142.33f;
    constexpr float CHROMIE_MID_ARRIVE = 10.0f;

    // Arthas's waypoint 0, at the bridge. NOT his spawn (1920.87, 1287.12), which
    // is 18yd further east: the anchor is where the escort BEGINS, so the party
    // forms up on his path rather than on top of him.
    constexpr float BRIDGE_X = 1903.17f, BRIDGE_Y = 1291.57f, BRIDGE_Z = 143.32f;
    constexpr float BRIDGE_ARRIVE = 12.0f;

    // Market Row / Town Hall front — EventPos[EVENT_SRC_MEATHOOK], which is also
    // EventPos[EVENT_SRC_SALRAMM]: the two bosses spawn on the same spot. Both
    // wave objectives anchor here because it is where the party is standing when
    // each of them completes.
    constexpr float MARKET_ROW_X = 2351.45f, MARKET_ROW_Y = 1197.81f, MARKET_ROW_Z = 130.45f;
    constexpr float MARKET_ROW_ARRIVE = 12.0f;

    // The heroic Corruptor's summon position (EventPos[EVENT_SRC_CORRUPTOR]). 81yd
    // from Market Row in a straight line and 927yd from it on foot — see the
    // NPC_INFINITE_CORRUPTOR note above for the measured table and for why that is
    // what positions objective 8. The number that matters here is 143yd: the walk
    // from Arthas's waypoint 54, which is the objective before it.
    //
    // Also the reason he never interferes with the waves: the nearest wave cluster
    // is 81yd away in a straight line and the whole Market district is a separate
    // component of the street graph, so neither the wave driver nor a wave mob ever
    // meets him.
    constexpr float CORRUPTOR_X = 2329.07f, CORRUPTOR_Y = 1276.98f, CORRUPTOR_Z = 132.68f;
    constexpr float CORRUPTOR_ARRIVE = 12.0f;

    // Arthas's waypoint 20, the Town Hall door, where he stops with gossip up at
    // PROGRESS_REACHED_TOWN_HALL.
    constexpr float TOWN_HALL_X = 2365.63f, TOWN_HALL_Y = 1194.84f, TOWN_HALL_Z = 131.97f;
    constexpr float TOWN_HALL_ARRIVE = 10.0f;

    // Arthas's waypoint 16, on the road between his wave stop and the Town Hall,
    // and where the Town Hall leg is anchored. THE ANCHOR IS A MEETING POINT ON HIS
    // ROUTE — not the start of it and not the end of it — and on this leg that is
    // not a stylistic choice. Both mistakes have now been made live, in that order.
    //
    // THE CONSTRAINT. Arthas is a DB spawn (creature guid 1970935), so he is not a
    // TempSummon and npc_escortAI never marks him active. Creature::IsUpdateNeeded
    // therefore keeps him ticking only while a player is inside the map's grid
    // activation range, which on an instance is Visibility.Distance.Instances =
    // 170yd. Both wave bosses spawn at Market Row, so that is where the wave phase
    // leaves the party — 265yd east of his WP11 stop, with him asleep.
    //
    // MISTAKE ONE, anchoring at the Town Hall door (tp-20260909-224741-1, 0/10).
    // The party garrisoned at the door and waited for DATA_ARTHAS_EVENT to reach
    // REACHED_TOWN_HALL. It never did: at that range he was never updated, so the
    // 10s timer that unpauses him after Salramm never ran. Ten runs read
    // `data(0)=6 (need >= 7)` until the watchdog ended them.
    //
    // MISTAKE TWO, anchoring at WP12, his first step out of the wave stop
    // (tp-20260910-073710-1, 0/10). The reasoning was that the party must walk back
    // and collect him. It does — but WP12 is where he LEAVES from, and he does not
    // wait to be collected. ACTION_KILLED_SALRAMM schedules a 10s resume
    // (culling_of_stratholme.cpp), and the party's own westward walk is what wakes
    // him: they cross into range around WP17, the overdue timer fires at once, and
    // he runs east on WP13..WP20 while they keep walking west. They pass each other
    // on the road. Every run arrived at an empty anchor, found no escortee inside
    // ESCORT_SEARCH, and held there until the run timed out — which is what the
    // player sees as the party running back to the city gate and milling about the
    // wave ground.
    //
    // WP16 IS THE POINT THAT SATISFIES BOTH ENDS. It is 137yd from his WP11 stop,
    // inside the 170yd activation range, so arriving there wakes him exactly as
    // WP12 did. And it is 156yd from the Town Hall door, so the whole of his
    // WP12..WP20 run lies inside ESCORT_SEARCH_TOWN_HALL below — the escort step
    // latches onto him wherever the head start left him, instead of requiring the
    // party to be somewhere before he is. It is the easternmost of his waypoints
    // that keeps the wake-up: WP17 is 181yd from WP11 and would not.
    constexpr float TOWN_HALL_MEET_X = 2210.39f, TOWN_HALL_MEET_Y = 1207.55f,
                    TOWN_HALL_MEET_Z = 136.26f;
    constexpr float TOWN_HALL_MEET_ARRIVE = 12.0f;

    // Arthas's waypoint 31, where he waits with gossip up after Epoch dies — the
    // start of the secret passage and Fire Street leg.
    constexpr float PASSAGE_X = 2423.12f, PASSAGE_Y = 1119.43f, PASSAGE_Z = 148.08f;
    constexpr float PASSAGE_ARRIVE = 12.0f;

    // Arthas's waypoint 54, at the edge of the Market, where he stops with his
    // gossip up at PROGRESS_BEFORE_MALGANIS and waits indefinitely for someone to
    // say "I'm ready to battle the dreadlord, sire."
    //
    // THAT INDEFINITE WAIT IS WHAT MAKES THE HEROIC BONUS POSSIBLE. It is the only
    // pause in the run that is both unbounded and 143 yards from the Infinite
    // Corruptor, so objective 8 can detour to him and come back without anything
    // being on a clock except the Corruptor's own.
    constexpr float MARKET_X = 2327.39f, MARKET_Y = 1412.47f, MARKET_Z = 127.69f;
    constexpr float MARKET_ARRIVE = 10.0f;

    // --- the five crates, in road order from Chromie -----------------------
    //
    // Gameobject positions (the helper on each stands within 0.2yd of it). Legs
    // are 49 / 62 / 81 / 60 yards, all on the main road, and the NEAREST PAIR IS
    // 49yd APART — which is what makes the step's 5yd receipt latch and 4yd
    // arrival radius unambiguous. Order is irrelevant to the instance (any five
    // distinct helpers count); this order is simply the walk.
    struct CratePos { float x, y, z; };
    inline constexpr CratePos CRATES[5] = {
        { 1579.42f, 621.45f,  99.73f },  // guid 67580 — Roger Owens
        { 1570.92f, 669.93f, 102.31f },  // guid 67579 — Silvio Perelli's stock
        { 1629.68f, 731.37f, 112.85f },  // guid 67582 — Martha Goslin's grain
        { 1628.98f, 812.14f, 120.69f },  // guid 67583 — Malcolm Moore's house
        { 1674.39f, 872.31f, 120.39f },  // guid 67581 — Bartleby Battson's cart
    };

    // --- the wave driver's tuning ------------------------------------------

    // Census radius from the tank. MR <-> ES is 264yd — the widest pair of
    // clusters — so this has to clear it from either end. Cheap in practice: it is
    // only run while the predicate holds, and every wave is summoned into a grid
    // the summon itself loads.
    constexpr float WAVE_SCAN = 320.0f;

    // How close to the nearest live wave mob counts as "at the wave". Above this
    // the driver travels; below it, it either yields to the fight or breaks a
    // standoff. The TravelTo leash is deliberately well under it so there is no
    // dead band between "travel" and "arrived" — the two tests are on the same
    // metric (3D distance to the mob) and the leash is the tighter one.
    constexpr float WAVE_ENGAGE_RANGE = 25.0f;
    constexpr float WAVE_TRAVEL_LEASH = 15.0f;

    // How long a live wave mob may stand inside WAVE_ENGAGE_RANGE with nobody in
    // combat before the driver starts the fight itself. Pit of Saron's budget: its
    // waves proved that a parked party and a parked mob can stand eight yards
    // apart indefinitely, because AC aggro is relocation-driven and neither side
    // is relocating. These mobs are ordinary REACT_AGGRESSIVE trash with ~20yd
    // detection, so this should never fire — it costs nothing if it does not.
    constexpr uint32 WAVE_STANDOFF_MS = 5000;

    // Do not REST with a live wave mob nearer than this. Rest has to be allowed
    // while a wave is alive (there is no gap between waves at all — see the wave
    // note above), so the only safeguard is distance: sit down well outside
    // anything's detection radius.
    constexpr float WAVE_REST_SAFE_DIST = 40.0f;

    // How long ONE rest window may last before the driver stops waiting and walks
    // to the wave anyway.
    //
    // The bound is the point. `partyRecovered` is a predicate the party can fail
    // for ever — a bot out of water, a member the rez ladder cannot reach, a rest
    // target above what drinking delivers — and an unbounded hold on it stops the
    // dungeon dead in a gap between waves with every watchdog reporting a healthy
    // party standing still. Two minutes is several times what drinking back to the
    // stock HighMana threshold takes, and the clock is cleared by combat, so a
    // party that is merely slow gets a fresh budget after every fight.
    constexpr uint32 WAVE_REST_BUDGET_MS = 120000;

    // Throttle on the driver's per-tick telemetry line.
    constexpr uint32 TELEMETRY_MS = 3000;

    // --- step timeouts ------------------------------------------------------
    //
    // One per step that can legitimately sit for minutes, each sized off the
    // measured segment rather than the 30s default.

    // Per crate. Old Hillsbrad's barrels measured 32-51s for a walk-in plus a
    // plant and still needed 120s once guards and rest holds were in the way;
    // these legs are comparable (49-81yd) and additionally carry the Disruptor's
    // 10s cooldown, so they get the same budget.
    constexpr uint32 CRATE_TIMEOUT_MS = 120000;

    // Chromie-middle is summoned 20s after the fifth crate. 90s is the loud
    // failure for a crate count that came up short — which is exactly the
    // self-validation Old Hillsbrad gets from waiting on Lieutenant Drake.
    constexpr uint32 CHROMIE_MID_TIMEOUT_MS = 90000;

    // The two wave objectives are pure bookkeeping holds on the counter, and the
    // thing they wait for is the whole wave phase: 10-15 minutes of fighting
    // driven by event 9. 30 minutes is the "something is badly wrong" line.
    constexpr uint32 WAVE_HOLD_TIMEOUT_MS = 1800000;

    // The heroic Corruptor. The objective's anchor IS his summon position, so if he
    // is there at all he is found on the first tick — which means this budget is
    // only ever spent in the OTHER case, where the 26-minute timer expired and the
    // instance despawned him. Kept short for exactly that reason: a missed bonus
    // should cost the run twenty seconds, not a minute.
    constexpr uint32 CORRUPTOR_SPAWN_TIMEOUT_MS = 20000;
    constexpr uint32 CORRUPTOR_KILL_TIMEOUT_MS  = 300000;

    // The wave driver's Custom step. The phase is 10-15 minutes and the event is
    // Repeatable + Optional, so this is a RE-ARM rather than a skip: a timeout
    // reports Skipped, the repeatable event is never latched, and
    // DcRunEventAction rewinds it and carries on. 20 minutes keeps the re-arm
    // rare without ever being the thing that ends a run.
    constexpr uint32 WAVES_TIMEOUT_MS = 1200000;

    // The escort steps carry NO flat timeout (DungeonEventExecutor::Advance
    // exempts EscortCreature outright — its own dead-air watchdog owns liveness),
    // which is the only reason the 3-minute Uther/Jaina intro and the 85-second
    // city intro do not have to be budgeted here.

    // --- escort geometry ----------------------------------------------------
    //
    // Shared by the three escort legs. The standoff is the module default; the
    // threat radii differ per leg and are set at each call site, because what is
    // worth engaging near Arthas changes completely between the city gate (where a
    // cosmetic IMMUNE_TO_PC Mal'ganis stands 27yd from his stop) and the Town Hall
    // (where Chrono-Lord Epoch spawns 40yd out and walks in).
    constexpr float ESCORT_STANDOFF  = 5.0f;
    constexpr float ESCORT_Z_BAND    = 20.0f;
    constexpr float ESCORT_SEARCH    = 150.0f;
    // The Town Hall leg searches wider than the rest. It is the one leg whose
    // escortee is ALREADY MOVING when the step starts: he resumes on a 10s timer
    // after Salramm and runs the 275yd from WP12 to WP20 whether or not the party
    // has arrived, and loot, rests and stragglers make the party's own walk to the
    // anchor anything from 3 to 30+ seconds. 200yd covers his entire possible
    // range from the WP16 anchor (156yd at the far end, the Town Hall door), so
    // there is no head start that can lose him. Every other leg starts on a
    // stationary escortee and 150yd is ample.
    constexpr float ESCORT_SEARCH_TOWN_HALL = 200.0f;
    constexpr float ESCORT_THREAT_CITY      = 20.0f;
    constexpr float ESCORT_THREAT_TOWN_HALL = 40.0f;
    constexpr float ESCORT_THREAT_LAST_CITY = 30.0f;
}

// The Culling of Stratholme (595) — TEN events: Chromie and the five plagued
// crates, the gossip that starts the Royal Escort, FOUR Arthas escort legs, two
// wave-objective holds, the heroic Infinite Corruptor, and the conditional wave
// controller that owns the party for the ten waves. See
// CullingOfStratholmeEvents.cpp.
void RegisterCullingOfStratholmeEvents(std::vector<DungeonEvent>& out);

// Every creature entry the wave census counts: the eight trash entries plus the
// two wave bosses.
//
// IT MUST STAY COMPLETE AND THE spawnId FILTER IS NOT OPTIONAL. Three of these
// entries ALSO have static spawns on map 595 — 103 Risen Zombies and 7 Enraging
// Ghouls line Fire Street, and two Crypt Fiends stand with them — and the
// Stratholme citizens UpdateEntry into Risen Zombies during the city intro. Every
// one of those keeps a non-zero GetSpawnId(); every wave member is a TempSummon
// with spawnId 0. Counting a static one would read "the wave is up" from the
// moment Fire Street is in scan range and send the party 250 yards the wrong way,
// into a gauntlet that is three objectives ahead of them.
std::vector<uint32> const& CosWaveEntries();

// --- Trial of the Champion (map 650) ---------------------------------------
//
// A three-encounter ARENA with no navigation problem and no roster. All three
// instance_encounters rows are ENCOUNTER_CREDIT_CAST_SPELL (68572 / 68574 /
// 68663), every boss is a script summon with no `creature` row, and two of the
// three NEVER DIE — the Grand Champions and Eadric/Paletress park at 1 HP, turn
// friendly and evade. So BossSpawnIndex emits nothing and every run used to fail
// at setup with "no boss roster for this map", the Culling of Stratholme
// diagnosis exactly.
//
// THE WHOLE DUNGEON IS ONE COUNTER, GetData(DATA_INSTANCE_PROGRESS), 0..9, and
// the announcer's gossip, which he re-offers at 0, 6 and 8. Unlike the Culling's
// counter this one GOES BACKWARDS: a full wipe runs InstanceCleanup, which
// rewinds 1-4 to 0 and 7 to 6 and re-homes the announcer with his gossip up. So
// nothing here may latch on having clicked him — the driver re-decides from the
// counter every tick, and the three objectives hold on thresholds a rewind
// cannot satisfy early.
//
// PHASE 1 IS A JOUST AND IT IS WON BY TRAMPLING, NOT BY DAMAGE. A Grand Champion
// at 1 HP drops his mount, goes NON_ATTACKABLE and walks to a spare horse, and
// remounts at 50k HP; 4 -> 5 needs all three dismounted at once, and a player
// riding the party's OWN-faction horse within 5yd of a walking champion stuns
// him for 15s. The fighting — lance, mount, the four vehicle buttons, the Charge
// cycle and the trample shadowing — is mod-playerbots' `wotlk-toc` strategy.
// This module only musters the party, clicks the announcer and keeps its own
// movement off a mounted tank.
//
// Every slot and value below is hand-copied from trial_of_the_champion.h, whose
// enums are plain and unnumbered past their first member.
namespace DcTrialOfTheChampion
{
    constexpr uint32 MAP_ID = 650;

    // --- instance data (enum eData) ------------------------------------------
    //
    // GetData answers ONLY slots 4 and 14; every other slot reads 0.
    // GetGuidData answers only 5 and 15.
    constexpr uint32 DATA_INSTANCE_PROGRESS  = 4;
    constexpr uint32 DATA_ANNOUNCER          = 5;   // GetGuidData — the announcer
    constexpr uint32 DATA_TEAMID_IN_INSTANCE = 14;
    // DATA_PALETRESS in the core, but it holds whichever Argent champion the click
    // at progress 6 rolled — Eadric OR Paletress.
    constexpr uint32 DATA_ARGENT_CHAMPION    = 15;  // GetGuidData

    // --- DATA_INSTANCE_PROGRESS values (enum eProgress) ------------------------
    constexpr uint32 PROGRESS_INITIAL               = 0;  // announcer gossip up (mounted clicker)
    constexpr uint32 PROGRESS_REACHED_DEST          = 1;  // champions parked; wave 1 incoming
    constexpr uint32 PROGRESS_GROUP_DIED_1          = 2;
    constexpr uint32 PROGRESS_GROUP_DIED_2          = 3;
    constexpr uint32 PROGRESS_GROUP_DIED_3          = 4;  // the mounted champions themselves
    constexpr uint32 PROGRESS_CHAMPIONS_UNMOUNTED   = 5;  // horses despawned; on-foot fight
    constexpr uint32 PROGRESS_CHAMPIONS_DEAD        = 6;  // bit 0; gossip back in 15s
    constexpr uint32 PROGRESS_SOLDIERS_DIED         = 7;  // Eadric/Paletress walks in
    constexpr uint32 PROGRESS_ARGENT_CHALLENGE_DIED = 8;  // bit 1; gossip back in 15s
    constexpr uint32 PROGRESS_FINISHED              = 9;  // bit 2; the Black Knight is dead

    // The three DungeonEncounter bits (DBC 334/336, 338/339, 340/341). All three
    // are cast-spell credits that really do land, so 0x7 is the expected final
    // mask on a roster with no boss rows at all.
    constexpr uint32 BIT_GRAND_CHAMPIONS  = 0;
    constexpr uint32 BIT_ARGENT_CHALLENGE = 1;
    constexpr uint32 BIT_BLACK_KNIGHT     = 2;

    // --- creatures -----------------------------------------------------------
    //
    // THE ANNOUNCER IS TWO ENTRIES AND ONE CREATURE: the DB spawn is 35004 and
    // Reset() UpdateEntry()s him to 35005 for an Alliance party. Resolve him
    // through GetGuidData(DATA_ANNOUNCER), never by entry.
    constexpr uint32 NPC_ANNOUNCER_HORDE    = 35004;  // Jaeren Sunsworn
    constexpr uint32 NPC_ANNOUNCER_ALLIANCE = 35005;  // Arelas Brightstar

    // The two horse entries, 12 of each ringing the wall in every instance. The
    // party rides its OWN faction's (Alliance 35644, Horde 36558) — the one the
    // trample hackfix counts — and the champions remount on the other. Either
    // counts as "mounted" here: the gossip only asks for a vehicle, and which
    // horse a bot picks is mod-playerbots' decision.
    constexpr uint32 NPC_ARGENT_WARHORSE   = 35644;
    constexpr uint32 NPC_ARGENT_BATTLEWORG = 36558;

    // The ten Grand Champions (five per side; the party faces the other side's).
    constexpr uint32 NPC_MOKRA = 35572, NPC_ERESSEA = 35569, NPC_RUNOK = 35571,
                     NPC_ZULTORE = 35570, NPC_VISCERI = 35617;
    constexpr uint32 NPC_JACOB = 34705, NPC_AMBROSE = 34702, NPC_COLOSOS = 34701,
                     NPC_JAELYNE = 34657, NPC_LANA = 34703;

    // The Argent Challenge. Nine soldiers, summoned at the gate mouth and marched
    // to three packs on the y ~ 650 line; `DoZoneInCombat` is COMMENTED OUT in
    // EVENT_ARGENT_SOLDIER_GROUP_ATTACK, so they engage by proximity only and the
    // two side packs, 30yd apart, have to be pulled.
    constexpr uint32 NPC_ARGENT_MONK         = 35305;
    constexpr uint32 NPC_ARGENT_PRIESTESS    = 35307;
    constexpr uint32 NPC_ARGENT_LIGHTWIELDER = 35309;
    // The Priestess's healing fountain: attackable (PACIFIED only), faction 16.
    constexpr uint32 NPC_FOUNTAIN_OF_LIGHT   = 35311;
    constexpr uint32 NPC_EADRIC              = 35119;
    constexpr uint32 NPC_PALETRESS           = 34928;

    // Paletress at 25%: Reflective Shield, a 999 999 absorb, until her Memory
    // dies — the Memory's JustDied calls her DoAction(1), which removes it. So
    // the AURA is the unkillable window, exactly; see the target-exclusion row.
    constexpr uint32 SPELL_REFLECTIVE_SHIELD = 66515;

    // The Black Knight rides in on a gryphon, kills the announcer and talks for
    // ~50s before he is attackable. 35614 is Desecration's ground hazard — a
    // CREATURE (unit_flags 0, faction 14, TRIGGER by flags_extra only), not a
    // DynamicObject — so it gets a hazard emitter AND a never-target row.
    constexpr uint32 NPC_BLACK_KNIGHT          = 35451;
    constexpr uint32 NPC_BLACK_KNIGHT_GRYPHON  = 35491;
    constexpr uint32 NPC_RISEN_JAEREN          = 35545;
    constexpr uint32 NPC_RISEN_ARELAS          = 35564;
    constexpr uint32 NPC_RISEN_CHAMPION        = 35590;
    constexpr uint32 NPC_DESECRATION_STALKER   = 35614;
    constexpr float  DESECRATION_RADIUS        = 8.0f;   // 67781, radius index 14

    // --- gameobjects ---------------------------------------------------------
    //
    // The four arena gates. All GAMEOBJECT_TYPE_DOOR, all opened and shut only by
    // the instance script, and two of them stand right where the party works: the
    // North Portcullis is 2.6yd from where the party lands and shuts at progress
    // 1, and the Main Gate is 17yd behind the soldier line.
    constexpr uint32 GO_MAIN_GATE        = 195647;  // (746.70, 677.47) north wall
    constexpr uint32 GO_EAST_PORTCULLIS  = 195648;  // (746.65, 556.93)
    constexpr uint32 GO_SOUTH_PORTCULLIS = 195649;  // (685.51, 618.06)
    constexpr uint32 GO_NORTH_PORTCULLIS = 195650;  // (807.84, 618.06) the entrance
    constexpr uint32 GO_LANCE_RACK       = 196398;  // four, one 7yd from the entrance

    // The lance and its aura, both gates on the horses' spellclick (a conditions
    // row AND BeforeSpellClick). mod-playerbots' `toc lance` takes it.
    constexpr uint32 ITEM_ARGENT_LANCE    = 46106;
    constexpr uint32 SPELL_LANCE_EQUIPPED = 62853;

    // The rider bar, for the record: mod-playerbots presses these, DC never does.
    constexpr uint32 SPELL_THRUST         = 68505;  // 0-6yd
    constexpr uint32 SPELL_SHIELD_BREAKER = 62575;  // 5-25yd, strips a Defend stack
    constexpr uint32 SPELL_CHARGE         = 68282;  // 5-25yd, 20 000
    constexpr uint32 SPELL_DEFEND         = 66482;  // self, 3 stacks
    constexpr uint32 SPELL_TRAMPLED       = 67867;  // the 15s stun on a walking champion

    // --- the announcer's gossip (menu 10614, npc_announcer_toc5) ---------------
    //
    // POSITIONAL, which SelectGossip maps to the DB OptionID. At progress 0 the
    // menu has two items, OptionIDs 0 ("I am ready.") and 3 ("...skip the
    // pageantry."), and ONLY WHEN THE CLICKER HAS A VEHICLE — unmounted, the text
    // comes back with no options at all. Option 1 is the short version: all three
    // champions summoned in place and progress straight to 1, against ~90s of
    // speeches, gate cycles and waypoint walks. At 6 and 8 there is one option.
    constexpr int32 GOSSIP_OPTION_SHORT_JOUST = 1;
    constexpr int32 GOSSIP_OPTION_NEXT_PHASE  = 0;

    // Interaction reach for the click, centre to centre. The core allows 5.5
    // (INTERACTION_DISTANCE, bounding radii included); a yard under it absorbs the
    // horse's own drift. The approach aims GOSSIP_STANDOFF short of him.
    constexpr float GOSSIP_REACH    = 4.5f;
    constexpr float GOSSIP_STANDOFF = 3.0f;

    // --- anchors -------------------------------------------------------------
    //
    // The arena floor is one open bowl around the centre (748.3, 619.4); every
    // anchor is inside it and route-probed by t/TestTrialOfTheChampionRouteProbe.cpp.
    // That probe reads the mmtiles off disk — LIVE there is no navmesh: the core
    // switches pathfinding off for map 650 (DisableMgr::IsPathfindingEnabled), so
    // no tile ever loads and every move here, DC's routes included, is a straight
    // line. That is only safe because nothing here leaves the bowl.
    constexpr float ARENA_X = 748.31f, ARENA_Y = 619.49f, ARENA_Z = 411.17f;  // announcer spawn

    // OBJ(1): 11yd south of the wave convergence point, the announcer 11yd north.
    // Also the JOUST POST — where the driver parks a mounted tank between waves.
    constexpr float CHAMPIONS_X = 748.3f, CHAMPIONS_Y = 608.0f, CHAMPIONS_Z = 411.3f;
    // OBJ(2): 19yd from the middle soldier pack (they aggro on arrival), 3yd from
    // the announcer's phase-2 spot, 4yd from where the boss walks to.
    constexpr float ARGENT_X = 747.0f, ARGENT_Y = 631.0f, ARGENT_Z = 411.4f;
    // OBJ(3): 3yd from the Black Knight's walk-to point (746.81, 623.15).
    constexpr float KNIGHT_X = 749.0f, KNIGHT_Y = 626.0f, KNIGHT_Z = 411.3f;
    constexpr float OBJ_ARRIVE  = 10.0f;
    constexpr float HOLD_RADIUS = 12.0f;

    // A mounted tank farther than this from the joust post between waves is
    // ridden back to it.
    constexpr float POST_RADIUS = 10.0f;

    // --- the driver's tuning -----------------------------------------------------

    // Grid-scan radius from the tank. The four gates put the bowl's radius at
    // ~60yd and the entrance is 57yd from its centre, so the far wall is ~117yd
    // from where the party lands; 125 covers every census from anywhere.
    constexpr float ARENA_RADIUS = 60.0f;
    constexpr float ARENA_SCAN   = 125.0f;

    // Four riders out of five — or everyone alive, if fewer — before the click,
    // and never waiting more than 90s past the tank's own mount for them.
    constexpr uint32 MUSTER_QUORUM     = 4;
    constexpr uint32 MUSTER_TIMEOUT_MS = 90000;

    // One WARN if the tank has had 2 minutes at progress 0-4 without a horse:
    // mod-playerbots' `toc lance` / `toc mount` are the only things that mount
    // it, and the run cannot start without them.
    constexpr uint32 MOUNT_WARN_MS = 120000;

    // Progress 8 with neither the Knight nor the announcer for this long means
    // the Knight evaded — which DESPAWNS him — after killing the announcer. The
    // survivors are stranded until a full wipe runs InstanceCleanup; reported,
    // never "fixed" from here.
    constexpr uint32 STRAND_GRACE_MS = 30000;

    constexpr uint32 TELEMETRY_MS     = 10000;  // the `DcToc progress=...` line
    constexpr uint32 WARN_THROTTLE_MS = 60000;
    // The driver's point-move floor. A re-issued MovePoint restarts the spline, so
    // the same destination is not re-sent inside this window; every leg in the
    // bowl is under five seconds on a horse at twice run speed.
    constexpr uint32 MOVE_REISSUE_MS  = 2000;
    // One announcer click per window. A click that landed drops his gossip flag at
    // once, so this only ever spaces out clicks that did NOT land.
    constexpr uint32 CLICK_RETRY_MS   = 2000;

    // --- step timeouts -----------------------------------------------------------
    constexpr uint32 CHAMPIONS_TIMEOUT_MS = 900000;   // 15 min: muster + joust + on-foot
    constexpr uint32 ARGENT_TIMEOUT_MS    = 900000;   // 15 min: nine soldiers + the boss
    constexpr uint32 KNIGHT_TIMEOUT_MS    = 720000;   // 12 min: ~50s intro + three phases
    // The driver's Custom step. Repeatable + Optional, so a timeout RE-ARMS it.
    constexpr uint32 DRIVER_TIMEOUT_MS    = 1200000;

    // --- event / objective ids -----------------------------------------------
    constexpr uint32 EVENT_CHAMPIONS    = 1;
    constexpr uint32 EVENT_ARGENT       = 2;
    constexpr uint32 EVENT_BLACK_KNIGHT = 3;
    constexpr uint32 EVENT_DRIVER       = 4;  // conditional driver, no objective

    constexpr uint32 ORDER_CHAMPIONS    = 1;
    constexpr uint32 ORDER_ARGENT       = 2;
    constexpr uint32 ORDER_BLACK_KNIGHT = 3;

    // ObjectiveHookRegistry id. One flat space; 37 follows the Culling's 36.
    constexpr uint32 HOOK_TOC_DRIVER = 37;
}

// Trial of the Champion (650) — FOUR events: three anchored counter holds, one
// per encounter, and the conditional driver that musters the party, clicks the
// announcer and pulls the Argent soldiers. See TrialOfTheChampionEvents.cpp.
void RegisterTrialOfTheChampionEvents(std::vector<DungeonEvent>& out);

// The ten Grand Champion entries, for the driver's "walking champion" census.
std::vector<uint32> const& TocChampionEntries();

// The three Argent soldier entries, for the driver's side-pack census. Every one
// is a summon, so there is no static spawn to filter out.
std::vector<uint32> const& TocSoldierEntries();

// Is `bot` a Trial of the Champion rider still on foot during the joust? Then
// mod-playerbots' `toc lance` / `toc mount` own its movement and follow-tank
// stands down (DcTocDriver::FollowerMountsItself has the why).
bool TocFollowerMountsItself(Player* bot);

// --- The Oculus (map 578) ---------------------------------------------------
//
// Four rings stacked in open air. Only the entrance floor is reachable on foot;
// Drakos's ring is across the Nexus Portal (a teleport), and every ring above it
// is reached by summoning a player-owned FLYING DRAKE from an essence item,
// flying it there, landing on a platform and dismounting. The plan is
// deployment-files/docs/mod-dungeon-clear_oculus_plan.md; the dossier is the
// dc-oculus-dossier memory. Every coordinate below was probed against the live
// map-578 navmesh (tools/probe_navmesh.py --column) on 2026-09-12 and is
// re-checked by t/TestOculusRouteProbe.cpp.
//
// THE SHAPE: three derived boss rows (Drakos, and Varos / Urom re-anchored onto
// the ground they are fought on), eight objectives (the portal crossing, three
// construct islands, Urom's three platforms, Eregos), and ONE conditional driver
// (hook 38) that decides when the party musters, flies and lands. The per-member
// half — gossip for an essence, mount, fly a lane, land, dismount, station on
// Eregos — is the rider rung (Action/DcOculusRiderAction.cpp), because every
// member steers its OWN drake. The decisions are pure kernels:
// Util/DcOculusDriverDecision.h and Util/DcOculusFlightDecision.h.
//
// THREE CORE TRAPS DECIDE THE DESIGN (oculus.cpp):
//   * a drake bar spell that hits a hostile whose CanFly() is false KILLS THE
//     RIDER (npc_oculus_drakeAI::SpellHitTarget) — so nobody lifts off while
//     anyone is in combat, and nobody hovers over a ground fight;
//   * a drake despawns 2s after its rider leaves and 5s after summon if nobody
//     boards — every landing costs a fresh essence use (15s cooldown);
//   * Vehicle::RemovePassenger casts no parachute inside an instance, and a
//     clientless bot never falls — a mid-air dismount strands it. The rider only
//     dismounts over snapped mesh.
namespace DcOculus
{
    constexpr uint32 MAP_ID = 578;

    // --- instance data -----------------------------------------------------------
    //
    // instance_oculus keeps its own m_auiEncounter and NEVER calls SetBossState:
    // GetBossState reads TO_BE_DECIDED for all four and IsEncounterInProgress is
    // always false. GetData is the only honest read.
    constexpr uint32 DATA_DRAKOS   = 0;
    constexpr uint32 DATA_VAROS    = 1;
    constexpr uint32 DATA_UROM     = 2;
    constexpr uint32 DATA_EREGOS   = 3;
    constexpr uint32 DATA_CC_COUNT = 5;   // Centrifuge Constructs killed, 0..10, saved
    constexpr uint32 STATE_IN_PROGRESS = 1;  // EncounterState
    constexpr uint32 STATE_DONE        = 3;
    constexpr uint32 CC_TOTAL          = 10;

    // --- creatures -----------------------------------------------------------------
    //
    // Heroic spawns keep the NORMAL entry (difficulty_entry swaps the template, not
    // GetEntry()), and all four instance_encounters rows credit the normal entry on
    // both difficulties, so nothing below needs a heroic twin.
    constexpr uint32 NPC_DRAKOS = 27654;
    constexpr uint32 NPC_VAROS  = 27447;
    constexpr uint32 NPC_UROM   = 27655;
    constexpr uint32 NPC_EREGOS = 27656;

    // The three drake-givers. Caged until Drakos dies; they walk out 3s later and
    // gain their gossip flag on arrival. Resolve them by entry — their spawn rows
    // are inside the cages.
    constexpr uint32 NPC_VERDISA       = 27657;  // Emerald
    constexpr uint32 NPC_BELGARISTRASZ = 27658;  // Ruby
    constexpr uint32 NPC_ETERNOS       = 27659;  // Amber

    constexpr uint32 NPC_RUBY_DRAKE    = 27756;
    constexpr uint32 NPC_AMBER_DRAKE   = 27755;
    constexpr uint32 NPC_EMERALD_DRAKE = 27692;

    constexpr uint32 NPC_AZURE_RING_GUARDIAN  = 27638;  // 43 hovering pickets, CanFly
    constexpr uint32 NPC_RING_LORD_SORCERESS  = 27639;
    constexpr uint32 NPC_RING_LORD_CONJURER   = 27640;
    constexpr uint32 NPC_CENTRIFUGE_CONSTRUCT = 27641;  // 10 of them gate Varos
    constexpr uint32 NPC_UNSTABLE_SPHERE      = 28166;  // Drakos; NOT_SELECTABLE
    constexpr uint32 NPC_CENTRIFUGE_CORE      = 28183;  // Varos; NOT_SELECTABLE
    constexpr uint32 NPC_AZURE_RING_CAPTAIN   = 28236;  // Varos; NOT_SELECTABLE
    constexpr uint32 NPC_ARCANE_BEAM          = 28239;  // Varos; NOT_SELECTABLE
    constexpr uint32 NPC_GREATER_LEY_WHELP    = 28276;  // Eregos adds, CanFly
    constexpr uint32 NPC_PLANAR_ANOMALY       = 30879;  // Eregos heroic; NOT_SELECTABLE

    // Urom's three Summon Menagerie packs, one per outer platform.
    constexpr uint32 NPC_PHANTASMAL_MAMMOTH      = 27642;
    constexpr uint32 NPC_PHANTASMAL_WOLF         = 27644;
    constexpr uint32 NPC_PHANTASMAL_CLOUDSCRAPER = 27645;
    constexpr uint32 NPC_PHANTASMAL_OGRE         = 27647;
    constexpr uint32 NPC_PHANTASMAL_NAGA         = 27648;
    constexpr uint32 NPC_PHANTASMAL_MURLOC       = 27649;
    constexpr uint32 NPC_PHANTASMAL_AIR          = 27650;
    constexpr uint32 NPC_PHANTASMAL_FIRE         = 27651;
    constexpr uint32 NPC_PHANTASMAL_WATER        = 27653;

    // The ONLY hostiles a drake bar spell may hit without killing its rider: the
    // three whose CanFly() is true. (The Planar Anomaly flies too but cannot be
    // targeted.) Every boss, construct, Ring-Lord and Phantasmal add is lethal.
    inline constexpr uint32 FLYING_SAFE_ENTRIES[] = {
        NPC_AZURE_RING_GUARDIAN, NPC_EREGOS, NPC_GREATER_LEY_WHELP,
    };

    // --- essences, calls, saddles ----------------------------------------------------
    constexpr uint32 ITEM_EMERALD_ESSENCE = 37815;
    constexpr uint32 ITEM_AMBER_ESSENCE   = 37859;
    constexpr uint32 ITEM_RUBY_ESSENCE    = 37860;

    // The item use-spells: 500ms cast, 15s item cooldown, summon the drake 10yd
    // ahead and 12yd up. The drake then casts its que aura on the summoner, and
    // 2.5s later the aura makes the summoner cast the saddle (CONTROL_VEHICLE).
    constexpr uint32 SPELL_CALL_EMERALD_DRAKE = 49345;
    constexpr uint32 SPELL_CALL_AMBER_DRAKE   = 49461;
    constexpr uint32 SPELL_CALL_RUBY_DRAKE    = 49462;
    constexpr uint32 SPELL_RIDE_EMERALD_DRAKE = 49346;
    constexpr uint32 SPELL_RIDE_AMBER_DRAKE   = 49460;
    constexpr uint32 SPELL_RIDE_RUBY_DRAKE    = 49464;

    // The bar, for the record: stock `occ drake attack` presses these, DC never does.
    constexpr uint32 SPELL_SEARING_WRATH       = 50232;  // Ruby
    constexpr uint32 SPELL_EVASIVE_MANEUVERS   = 50240;
    constexpr uint32 SPELL_MARTYR              = 50253;  // Ruby slot 5, post-Urom
    constexpr uint32 SPELL_SHOCK_LANCE         = 49840;  // Amber
    constexpr uint32 SPELL_STOP_TIME           = 49838;
    constexpr uint32 SPELL_TEMPORAL_RIFT       = 49592;  // Amber slot 5, channel
    constexpr uint32 SPELL_LEECHING_POISON     = 50328;  // Emerald
    constexpr uint32 SPELL_TOUCH_THE_NIGHTMARE = 50341;
    constexpr uint32 SPELL_DREAM_FUNNEL        = 50344;  // Emerald slot 5, channel

    // Eregos heroic: 18s immune, anomalies chase a random player. The scatter key.
    constexpr uint32 SPELL_PLANAR_SHIFT = 51162;

    // --- the givers' gossip ------------------------------------------------------------
    //
    // POSITIONAL options (SelectGossip maps them to the DB OptionID). Verdisa and
    // Eternos: item 0 is lore, item 1 is whichever give/swap option the bot's bags
    // allow (OptionID 1, 2 or 3 — all three store this giver's essence), item 2 is
    // lore. Belgaristrasz: his only item opens submenu 9575, and SelectGossip's
    // drill-down then selects that submenu's FIRST item — again the give/swap
    // option whatever the bags hold. So one positional option per colour covers
    // "no essence" and "the wrong essence" alike. Never gossip for the colour the
    // bot already holds: that position is then the lore option.
    constexpr uint32 GOSSIP_MENU_VERDISA       = 9573;
    constexpr uint32 GOSSIP_MENU_ETERNOS       = 9574;
    constexpr uint32 GOSSIP_MENU_BELGARISTRASZ = 9708;  // -> 9575
    constexpr float  GIVER_REACH    = 4.5f;
    constexpr float  GIVER_STANDOFF = 2.5f;
    constexpr float  GIVER_SCAN     = 60.0f;

    // A member on Drakos's ring still without its essence this long into the
    // muster is handed it (FABRICATE_ESSENCE_OFF_RING's item). The on-foot tank's
    // muster has no timeout of its own, so a giver that never answers must not be
    // able to hold the whole party. Live tp-20260913-003200-1 (tr-…-3): the tank
    // stood 63.9yd from Belgaristrasz, outside GIVER_SCAN, and four mounted riders
    // waited on it for eleven minutes.
    constexpr uint32 GIVER_WAIT_MS  = 45000;

    enum class Colour : uint8
    {
        Ruby = 0,
        Amber,
        Emerald,
    };

    // post: where the giver stands once freed — oculus.h's *POS, the MovePoint
    // npc_oculus_drakegiverAI walks it to 3s after Drakos dies. A member that
    // cannot see its giver walks here rather than waiting where it stands.
    struct ColourRow
    {
        uint32      giver;
        int32       gossipOption;
        uint32      essence;
        uint32      callSpell;
        uint32      drake;
        char const* name;
        float       postX;
        float       postY;
        float       postZ;
    };

    inline constexpr ColourRow COLOURS[3] = {
        { NPC_BELGARISTRASZ, 0, ITEM_RUBY_ESSENCE,    SPELL_CALL_RUBY_DRAKE,    NPC_RUBY_DRAKE,    "Ruby",
          941.355f, 1044.26f, 359.967f },
        { NPC_ETERNOS,       1, ITEM_AMBER_ESSENCE,   SPELL_CALL_AMBER_DRAKE,   NPC_AMBER_DRAKE,   "Amber",
          943.202f, 1059.35f, 359.967f },
        { NPC_VERDISA,       1, ITEM_EMERALD_ESSENCE, SPELL_CALL_EMERALD_DRAKE, NPC_EMERALD_DRAKE, "Emerald",
          949.056f, 1032.97f, 359.967f },
    };

    inline constexpr ColourRow const& RowFor(Colour c) { return COLOURS[static_cast<uint8>(c)]; }

    // Tank -> Ruby (Evasive Maneuvers, Martyr), healer -> Emerald (Dream Funnel),
    // everyone else -> Amber (Stop Time answers every Enraged Assault). Derived
    // from the party, so every member reaches the same answer with no shared state.
    inline constexpr Colour ColourForRole(bool isTank, bool isHealer)
    {
        return isTank ? Colour::Ruby : (isHealer ? Colour::Emerald : Colour::Amber);
    }

    // Essences come from gossip. A member that lacks one while it is NOT standing
    // on Drakos's ring (the givers never leave it) is handed the item instead,
    // loudly: walking it back is not possible from an island, and the item is
    // exactly what the gossip would have produced.
    constexpr bool FABRICATE_ESSENCE_OFF_RING = true;

    // --- gameobjects -----------------------------------------------------------------
    //
    // The Nexus Portal is a SPELLCASTER GO whose spell 49305 teleports the clicker
    // to a spell_target_position row on Drakos's ring — the only way across. The
    // Orb of the Nexus 8yd from the entrance is the EXIT (48760 -> Coldarra).
    constexpr uint32 GO_NEXUS_PORTAL       = 189985;
    constexpr uint32 SPELL_NEXUS_PORTAL    = 49305;
    constexpr uint32 GO_ORB_OF_THE_NEXUS   = 188715;
    constexpr uint32 GO_CACHE_OF_EREGOS    = 191349;
    constexpr uint32 GO_CACHE_OF_EREGOS_H  = 193603;
    constexpr float PORTAL_X = 1045.57f, PORTAL_Y = 1104.24f, PORTAL_Z = 361.07f;
    constexpr float PORTAL_LAND_X = 983.108f, PORTAL_LAND_Y = 1054.51f, PORTAL_LAND_Z = 359.967f;
    constexpr float PORTAL_RADIUS = 6.0f;
    constexpr float ORB_X = 1048.27f, ORB_Y = 991.31f, ORB_Z = 361.07f;
    constexpr float ENTRANCE_X = 1055.93f, ENTRANCE_Y = 986.85f, ENTRANCE_Z = 361.07f;

    // --- Urom ------------------------------------------------------------------------
    struct Pt3
    {
        float x, y, z;
    };
    // boss_urom.cpp cords[]: the three outer platforms he summons a pack on and
    // teleports between, then the inner arena where he is actually fought.
    inline constexpr Pt3 UROM_CORDS[4] = {
        { 1177.47f,  937.72f, 527.41f },
        {  968.66f, 1042.53f, 527.32f },
        { 1164.02f, 1170.85f, 527.32f },
        { 1118.31f, 1080.38f, 508.36f },
    };

    // --- the site table ------------------------------------------------------------------
    //
    // One row per navmesh ISLAND the party stands on. `pad` is the landing point ON
    // MESH (probed); `hover` is the altitude a leg to this site cruises at — above
    // every platform of its ring and below the next ring's lowest floor. `island*`
    // is a circle + vertical band that answers "is this unit on that island". A
    // rider lands on a small circle of `landRadius` around the pad, never on the
    // rim. `column` is where the leg changes altitude when the straight climb from
    // wherever it stands is blocked — the central shaft at (1100, 1050) has no
    // floor at any ring height.
    //
    // The two construct pads are ~90yd platforms with their whole pack (three
    // constructs and a Ring-Lord) within 13yd of the middle. Their pads sit on the
    // RIM, 31yd from the nearest mob (aggro is 21yd: detection 20, level 79 vs 80)
    // with 9yd of floor behind them and 30yd from every hovering picket, so the
    // party lands out of the fight and the clear pulls the pack to it. The island
    // circle covers the whole platform, the clear only the pack.
    enum Site : uint8
    {
        SITE_R1_GIVERS = 0,  // Drakos's ring (the portal landing, the givers)
        SITE_R1_ENTRY,       // the entrance floor
        SITE_R2C,            // Ring 2 central ring: 4 constructs + Ring-Lords
        SITE_R2S,            // Ring 2 south pad: 3 constructs + a Conjurer
        SITE_R2N,            // Ring 2 north pad: 3 constructs + a Sorceress
        SITE_R2V,            // Ring 2 Varos's platform
        SITE_R3P0,           // Urom platform 0
        SITE_R3P1,           // Urom platform 1
        SITE_R3P2,           // Urom platform 2
        SITE_R3IN,           // Urom's inner arena
        SITE_R4,             // the Ring 4 floor under Eregos
        SITE_COUNT,
        SITE_NONE = 0xFF,
    };

    struct OcSite
    {
        char const* name;
        uint8       ring;
        float       padX, padY, padZ;
        float       hoverZ;
        float       islandX, islandY, islandRadius, islandZBand;
        float       landRadius;
        float       columnX, columnY;
        float       clearX, clearY, clearZ, clearRadius;  // 0 radius => no clear step
    };

    constexpr float SHAFT_X = 1100.0f, SHAFT_Y = 1050.0f;

    inline constexpr OcSite SITES[SITE_COUNT] = {
        // name                 ring  pad (x, y, z)                   hover   island (x, y, r, zBand)          land  column (x, y)         clear (x, y, z, r)
        { "Drakos's ring",        1,  955.00f, 1046.00f, 360.06f,  372.0f,  961.0f, 1049.0f,  45.0f,  8.0f,  4.0f,  955.00f, 1046.00f,     0.0f,    0.0f,   0.0f,  0.0f },
        { "the entrance floor",   1, 1055.93f,  986.85f, 361.13f,  372.0f, 1110.0f, 1045.0f, 110.0f,  8.0f,  4.0f, 1055.93f,  986.85f,     0.0f,    0.0f,   0.0f,  0.0f },
        { "the central ring",     2, 1045.78f, 1067.77f, 432.51f,  465.0f, 1101.0f, 1052.0f,  80.0f, 10.0f,  5.0f, SHAFT_X,  SHAFT_Y,        0.0f,    0.0f,   0.0f,  0.0f },
        { "the south pad",        2, 1014.00f,  862.00f, 439.45f,  465.0f, 1036.5f,  890.5f,  48.0f,  8.0f,  3.0f, 1014.00f,  862.00f,  1034.0f,  888.0f, 439.5f, 22.0f },
        { "the north pad",        2,  997.00f, 1233.00f, 439.19f,  465.0f, 1017.0f, 1201.5f,  48.0f,  8.0f,  3.0f,  997.00f, 1233.00f,  1018.0f, 1202.0f, 439.5f, 24.0f },
        { "Varos's platform",     2, 1250.00f, 1070.00f, 439.29f,  465.0f, 1285.0f, 1070.0f,  46.0f,  8.0f,  5.0f, 1250.00f, 1070.00f,     0.0f,    0.0f,   0.0f,  0.0f },
        { "Urom's platform 0",    3, 1177.00f,  950.00f, 526.92f,  548.0f, 1177.0f,  945.0f,  25.0f,  8.0f,  3.0f, 1177.00f,  950.00f,  1177.5f,  937.7f, 527.4f, 30.0f },
        { "Urom's platform 1",    3,  975.00f, 1050.00f, 527.06f,  548.0f,  969.0f, 1043.0f,  25.0f,  8.0f,  3.0f,  975.00f, 1050.00f,   968.7f, 1042.5f, 527.3f, 30.0f },
        { "Urom's platform 2",    3, 1172.00f, 1164.00f, 527.03f,  548.0f, 1166.0f, 1168.0f,  25.0f,  8.0f,  3.0f, 1172.00f, 1164.00f,  1164.0f, 1170.9f, 527.3f, 30.0f },
        // The arena is a ring round the shaft, and Urom waits at UROM_CORDS[3] on it:
        // the pad is 31yd round the ring from him (aggro 21yd), 9yd from the rim.
        { "Urom's inner arena",   3, 1088.00f, 1087.00f, 508.52f,  548.0f, 1110.0f, 1065.0f,  40.0f,  8.0f,  4.0f, SHAFT_X,  SHAFT_Y,        0.0f,    0.0f,   0.0f,  0.0f },
        { "the Ring 4 floor",     4, 1066.00f, 1068.00f, 601.99f,  625.0f, 1066.0f, 1068.0f,  55.0f,  8.0f,  6.0f, SHAFT_X,  SHAFT_Y,        0.0f,    0.0f,   0.0f,  0.0f },
    };

    inline constexpr OcSite const& SiteRow(uint8 site) { return SITES[site < SITE_COUNT ? site : 0]; }

    // Every ground hostile the construct islands hold. The clears are filtered to
    // these so a hovering Azure Ring Guardian inside the volume is never the
    // thing the party walks off the rim after.
    inline constexpr uint32 RING2_CLEAR_ENTRIES[] = {
        NPC_CENTRIFUGE_CONSTRUCT, NPC_RING_LORD_SORCERESS, NPC_RING_LORD_CONJURER,
    };

    // --- the central ring's sweep ----------------------------------------------------
    //
    // The central ring is a HORSESHOE: its floor breaks due south of the shaft
    // (bearing ~175-190 below; the navmesh has no floor there, and a walk from one
    // end of the break to the other goes ~320yd round the whole ring). The party
    // lands on the break's west end, clear of all four hostile arcs (the old pad at
    // (1100, 1000) sat inside the south-east one, so every landing was a fight), and
    // walks the horseshoe to its other end clearing one arc at a time: south-west,
    // north-west, north-east, south-east — the only way round from the landing.
    // Bearing is atan2(y - SHAFT_Y, x - SHAFT_X) with +X north and +Y west; it FALLS
    // along the sweep. The hop splits the one long walk, across the north.
    //
    // The arcs are centred on their spawns' centroids (the `creature` table), and
    // 22yd holds every spawn of an arc with 7yd to spare. There is NO whole-ring
    // clear: a ClearRadius only certifies "clear" from within 12yd of its centre,
    // and this ring's centre is the shaft — live, that clear walked the tank off the
    // rim.
    struct OcRingStop
    {
        char const* name;
        float       x, y, z;
        float       radius;  // 0 => a MoveTo hop; else a ClearRadius of this radius
    };

    constexpr float  RING_ARC_RADIUS     = 22.0f;
    constexpr float  RING_HOP_ARRIVE     = 6.0f;
    constexpr uint32 RING_HOP_TIMEOUT_MS = 90000;

    inline constexpr OcRingStop R2C_SWEEP[] = {
        // name                  x         y        z       radius              bearing
        { "south-west arc",    1074.6f, 1102.7f, 433.0f, RING_ARC_RADIUS },  // 116
        { "north-west arc",    1124.1f, 1108.7f, 433.0f, RING_ARC_RADIUS },  //  68
        { "ring hop (north)",  1160.0f, 1050.0f, 433.0f, 0.0f },             //   0
        { "north-east arc",    1129.5f,  994.3f, 433.0f, RING_ARC_RADIUS },  // 298
        { "south-east arc",    1074.9f,  996.3f, 433.0f, RING_ARC_RADIUS },  // 245
    };

    // --- flight tuning ---------------------------------------------------------------------
    constexpr uint32 MOUNT_SETTLE_MS   = 4000;   // hold still after the Call cast
    constexpr uint32 ESSENCE_CD_MS     = 15000;
    constexpr float  LAND_TOLERANCE    = 2.5f;   // base may hover this far above the snap
    constexpr float  LAND_SNAP_2D      = 2.5f;   // ...and this far beside it
    constexpr float  LAND_HOVER        = 1.5f;   // the final waypoint sits this far above the pad
    constexpr float  LANE_SPACING      = 6.0f;   // perpendicular spacing of the cruise lanes
    constexpr float  LANE_Z            = 2.0f;   // ...and their vertical stagger
    constexpr float  LEG_ARRIVE        = 4.0f;   // 3D waypoint arrival
    constexpr float  LIFT_MIN          = 8.0f;   // first climb when the start has no hover row
    constexpr float  COLUMN_SLIDE_Z    = 8.0f;   // slide to the column this far above the start
    constexpr uint32 LEG_STALL_MS      = 20000;  // no base displacement for this long = stalled
    constexpr float  LEG_PROGRESS_YD   = 2.0f;
    constexpr uint32 LAND_WAIT_MS      = 30000;  // tank holds mounted for stragglers this long
    constexpr uint32 PAD_FALLBACK_MS   = 30000;  // lane landing failing -> land at pad centre
    constexpr uint32 MUSTER_QUORUM     = 5;
    constexpr uint32 MUSTER_MIN_ON_TIMEOUT = 4;
    constexpr uint32 MUSTER_TIMEOUT_MS = 120000;
    // A playerbots master waits this long for the members it masters to mount
    // before it takes its own saddle (see DcOculusRider::Inputs::mastersOthers).
    constexpr uint32 MASTER_MOUNT_WAIT_MS = 60000;

    // Eregos. Stations 50yd out on bearings 36 degrees apart; Planar Shift (heroic)
    // scatters 45yd further out and 15 up until it drops.
    constexpr float  EREGOS_STATION_RANGE = 50.0f;
    // Nothing brings Eregos into the fight on its own: the tank's drake closes to
    // this range with him as its current target, and stock `occ drake attack`
    // (60yd bar spells) opens. SetInCombatWithZone does the rest.
    constexpr float  EREGOS_PULL_RANGE    = 40.0f;
    constexpr float  EREGOS_LANE_DEG      = 36.0f;
    constexpr float  EREGOS_RESTATION     = 8.0f;
    constexpr float  EREGOS_ENGAGE_RANGE  = 70.0f;
    constexpr float  EREGOS_SCAN          = 200.0f;
    constexpr float  PLANAR_SCATTER_OUT   = 45.0f;
    constexpr float  PLANAR_SCATTER_UP    = 15.0f;
    constexpr float  STAGE_ARRIVE         = 20.0f;

    // Anything standing below this is in the basement under the rings: a fall.
    constexpr float  BASEMENT_Z = 300.0f;

    constexpr float  SNAP_RADIUS = 4.0f;
    constexpr float  SNAP_VERT   = 10.0f;

    constexpr uint32 MOVE_REISSUE_MS     = 1500;
    constexpr uint32 GOSSIP_RETRY_MS     = 2000;
    constexpr uint32 PLAN_MEMO_MS        = 200;    // the party plan is re-derived at most this often
    constexpr uint32 REGROUP_COOLDOWN_MS = 60000;
    constexpr uint32 TELEMETRY_MS        = 10000;
    constexpr uint32 WARN_THROTTLE_MS    = 60000;

    // --- step timeouts ---------------------------------------------------------------------
    constexpr uint32 PORTAL_TIMEOUT_MS  = 600000;   // 10 min: 120yd of entrance trash
    constexpr uint32 SITE_TIMEOUT_MS    = 900000;   // 15 min per construct island / pack
    constexpr uint32 CC_HOLD_TIMEOUT_MS = 300000;
    constexpr uint32 EREGOS_TIMEOUT_MS  = 1800000;
    // The driver's Custom step. Repeatable + Optional, so a timeout RE-ARMS it.
    constexpr uint32 DRIVER_TIMEOUT_MS  = 2400000;

    // --- event / objective ids -----------------------------------------------------------
    //
    // OBJ(n) carries event n. The plan numbers the objectives from 0; here each
    // objective's synthetic entry is OBJ(its event id), the Trial of the Champion
    // convention.
    constexpr uint32 EVENT_PORTAL  = 1;
    constexpr uint32 EVENT_R2C     = 2;
    constexpr uint32 EVENT_R2S     = 3;
    constexpr uint32 EVENT_R2N     = 4;
    constexpr uint32 EVENT_UROM_P0 = 5;
    constexpr uint32 EVENT_UROM_P1 = 6;
    constexpr uint32 EVENT_UROM_P2 = 7;
    constexpr uint32 EVENT_EREGOS  = 8;
    constexpr uint32 EVENT_DRIVER  = 9;   // conditional driver, no objective

    constexpr int32 ORDER_PORTAL  = 1;
    constexpr int32 ORDER_DRAKOS  = 2;
    constexpr int32 ORDER_R2C     = 3;
    constexpr int32 ORDER_R2S     = 4;
    constexpr int32 ORDER_R2N     = 5;
    constexpr int32 ORDER_VAROS   = 6;
    constexpr int32 ORDER_UROM_P0 = 7;
    constexpr int32 ORDER_UROM_P1 = 8;
    constexpr int32 ORDER_UROM_P2 = 9;
    constexpr int32 ORDER_UROM    = 10;
    constexpr int32 ORDER_EREGOS  = 11;

    constexpr float OBJ_ARRIVE    = 8.0f;
    constexpr float EREGOS_ARRIVE = 12.0f;

    // Urom's poke: the search that finds him from anywhere on his platform (he
    // stands 10-13yd from each pad) and loses him once Summon Menagerie has
    // teleported him on — 230yd to the next platform, ~100yd to the arena.
    constexpr float UROM_POKE_RADIUS = 40.0f;

    // ObjectiveHookRegistry ids. One flat space; 38 and 39 follow ToC's 37.
    constexpr uint32 HOOK_OC_DRIVER = 38;
    constexpr uint32 HOOK_OC_EREGOS = 39;
}

// The Oculus (578) — NINE events: the portal crossing, six anchored site clears
// (three construct islands, Urom's three platforms), the Eregos hold, and the
// conditional flight driver. See OculusEvents.cpp.
void RegisterOculusEvents(std::vector<DungeonEvent>& out);

// Karazhan (map 532) — the numbers the roster, the event rows, the hooks and the
// gtests share. The reasoning is in KarazhanEvents.cpp.
//
// TWO INDEX SPACES, and they disagree. `BIT_*` are DungeonEncounter.dbc bits (the
// completion mask). `STATE_*` are karazhan.h DATA_* boss-state slots, which only
// GetBossState reads. Opera is bit 3 but slot 4; Nightbane is bit 10 but slot 11.
namespace DcKarazhan
{
    constexpr uint32 MAP = 532;

    // Creature entries.
    constexpr uint32 NPC_MIDNIGHT    = 16151;
    constexpr uint32 NPC_MOROES      = 15687;
    constexpr uint32 NPC_MAIDEN      = 16457;
    constexpr uint32 NPC_BARNES      = 16812;
    constexpr uint32 NPC_CURATOR     = 15691;
    constexpr uint32 NPC_TERESTIAN   = 15688;
    constexpr uint32 NPC_ARAN        = 16524;
    constexpr uint32 NPC_NETHERSPITE = 15689;
    constexpr uint32 NPC_CHESS       = 22520;
    constexpr uint32 NPC_PRINCE      = 15690;
    constexpr uint32 NPC_NIGHTBANE   = 17225;

    // DungeonEncounter.dbc bits.
    constexpr uint32 BIT_ATTUMEN   = 0;
    constexpr uint32 BIT_OPERA     = 3;
    constexpr uint32 BIT_NIGHTBANE = 10;

    // karazhan.h boss-state slots.
    constexpr int32 STATE_MOROES    = 1;
    constexpr int32 STATE_OPERA     = 4;
    constexpr int32 STATE_NIGHTBANE = 11;

    // Clear order. Nightbane sits after Opera: the stage-to-terrace leg is the
    // cheapest place to fit the urn (+249yd of navmesh walking against +693 for
    // the next best slot).
    constexpr int32 ORDER_ATTUMEN       = 0;
    constexpr int32 ORDER_MOROES        = 1;
    constexpr int32 ORDER_MAIDEN        = 2;
    constexpr int32 ORDER_OPERA         = 3;
    constexpr int32 ORDER_NIGHTBANE_URN = 4;
    constexpr int32 ORDER_NIGHTBANE     = 5;
    constexpr int32 ORDER_CURATOR       = 6;
    constexpr int32 ORDER_TERESTIAN     = 7;
    constexpr int32 ORDER_ARAN          = 8;
    constexpr int32 ORDER_NETHERSPITE   = 9;
    constexpr int32 ORDER_CHESS         = 10;
    constexpr int32 ORDER_PRINCE        = 11;

    // Midnight's spawn: the stables, and the Attumen anchor.
    constexpr float MIDNIGHT_X = -11126.3f, MIDNIGHT_Y = -1929.1f, MIDNIGHT_Z = 49.3f;
    // Where the Attumen route ends and the engage starts: 18yd due west of
    // Midnight, in the mouth of his pen, the only side with a straight line in.
    constexpr float WEST_STANDOFF_X = -11126.30f, WEST_STANDOFF_Y = -1911.10f, WEST_STANDOFF_Z = 49.76f;
    // Moroes' spawn, at the north end of the Banquet Hall.
    constexpr float MOROES_X = -10982.7f, MOROES_Y = -1877.93f, MOROES_Z = 81.81f;

    // --- Opera ------------------------------------------------------------
    constexpr uint32 EV_OPERA = 1;
    // The first cast member the curtain summons, one per play (urand(1,3) per
    // instance): Oz, Red Riding Hood, Romulo and Julianne.
    constexpr uint32 NPC_DOROTHEE    = 17535;
    constexpr uint32 NPC_GRANDMOTHER = 17603;
    constexpr uint32 NPC_JULIANNE    = 17534;

    // Barnes' spawn, backstage beside Stage Door Left: the Opera objective.
    constexpr float BARNES_X = -10866.7f, BARNES_Y = -1781.0f, BARNES_Z = 90.6f;
    // The party's spot on the front stage: 8.5yd clear disc, in front of the
    // curtain (y -1772.1), 18yd from the cast line and 21.5yd from Stage Door
    // Left. The stage has no way up from the audience; it is reached only
    // through the stage doors, and Barnes shuts Stage Door Left behind the party
    // at his waypoint 8, so the party must be here by then.
    constexpr float STAGE_X = -10893.0f, STAGE_Y = -1776.0f, STAGE_Z = 90.6f;

    // --- Nightbane --------------------------------------------------------
    constexpr uint32 EV_NIGHTBANE = 2;
    constexpr uint32 GO_BLACKENED_URN = 194092;
    // mod-individual-progression's go_blackened_urn refuses a clicker without it.
    constexpr uint32 ITEM_BLACKENED_URN = 24140;

    // Where the intro path puts him down on the Master's Terrace.
    constexpr float LANDING_X = -11142.7f, LANDING_Y = -1891.2f, LANDING_Z = 92.3f;
    // The Blackened Urn (terrace floor, 1yd clear around it).
    constexpr float URN_X = -11107.8f, URN_Y = -1879.4f, URN_Z = 91.8f;
    // The urn objective is anchored ON the urn, inside the executor's 5yd use
    // range, so the click step never walks the tank out of its own arrive ring.
    // Anchored at the muster point (18.1yd off) the click step pulled the tank
    // out, the at-objective trigger dropped and Advance hauled it back
    // (tr-20260923-230203-1).
    constexpr float URN_ARRIVE_RADIUS = 4.0f;
    // The muster point: terrace floor with a 6yd clear disc, 18.8yd from the
    // landing and 18.1yd from the urn. He evades 8s after landing unless a
    // player is within 45yd of him, so the party holds here through the intro.
    constexpr float NB_MUSTER_X = -11125.0f, NB_MUSTER_Y = -1885.0f, NB_MUSTER_Z = 91.9f;

    // --- Chess (the Gamesman's Hall) --------------------------------------
    //
    // Plan: deployment-files/docs/mod-dungeon-clear_karazhan-chess_plan.md. The
    // board, the pieces and the policy are the pure kernels Util/DcChessBoard.h
    // and Util/DcChessDecision.h; the conductor and the member rung are
    // Overrides/KarazhanChessDriver.cpp and Action/DcChessPieceAction.cpp.
    constexpr uint32 EV_CHESS = 3;
    constexpr uint32 OBJ_CHESS = 3;  // the chess objective's OBJ() sequence (roster entry 0x4F000003)
    constexpr uint32 BIT_CHESS = 8;  // DBC encounter 660 — credits the Status Bar, which never dies
    constexpr uint32 HOOK_KZ_CHESS_PLAY  = 42;
    constexpr uint32 HOOK_KZ_CHESS_SETUP = 43;

    // karazhan.h instance data. NOT boss-state slots: the script never calls
    // SetBossState for chess, and none of it is saved.
    constexpr uint32 DATA_CHESS_EVENT      = 9;   // NOT_STARTED 0 / IN_PROGRESS 1 / DONE 3 / SPECIAL 4
    constexpr uint32 DATA_CHESS_TEAM       = 33;  // TeamId of the player who started it
    constexpr uint32 DATA_CHESS_GAME_PHASE = 35;
    constexpr uint32 CHESS_EVENT_DONE = 3;

    // KarazhanChessGamePhase.
    constexpr uint32 CHESS_PHASE_NOT_STARTED    = 0;
    constexpr uint32 CHESS_PHASE_PVE_WARMUP     = 1;  // Medivh talked to, King not yet taken
    constexpr uint32 CHESS_PHASE_INPROGRESS_PVE = 2;
    constexpr uint32 CHESS_PHASE_FAILED         = 3;
    constexpr uint32 CHESS_PHASE_PVE_FINISHED   = 4;  // won; Medivh's ordinal 0 is now PvP

    constexpr uint32 NPC_ECHO_OF_MEDIVH = 16816;
    constexpr uint32 GO_GAMESMAN_HALL_DOOR = 184276;       // the way in; lock-free, scriptless
    constexpr uint32 GO_GAMESMAN_HALL_EXIT_DOOR = 184277;  // opens on DONE; the only way to Prince
    constexpr uint32 GO_DUST_COVERED_CHEST = 185119;       // respawns on DONE; lock 57, loot 20712

    constexpr float ECHO_X  = -11098.8f,  ECHO_Y  = -1853.56f, ECHO_Z  = 221.15f;
    constexpr float CHEST_X = -11102.7f,  CHEST_Y = -1848.98f, CHEST_Z = 221.07f;
    // Where OnCharmed(false) teleports a released controller: a balcony on the
    // hall's west wall, board (3.45, -4.0) at z 229.6. It IS on the navmesh and
    // it IS joined to the floor — a 53yd walk down the west stair
    // (TestKarazhanChessProbe) — so a released bot walks back like anyone else.
    constexpr float WAITING_X = -11106.92f, WAITING_Y = -1843.32f, WAITING_Z = 229.626f;

    // The objective anchor, the event's first stand, the chess scenario's
    // landing and the one point every bot waits and stands at: the open floor
    // one cell off the col-0 edge, board (3.5, -1), level with Echo and ~4yd
    // from him, ~9.6yd from every wall on the navmesh. Everything further off
    // the board is a thin rim against the walls (the old anchor, board (2, -3),
    // had 0.0yd and bots looped trying to reach it).
    constexpr float HALL_X = -11096.19f, HALL_Y = -1856.44f, HALL_Z = 221.30f;

    // How far from the board's centre a bot still counts as "at the game". The
    // board's corner is 28yd from its centre and the waiting balcony 22yd; 60
    // takes in the whole hall and nothing of the corridors outside it.
    constexpr float CHESS_HALL_RADIUS = 60.0f;

    // The sideline: where each controller stands while it plays — off the col-0
    // edge, two columns of five, either side of Echo and the chest. Board
    // coordinates, rows first; every slot is navmesh floor (the probe prints the
    // whole edge). Nobody ever idles on a cell.
    constexpr float SIDELINE_ROWS[5] = { 0.5f, 1.5f, 2.3f, 4.6f, 5.6f };
    constexpr float SIDELINE_COLS[2] = { -2.4f, -3.4f };
    constexpr uint32 SIDELINE_SLOTS = 10;

    // The hall's floor is z 221 and its balconies reach z 238; another floor of
    // the tower (the Master's Terrace, z 92) lies under the same x/y, so "in the
    // hall" is a 3D test.
    constexpr float HALL_Z_MIN = 210.0f;
    constexpr float HALL_Z_MAX = 250.0f;

    // --- the chess glue (Overrides/KarazhanChessDriver.cpp) ----------------
    // Is a game on the board right now — phase WARMUP or INPROGRESS, read live?
    bool KaraChessLive(Map* map);
    // Is chess the raid's business right now: `bot` on map 532, and a game live
    // or the run's conductor armed. The cheap gate the watchdogs stand down on
    // (phantom combat, stranded recovery, the stall, the combat purge).
    bool ChessIsOn(Player* bot);
    // The member rung's trigger: ChessIsOn, `bot` alive and in the hall, and — in
    // the conductor's Loot state — the run owner only.
    bool ChessRungLive(Player* bot);
    // Is `bot` held by the game (the multiplier clamp)? Reads the stamp the rung
    // leaves on the bot's own run state, so it costs one compare per action.
    bool ChessHoldsTheBot(Player* bot, PlayerbotAI* botAI);
    // The member rung's action: the conductor (on the run owner) and this bot's
    // own seat — take its piece, hold it, stand on the sideline, fight nothing.
    // Returns whether it claims the tick.
    bool ChessRungTick(Player* bot, PlayerbotAI* botAI);
}

void RegisterKarazhanEvents(std::vector<DungeonEvent>& out);

// Is the Oculus flight driver due for `bot`'s map right now? Exposed for the rider
// trigger, which runs on every member and must agree with the driver.
bool OculusDriverDue(Player* bot);


// Every TempSummon the siege can field — the trash, the elites, the three portal
// keepers, Ichoron's globules, Xevozz's spheres and Cyanigosa. Probed by
// ALIVENESS by the wave event's activation predicate, which is sound only
// because none of them exists before the encounter creates it. The six caged
// prisoners and Erekem's guards are world spawns and are NOT here — see
// VioletHoldPrisonerEntries().
// --- Halls of Stone shared entry lists (HallsOfStoneEvents.cpp) -----------

// The three Tribunal wave adds. Every one is a TempSummon with spawnId 0, so the
// spawn store cannot see them and both the activation predicate and the wave
// driver grid-scan this list instead.
//
// It must stay COMPLETE: an entry missing here reads as "the arena is quiet",
// hands the tick back to the plain garrison, and lets that add walk to Brann —
// whose death is the encounter's only fail condition and restarts the entire
// escort. It must also stay EXCLUSIVE of the three heads, which are probed
// separately (they exist for the WHOLE fight, including the quiet first 52
// seconds, and are what arms the driver before any add has spawned).
//
// NORMAL ENTRIES ONLY, on both difficulties — see the difficulty-twin note in
// namespace DcHallsOfStone for why adding 31876/31877/31380 would be dead weight.
std::vector<uint32> const& HallsOfStoneWaveEntries();

// Kaddrak / Marnak / Abedneum, probed for mere ALIVENESS — sound because they
// exist only between InitializeEvent() and EndTribunalFight()/ResetEvent().
std::vector<uint32> const& HallsOfStoneHeadEntries();

std::vector<uint32> const& VioletHoldWaveEntries();

// Portal Guardian 30660 / Portal Keeper 30695 / 30893 — the ONLY thing whose
// death closes a keeper portal and stops its 20-second, never-ending add pump
// (npc_vh_teleportation_portal kills itself once nothing is left to channel
// 58012 on). Shared with the wave driver, which selects and travels by it.
// Deliberately disjoint from the trash: keepers never walk to the door and never
// drain the seal, and no trash mob ever closes a portal.
std::vector<uint32> const& VioletHoldKeeperEntries();

// The six caged prisoners plus Erekem's two guards. World spawns, present from
// map load behind sealed cells, so they are probed with DcVioletHold::IsReleased
// (the instance clears NON_ATTACKABLE / IMMUNE_TO_PC on release) and never for
// mere aliveness — an aliveness probe on these would read true on an inert
// dungeon and hand the wave driver the tick before the party had even entered.
std::vector<uint32> const& VioletHoldPrisonerEntries();

// --- roster patches (one appender per dungeon that corrects the boss list) -
// Each relocates that dungeon's BossRosterPatch out of BossRosterRegistry.cpp
// so a dungeon's whole clear definition lives in one file. Aggregated by
// PatchTable() (BossRosterRegistry.cpp). Only dungeons that patch the derived
// roster appear here (e.g. Shadowfang Keep / Blood Furnace have events but no
// patch, so no roster appender).
void RegisterScarletMonasteryRoster(std::vector<BossRosterPatch>& t);
void RegisterScholomanceRoster(std::vector<BossRosterPatch>& t);
void RegisterSunkenTempleRoster(std::vector<BossRosterPatch>& t);
void RegisterRazorfenDownsRoster(std::vector<BossRosterPatch>& t);
void RegisterZulFarrakRoster(std::vector<BossRosterPatch>& t);
void RegisterBlackrockDepthsRoster(std::vector<BossRosterPatch>& t);
void RegisterBlackrockSpireRoster(std::vector<BossRosterPatch>& t);
void RegisterBlackwingLairRoster(std::vector<BossRosterPatch>& t);
void RegisterDeadminesRoster(std::vector<BossRosterPatch>& t);
void RegisterWailingCavernsRoster(std::vector<BossRosterPatch>& t);
void RegisterBlackfathomDeepsRoster(std::vector<BossRosterPatch>& t);
void RegisterStratholmeRoster(std::vector<BossRosterPatch>& t);
void RegisterDireMaulRoster(std::vector<BossRosterPatch>& t);
void RegisterUldamanRoster(std::vector<BossRosterPatch>& t);
void RegisterHellfireRampartsRoster(std::vector<BossRosterPatch>& t);
void RegisterSlavePensRoster(std::vector<BossRosterPatch>& t);
void RegisterUnderbogRoster(std::vector<BossRosterPatch>& t);
void RegisterOldHillsbradRoster(std::vector<BossRosterPatch>& t);
void RegisterMechanarRoster(std::vector<BossRosterPatch>& t);
void RegisterShatteredHallsRoster(std::vector<BossRosterPatch>& t);
void RegisterSteamvaultRoster(std::vector<BossRosterPatch>& t);
void RegisterArcatrazRoster(std::vector<BossRosterPatch>& t);
void RegisterSethekkHallsRoster(std::vector<BossRosterPatch>& t);
void RegisterBlackMorassRoster(std::vector<BossRosterPatch>& t);
void RegisterMaraudonRoster(std::vector<BossRosterPatch>& t);
void RegisterUtgardeKeepRoster(std::vector<BossRosterPatch>& t);
void RegisterNexusRoster(std::vector<BossRosterPatch>& t);
void RegisterAzjolNerubRoster(std::vector<BossRosterPatch>& t);
void RegisterAhnkahetRoster(std::vector<BossRosterPatch>& t);
void RegisterDrakTharonKeepRoster(std::vector<BossRosterPatch>& t);
void RegisterVioletHoldRoster(std::vector<BossRosterPatch>& t);
void RegisterHallsOfStoneRoster(std::vector<BossRosterPatch>& t);
void RegisterGundrakRoster(std::vector<BossRosterPatch>& t);
void RegisterMoltenCoreRoster(std::vector<BossRosterPatch>& t);
// Zul'Gurub (309): High Priestess Arlokk, summoned by the Gong of Bethekk.
void RegisterZulGurubRoster(std::vector<BossRosterPatch>& t);
// Temple of Ahn'Qiraj (531): Ouro, summoned when a player reaches his mound.
void RegisterTempleOfAhnQirajRoster(std::vector<BossRosterPatch>& t);
// Utgarde Pinnacle (575) — the ONE row that makes this dungeon runnable at all
// (Svala, whose credit entry 26668 is a runtime UpdateEntry target with no spawn
// row, so BossSpawnIndex drops her silently) plus the three travel objectives the
// two areatriggers and the Stasis Generator hang off. See the namespace block
// above and UtgardePinnacleEvents.cpp for why each exists.
void RegisterUtgardePinnacleRoster(std::vector<BossRosterPatch>& t);

// Pit of Saron (658) — the ONE row that makes this dungeon finishable
// (Scourgelord Tyrannus, whose kill-credit encounter has no `creature` spawn
// because he is Rimefang's vehicle accessory, so BossSpawnIndex drops him and a
// run reports SUCCESS at 2/2) plus the ledge objective the Tyrannus areatrigger
// hangs off, and the two reorders that make room for it. See the namespace block
// above and PitOfSaronEvents.cpp.
void RegisterPitOfSaronRoster(std::vector<BossRosterPatch>& t);

// Halls of Reflection (668) — the escape row, three travel objectives and three
// reorders. Unlike Utgarde Pinnacle and Pit of Saron this map's DERIVATION is
// sound: Falric and Marwyn both have instance_encounters rows AND spawns. What
// is missing is the third encounter, "Escaped from Arthas", which has
// DungeonEncounter.dbc rows 843/844 and NO instance_encounters row at all — so
// there is no credit entry to join on and no kill bit to inherit. It is
// completed by the instance's own boss-state slot (the Nexus Frozen Commander
// shape) and anchored at the END of the escape path, so that when the escape
// driver is not running nothing walks the party TO the Lich King.
void RegisterHallsOfReflectionRoster(std::vector<BossRosterPatch>& t);

// The Culling of Stratholme (595) — EIGHT objectives and NO boss rows, plus a
// second HeroicOnly patch for the Infinite Corruptor's ninth.
//
// Nothing here repairs a derivation, because there is nothing to repair: all four
// encounters are script TempSummons with no `creature` row anywhere on map 595, so
// BossSpawnIndex emits an EMPTY list and every run before this one failed at setup
// with "no boss roster for this map". The objectives ARE the clear.
//
// They are objectives rather than MakeBossWithBit rows even though every bit
// exists, and that is the Old Hillsbrad decision rather than a limitation: an
// independently navigable boss anchor would have the clear walk the party AT
// Meathook's spawn point while the escort is still three hundred yards back with
// Arthas, and at Mal'ganis while Arthas is mid-cutscene. The kills happen INSIDE
// the escort and wave events, and the bits flip from those kills exactly as they
// would from any other.
void RegisterCullingOfStratholmeRoster(std::vector<BossRosterPatch>& t);

// Trial of the Champion (650) — THREE objectives and NO boss rows, one per
// encounter, each completed by the progress counter. BossSpawnIndex derives an
// EMPTY list here for the Culling's reason (every boss is a summon; every credit
// is a cast-spell), and a boss row would be wrong even if one could be joined:
// two of the three never die, and the third does not exist until a gossip click.
void RegisterTrialOfTheChampionRoster(std::vector<BossRosterPatch>& t);

// The Oculus (578) — THREE boss rows and EIGHT objectives. The derivation is sound
// (all four encounters are creditType 0 with real spawns); what it gets wrong is
// WHERE. Eregos spawns 53.6yd above the highest mesh, so his row can never snap
// and is replaced by an objective on the Ring 4 floor; Varos and Urom are
// re-anchored onto the ground they are fought on; and the portal, the construct
// islands and Urom's platforms are objectives the driver flies the party to.
void RegisterOculusRoster(std::vector<BossRosterPatch>& t);
// Karazhan (532) — adds Attumen (Midnight carries bit 0; the credit entry has no
// spawn), skips Chess and Prince, and replaces Barnes and the Nightbane perch
// with the event anchors that summon them. See KarazhanEvents.cpp.
void RegisterKarazhanRoster(std::vector<BossRosterPatch>& t);

// --- wing layouts (one appender per split map) ---------------------------
// Records which boss credit-entries belong to which wing of a multi-wing map;
// aggregated by DungeonWingRegistry. Only split maps appear here. Maraudon has
// no events (wings + one roster removal) and lives in MaraudonEvents.cpp; like
// Blackrock Spire its wing is chosen per run (WingSelect::Explicit).
void RegisterDireMaulWings(std::unordered_map<uint32, DungeonWingLayout>& store);
void RegisterScarletMonasteryWings(std::unordered_map<uint32, DungeonWingLayout>& store);
void RegisterMaraudonWings(std::unordered_map<uint32, DungeonWingLayout>& store);
// Blackrock Spire (229) — LBRS / UBRS, chosen per run (WingSelect::Explicit).
// Lives in BlackrockSpireEvents.cpp beside UBRS's events and roster patch.
void RegisterBlackrockSpireWings(std::unordered_map<uint32, DungeonWingLayout>& store);
// Blackrock Depths (230) — Detention Block / Upper City, chosen per run.
void RegisterBlackrockDepthsWings(std::unordered_map<uint32, DungeonWingLayout>& store);

// --- anchor routes (one appender per dungeon that hand-authors a route) ---
// Waypoint anchors StridedPathfinder walks INSTEAD of asking the navmesh
// pathfinder for a corridor, for stretches where the mesh defeats it. These
// take no `out` parameter — they call DungeonClearRouteRegistry::Register
// directly — and are invoked from DungeonClearRouteRegistry's own one-time
// seed, for the same linkage reason as the tables above.
void RegisterAzjolNerubRoute();
// Blackwing Lair (469) — Vaelastrasz -> Broodlord Lashlayer, the suppression
// rooms. Unlike the Azjol-Nerub row this is NOT there because the mesh defeats
// the pathfinder: the corridor routes fine. It is there because the SUPPRESSION
// TRANSIT needs a fixed, monotone polyline to run a cursor along — the pack
// leash, the hold decisions and the telemetry all measure against the same
// authored legs, and a route re-derived every tick from the leader's live
// position is not something a cursor can advance through. See
// DcBlackwingLair's transit block.
void RegisterBlackwingLairRoute();
// Halls of Lightning (602) — Bjarngrim -> Volkhan, the Slag Furnace. Same reason
// as the Blackwing Lair row and not the Azjol-Nerub one: the corridor routes
// fine, but the SLAG FURNACE TRANSIT needs a fixed, monotone polyline to run a
// cursor along, and the middle of that row is sliced out and handed to the driver
// (DcHallsOfLightning::TRANSIT_STAGE_ANCHOR_INDEX .. TRANSIT_END_ANCHOR_INDEX).
void RegisterHallsOfLightningRoute();
// Utgarde Pinnacle (575) — SIX rows, one per designed leg, and the Azjol-Nerub
// reason rather than the Blackwing Lair / Halls of Lightning one: nothing here
// runs a transit cursor, but the navmesh is baked DOOR-BLIND and the A* corridors
// it offers between these anchors thread two permanently shut portcullises. The
// 206yd entrance -> Ymiron shortcut is the sharpest case — the SHORTEST path in
// the dungeon and impassable for the whole run. The anchor route is what stops
// A* re-deriving it on every rebuild.
//
// Each row is keyed on the entry of the leg's DESTINATION (boss or objective) and
// STARTS AT THE PREVIOUS ONE, because SeedCursor projects the party onto the row
// from where it stands and a row that starts somewhere else snaps the cursor to
// the far end ([[dc-anchor-route-must-cover-where-the-party-stands]]).
void RegisterUtgardePinnacleRoute();
// Upper Blackrock Spire (229) — Emberseer's room -> the stadium, across the
// Rookery's 149 egg traps. The navmesh's own corridor happens to keep 3.3yd off
// them today; the measured north lane keeps 3.8yd and does not move when the
// corridor does (a different start point, an mmaps regen, a penalty row).
void RegisterBlackrockSpireRoute();

// Pit of Saron (658) — TWO rows, Krick -> the ledge and the ledge -> Tyrannus.
//
// The long row exists for BOTH of the reasons the other maps' rows exist, one
// after the other along the same leg. Over the gauntlet it is the Blackwing Lair
// / Halls of Lightning reason — a fixed polyline for the ordinary clear to walk
// while the driver owns the gates — and it carries AnchorFlag::NO_STOP across the
// three gate crossings and the icicle tunnel, because a camp dragged back down
// this corridor would put the party back inside a sphere it has already spent.
// Past the tunnel it is the plain "walk the leg" reason.
//
// Each row is keyed on the entry of its DESTINATION and STARTS WHERE THE PARTY
// WILL BE STANDING when that leg begins, because DungeonPathFollower::SeedCursor
// projects the bot onto the row from its own position and a row that starts
// somewhere else snaps the cursor to the far end
// ([[dc-anchor-route-must-cover-where-the-party-stands]]).
void RegisterPitOfSaronRoute();

// Halls of Reflection (668) — THREE rows: Marwyn -> the Frostsworn General, the
// General -> the throne room, and the whole escape corridor keyed on the Lich
// King's anchor.
//
// The first two are the plain "walk the leg" reason plus one door apiece
// (197341 and 197342, both script-only). The escape row is the Blackwing Lair /
// Halls of Lightning reason taken to its limit: the driver moves the tank stand
// point to stand point with TravelTo and never consults the row at all, but if
// it EVER yields the leg to DcRel::Advance the party must move FORWARD along
// the path and must never plan a camp on it — so every anchor carries NO_STOP
// and the row runs strictly -x, -y from the throne room to WP18.
//
// Each row is keyed on the entry of its DESTINATION and STARTS WHERE THE PARTY
// WILL BE STANDING when that leg begins
// ([[dc-anchor-route-must-cover-where-the-party-stands]]).
void RegisterHallsOfReflectionRoute();

// Karazhan (532) — the entrance to Attumen (keyed on Midnight). The plain "walk
// the leg" reason with a twist: the corridor routes fine, but it runs along the
// back wall of Midnight's pen inside the at-boss handoff range, and the row is
// what holds the engage until the party has come round to the pen's west mouth.
void RegisterKarazhanRoute();

#endif
