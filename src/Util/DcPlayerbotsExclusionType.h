/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCPLAYERBOTSEXCLUSIONTYPE_H
#define _PLAYERBOT_DCPLAYERBOTSEXCLUSIONTYPE_H

#include "Strategy.h"

// mod-playerbots #2912 renames the TargetValueExclusionType enumerators after the
// value each one serves (`Dps` -> `DpsTarget`, `Attacker` -> `DebuffTarget`,
// `Tank` -> `TankTarget`), drops `None` and adds `Aoe`. test-staging will carry
// it before master does, and the module must build against both, so the strategy
// names its pools through these accessors instead of the enumerators. Each one
// tests, on the dependent type E, which spelling the compiled-against playerbots
// declares; the discarded branch is never instantiated, so the missing name is
// not an error. Same device as DC_PB_CONFIG in DcPlayerbotsConfig.h.
//
// Once master carries #2912, replace each call with the enumerator and delete
// this header.
namespace DcPbExclusion
{
    template <typename E = TargetValueExclusionType>
    constexpr E DpsTarget()
    {
        if constexpr (requires { E::DpsTarget; })
            return E::DpsTarget;
        else
            return E::Dps;
    }

    template <typename E = TargetValueExclusionType>
    constexpr E DebuffTarget()
    {
        if constexpr (requires { E::DebuffTarget; })
            return E::DebuffTarget;
        else
            return E::Attacker;
    }

    template <typename E = TargetValueExclusionType>
    constexpr E TankTarget()
    {
        if constexpr (requires { E::TankTarget; })
            return E::TankTarget;
        else
            return E::Tank;
    }

    // Pre-#2912 playerbots never asks for an AoE pool, so there the answer is
    // simply "no" for every type.
    template <typename E>
    constexpr bool IsAoe(E type)
    {
        if constexpr (requires { E::Aoe; })
            return type == E::Aoe;
        else
            return false;
    }
}

#endif
