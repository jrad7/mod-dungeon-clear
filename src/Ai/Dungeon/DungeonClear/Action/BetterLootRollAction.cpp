/*
 * mod-dungeon-clear — BetterLootRollAction.cpp
 */

#include "BetterLootRollAction.h"

#include <utility>
#include <vector>

#include "Group.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomItemMgr.h"
#include "StatsWeightCalculator.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcPlayerbotCompat.h"

namespace
{
    // RandomItemMgr::CanEquip{Weapon,Armor} reordered their parameters between
    // playerbots branches: stock master is (clazz, [level,] proto); test-staging
    // is (proto, clazz[, level]). SFINAE-dispatch to whichever overload links so
    // the module compiles against either API.
    template <typename Mgr>
    auto DcCanEquipWeapon(Mgr& mgr, uint8 clazz, ItemTemplate const* proto, int)
        -> decltype(mgr.CanEquipWeapon(proto, clazz))
    {
        return mgr.CanEquipWeapon(proto, clazz);
    }
    template <typename Mgr>
    auto DcCanEquipWeapon(Mgr& mgr, uint8 clazz, ItemTemplate const* proto, long)
        -> decltype(mgr.CanEquipWeapon(clazz, proto))
    {
        return mgr.CanEquipWeapon(clazz, proto);
    }

    template <typename Mgr>
    auto DcCanEquipArmor(Mgr& mgr, uint8 clazz, uint32 level, ItemTemplate const* proto, int)
        -> decltype(mgr.CanEquipArmor(proto, clazz, level))
    {
        return mgr.CanEquipArmor(proto, clazz, level);
    }
    template <typename Mgr>
    auto DcCanEquipArmor(Mgr& mgr, uint8 clazz, uint32 level, ItemTemplate const* proto, long)
        -> decltype(mgr.CanEquipArmor(clazz, level, proto))
    {
        return mgr.CanEquipArmor(clazz, level, proto);
    }
}

bool DungeonClearBetterLootRollAction::isUseful()
{
    // Only intercept self-bots (master == bot). A bot driven for a separate
    // human master keeps stock rolling — its vote is its own GUID, no conflict.
    if (IsSelfBot(bot) && DcSettings::GetBool(bot, "BetterLootRolling"))
        return false;  // bot-self: cast no vote so the human gets to roll

    return LootRollAction::isUseful();
}

bool DungeonClearBetterLootRollAction::Execute(Event event)
{
    if (!DcSettings::GetBool(bot, "BetterLootRolling"))
        return LootRollAction::Execute(event);

    // The self-bot suppression isUseful() already applies, repeated here
    // because the engine's queued basket outlives the trigger that filled it:
    // an action queued while it was useful still runs after isUseful() would
    // refuse it, so a gate that exists only in isUseful() is not a gate.
    if (DcPlayerbotCompat::IsSelfBot(bot))
        return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    // Decide every pending roll first and vote afterwards. A vote that
    // completes a roll destroys it — Group::CountRollVote calls CountTheRoll,
    // which erases the entry and deletes the Roll — so no Roll* may be read
    // after any vote has been cast.
    std::vector<std::pair<ObjectGuid, RollVote>> decided;
    for (Roll const* roll : group->GetRolls())
    {
        auto voteItr = roll->playerVote.find(bot->GetGUID());
        if (voteItr == roll->playerVote.end() || voteItr->second != NOT_EMITED_YET)
            continue;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(roll->itemid);
        // Anything that is not the over-level case is stock's to answer, and
        // the pass below answers it — leave the vote unemitted for now.
        if (!proto || !IsFutureWearable(proto))
            continue;

        int32 randomProperty = 0;
        if (roll->itemRandomPropId)
            randomProperty = roll->itemRandomPropId;
        else if (roll->itemRandomSuffix)
            randomProperty = -((int)roll->itemRandomSuffix);

        RollVote vote = CalculateFutureVote(proto, randomProperty);

        // Same post-processing as stock LootRollAction::Execute.
        if (vote == NEED)
        {
            if (sPlayerbotAIConfig.lootNeedRollLevel == 0 || RollUniqueCheck(proto, bot))
                vote = PASS;
            else if (sPlayerbotAIConfig.lootNeedRollLevel == 1)
                vote = GREED;
        }
        else if (vote == GREED && !sPlayerbotAIConfig.lootGreedRollLevel)
            vote = PASS;

        switch (group->GetLootMethod())
        {
            case MASTER_LOOT:
            case FREE_FOR_ALL:
                vote = PASS;
                break;
            default:
                break;
        }
        decided.emplace_back(roll->itemGUID, vote);
    }

    for (auto const& [itemGuid, vote] : decided)
        group->CountRollVote(bot->GetGUID(), itemGuid, vote);

    // Then stock, for every roll this pass left alone. Upstream #2496 turned
    // that from one item per Execute into all of them, and matching it is the
    // point: a five-item boss drop used to hold the action slot for five ticks
    // (this action runs off a per-tick trigger, not stock's random one), which
    // is exactly the starvation the one-action-per-tick rule warns about.
    // The rolls voted above are skipped there — CountRollVote has moved them
    // off NOT_EMITED_YET.
    bool const stockVoted = LootRollAction::Execute(event);
    return !decided.empty() || stockVoted;
}

bool DungeonClearBetterLootRollAction::IsFutureWearable(ItemTemplate const* proto) const
{
    if (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR)
        return false;

    if (proto->RequiredLevel <= bot->GetLevel())
        return false;

    // CanUseItem checks faction, class/race, skill and spell BEFORE level, so
    // this exact error means the level requirement is the only blocker.
    return bot->BotCanUseItem(proto) == EQUIP_ERR_CANT_EQUIP_LEVEL_I;
}

RollVote DungeonClearBetterLootRollAction::CalculateFutureVote(ItemTemplate const* proto, int32 randomProperty)
{
    // Proficiency judged at the item's required level, not the bot's current
    // one — a 35 warrior WILL wear level-42 plate (plate unlocks at 40).
    bool proficient = proto->Class == ITEM_CLASS_WEAPON
        ? DcCanEquipWeapon(sRandomItemMgr, bot->getClass(), proto, 0)
        : DcCanEquipArmor(sRandomItemMgr, bot->getClass(), proto->RequiredLevel, proto, 0);

    if (!proficient)
        return GREED;  // never their gear, but still vendor/AH value

    StatsWeightCalculator calculator(bot);
    calculator.SetItemSetBonus(false);
    calculator.SetOverflowPenalty(false);

    if (sRandomPlayerbotMgr.IsSpecPvp(bot->GetGUID().GetCounter(), bot->getClass()))
        calculator.SetPvpSpec(true);

    return calculator.CalculateItem(proto->ItemId, randomProperty) > 0 ? NEED : GREED;
}
