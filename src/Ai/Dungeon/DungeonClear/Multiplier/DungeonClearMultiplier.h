/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARMULTIPLIER_H
#define _PLAYERBOT_DUNGEONCLEARMULTIPLIER_H

#include "Multiplier.h"

class PlayerbotAI;

// Suppresses wander-style actions (grind, new-rpg, rpg, travel) while dungeon
// clear is enabled, so the tank doesn't drift off looking for grind targets
// during the between-pulls rest wait.
class DungeonClearMultiplier : public Multiplier
{
public:
    DungeonClearMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "dungeon clear") {}
    float GetValue(Action* action) override;
};

// COMBAT-engine companion multiplier. The DungeonClearMultiplier above rides the
// NON-combat strategy, so it can only touch non-combat actions. This one rides the
// combat strategy and touches EXACTLY ONE combat action — the stock "drop target"
// — to stop the flip-early party-assist from ping-ponging the engine (drop target
// treats an out-of-LOS seeded fight target as invalid and leaves the combat engine
// before reach can close). Everything else in the combat engine stays fully stock.
class DungeonClearCombatMultiplier : public Multiplier
{
public:
    DungeonClearCombatMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "dungeon clear combat") {}
    float GetValue(Action* action) override;
};

#endif
