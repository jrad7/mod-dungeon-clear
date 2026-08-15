/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonWingRegistry.h"

#include <unordered_map>

// Maraudon (map 349) has no scripted events or roster patch — its only
// clear-data is the wing LABELS (isolated == false: display only, not a
// filter). This file is Maraudon's definition unit so that data lives with
// its dungeon like every other. The TU stays linked because
// DungeonWingRegistry's aggregator calls RegisterMaraudonWings explicitly.

// --- wing layout (relocated from DungeonWingRegistry) --------------------
void RegisterMaraudonWings(std::unordered_map<uint32, DungeonWingLayout>& store)
{
    // --- Maraudon (map 349) --------------------------------------
    // Three named regions — Orange (Foulspore Cavern), Purple (Wicked
    // Grotto) and the inner Pristine Waters (Earth Song Falls) — but
    // UNLIKE Dire Maul they share one connected interior: orange and
    // purple converge at the Celebras seal, which opens into the inner
    // waters, and the Earth Song Falls surface portal drops straight
    // into that same inner area. So every boss is reachable from any
    // entrance.
    //
    // isolated == false: this is a LABEL only. Do NOT filter — all
    // eight bosses stay in the clear list; the wing name is surfaced in
    // status/UI so the player can see which region each boss sits in.
    //
    // Entries are the kill-creature credit-entries from
    // instance_encounters (what BossSpawnIndex emits). Celebras the
    // Cursed sits at the purple-side seal he unlocks, so he is grouped
    // with Purple.
    store[349] = {false, {
        {"Maraudon (Orange)", {
            13282,  // Noxxion
            12258,  // Razorlash
        }},
        {"Maraudon (Purple)", {
            12236,  // Lord Vyletongue
            12225,  // Celebras the Cursed
        }},
        {"Maraudon (Pristine Waters)", {
            13601,  // Tinkerer Gizlock
            12203,  // Landslide
            13596,  // Rotgrip
            12201,  // Princess Theradras
        }},
    }};
}
