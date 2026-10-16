/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCREZRECOVERY_H
#define _PLAYERBOT_DCREZRECOVERY_H

#include <string>
#include <utility>
#include <vector>

#include "ObjectGuid.h"

#include "Ai/Dungeon/DungeonClear/Util/DcRezDecision.h"

class Player;
class SpellInfo;

// Engine glue for post-combat party resurrection (see DcRezDecision.h for the
// kernel semantics). This is the only place the feature touches game objects:
// it resolves the run owner, snapshots the same-map group (KEEPING dead
// members), reads the settings via DcSettings, stamps/clears the recovery
// clock on the owner's DcRunState, runs the pure kernel, and emits the
// (deduped) party-chat announcements.
//
// RUN-OWNER RESOLUTION IS DEAD-TOLERANT. DcLeaderSignal::FindLeaderTank only
// elects among ALIVE tank bots, so when the leader itself is the corpse the
// election returns null (or a different tank whose own run state is default).
// Recovery must keep working exactly then — a healer follower has to walk over
// and raise its tank — so ResolveRunOwner falls back to scanning the same-map
// group for the member whose OWN DcRunState is enabled (the run owner), dead
// or alive. All clocks/announce stamps live on that owner's run state and are
// written cross-bot (the same single-threaded access DcSmartRest relies on),
// dying with DcRunState::Reset() automatically.
namespace DcRezRecovery
{
    // DungeonClear.PostCombatRez, per-run override -> conf -> default.
    bool Enabled(Player* bot);

    // A non-passive resurrect spell (SPELL_EFFECT_RESURRECT / _RESURRECT_NEW)
    // that `p` knows and that is not on cooldown, or nullptr. The fallback for
    // classes outside the stock Priest/Paladin/Shaman/Druid switch: servers
    // with custom classes give those classes their own resurrects, which the
    // switch cannot know. Prefers a spell with no recovery time over a
    // cooldown-gated battle rez. Cached per player for 60s (it is read for
    // every member on every evaluation).
    SpellInfo const* KnownResurrectSpell(Player const* p);

    // The kernel verdict resolved to live identities. rezzer/target are only
    // meaningful for Hold outcomes; deadName always names the first same-map
    // corpse (for disable messages).
    struct Plan
    {
        DcRezDecision::Result verdict;
        ObjectGuid  rezzer;      // elected rezzer (the human when WaitingOnHuman)
        ObjectGuid  target;      // dead member to raise first
        std::string rezzerName;
        std::string targetName;
        std::string deadName = "Someone";
        // Parallel pairing, resolved to GUIDs (see DcRezDecision::Result::pairs).
        // Consumed on RAID runs only — each paired rezzer walks to its own
        // corpse; 5-mans keep the single rezzer/target above.
        std::vector<std::pair<ObjectGuid, ObjectGuid>> pairs;
    };

    // The corpse `bot` is assigned to raise under `plan`, or Empty when it is
    // not an elected rezzer. Honors the map scoping above: the single election
    // everywhere, plus the parallel pairs on a raid map.
    ObjectGuid PairedTargetFor(Plan const& plan, Player* bot);

    // Evaluate recovery for `bot`'s run. Callable by ANY member every tick
    // (leader and followers compute the same deterministic answer); maintains
    // the pending clock + announce dedup on the run owner's DcRunState as a
    // side effect, and emits the start/resume announcements. Returns
    // outcome None when there is no enabled+unpaused run, no deaths, or —
    // deliberately — while the run is PAUSED (a pause holds recovery too).
    // With the feature disabled and a death present it returns
    // Disable/Disabled so the party-died trigger keeps its classic behavior.
    Plan Evaluate(Player* bot);

    // Cheap hold-gate read for the leader's readiness sites (between-pulls,
    // event rest): true while the run should hold for a recovery — feature on,
    // run enabled+unpaused, and any same-map group member dead. Deliberately
    // stateless (a group walk, not a clock read) so gate ordering within a
    // tick can never race the clock stamping.
    bool IsPending(Player* leaderTank);

    // Chat-path relaxation (`dc on` / resume with corpses): true when the
    // feature is on and at least one LIVING same-map member's class can rez —
    // enabling is allowed and the recovery flow takes it from there.
    bool CanRecover(Player* bot);

    // "Is THIS bot the one that has to walk to a corpse right now" — the
    // stand-down gate for every follower rung that moves (follow-tank /
    // scout-lag, hold-at-camp, heal-reposition). Tracks
    // DungeonClearRezPartyTrigger::IsActive — same alive / out-of-combat /
    // in-a-dungeon preconditions, same Hold+Recovering+this-bot verdict — so a
    // rung stands down on the ticks the rez rung is armed and not on others.
    //
    // One deliberate narrowing on top of the trigger: false while ANY same-map
    // member is engaged. Handing the bot's movement to the rez rung is a bigger
    // claim than arming that rung, and it must not strand a healer that happens
    // to be unflagged in the middle of a live fight. Costs the recovery nothing
    // — its budget clock is stopped for as long as anyone is engaged.
    //
    // READ-ONLY, unlike Evaluate: a rung asking "is this me" must not stamp the
    // recovery clock or fire an announcement. Cheap-early-outs on class before
    // it evaluates anything, because only a living rez class is ever elected.
    bool IsElectedRezzer(Player* bot);

    // RAID full-wipe recovery: revive every same-map BOT member at the
    // instance entrance (dungeon-catalogue coords), drop the lost fight's
    // combat/threat/targets, reset the owner's approach cursors, and leave the
    // run ENABLED to continue from the entrance. Humans are never revived or
    // relocated. False when no entrance row exists for the map (the caller
    // falls back to the classic disable).
    bool RegroupAtEntrance(Player* bot);

    // "Neko is coming to resurrect Bib." / "Waiting for you to resurrect
    // Bib." — one status-panel sentence for the current recovery, empty when
    // none is in progress. Read-only (no clock side effects).
    std::string DescribeWait(Player* bot);
}

#endif  // _PLAYERBOT_DCREZRECOVERY_H
