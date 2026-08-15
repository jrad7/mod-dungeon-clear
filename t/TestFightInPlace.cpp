/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "Ai/Dungeon/DungeonClear/Data/FightInPlaceRegistry.h"

// Selin Fireheart's room (Magisters' Terrace, map 585). The registry must veto the
// advanced pull for anything inside the room (X>216, the boss's own CanAIAttack
// plane) while leaving the antechamber and the instance's other encounters pullable.

TEST(FightInPlaceTest, SelinRoomGuardsAreInTheZone)
{
    // The three room-guard spawn extremes (24688/24689/24690): X 222.3-231.7,
    // Y -23.0..+23.8. Every one must read as no-pull.
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(585, 222.3f, -18.0f));
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(585, 231.7f, 2.6f));
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(585, 227.3f, -23.0f));
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(585, 228.0f, 23.8f));
    // Selin himself and the deep end of his room.
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(585, 242.1f, 0.3f));
}

TEST(FightInPlaceTest, AntechamberStaysPullable)
{
    // Sunblade trash top out at X=182.3 — below Selin's X=216 gate, so a normal
    // pull must still fire there. The camp the stuck runs landed on (X~197) is also
    // outside the room and must not be swallowed.
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(585, 182.3f, 0.0f));
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(585, 197.0f, 7.0f));
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(585, 216.0f - 0.01f, 0.0f));
}

TEST(FightInPlaceTest, OtherMgtEncountersAreNotVetoed)
{
    // Priestess Delrissa (X=126.9) — far west, below the gate.
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(585, 126.9f, 19.2f));
    // Vexallus (X=231.4 but Y=-214.3) — inside the X band but far outside the Y band.
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(585, 231.4f, -214.3f));
}

TEST(FightInPlaceTest, OtherMapsAreNeverVetoed)
{
    // The same coordinates on any other map carry no zone.
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(0, 242.0f, 0.0f));
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(560, 230.0f, 0.0f));
}

TEST(FightInPlaceTest, ZoneBoundsAreInclusive)
{
    // The gate plane (X=216) and the Y edges are inside the room (closed interval).
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(585, 216.0f, 0.0f));
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(585, 260.0f, 45.0f));
    EXPECT_TRUE(FightInPlaceRegistry::IsNoPullZone(585, 240.0f, -45.0f));
    // Just past the far/side walls: out.
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(585, 260.01f, 0.0f));
    EXPECT_FALSE(FightInPlaceRegistry::IsNoPullZone(585, 240.0f, 45.01f));
}
