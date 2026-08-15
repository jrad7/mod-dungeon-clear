/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCHAZARD_H
#define _PLAYERBOT_DCHAZARD_H

#include "Define.h"

class Player;

// The LIVE half of DcHazardRegistry: "is this spot / this leg standing in a
// persistent damage aura?" See DcHazardRegistry.h for the mechanism and why it
// is split from the route half.
//
// MAP THREAD ONLY. These resolve live creature positions through
// DungeonClearHazardsValue (500ms-cached) and must never be called from the
// DcPathWorker thread — the route producer avoids hazards via the
// DcNavPenaltyRegistry boxes instead, which need no game state at all.
//
// Both predicates take the cheap DcHazardRegistry::HasEmitters(mapId) early-out
// first, so a map with no rows pays one bool per call and nothing else.
namespace DcHazard
{
    // Extra yards beyond an emitter's vacate radius. `RetreatSlack` is how far PAST
    // the rim the retreat AIMS; `StayBand` is how far out the bot still counts as
    // in-danger.
    //
    // RetreatSlack > StayBand ON PURPOSE, so the retreat OVERSHOOTS the danger band
    // and the bot stops reading in-danger once it arrives — the trigger goes inert
    // and normal driving (advance / follow) resumes, carrying the party ONWARD past
    // the corpse. It does NOT pin the party at the rim until the summon despawns:
    // the Destroyed Sentinel can linger a long time, and there is nothing to hold
    // for — the summon is unattackable, so unlike a live-mob fight nothing pulls a
    // cleared bot back toward it. A thin StayBand (just outside the real pulse) is
    // all that's needed to fire the retreat before a drifting bot re-clips the
    // damaging radius; the advancing party then simply leaves.
    inline constexpr float VacateRetreatSlack = 6.0f;
    inline constexpr float VacateStayBand     = 2.0f;

    // True when (x,y,z) lies inside any live emitter's keep-out cylinder.
    // The test to use when validating a point the party will STAND on: a camp
    // anchor, a per-bot camp slot, a standoff position.
    bool PointIsHot(Player* bot, float x, float y, float z);

    // True when the straight segment (ax,ay,az)->(bx,by,bz) clips any live
    // emitter's keep-out cylinder. The test to use when validating something the
    // party will WALK ALONG, since a leg can pass straight through an emitter
    // while both of its endpoints sit clear.
    //
    // Callers walking a PathGenerator polyline must feed CONSECUTIVE PAIRS to
    // this, never the vertices to PointIsHot: PathGenerator returns string-pulled
    // CORNER points, not a densified line, so an open-room leg is often just
    // {start, end} and a per-vertex scan degenerates to testing the destination
    // alone — reintroducing the exact hole this function exists to close.
    bool SegmentIsHot(Player* bot, float ax, float ay, float az,
                      float bx, float by, float bz);

    // Convenience wrapper: SegmentIsHot from `bot`'s current position to (x,y,z).
    bool LegIsHot(Player* bot, float x, float y, float z);

    // The nearest live active-vacate hazard emitter (DcHazardEmitter::vacateRadius
    // > 0 — the Destroyed Sentinel's persistent pulse) whose radius the bot is
    // standing inside (same-floor). Returned as its live position + the raw pulse
    // radius, so the retreat action can compute a point to clear to. `ok == false`
    // when the bot is clear of every vacate emitter.
    //
    // Presence-based, not aura-based: a vacate emitter is dangerous the whole time
    // it exists (the summon pulses permanently), so there is no wind-up signal to
    // wait for — being inside its radius is the signal.
    //
    // MAP THREAD ONLY, same as the predicates above. This is what the retreat
    // (DungeonClearHazardVacate{Trigger,Action}) fires on.
    struct VacateEmitter
    {
        bool  ok{false};
        float x{0.0f}, y{0.0f}, z{0.0f};
        float pulseRadius{0.0f};
    };
    VacateEmitter NearestVacate(Player* bot);
}

#endif
