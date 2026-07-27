/*
 * mod-dungeon-clear — DcSpectator.cpp
 *
 * See DcSpectator.h for the design and the Free-vs-Follow split.
 *
 * Free mode's load-bearing detail: the possess branch of Unit::SetCharmedBy
 * sets UNIT_FLAG_DISABLE_MOVE on the CHARMER (the player body). Nearly every
 * MotionMaster entry point refuses a UNIT_FLAG_DISABLE_MOVE owner — including
 * the two DungeonClear uses, MovePoint (MotionMaster.cpp:488) and
 * MoveSplinePath (MotionMaster.cpp:506) — so without clearing that flag the bot
 * body freezes solid the moment the camera detaches. We clear it immediately
 * after a successful possess; it is a one-time set (nothing re-asserts it
 * during possession), and RemoveCharmedBy clears it again on teardown, so the
 * early clear is harmless on exit.
 *
 * Follow mode's load-bearing detail: Player::SetViewpoint stores the seer in
 * PLAYER_FARSIGHT via AddGuidValue, which REFUSES to overwrite a non-empty
 * field. Possession also parks the dummy there (SetClientControl ->
 * SetViewpoint). So the two modes can never be applied on top of one another —
 * every entry point tears the other down first, and ClearViewpoint below
 * force-clears the field when the watched unit has vanished, because
 * SetViewpoint(obj, false) needs a live object to hand back.
 */

#include "Util/DcSpectator.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "Chat.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "TemporarySummon.h"

#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"

namespace
{
    enum class Mode : uint8
    {
        Free,       // possessed dummy, client-steered
        Follow      // farsight on a bot, server-steered
    };

    struct SpectatorState
    {
        Mode mode = Mode::Free;
        ObjectGuid dummy;               // Free: the possessed camera TempSummon
        ObjectGuid watched;             // Follow: the unit the camera rides
        bool gmModeApplied = false;     // we turned GM mode on; undo it on Stop
        uint32 retargetAccumMs = 0;
    };

    // A Follow camera armed before its viewer finished zoning in. Started by
    // Tick once the viewer lands on the tank's map (see RequestFollowOnArrival).
    struct PendingFollow
    {
        ObjectGuid tank;
        bool gmModeApplied = false;
        uint32 ageMs = 0;
    };

    // Give up on an unfulfilled arrival request rather than latching a stale
    // intent forever — a teleport that never lands (declined, disconnected,
    // bounced back) must not ambush the GM with a camera minutes later.
    constexpr uint32 PENDING_FOLLOW_TIMEOUT_MS = 60 * 1000;

    // How often a live Follow camera re-resolves its target. Slow on purpose:
    // it only has to notice a leader re-election or a death, and every swap
    // costs a visibility rebuild around the new seer.
    constexpr uint32 RETARGET_INTERVAL_MS = 2000;

    // player guid -> camera state. Never store the dummy or the watched unit as
    // a raw pointer — resolve via ObjectAccessor at each use so a despawn race
    // can't dangle. Guarded by g_mutex: teardown hooks fire on map threads and
    // both Tick and the AI mover window read this from every map thread.
    std::mutex g_mutex;
    std::unordered_map<ObjectGuid, SpectatorState> g_spectators;
    std::unordered_map<ObjectGuid, PendingFollow> g_pending;

    // Lock-free fast path for the per-tick entry points: zero on the
    // overwhelmingly common no-spectator server, so Tick and the mover window
    // cost one relaxed atomic load per player tick. Counts pending requests
    // too — an armed request still needs Tick to run for its viewer.
    std::atomic<uint32> g_activeCount{0};

    // Snapshot a player's camera state, or false when not spectating.
    bool LookupState(Player* player, SpectatorState& out)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_spectators.find(player->GetGUID());
        if (it == g_spectators.end())
            return false;
        out = it->second;
        return true;
    }

    void Announce(Player* player, std::string const& msg)
    {
        if (player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage(msg);
    }

    bool Refuse(std::string* whyNot, char const* reason)
    {
        if (whyNot)
            *whyNot = reason;
        return false;
    }

    // Shared entry validation for both modes.
    bool CommonStartChecks(Player* player, std::string* whyNot)
    {
        // Admin gate: some servers treat detaching the player from their body as
        // a cheat. Server-only conf flag, so a player can't override it. Only the
        // entry path is gated — Stop() is always allowed so an in-progress
        // spectate can still be ended if the flag is flipped off mid-session.
        if (!DcSettings::GetBool(player, "SpectateEnable"))
            return Refuse(whyNot, "Spectator mode is disabled on this server.");
        if (!player->IsAlive())
            return Refuse(whyNot, "You are dead.");
        if (player->GetVehicle())
            return Refuse(whyNot, "Cannot spectate while on a vehicle.");
        if (player->IsInFlight())
            return Refuse(whyNot, "Cannot spectate while on a flight path.");
        if (player->GetCharmGUID())
            return Refuse(whyNot, "Cannot spectate while controlling another unit.");
        if (player->GetViewpoint())
            return Refuse(whyNot, "Cannot spectate while viewing through something else.");
        return true;
    }

    // Drop the player's farsight, live object or not. SetViewpoint(obj, false)
    // is the correct path (it also unregisters us from the target's vision
    // list), but it needs the object — when the watched bot has logged out or
    // changed map we still have to clear PLAYER_FARSIGHT ourselves, or the
    // field stays latched and the next AddGuidValue refuses.
    void ClearViewpoint(Player* player, ObjectGuid watched)
    {
        if (watched.IsEmpty())
            return;

        if (Player* target = ObjectAccessor::FindConnectedPlayer(watched))
            player->SetViewpoint(target, false);
        else
        {
            player->SetSeer(player);
            player->SetGuidValue(PLAYER_FARSIGHT, ObjectGuid::Empty);
        }
    }

    // Is this bot still worth watching from `viewer`'s seat?
    bool IsWatchable(Player* viewer, Player* target)
    {
        return target && target != viewer && target->IsInWorld() &&
               target->GetMap() == viewer->GetMap() &&
               GET_PLAYERBOT_AI(target) && target->IsAlive();
    }
}

namespace DcSpectator
{
    bool IsActive(Player* player)
    {
        if (!player)
            return false;
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_spectators.count(player->GetGUID()) != 0;
    }

    bool IsFollowing(Player* player)
    {
        if (!player)
            return false;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_spectators.find(player->GetGUID());
        return it != g_spectators.end() && it->second.mode == Mode::Follow;
    }

    ObjectGuid WatchedGuid(Player* player)
    {
        if (!player)
            return ObjectGuid::Empty;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_spectators.find(player->GetGUID());
        if (it == g_spectators.end() || it->second.mode != Mode::Follow)
            return ObjectGuid::Empty;
        return it->second.watched;
    }

    Player* ResolveWatchTarget(Player* viewer, Player* exclude)
    {
        if (!viewer)
            return nullptr;

        // In a group with the run: the elected leader tank is the run's driver,
        // and therefore the most informative seat in the house.
        if (viewer->GetGroup())
            if (Player* leader = DcLeaderSignal::FindLeaderTank(viewer))
                if (leader != exclude && IsWatchable(viewer, leader))
                    return leader;

        // Outside the party — the GM-watcher case, and the reason this function
        // exists at all. Scan the map for the bot driving a dungeon-clear run,
        // falling back to any alive tank bot. An instance holds one party, so
        // this walks a handful of players, at most once every couple of seconds.
        Map* map = viewer->GetMap();
        if (!map)
            return nullptr;

        Player* fallback = nullptr;
        Map::PlayerList const& players = map->GetPlayers();
        for (Map::PlayerList::const_iterator it = players.begin(); it != players.end(); ++it)
        {
            Player* candidate = it->GetSource();
            if (candidate == exclude || !IsWatchable(viewer, candidate))
                continue;

            // The run's driver: exactly the bot whose decisions you want to see.
            if (DcLeaderSignal::IsDungeonClearLeader(candidate))
                return candidate;

            if (!fallback && PlayerbotAI::IsTank(candidate))
                fallback = candidate;
        }

        return fallback;
    }

    bool Start(Player* player, std::string* whyNot)
    {
        if (!player)
            return Refuse(whyNot, "No player.");
        if (IsActive(player))
            return Refuse(whyNot, "Already spectating.");
        if (!CommonStartChecks(player, whyNot))
            return false;
        // Free-fly is a dungeon-clear feature: the camera flies around bots
        // running an instance. Outside a dungeon there is nothing to spectate,
        // and possessing a WORLD_TRIGGER in the open world has caused issues.
        // (Follow mode carries no such restriction — farsight is the same
        // mechanism Mind Vision uses, and works anywhere.)
        Map* map = player->GetMap();
        if (!map || !map->IsDungeon())
            return Refuse(whyNot, "Free-fly spectator mode is only available inside a dungeon.");

        // Core-defined invisible utility template — no creature_template SQL
        // row needed, runtime hardening below covers the rest.
        TempSummon* dummy = player->SummonCreature(WORLD_TRIGGER,
            player->GetPosition(), TEMPSUMMON_MANUAL_DESPAWN);
        if (!dummy)
            return Refuse(whyNot, "Failed to summon the camera.");

        dummy->SetFaction(FACTION_FRIENDLY);
        dummy->SetImmuneToAll(true);
        dummy->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_NOT_SELECTABLE);
        dummy->SetReactState(REACT_PASSIVE);

        if (!dummy->SetCharmedBy(player, CHARM_TYPE_POSSESS))
        {
            dummy->DespawnOrUnsummon();
            return Refuse(whyNot, "Possession failed.");
        }

        // Mandatory: the possess branch just set UNIT_FLAG_DISABLE_MOVE on the
        // player body, which MotionMaster::MovePoint (MotionMaster.cpp:488) and
        // MoveSplinePath (MotionMaster.cpp:506) refuse — the bot AI could no
        // longer move the body. Clear it so the body keeps clearing.
        player->RemoveUnitFlag(UNIT_FLAG_DISABLE_MOVE);

        // AFTER possession the dummy is client-controlled, so SetCanFly routes
        // SMSG_MOVE_SET_CAN_FLY to the controlling session with the proper
        // order counter. (Pre-possession the packet path differs — keep order.)
        float const speed = DcSettings::GetFloat(player, "SpectateSpeed");
        dummy->SetCanFly(true);
        dummy->SetSpeed(MOVE_FLIGHT, speed, true);
        dummy->SetSpeed(MOVE_RUN, speed, true);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            SpectatorState& state = g_spectators[player->GetGUID()];
            state.mode = Mode::Free;
            state.dummy = dummy->GetGUID();
        }
        g_activeCount.fetch_add(1, std::memory_order_release);
        Announce(player, "Spectator camera on — your character stays under bot control. Toggle again to return.");
        return true;
    }

    bool StartFollow(Player* player, Player* target, std::string* whyNot)
    {
        if (!player)
            return Refuse(whyNot, "No player.");

        // Read the arrival request's GM-mode flag BEFORE any Stop below: Stop
        // disarms a pending request, and losing this flag would strand the GM
        // invisible after the camera ends.
        bool gmModeApplied = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto pending = g_pending.find(player->GetGUID());
            if (pending != g_pending.end())
                gmModeApplied = pending->second.gmModeApplied;
        }

        // Switching modes is a legitimate one-step operation: tear the free
        // camera down first rather than making every caller sequence it (and
        // rather than letting SetViewpoint silently refuse the already-latched
        // PLAYER_FARSIGHT field).
        if (IsActive(player))
            Stop(player);

        if (!CommonStartChecks(player, whyNot))
            return false;

        if (!target)
            target = ResolveWatchTarget(player);
        if (!target)
            return Refuse(whyNot, "No bot here to follow.");
        if (!IsWatchable(player, target))
            return Refuse(whyNot, "That target can't be followed from here.");

        // Farsight: the camera rides the target and the client renders its
        // ordinary server-driven movement. No client control is granted, which
        // is exactly what makes this smooth where possession is not.
        player->SetViewpoint(target, true);

        // The body keeps its own movement input while farsighted (Mind Vision
        // behaves the same way), so root it — otherwise a blind WASD tap walks
        // the watcher through the instance while they stare at the party.
        // SetControlled, not Unit::SetRooted: the latter is protected, and
        // reaching for it would mean a core edit for a convenience root.
        player->SetControlled(true, UNIT_STATE_ROOT);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            SpectatorState& state = g_spectators[player->GetGUID()];
            state.mode = Mode::Follow;
            state.watched = target->GetGUID();
            state.retargetAccumMs = 0;
            // Carry over a GM-mode flip made by the watch entry point (snapshot
            // above), so Stop puts the GM back the way it found them.
            state.gmModeApplied = gmModeApplied;

            // This request is now fulfilled — consume it.
            if (g_pending.erase(player->GetGUID()))
                g_activeCount.fetch_sub(1, std::memory_order_release);
        }
        g_activeCount.fetch_add(1, std::memory_order_release);

        Announce(player, "Following " + target->GetName() +
                 " — your view rides the bot. `.dc spectate follow` again to stop.");
        return true;
    }

    void Stop(Player* player)
    {
        if (!player)
            return;

        SpectatorState state;
        {
            std::lock_guard<std::mutex> lock(g_mutex);

            // Drop any unfulfilled arrival request too: a Stop means the human
            // changed their mind, and an armed camera must not fire later.
            if (g_pending.erase(player->GetGUID()))
                g_activeCount.fetch_sub(1, std::memory_order_release);

            auto it = g_spectators.find(player->GetGUID());
            if (it == g_spectators.end())
                return;
            state = it->second;
            g_spectators.erase(it);
        }
        g_activeCount.fetch_sub(1, std::memory_order_release);

        if (state.mode == Mode::Follow)
        {
            ClearViewpoint(player, state.watched);
            player->SetControlled(false, UNIT_STATE_ROOT);
            player->UpdateVisibilityForPlayer();
        }
        else if (Creature* dummy = ObjectAccessor::GetCreature(*player, state.dummy))
        {
            dummy->RemoveCharmedBy(player);
            dummy->DespawnOrUnsummon();
        }
        else if (player->GetCharmGUID() == state.dummy)
        {
            // Stale half-state: the dummy is gone but the charm bookkeeping
            // still points at it. StopCastingCharm walks the generic uncharm
            // path and restores client control.
            player->StopCastingCharm();
        }

        // Put a GM we un-hid back the way we found them, so a watcher can't
        // wander off still invisible and untargetable without noticing.
        if (state.gmModeApplied && player->IsGameMaster())
            player->SetGameMaster(false);

        // Deliberately no motion-master surgery on the body here: DC's own AI
        // re-issues movement on its next tick. Per the stale-MoveFollow lesson,
        // only clean up what WE installed — the possession and the dummy.

        Announce(player, "Spectator camera off.");
    }

    void NotifyWatchedLoggingOut(Player* watched)
    {
        if (!watched || g_activeCount.load(std::memory_order_acquire) == 0)
            return;

        ObjectGuid const watchedGuid = watched->GetGUID();

        // Collect first, act second: Stop/SetViewpoint must not run under the
        // lock (they call back into core visibility code), and the viewer set
        // is at most a handful of entries.
        std::vector<ObjectGuid> viewers;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (auto const& entry : g_spectators)
                if (entry.second.mode == Mode::Follow && entry.second.watched == watchedGuid)
                    viewers.push_back(entry.first);
        }

        for (ObjectGuid const& viewerGuid : viewers)
        {
            Player* viewer = ObjectAccessor::FindConnectedPlayer(viewerGuid);
            if (!viewer)
                continue;

            // Hand the camera to another live bot when there is one, so a
            // watcher doesn't get kicked out of the run just because one member
            // logged out. Otherwise end the camera cleanly.
            Player* next = ResolveWatchTarget(viewer, watched);
            if (next)
            {
                viewer->SetViewpoint(watched, false);
                viewer->SetViewpoint(next, true);
                viewer->UpdateVisibilityForPlayer();

                std::lock_guard<std::mutex> lock(g_mutex);
                auto it = g_spectators.find(viewerGuid);
                if (it != g_spectators.end())
                    it->second.watched = next->GetGUID();
            }
            else
            {
                Stop(viewer);
            }
        }
    }

    bool Toggle(Player* player, std::string* whyNot)
    {
        if (IsActive(player))
        {
            Stop(player);
            return true;
        }
        return Start(player, whyNot);
    }

    bool ToggleFollow(Player* player, Player* target, std::string* whyNot)
    {
        if (IsFollowing(player))
        {
            Stop(player);
            return true;
        }
        return StartFollow(player, target, whyNot);
    }

    void RequestFollowOnArrival(Player* viewer, ObjectGuid tankGuid, bool gmModeApplied)
    {
        if (!viewer)
            return;

        std::lock_guard<std::mutex> lock(g_mutex);
        auto const res = g_pending.insert({viewer->GetGUID(), PendingFollow()});
        if (res.second)
            g_activeCount.fetch_add(1, std::memory_order_release);

        PendingFollow& pending = res.first->second;
        pending.tank = tankGuid;
        // A repeat request must not forget that the FIRST one hid the GM.
        pending.gmModeApplied = pending.gmModeApplied || gmModeApplied;
        pending.ageMs = 0;
    }

    void Tick(Player* player, uint32 diff)
    {
        if (!player || g_activeCount.load(std::memory_order_acquire) == 0)
            return;

        // --- pending arrival ------------------------------------------------
        ObjectGuid pendingTank;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_pending.find(player->GetGUID());
            if (it != g_pending.end())
            {
                it->second.ageMs += diff;
                if (it->second.ageMs >= PENDING_FOLLOW_TIMEOUT_MS)
                {
                    g_pending.erase(it);
                    g_activeCount.fetch_sub(1, std::memory_order_release);
                }
                else
                    pendingTank = it->second.tank;
            }
        }

        if (!pendingTank.IsEmpty())
        {
            // Wait for the loading screen to end and the viewer to actually be
            // standing on the tank's map before turning the camera on.
            Player* tank = ObjectAccessor::FindConnectedPlayer(pendingTank);
            if (tank && !player->IsBeingTeleported() && player->IsInWorld() &&
                tank->IsInWorld() && tank->GetMap() == player->GetMap())
            {
                std::string whyNot;
                if (!StartFollow(player, tank, &whyNot))
                {
                    // Refused on arrival (dead, disabled, …): report once and
                    // disarm rather than retrying every tick forever.
                    Announce(player, whyNot);
                    std::lock_guard<std::mutex> lock(g_mutex);
                    if (g_pending.erase(player->GetGUID()))
                        g_activeCount.fetch_sub(1, std::memory_order_release);
                }
            }
            return;
        }

        // --- live follow camera ----------------------------------------------
        SpectatorState state;
        if (!LookupState(player, state) || state.mode != Mode::Follow)
            return;

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_spectators.find(player->GetGUID());
            if (it == g_spectators.end())
                return;
            it->second.retargetAccumMs += diff;
            if (it->second.retargetAccumMs < RETARGET_INTERVAL_MS)
                return;
            it->second.retargetAccumMs = 0;
        }

        Player* watched = ObjectAccessor::FindConnectedPlayer(state.watched);
        if (IsWatchable(player, watched))
            return;

        // The seat went bad — the tank died, left the map, or logged out. Hand
        // the camera to whoever is still driving rather than stranding the
        // watcher on a corpse.
        Player* next = ResolveWatchTarget(player);
        if (!next || next->GetGUID() == state.watched)
            return;

        ClearViewpoint(player, state.watched);
        player->SetViewpoint(next, true);
        player->UpdateVisibilityForPlayer();

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_spectators.find(player->GetGUID());
            if (it != g_spectators.end())
                it->second.watched = next->GetGUID();
        }

        Announce(player, "Camera handed off to " + next->GetName() + ".");
    }

    void BeginBotAiMoverWindow(Player* player)
    {
        if (!player || g_activeCount.load(std::memory_order_acquire) == 0)
            return;

        SpectatorState state;
        if (!LookupState(player, state) || state.mode != Mode::Free)
            return;

        // Only swap when the mover really is our dummy — if some other charm
        // owns the mover, leave it alone.
        Unit* mover = player->m_mover;
        if (mover && mover->GetGUID() == state.dummy)
            player->m_mover = player;   // raw swap; no SetMover side effects
    }

    void EndBotAiMoverWindow(Player* player)
    {
        if (!player || g_activeCount.load(std::memory_order_acquire) == 0)
            return;

        SpectatorState state;
        if (!LookupState(player, state) || state.mode != Mode::Free)
            return;     // Stop() ran mid-window and already restored control

        if (player->m_mover != player)
            return;     // window never opened (foreign charm) — nothing to undo

        if (Creature* dummy = ObjectAccessor::GetCreature(*player, state.dummy))
            player->m_mover = dummy;
        // Dummy unresolvable while still registered: leave the mover on the
        // player; the teardown safety net will Stop() and clear the half-state.
    }
}
