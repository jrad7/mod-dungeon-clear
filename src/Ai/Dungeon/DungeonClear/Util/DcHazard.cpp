/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcHazard.h"

#include "Playerbots.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Data/DcHazardRegistry.h"

#include <cmath>
#include <limits>

namespace
{
    // Walk the cached emitter set, calling `test(row, emitterPosition)` on each.
    // Returns true on the first hit. Centralises the map lookup, the registry
    // early-out and the guid resolution so the two public predicates differ only
    // in their geometry.
    template <typename Test>
    bool AnyEmitter(Player* bot, Test&& test)
    {
        if (!bot)
            return false;
        if (!DcHazardRegistry::HasEmitters(bot->GetMapId()))
            return false;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI)
            return false;

        AiObjectContext* ctx = botAI->GetAiObjectContext();
        if (!ctx)
            return false;

        GuidVector const& hazards = ctx->GetValue<GuidVector>(DcKey::Hazards)->Get();
        for (ObjectGuid guid : hazards)
        {
            Unit* u = ObjectAccessor::GetUnit(*bot, guid);
            if (!u)
                continue;

            // Re-check the registry on the resolved unit rather than trusting the
            // cached set: the value can be up to 500ms stale, and a bot that has
            // changed map in that window would otherwise measure against an
            // emitter from the instance it just left.
            DcHazardEmitter const* e = DcHazardRegistry::Find(u->GetMapId(), u->GetEntry());
            if (!e || u->GetMapId() != bot->GetMapId())
                continue;

            if (test(*e, u))
                return true;
        }
        return false;
    }
}

bool DcHazard::PointIsHot(Player* bot, float x, float y, float z)
{
    return AnyEmitter(bot, [&](DcHazardEmitter const& e, Unit* u)
    {
        return DcHazardRegistry::PointInside(e,
                                             u->GetPositionX(), u->GetPositionY(), u->GetPositionZ(),
                                             x, y, z);
    });
}

bool DcHazard::SegmentIsHot(Player* bot, float ax, float ay, float az,
                            float bx, float by, float bz)
{
    return AnyEmitter(bot, [&](DcHazardEmitter const& e, Unit* u)
    {
        return DcHazardRegistry::SegmentClips(e,
                                              u->GetPositionX(), u->GetPositionY(), u->GetPositionZ(),
                                              ax, ay, az, bx, by, bz);
    });
}

bool DcHazard::LegIsHot(Player* bot, float x, float y, float z)
{
    if (!bot)
        return false;
    return SegmentIsHot(bot,
                        bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                        x, y, z);
}

DcHazard::VacateEmitter DcHazard::NearestVacate(Player* bot)
{
    VacateEmitter best;
    if (!bot)
        return best;
    if (!DcHazardRegistry::HasEmitters(bot->GetMapId()))
        return best;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return best;
    AiObjectContext* ctx = botAI->GetAiObjectContext();
    if (!ctx)
        return best;

    float bestDistSq = std::numeric_limits<float>::max();
    GuidVector const& hazards = ctx->GetValue<GuidVector>(DcKey::Hazards)->Get();
    for (ObjectGuid guid : hazards)
    {
        Unit* u = ObjectAccessor::GetUnit(*bot, guid);
        if (!u || !u->IsAlive())
            continue;

        DcHazardEmitter const* e = DcHazardRegistry::Find(u->GetMapId(), u->GetEntry());
        if (!e || u->GetMapId() != bot->GetMapId())
            continue;

        // Not an active-vacate emitter (a fought creature / a plain avoid).
        if (e->vacateRadius <= 0.0f)
            continue;

        // Only if the bot is inside the danger band — the pulse radius plus the
        // hysteresis StayBand, so a bot already parked at pulse+slack still reads
        // as in-danger and holds there (no ping-pong). Same-floor: a pulse one
        // level down does not reach.
        if (std::fabs(u->GetPositionZ() - bot->GetPositionZ()) > e->zBand)
            continue;
        float const reach = e->vacateRadius + VacateStayBand;
        float const distSq = bot->GetExactDist2dSq(u);
        if (distSq > reach * reach)
            continue;

        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            best.ok = true;
            best.x = u->GetPositionX();
            best.y = u->GetPositionY();
            best.z = u->GetPositionZ();
            best.pulseRadius = e->vacateRadius;
        }
    }
    return best;
}
