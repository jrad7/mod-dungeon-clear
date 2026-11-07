/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCTESTRUNMANAGER_H
#define _PLAYERBOT_DCTESTRUNMANAGER_H

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "ObjectGuid.h"
#include "TestRun/DcTestComp.h"
#include "TestRun/DcTestGearTiers.h"
#include "TestRun/DcTestRunJob.h"

class Player;

// Registry of live `.dc test` runs. Each run is a DcTestRunJob owning its own
// 5-bot party, instance, watchdog and record; the manager only launches jobs,
// ticks them, routes the two observer callbacks to the right job by tank guid,
// arbitrates the shared bot pool, and writes the one multi-run live-status file
// the dashboard polls. Any number of runs execute at once — uncapped by default
// (DungeonClear.TestRun.MaxConcurrent, 0 = unlimited), so the real ceiling is
// the addclass pool (a start with no free character for a comp slot is refused
// by name) and the machine. Playerbots.MaxAddedBots does not apply: the
// harness's own adds are exempt from it (DcTestRunJob::Create).
//
// Threading rules:
//   * _runs is mutated ONLY on the world thread (Start push_back, Tick erase)
//     but read by observer callbacks that fire from map-update threads. Lock
//     _runsMutex in: Start's push_back, Tick's erase pass, and both observer
//     callbacks for the lookup PLUS the forwarded call (so a job can't be
//     erased out from under a map thread mid-callback). WriteLiveStatus locks
//     it while gathering per-job snapshots. World-thread reads (Tick iteration,
//     StatusText, Stop, cap/budget checks in Start) need no lock.
//   * _reservedGuids and _provisionRR are world-thread only.
//   * Each job owns its own _obsMutex for its observer-written fields, so N
//     runs' observers never contend with each other.
class DcTestRunManager
{
public:
    static DcTestRunManager& Instance();

    // Machine-readable Start rejection class, so the plan scheduler can tell a
    // transient condition (retry with backoff) from a permanent one (abort the
    // plan) without matching on the human message.
    enum class StartErr : uint8
    {
        None,
        UnknownDungeon,  // permanent
        NoMgr,           // permanent
        CapHit,          // transient — a run will finish
        PoolExhausted,   // transient — other runs hold the pool chars
        BadRoster,       // permanent — malformed party=, unknown/duplicate name
        FactionMismatch, // permanent — a roster cannot span factions
        CharacterOnline, // transient — a human is playing that character
        CharacterBusy    // transient — another live run holds that character
    };

    // Validate + launch a run. On success sets *msg to the start confirmation
    // and returns true; on failure sets *msg to the reason and returns false.
    // Rejections: unknown dungeon, heroic on a row without a heroic mode, no
    // playerbot manager, concurrency cap hit, or no free pool character for a
    // comp class.
    // seed 0 rolls a random comp; a nonzero seed reproduces a specific comp.
    // heroic runs the instance at DUNGEON_DIFFICULTY_HEROIC with the row's
    // heroicLevel as the default bot level (level override still wins).
    // planId ties the run to a `.dc test plan` campaign ("" = ad-hoc); errOut /
    // runIdOut are optional feedback for the plan scheduler.
    // gear is the run's own item-level / quality ceiling; a default-constructed
    // Spec inherits the Playerbots.AutoGear* conf values.
    // size 0 = the classic 5-man comp; 2-40 fields a sized (raid) comp — see
    // DcTestComp::RoleQuota / DcTestRunJob::Create.
    bool Start(Player* gm, std::string const& dungeonToken, uint32 levelOverride, uint32 seed,
               bool heroic, DcTestGearTiers::Spec const& gear, std::string* msg,
               std::string const& planId = "", StartErr* errOut = nullptr,
               std::string* runIdOut = nullptr, uint32 size = 0);

    // Validate + launch a HAND-PICKED party (`.dc test start <d> party=a,b,c,d,e`).
    // `partySpec` is the raw comma-separated name list; roles are positional
    // (tank, heal, dps, dps, dps — see DcTestRoster).
    //
    // Refuses on: malformed list, a name no character answers to, a character a
    // human is currently playing, a character another live run already holds, and
    // a roster spanning both factions (which cannot be grouped). Level comes from
    // the characters, so there is no level override and no comp seed.
    //
    // The characters are NOT respecced, regeared, or releveled — see
    // DcTestRunJob::CreateFromRoster.
    bool StartRoster(Player* gm, std::string const& dungeonToken, std::string const& partySpec,
                     bool heroic, std::string* msg, std::string const& planId = "",
                     StartErr* errOut = nullptr, std::string* runIdOut = nullptr);

    // Launch a Pull Lab party (src/Lab): the ordinary pool party of exactly
    // `comp`, handed to `driver` once it is standing in a fresh instance of
    // `dungeonToken`. Same validation and pool reservation as Start; the job
    // shows in `.dc test status` like any run.
    bool StartLab(Player* gm, std::string const& dungeonToken, uint32 levelOverride, uint32 seed,
                  bool heroic, DcTestGearTiers::Spec const& gear,
                  std::vector<DcTestComp::Slot> const& comp,
                  std::shared_ptr<DcTestRunJob::LabDriver> driver, std::string* msg,
                  StartErr* errOut = nullptr, std::string* runIdOut = nullptr);

    // Stop the run(s) the selector resolves to (see DcTestRunSelect). Bare
    // selector = the single active run. False (with an explanatory *msg) on
    // no-runs / ambiguous / not-found.
    bool Stop(std::string const& selector, std::string* msg);

    // Resolve the selector to ONE run's leader tank, for `.dc test watch` — the
    // GM camera needs a single seat, so "all" and a bare selector with several
    // runs live are both refused with the run list in *msg (same selector
    // grammar and messages as Stop, minus the fan-out). *tokenOut carries the
    // run's dungeon token so the watcher can land at that row's entrance rather
    // than on top of the tank.
    bool WatchTarget(std::string const& selector, ObjectGuid* tankOut,
                     std::string* msg, std::string* tokenOut = nullptr) const;

    // `.dc test watch next` — the run AFTER the one `watcher` is currently
    // sitting in, wrapping, so one repeated command tours every live run
    // without anyone having to read a runId off the status list.
    //
    // "Currently sitting in" is the watcher's own map INSTANCE: watching is
    // farsight from a body parked at that run's entrance, and every run gets a
    // fresh copy, so the instance the watcher stands in names the run they are
    // on with no extra bookkeeping to go stale.
    //
    // Only runs whose tank is already in world are candidates — a run still
    // spawning its party has no seat to offer, and skipping it here is what
    // keeps `next` from stalling the tour on a run that cannot be watched yet.
    // False (with *msg) when there is nowhere to hop: no watchable run, or the
    // only one is the one already up. See DcTestRunSelect::NextWatchIndex.
    bool NextWatchTarget(Player* watcher, ObjectGuid* tankOut,
                         std::string* msg, std::string* tokenOut = nullptr) const;

    // `.dc test start ... trace=1`: arm the Pull Lab trace on a run just
    // started (by its runId). False when no live run has that id.
    bool EnableTrace(std::string const& runId);

    std::string StatusText() const;
    bool IsActive() const { return !_runs.empty(); }

    // Remaining global-cap headroom for the plan scheduler's launch decision
    // (UINT32_MAX when MaxConcurrent is 0 = unlimited). World-thread read.
    uint32 CapHeadroom() const;

    // Is this pool character already claimed by a live run? World-thread only.
    // Read by the RDF queue fill, which draws from the same addclass pool and
    // must not hand a second subsystem a character a run is about to log in.
    bool IsReserved(ObjectGuid guid) const
    {
        return _reservedGuids.find(guid) != _reservedGuids.end();
    }

    // Drive every live job from the global playerbots tick (world thread).
    void Tick(uint32 diff);

    // Observer: every DisableDungeonClear lands here; routed to the monitoring
    // job whose leader tank matches (no-op otherwise).
    void OnRunDisabled(Player* leader, std::string const& reason);

    // Observer: DcStatusPublisher pushes each changed STATUS payload here;
    // routed to the job whose tank matches.
    void OnStatusPayload(ObjectGuid tank, std::string const& payload);

    // Observer: the core's loot hooks report every rare-or-better item a player
    // receives here; routed to the monitoring job whose party holds `looter`
    // (no-op otherwise).
    void OnLoot(Player* looter, DcTestRunRecord::LootEntry const& entry);

private:
    DcTestRunManager() = default;

    // Collect a snapshot per job and truncate-write the multi-run live file.
    void WriteLiveStatus();

    // Newline-joined StatusLine per run, for ambiguous/not-found messages.
    std::string ListActiveRuns() const;

    std::vector<std::unique_ptr<DcTestRunJob>> _runs;
    mutable std::mutex _runsMutex;                  // guards _runs vs observer threads
    std::unordered_set<ObjectGuid> _reservedGuids;  // world-thread only
    uint32 _liveAccumMs = 0;                        // live-file throttle
    bool _liveWasActive = false;                    // for the final active:false write
    std::size_t _provisionRR = 0;                   // round-robin tick offset
};

#endif  // _PLAYERBOT_DCTESTRUNMANAGER_H
