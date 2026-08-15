/*
 * mod-dungeon-clear — AiObjectContextAccess.h
 *
 * mod-playerbots exposes no extension seam: the engine's shared strategy /
 * action / trigger / value registries are PRIVATE static members of
 * AiObjectContext, populated once at startup by hardcoded functions. To plug
 * DungeonClear in without editing a single playerbots file, we grab the
 * addresses of those four private statics here.
 *
 * The technique is the standard explicit-instantiation access bypass: naming a
 * private member in an explicit template instantiation argument is exempt from
 * access control ([temp.explicit]/[class.access.general]), so the instantiation
 * can stash the member's address in a friend function that ADL then finds. This
 * is well-defined C++ (not `#define private public`, no UB). The only cost is
 * GCC's -Wnon-template-friend, silenced locally below.
 *
 * The lists themselves (SharedNamedObjectContextList<T>) are fully public, and
 * each per-bot context holds them BY REFERENCE — so an append done after
 * startup is immediately visible to every bot, present and future.
 *
 * CAVEAT: this couples to playerbots' internal class shape. If upstream renames
 * or moves AiObjectContext::shared*Contexts, this header fails to COMPILE —
 * loud, at build time, never a silent runtime break.
 */

#ifndef MOD_DUNGEONCLEAR_AIOBJECTCONTEXTACCESS_H
#define MOD_DUNGEONCLEAR_AIOBJECTCONTEXTACCESS_H

#include "Action.h"
#include "AiObjectContext.h"
#include "NamedObjectContext.h"
#include "Strategy.h"
#include "Trigger.h"
#include "Value.h"

namespace dc_access
{
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-template-friend"
#endif

    // Robber<Tag, &Member> defines the friend `get(Tag)` to return &Member.
    template <typename Tag, typename Tag::type Ptr>
    struct Robber
    {
        friend typename Tag::type get(Tag) { return Ptr; }
    };

    struct StrategyListTag
    {
        typedef SharedNamedObjectContextList<Strategy>* type;
        friend type get(StrategyListTag);
    };
    struct ActionListTag
    {
        typedef SharedNamedObjectContextList<Action>* type;
        friend type get(ActionListTag);
    };
    struct TriggerListTag
    {
        typedef SharedNamedObjectContextList<Trigger>* type;
        friend type get(TriggerListTag);
    };
    struct ValueListTag
    {
        typedef SharedNamedObjectContextList<UntypedValue>* type;
        friend type get(ValueListTag);
    };

    template struct Robber<StrategyListTag, &AiObjectContext::sharedStrategyContexts>;
    template struct Robber<ActionListTag, &AiObjectContext::sharedActionContexts>;
    template struct Robber<TriggerListTag, &AiObjectContext::sharedTriggerContexts>;
    template struct Robber<ValueListTag, &AiObjectContext::sharedValueContexts>;

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

    inline SharedNamedObjectContextList<Strategy>* SharedStrategyContexts()
    {
        return get(StrategyListTag{});
    }
    inline SharedNamedObjectContextList<Action>* SharedActionContexts()
    {
        return get(ActionListTag{});
    }
    inline SharedNamedObjectContextList<Trigger>* SharedTriggerContexts()
    {
        return get(TriggerListTag{});
    }
    inline SharedNamedObjectContextList<UntypedValue>* SharedValueContexts()
    {
        return get(ValueListTag{});
    }
}

#endif
