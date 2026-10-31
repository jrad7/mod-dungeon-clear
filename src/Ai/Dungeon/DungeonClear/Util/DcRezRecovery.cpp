/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcRezRecovery.h"

#include "Ai/Dungeon/DungeonClear/Data/DcNeverTargetRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcCombatFlag.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRunWing.h"
#include "Ai/Dungeon/DungeonClear/Util/DcStatusPublisher.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "Ai/Dungeon/DungeonClear/DcApproachState.h"
#include "Ai/Dungeon/DungeonClear/Util/ChunkedPathfinder.h"
#include "TestRun/DcTestDungeonRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DungeonClearTuning.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

namespace
{
    using DcRezDecision::Member;

    // Does a dead same-map group member hold a resurrect offer from `p`? A
    // completed resurrect raises no one: it leaves an offer, which the rez rung
    // accepts on the corpse's behalf on its next tick (DungeonClearRezPartyAction).
    // By then a cooldown-gated resurrect is on cooldown, so KnownResurrectSpell no
    // longer returns it. Without this the caster would drop out of the election
    // before it accepts, and the body would wait on an offer nobody takes up.
    bool HoldsResurrectOffer(Player const* p)
    {
        Group const* group = p->GetGroup();
        if (!group)
            return false;
        for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player const* member = ref->GetSource();
            if (member && member != p && member->GetMapId() == p->GetMapId() &&
                member->isDead() && member->isResurrectRequestedBy(p->GetGUID()))
                return true;
        }
        return false;
    }

    bool IsRezClass(Player const* p)
    {
        switch (p->getClass())
        {
            case CLASS_PRIEST:
            case CLASS_PALADIN:
            case CLASS_SHAMAN:
            case CLASS_DRUID:
                return true;
            // The other six stock classes cannot know a spell with effect 18 or
            // 113 (checked against Spell.dbc). Listing them keeps stock literally
            // unchanged and keeps them off the scan and its lock.
            case CLASS_WARRIOR:
            case CLASS_ROGUE:
            case CLASS_HUNTER:
            case CLASS_MAGE:
            case CLASS_WARLOCK:
            case CLASS_DEATH_KNIGHT:
                return false;
            default:
                // Any other class counts when it knows a resurrect of its own —
                // custom classes on forks (Conquest of Azeroth's classes 12-32:
                // Revivify, Phoenix Rebirth, Reclaim Soul, ...). Without this a
                // party made of them never has a rezzer, and every non-wipe
                // death ends the run as NoRezzer. `default:` rather than a
                // `>= MAX_CLASSES` test, since a fork that adds classes has
                // usually raised MAX_CLASSES.
                return DcRezRecovery::KnownResurrectSpell(p) != nullptr ||
                       HoldsResurrectOffer(p);
        }
    }

    bool IsResurrectSpell(SpellInfo const* info)
    {
        for (SpellEffectInfo const& effect : info->Effects)
            if (effect.Effect == SPELL_EFFECT_RESURRECT ||
                effect.Effect == SPELL_EFFECT_RESURRECT_NEW)
                return true;
        return false;
    }

    // The member whose DcRunState owns this run — DEAD OR ALIVE. This used to be a
    // copy of the walk; it now shares one body with the terminal rungs, which need
    // exactly the same dead-tolerance for exactly the same reason. See
    // DcLeaderSignal::FindRunOwner and the header comment.
    Player* ResolveRunOwner(Player* bot) { return DcLeaderSignal::FindRunOwner(bot); }

    // Is a live hostile nobody is fighting standing beside this body? Then a rezzer
    // walking to it walks into an unpulled pack — see DcRezDecision's
    // UnsafeStandDown branch. Idle only: a mob still fighting the party is the
    // engagement gate's business, and counting it would stand the recovery down
    // (and spend the episode's one announcement) in the middle of a fight that is
    // about to leave the body safe.
    bool CorpseAmongIdleHostiles(Player* corpse)
    {
        if (!corpse || !corpse->IsInWorld())
            return false;
        std::list<Unit*> nearby;
        Acore::AnyUnfriendlyUnitInObjectRangeCheck check(corpse, corpse,
                                                         DC_REZ_CORPSE_HOSTILE_RADIUS);
        Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(
            corpse, nearby, check);
        Cell::VisitObjects(corpse, searcher, DC_REZ_CORPSE_HOSTILE_RADIUS);
        for (Unit* u : nearby)
        {
            Creature* c = u ? u->ToCreature() : nullptr;
            if (!c || !c->IsAlive() || c->IsInCombat() || c->IsTotem() || c->IsCivilian() ||
                c->IsCritter() || c->IsTrigger() || c->HasReactState(REACT_PASSIVE) ||
                c->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE))
                continue;
            if (!c->IsHostileTo(corpse))
                continue;
            if (DcNeverTargetRegistry::IsNeverTarget(corpse->GetMapId(), c->GetEntry(), c->GetPositionX(),
                                                    c->GetPositionY()))
                continue;
            return true;
        }
        return false;
    }

    // Same-map group snapshot, KEEPING dead members (unlike the Smart Rest
    // snapshot — the corpses are the whole point here). `players` receives the
    // matching Player* per row so verdict indices resolve to live identities.
    void BuildSnapshot(Player* bot, Player* owner, std::vector<Member>& out,
                       std::vector<Player*>& players)
    {
        auto add = [&](Player* member)
        {
            Member m;
            m.isDead = member->isDead();
            m.canRezClass = IsRezClass(member);
            m.isHealerRole = PlayerbotAI::IsHeal(member);
            m.isTankRole = PlayerbotAI::IsTank(member);
            m.isBot = GET_PLAYERBOT_AI(member) != nullptr;
            m.corpseUnsafe = m.isDead && CorpseAmongIdleHostiles(member);
            m.isLeader = owner && member == owner;
            out.push_back(m);
            players.push_back(member);
        };

        Group* group = bot->GetGroup();
        if (!group)
        {
            add(owner ? owner : bot);
            return;
        }
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member->GetMapId() != bot->GetMapId())
                continue;
            add(member);
        }
    }

    // Instantaneous "is the party fighting" read for the clock freeze. The
    // latched variant (IsPartyEngagedLatched) is file-local to DcLeaderSignal;
    // a bare read is safe HERE because a false flicker merely clears and
    // re-stamps the clock — which RESETS the timeout budget, the forgiving
    // direction — it can never expire recovery early.
    //
    // ENGAGEMENT, not the combat flag: a resurrection cast is refused while
    // flagged ("rez party: cast 'resurrection' -> not possible yet"), so under a
    // phantom flag — a 45yd hostile area aura with nothing aggroed — the recovery
    // can never complete AND the flag froze this very clock, so the timeout that
    // is supposed to bound it never arrived. The run then held over the corpse
    // forever, inside the aura (Arcatraz heroic, tr-20260801-194932-20: hand-
    // rezzed by the player to unstick it). A real fight still freezes the clock.
    bool AnyMemberEngaged(std::vector<Player*> const& players)
    {
        for (Player* p : players)
            if (DcCombatFlag::IsEngaged(p))
                return true;
        return false;
    }

    // "Would the resurrect spell even be allowed here?" — a mirror of the exploit
    // guard in Spell::CheckCast (Spell.cpp, "Xinef: exploit protection"), which
    // refuses SPELL_EFFECT_RESURRECT outright — SPELL_FAILED_TARGET_CANNOT_BE_
    // RESURRECTED, in or out of combat — for any player casting inside a dungeon
    // whose InstanceScript reports an encounter in progress.
    //
    // Read from the CASTER's side because that is what the core reads: the guard
    // keys off m_caster's map and instance script, not the corpse's.
    bool RezBlockedByInstance(Player* p)
    {
        if (!p)
            return false;
        Map* map = p->GetMap();
        if (!map || !map->IsDungeon())
            return false;
        InstanceScript* const instance = DcTargeting::GetInstanceScript(p);
        return instance && instance->IsEncounterInProgress();
    }

    // Same read, from a bare group walk instead of a built snapshot — for
    // IsElectedRezzer, which answers a stand-down question and has no reason to
    // pay for the snapshot EvaluateImpl builds.
    bool AnySameMapMemberEngaged(Player* bot)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return DcCombatFlag::IsEngaged(bot);
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member->GetMapId() != bot->GetMapId())
                continue;
            if (DcCombatFlag::IsEngaged(member))
                return true;
        }
        return false;
    }

    // "Is there a corpse at all?" — EvaluateImpl's very first question, answered
    // from the group list instead of from the two vectors BuildSnapshot allocates
    // to ask it. IsElectedRezzer runs on every rez-class follower on every tick
    // (it gates the follower movers' stand-down), and outside a recovery the whole
    // snapshot was built only to conclude "nobody is dead".
    //
    // Ungrouped bots return true so they still fall through to EvaluateImpl, whose
    // no-group snapshot is over the resolved run OWNER, not over `bot`.
    bool AnySameMapMemberDead(Player* bot)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return true;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member->GetMapId() != bot->GetMapId())
                continue;
            if (member->isDead())
                return true;
        }
        return false;
    }

    DcRezRecovery::Plan EvaluateImpl(Player* bot, bool mutate)
    {
        DcRezRecovery::Plan plan;
        if (!bot)
            return plan;

        Player* owner = ResolveRunOwner(bot);
        PlayerbotAI* ownerAI = owner ? GET_PLAYERBOT_AI(owner) : nullptr;
        if (!ownerAI)
            return plan;
        DcRunState& run = DcRun::Of(ownerAI);
        if (!run.enabled || run.paused)
            return plan;  // a pause holds recovery too; clocks stay as-is

        std::vector<Member> members;
        std::vector<Player*> players;
        BuildSnapshot(bot, owner, members, players);

        uint32 const now = getMSTime();

        bool anyDead = false;
        for (Member const& m : members)
            anyDead = anyDead || m.isDead;

        if (!anyDead)
        {
            if (mutate && (run.rezPendingSinceMs || run.rezAnnounceMs))
            {
                // A recovery episode just completed — everyone is back up. The
                // rezzed member's low HP/mana now holds the tank via the
                // default between-pulls readiness floors until they recover.
                if (run.rezAnnounceMs)
                    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                        DcStatusPublisher::SendAddonMessage(
                            botAI, "CHAT\tParty restored \xe2\x80\x94 resuming.");
                run.rezPendingSinceMs = 0;
                run.rezAnnounceMs = 0;
            }
            // The NoRezzer floor's clocks belong to the episode that just ended,
            // too. They are otherwise only cleared by a verdict OUTSIDE the
            // no-rezzer branch, which a party with no living rez class never
            // produces — so when a corpse is raised by anything other than the rez
            // rung (a soulstone, a .revive, a human, another module's resurrect),
            // noRezzerSinceMs stays stamped from that first death, and the NEXT
            // death reads holdCapped at once and disables the run mid-fight,
            // skipping the "finishing this fight first" hold.
            if (mutate)
            {
                run.noRezzerSinceMs = 0;
                run.noRezzerQuietSinceMs = 0;
            }
            return plan;  // None / NoDeaths
        }

        // First corpse by group order, for the disable messages.
        for (std::size_t i = 0; i < members.size(); ++i)
            if (members[i].isDead)
            {
                plan.deadName = players[i]->GetName();
                break;
            }

        if (!DcRezRecovery::Enabled(bot))
        {
            // Feature off: hand the party-died trigger its classic verdict.
            plan.verdict.outcome = DcRezDecision::Outcome::Disable;
            plan.verdict.reason = DcRezDecision::Reason::Disabled;
            return plan;
        }

        // THE PULLER DIED — retire the pull it was running.
        //
        // Nothing else can. The whole pull FSM is leader-driven and the leader is
        // the corpse: DungeonClearPullTrigger and the maneuver both stand down on a
        // dead bot, and MaintainScoutCamp (which owns the orphan release) is only
        // reached through the leader election, which excludes the dead. So the
        // phase, the camp and any latched scripted stage simply persist — in the
        // MgT heroic audit the teardown snapshots show phase 3/4 held for 606-708k
        // ms, and tr-20260805-191834-3 ended at phase=4 with camp=True and all five
        // members corpses.
        //
        // That residue is not inert. A latched stage pins the pull target, forces
        // the pull mode on and stands the advance rung down; the camp keeps the
        // followers' hold-at-camp reading a camp nobody is dragging to. Both come
        // back with the tank when it is rezzed, pointed at a fight that is over.
        //
        // Clearing it here rather than in the pull code because this is the one rung
        // that runs on EVERY bot, dead leader included, and already resolves the
        // owner. Idempotent and self-limiting: the guard goes false after the first
        // pass and cannot re-fire until a live pull is re-formed.
        if (mutate && owner->isDead())
        {
            DcPullContext& pull =
                ownerAI->GetAiObjectContext()->GetValue<DcPullContext&>(DcKey::PullContext)->Get();
            if (pull.phase != DcPullPhase::Idle || pull.HasCamp() || pull.scriptedStage >= 0)
            {
                LOG_INFO("playerbots.dungeonclear",
                         "[DC:{}] the puller died mid-pull (phase {}, stage {}, camp {}) "
                         "-> pull FSM reset; the party stops holding a camp nobody is "
                         "dragging to", owner->GetName(), static_cast<uint32>(pull.phase),
                         pull.scriptedStage, pull.HasCamp() ? "set" : "none");
                pull.Reset();
            }
        }

        // Is the instance refusing the spell right now, and since when? The clock
        // clears the instant the block lifts, so the wait the kernel measures is
        // always the CURRENT block — a boss that resets and re-engages does not
        // inherit the seconds its last attempt burned.
        bool const rezBlocked = RezBlockedByInstance(bot);
        if (mutate)
        {
            if (!rezBlocked)
                run.rezBlockedSinceMs = 0;
            else if (run.rezBlockedSinceMs == 0)
                run.rezBlockedSinceMs = now ? now : 1;
        }

        // The recovery clock: runs only while nobody is FIGHTING (a fight clears
        // it, so a mid-recovery add pull resets the budget instead of burning it).
        // A block clears it for the same reason and then some: time spent unable to
        // cast must not be charged to a recovery that has not been allowed to start,
        // or the timeout fires the moment the block lifts.
        // A live chess game holds every body at the board (the rezzer included),
        // so it stops the clock too: the tank that died at the board is raised
        // once the game is over, not timed out in the middle of it.
        // Every corpse among idle hostiles stands the recovery down (see the
        // kernel), and like a block it must not spend the budget meanwhile — or the
        // corpse that comes back into play once the party clears beside it would
        // time the run out on the spot.
        bool allCorpsesUnsafe = true;
        for (Member const& m : members)
            if (m.isDead && !DcRezDecision::SkipAsUnsafe(m))
                allCorpsesUnsafe = false;
        bool const partyEngaged = AnyMemberEngaged(players);
        if (mutate)
        {
            if (partyEngaged || rezBlocked || allCorpsesUnsafe || DcKarazhan::ChessIsOn(bot))
                run.rezPendingSinceMs = 0;
            else if (run.rezPendingSinceMs == 0)
                run.rezPendingSinceMs = now ? now : 1;
        }

        // The NoRezzer floor's other input: is any LIVING survivor still carrying
        // the core combat flag? Deliberately the raw flag rather than engagement —
        // the shape it exists to catch is a boss that has stopped fighting and not
        // yet released the party (Kael'thas' 11s defeat sequence), where every
        // survivor is flagged and nothing is swinging.
        bool anySurvivorCombatFlagged = false;
        for (Player* p : players)
            if (p && p->IsAlive() && p->IsInCombat())
            {
                anySurvivorCombatFlagged = true;
                break;
            }

        DcRezDecision::Inputs in;
        in.enabled = true;
        in.nowMs = now;
        in.pendingSinceMs = run.rezPendingSinceMs;
        in.timeoutMs = DcSettings::GetUInt(bot, "PostCombatRezTimeoutSecs") * 1000;
        // Raid wipe semantics: fraction verdict + entrance regroup (the run
        // continues; DungeonClearDisableOnDeathAction routes Regroup to
        // RegroupAtEntrance instead of the disable funnel).
        if (bot->GetMap() && bot->GetMap()->IsRaid())
        {
            in.wipeFractionPct = DcSettings::GetUInt(bot, "RaidWipeFractionPct");
            in.regroupOnWipe = true;
        }
        // Five-man maps that regroup on a FULL wipe like a raid does. The Oculus: an
        // Eregos wipe is the most likely failure in the dungeon, it happens in the
        // air where no body can be reached, and dead bosses stay dead — ending the
        // run on the first one would make a battery measure nothing but that.
        else if (bot->GetMapId() == DcOculus::MAP_ID)
            in.regroupOnWipe = true;
        // Raid corpse piles get more clock: even in parallel, waves of raises
        // (each adding rezzers back) and the drinking between them take real
        // time. Scale by the corpses beyond the first, capped at twice the
        // configured budget (which itself resolves through the .Raid layer).
        if (bot->GetMap() && bot->GetMap()->IsRaid())
        {
            uint32 deadCount = 0;
            for (Member const& m : members)
                if (m.isDead)
                    ++deadCount;
            uint32 const scaled =
                in.timeoutMs + (deadCount > 1 ? (deadCount - 1) * DC_REZ_RAID_PER_CORPSE_MS
                                              : 0);
            in.timeoutMs = std::min(scaled, in.timeoutMs * 2);
        }
        in.partyEngaged = partyEngaged;
        in.anySurvivorCombatFlagged = anySurvivorCombatFlagged;
        in.noRezzerSinceMs = run.noRezzerSinceMs;
        in.noRezzerQuietSinceMs = run.noRezzerQuietSinceMs;
        in.noRezzerQuietGraceMs = DC_NO_REZZER_QUIET_GRACE_MS;
        in.noRezzerHoldMaxMs = DC_NO_REZZER_HOLD_MAX_MS;
        in.rezBlocked = rezBlocked;
        in.blockedSinceMs = run.rezBlockedSinceMs;
        in.blockedHoldMaxMs = DC_REZ_BLOCKED_HOLD_MAX_MS;

        plan.verdict = DcRezDecision::Decide(in, members);

        // Advance the floor's clocks from the verdict we just got. They can only be
        // stamped after the fact — whether a rezzer is left is precisely what the
        // kernel decides — so both run a tick behind, which is immaterial against
        // graces measured in seconds. A verdict that is not the no-rezzer branch
        // clears them, so a rezzer coming back up (or a fresh episode) starts clean.
        if (mutate)
        {
            bool const noRezzer = plan.verdict.reason == DcRezDecision::Reason::NoRezzer ||
                                  plan.verdict.reason == DcRezDecision::Reason::NoRezzerInFight;
            if (!noRezzer)
            {
                run.noRezzerSinceMs = 0;
                run.noRezzerQuietSinceMs = 0;
            }
            else
            {
                if (run.noRezzerSinceMs == 0)
                    run.noRezzerSinceMs = now ? now : 1;
                if (partyEngaged || anySurvivorCombatFlagged)
                    run.noRezzerQuietSinceMs = 0;
                else if (run.noRezzerQuietSinceMs == 0)
                    run.noRezzerQuietSinceMs = now ? now : 1;
            }
        }

        if (plan.verdict.rezzerIdx >= 0 &&
            plan.verdict.rezzerIdx < static_cast<int>(players.size()))
        {
            plan.rezzer = players[plan.verdict.rezzerIdx]->GetGUID();
            plan.rezzerName = players[plan.verdict.rezzerIdx]->GetName();
        }
        if (plan.verdict.targetIdx >= 0 &&
            plan.verdict.targetIdx < static_cast<int>(players.size()))
        {
            plan.target = players[plan.verdict.targetIdx]->GetGUID();
            plan.targetName = players[plan.verdict.targetIdx]->GetName();
        }
        for (auto const& [ri, ti] : plan.verdict.pairs)
            if (ri >= 0 && ti >= 0 && ri < static_cast<int>(players.size()) &&
                ti < static_cast<int>(players.size()))
                plan.pairs.emplace_back(players[ri]->GetGUID(), players[ti]->GetGUID());

        // One announcement per recovery episode, from whichever member
        // evaluates first (the stamp on the shared run state dedupes the rest).
        //
        // BlockedWaiting is deliberately NOT announced: it is a 20s probe of whether
        // the instance is going to let go, and announcing it would spend the episode's
        // one line on a state the party is about to leave — either into a real
        // recovery (which then has nothing left to say) or into the stand-down, which
        // is the line actually worth reading. The stand-down is not a Hold, so it is
        // named explicitly here.
        bool const blockedStandDown =
            plan.verdict.reason == DcRezDecision::Reason::BlockedStandDown;
        bool const unsafeStandDown =
            plan.verdict.reason == DcRezDecision::Reason::UnsafeStandDown;
        bool const announceable =
            blockedStandDown || unsafeStandDown ||
            (plan.verdict.outcome == DcRezDecision::Outcome::Hold &&
                                 plan.verdict.reason != DcRezDecision::Reason::BlockedWaiting);
        if (mutate && announceable && run.rezAnnounceMs == 0)
        {
            run.rezAnnounceMs = now ? now : 1;
            std::string line;
            if (blockedStandDown)
                // The one case where the run neither holds nor ends: nothing can raise
                // them here, so the survivors keep clearing and the corpse comes back
                // up when the encounter releases (or at the end of the run).
                line = plan.targetName + " died and the encounter won't allow a "
                       "resurrection \xe2\x80\x94 carrying on without them.";
            else if (unsafeStandDown)
                line = plan.targetName + " died among enemies we haven't pulled \xe2\x80\x94 "
                       "carrying on without them rather than walk into the pack.";
            else if (plan.verdict.reason == DcRezDecision::Reason::WaitingOnHuman)
                line = plan.targetName + " died \xe2\x80\x94 waiting for you to resurrect them (" +
                       std::to_string(in.timeoutMs / 1000) + "s).";
            else if (plan.verdict.reason == DcRezDecision::Reason::NoRezzerInFight)
                // No rezzer elected — there is none left. The hold is only until the
                // fight ends, at which point the verdict becomes the classic disable.
                line = plan.targetName + " died and no one left can resurrect \xe2\x80\x94 "
                       "finishing this fight first.";
            else
                line = plan.targetName + " died \xe2\x80\x94 " + plan.rezzerName +
                       " is coming to resurrect them.";
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                DcStatusPublisher::SendAddonMessage(botAI, "CHAT\t" + line);
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] post-combat rez: {} \xe2\x80\x94 {}", owner->GetName(),
                     blockedStandDown ? "the instance refuses every resurrect, clearing "
                                        "short-handed"
                     : unsafeStandDown ? "every corpse lies among idle hostiles, clearing "
                                         "short-handed"
                                       : "holding the run", line);
        }

        return plan;
    }
}

namespace DcRezRecovery
{
    bool Enabled(Player* bot)
    {
        return DcSettings::GetBool(bot, "PostCombatRez");
    }

    SpellInfo const* KnownResurrectSpell(Player const* p)
    {
        if (!p)
            return nullptr;

        // Only the SCAN is cached, never its cooldown answer. The scan walks the
        // whole spell map and runs for every member on every evaluation, so it is
        // kept for 60s: guid counter -> {candidate ids, getMSTime() of the scan}.
        // The cooldown is asked on every call. Cached along with the id, a class
        // whose only resurrect has a cooldown would stay "a rezzer" for up to 60s
        // after casting it (re-elected into a refusal), and "not a rezzer" for up
        // to 60s after the cooldown ended.
        // Shared across map-update threads, hence the lock; one small entry per
        // player that was ever evaluated.
        struct Scan
        {
            std::vector<uint32> spellIds;  // preference order, see below
            uint32 scannedMs = 0;
        };
        static std::mutex lock;
        static std::unordered_map<ObjectGuid::LowType, Scan> cache;
        constexpr uint32 CACHE_MS = 60000;

        auto const firstOffCooldown = [p](std::vector<uint32> const& spellIds)
            -> SpellInfo const*
        {
            for (uint32 spellId : spellIds)
                if (!p->HasSpellCooldown(spellId))
                    return sSpellMgr->GetSpellInfo(spellId);
            return nullptr;
        };

        uint32 const now = getMSTime();
        ObjectGuid::LowType const key = p->GetGUID().GetCounter();
        {
            std::lock_guard<std::mutex> guard(lock);
            auto const it = cache.find(key);
            if (it != cache.end() && getMSTimeDiff(it->second.scannedMs, now) < CACHE_MS)
                return firstOffCooldown(it->second.spellIds);
        }

        // Every known, active, non-passive resurrect. The ones without a recovery
        // time go first, so a plain out-of-combat resurrect wins over a
        // cooldown-gated battle rez whenever both are ready.
        Scan scan;
        scan.scannedMs = now;
        std::vector<uint32> gated;
        for (auto const& [spellId, spell] : p->GetSpellMap())
        {
            if (!spell || spell->State == PLAYERSPELL_REMOVED || !spell->Active)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
            if (!info || info->IsPassive() || !IsResurrectSpell(info))
                continue;
            if (info->RecoveryTime || info->CategoryRecoveryTime)
                gated.push_back(spellId);
            else
                scan.spellIds.push_back(spellId);
        }
        scan.spellIds.insert(scan.spellIds.end(), gated.begin(), gated.end());

        SpellInfo const* const ready = firstOffCooldown(scan.spellIds);
        std::lock_guard<std::mutex> guard(lock);
        cache[key] = std::move(scan);
        return ready;
    }

    Plan Evaluate(Player* bot)
    {
        return EvaluateImpl(bot, /*mutate*/ true);
    }

    bool IsPending(Player* leaderTank)
    {
        if (!leaderTank || !Enabled(leaderTank))
            return false;
        PlayerbotAI* ai = GET_PLAYERBOT_AI(leaderTank);
        if (!ai)
            return false;
        DcRunState const& run = DcRun::Of(ai);
        if (!run.enabled || run.paused)
            return false;

        // A dead leader is always a recovery in progress: no stand-down applies to the
        // corpse the run is driven from (DcRezDecision::LeaderCorpse), so neither may
        // the block's clock below release the hold over it.
        if (leaderTank->isDead())
            return true;

        // THE ONE CLOCK READ THIS PREDICATE MAKES, and it is here to stop a hold that
        // can never end. When the instance refuses every resurrect (see
        // RezBlockedByInstance) a corpse is not a recovery in progress, it is just a
        // corpse — and holding the run on it parks the party over a body while the
        // encounter that forbade the spell carries on around them. Match the kernel:
        // hold while the block might still lift, then stop gating on it entirely.
        //
        // An unstamped clock reads as "the block just started", so the ordering
        // caveat above still cuts the safe way: at worst this holds one tick longer
        // than the verdict does, never one tick less.
        if (RezBlockedByInstance(leaderTank) && run.rezBlockedSinceMs != 0 &&
            getMSTime() - run.rezBlockedSinceMs >= DC_REZ_BLOCKED_HOLD_MAX_MS)
            return false;

        Group* group = leaderTank->GetGroup();
        if (!group)
            return false;
        // A corpse among idle hostiles is not a recovery in progress either — the
        // kernel stands down on it (UnsafeStandDown), so holding the run over it
        // would park the party with nobody walking. Same scan as the snapshot.
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member->GetMapId() != leaderTank->GetMapId())
                continue;
            if (member->isDead() && !CorpseAmongIdleHostiles(member))
                return true;
        }
        return false;
    }

    bool CanRecover(Player* bot)
    {
        if (!bot || !Enabled(bot))
            return false;
        // Nothing can rez here, so "is a rez class alive" is not the question — the
        // run is allowed to resume and will clear short-handed. Refusing `dc on`
        // over an unraisable corpse would leave the party standing in a Violet Hold
        // that is still counting down.
        if (RezBlockedByInstance(bot))
            return true;
        Group* group = bot->GetGroup();
        if (!group)
            return bot->IsAlive() && IsRezClass(bot);
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member->GetMapId() != bot->GetMapId())
                continue;
            if (member->IsAlive() && IsRezClass(member))
                return true;
        }
        return false;
    }

    bool IsElectedRezzer(Player* bot)
    {
        // Cheapest read first — this runs on every follower rung, every tick,
        // and only a living rez class is ever elected. Non-rez classes (and
        // every bot mid-fight) never reach the group walk in EvaluateImpl.
        if (!bot || bot->isDead() || bot->IsInCombat() || !IsRezClass(bot))
            return false;
        Map* map = bot->GetMap();
        if (!map || !map->IsDungeon())
            return false;

        // DELIBERATELY NARROWER THAN THE TRIGGER: the trigger only asks whether THIS
        // bot is flagged, but this predicate hands the bot's movement to the rez rung
        // by taking it away from every other mover. Doing that while the party is
        // still swinging would let an unflagged healer walk out of a live fight to
        // start a corpse run. Recovery loses nothing by waiting — the glue clears the
        // pending clock for exactly as long as anyone is engaged, so the budget this
        // defers is not being spent.
        if (AnySameMapMemberEngaged(bot))
            return false;

        // NO CORPSE -> no recovery to be elected for. EvaluateImpl reaches the same
        // answer (its no-deaths plan is Outcome::None, which fails the Hold test
        // below) after allocating and filling the snapshot; ask it the cheap way.
        if (!AnySameMapMemberDead(bot))
            return false;

        // mutate=false: see the header. The trigger owns the clock; this is the
        // same verdict read without the side effects.
        Plan const plan = EvaluateImpl(bot, /*mutate*/ false);
        if (plan.verdict.outcome != DcRezDecision::Outcome::Hold ||
            plan.verdict.reason != DcRezDecision::Reason::Recovering ||
            PairedTargetFor(plan, bot).IsEmpty())
            return false;

        // ...AND NOTHING ALIVE IS STILL ON THE PARTY. The engagement test above is
        // `victim || attackers`, a snapshot of who is mid-swing this instant: it
        // goes false in the gap between a pack's swings, and this predicate then
        // hands the rezzer's movement to the corpse walk — out of the camp, across
        // the room, alone, into mobs that are still very much alive.
        //
        // Six of the 45 rez approaches in tp-20260808-162331-1 killed the rezzer
        // within 40s and four of those ended the run. tr-20260808-162337-8 is the
        // clean read: the tank died at 10:41 mid-pack, the healer was released the
        // same second, and was dead at 10:45 to a Sunblade Blood Knight — with the
        // healer the only rez class in the group, that was the run.
        //
        // Placed LAST on purpose. The scan costs a pathfind per combat reference,
        // and everything above already narrows this to the elected rezzer of a
        // pending recovery, which is rare. Radius-bounded so a hostile area aura
        // (45yd, nothing actually on us) cannot stall recovery forever.
        return !DcCombatFlag::AnyPartyHeldByLiveEnemy(bot, DC_FIGHT_HOLDER_RADIUS);
    }

    ObjectGuid PairedTargetFor(Plan const& plan, Player* bot)
    {
        if (!bot)
            return ObjectGuid::Empty;
        if (plan.verdict.outcome != DcRezDecision::Outcome::Hold ||
            plan.verdict.reason != DcRezDecision::Reason::Recovering)
            return ObjectGuid::Empty;
        if (plan.rezzer == bot->GetGUID())
            return plan.target;
        // Raid runs recover in PARALLEL: every paired rezzer works its own
        // corpse. Dungeons keep the single election — the second rez class
        // stays with the party (5 people, 1-2 corpses; a second walker is more
        // exposure for no time saved).
        if (bot->GetMap() && bot->GetMap()->IsRaid())
            for (auto const& pr : plan.pairs)
                if (pr.first == bot->GetGUID())
                    return pr.second;
        return ObjectGuid::Empty;
    }

    bool RegroupAtEntrance(Player* bot)
    {
        if (!bot)
            return false;
        Player* owner = ResolveRunOwner(bot);
        PlayerbotAI* ownerAI = owner ? GET_PLAYERBOT_AI(owner) : nullptr;
        if (!ownerAI)
            return false;
        Map* map = owner->FindMap();
        if (!map)
            return false;

        // The entrance teleport target comes from the dungeon catalogue (its
        // rows carry the world-DB areatrigger targets). No row -> the caller
        // falls back to the classic disable.
        //
        // On a split map the row is the RUN's wing's (row token == wing token):
        // a wiped UBRS party regroups at the UBRS entrance, a Scarlet Monastery
        // Library party at the Library's — not at whichever wing's row happens to
        // come first in the table. Any other map (or no wing resolved) takes the
        // first non-scenario row for the map, as before.
        std::string const wingToken = DcRunWing::ActiveWingToken(owner);
        DcTestDungeonRegistry::Row const* row = nullptr;
        for (DcTestDungeonRegistry::Row const& r : DcTestDungeonRegistry::All())
        {
            // A scenario's drop point is mid-instance, never the entrance.
            if (r.mapId != map->GetId() || DcTestDungeonRegistry::IsScenario(r))
                continue;
            if (!wingToken.empty() && wingToken == r.token)
            {
                row = &r;
                break;
            }
            if (!row)
                row = &r;
        }
        if (!row)
            return false;

        // THE OCULUS REGROUPS AT THE PORTAL LANDING, not the entrance. The entrance
        // floor is an island of its own: a party revived there would have to re-cross
        // the Nexus Portal, whose objective has already latched, and walk to Drakos's
        // ring for the givers anyway. The landing is where the portal's own spell puts
        // a player, beside the givers and the lift-off pad.
        float destX = row->x, destY = row->y, destZ = row->z, destO = row->o;
        if (map->GetId() == DcOculus::MAP_ID)
        {
            destX = DcOculus::PORTAL_LAND_X;
            destY = DcOculus::PORTAL_LAND_Y;
            destZ = DcOculus::PORTAL_LAND_Z;
        }

        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] raid wipe -> reviving the raid at the {} entrance and "
                 "continuing the run", owner->GetName(), row->name);

        Group* group = owner->GetGroup();
        auto const regroupMember = [&](Player* member)
        {
            if (!member || !member->IsInWorld() || member->GetMapId() != map->GetId())
                return;
            PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
            if (!memberAI)
                return;  // never revive/relocate a human — they release normally

            // End the lost fight from both sides before moving anyone: threat
            // and combat refs first, then the TARGET — CombatStop leaves
            // `current target` set, and a bot relocated with a stale target
            // walks the party straight back to the boss (the relocation-must-
            // drop-the-target lesson).
            member->GetThreatMgr().ClearAllThreat();
            member->CombatStop(true);
            member->AttackStop();
            member->SetTarget();
            memberAI->GetAiObjectContext()
                ->GetValue<Unit*>(DcKey::Stock::CurrentTarget)->Set(nullptr);

            if (!member->IsAlive() || member->HasPlayerFlag(PLAYER_FLAGS_GHOST))
            {
                // Same shape as the harness teardown revive: Spirit of
                // Redemption must come off first, no resurrection sickness, and
                // the corpse turns to bones so nothing resurrectable is left
                // behind at the boss.
                member->RemoveAurasDueToSpell(27827);
                member->ResurrectPlayer(1.0f, /*applySickness*/ false);
                member->SpawnCorpseBones();
            }
            member->TeleportTo(row->mapId, destX, destY, destZ, destO);
        };

        if (group)
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                regroupMember(ref->GetSource());
        }
        else
            regroupMember(owner);

        // Run-state hygiene on the owner: the fight is over and the raid is at
        // the entrance, so every cursor pointed at the boss room is stale. Same
        // cluster `dc on` clears, minus the run-level toggles — the run stays
        // ENABLED and re-enters the normal advance pipeline from the entrance.
        AiObjectContext* ctx = ownerAI->GetAiObjectContext();
        ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get().Reset();
        ctx->GetValue<DcApproachState&>(DcKey::ApproachState)->Get().Reset();
        ctx->GetValue<ChunkedPathfinder::Result&>(DcKey::LongPath)->Reset();
        ctx->GetValue<uint32>(DcKey::StickyBoss)->Set(0u);
        ctx->GetValue<std::string&>(DcKey::StallReason)->Get().clear();

        DcRunState& run = DcRun::Of(ownerAI);
        run.rezPendingSinceMs = 0;
        run.rezAnnounceMs = 0;
        run.noRezzerSinceMs = 0;
        run.noRezzerQuietSinceMs = 0;
        run.progress.stampMs = 0;  // stranded clock re-arms on the next evaluation

        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
            DcStatusPublisher::SendAddonMessage(
                botAI, "CHAT	The raid wiped — regrouping at the entrance "
                       "and continuing.");
        return true;
    }

    std::string DescribeWait(Player* bot)
    {
        Plan const plan = EvaluateImpl(bot, /*mutate*/ false);
        if (plan.verdict.outcome != DcRezDecision::Outcome::Hold)
            return "";
        if (plan.verdict.reason == DcRezDecision::Reason::WaitingOnHuman)
            return "Waiting for you to resurrect " + plan.targetName + ".";
        if (plan.verdict.reason == DcRezDecision::Reason::BlockedWaiting)
            return "Can't resurrect " + plan.targetName +
                   " while the encounter is in progress.";
        // No rezzer is elected on this hold, so rezzerName is empty (the panel
        // read " is coming to resurrect X.").
        if (plan.verdict.reason == DcRezDecision::Reason::NoRezzerInFight)
            return "No one left can resurrect " + plan.targetName +
                   " \xe2\x80\x94 finishing this fight first.";
        return plan.rezzerName + " is coming to resurrect " + plan.targetName + ".";
    }
}
