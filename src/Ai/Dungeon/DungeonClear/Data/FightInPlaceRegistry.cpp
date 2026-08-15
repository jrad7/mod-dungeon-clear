/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "FightInPlaceRegistry.h"

namespace
{
    // The rooms, sized off live spawn data (acore_world.creature on map 585):
    //   * Selin Fireheart spawns at X=242.1, Y=0.3; his CanAIAttack plane is X>216.
    //   * His 14 room-guard spawns (24688/24689/24690) sit at X 222.3-231.7,
    //     Y -23.0..+23.8 — the whole occupied room.
    //   * The antechamber Sunblade trash top out at X=182.3 (all X<216), so they
    //     stay normally pullable — the box floor at X=216 (Selin's own gate) keeps
    //     them out.
    //   * The instance's other encounters are far outside: Priestess Delrissa at
    //     X=126.9, and Vexallus at X=231.4 but Y=-214.3 — the Y band [-45,45]
    //     excludes him cleanly.
    // So [216,260] x [-45,45] is exactly Selin's room and nothing else.
    FightInPlaceZone const kZones[] =
    {
        { 585, 216.0f, 260.0f, -45.0f, 45.0f },  // Magisters' Terrace — Selin Fireheart's room
    };
}

bool FightInPlaceRegistry::IsNoPullZone(uint32 mapId, float x, float y)
{
    for (FightInPlaceZone const& z : kZones)
    {
        if (z.mapId != mapId)
            continue;
        if (x >= z.minX && x <= z.maxX && y >= z.minY && y <= z.maxY)
            return true;
    }
    return false;
}
