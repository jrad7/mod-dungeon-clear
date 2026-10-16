/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Lab/DcLabOracles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "Lab/DcLabJson.h"

namespace DcLabOracles
{
    using namespace DcLab;
    // DcLab::Unit is spelled out below: through the using-directive a bare
    // `Unit` is ambiguous with the core's ::Unit as soon as this file sees a
    // declaration of it (e.g. when modules are compiled with the game PCH).

    namespace
    {
        float Dist2d(float ax, float ay, float bx, float by)
        {
            float const dx = ax - bx, dy = ay - by;
            return std::sqrt(dx * dx + dy * dy);
        }

        std::string Sec(std::uint32_t ms)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1fs", ms / 1000.0);
            return buf;
        }

        bool Holding(std::uint32_t phase)
        {
            return phase == static_cast<std::uint32_t>(Phase::Forming) ||
                   phase == static_cast<std::uint32_t>(Phase::Advancing) ||
                   phase == static_cast<std::uint32_t>(Phase::Returning);
        }

        // Shared lookups over one trace.
        struct Index
        {
            Trace const& tr;
            std::uint64_t tank = 0;
            std::unordered_set<std::uint64_t> party;
            std::unordered_set<std::uint64_t> humans;
            std::unordered_map<std::uint64_t, std::string> role;

            explicit Index(Trace const& t) : tr(t)
            {
                if (Member const* l = t.Leader())
                    tank = l->guid;
                for (Member const& m : t.header.party)
                {
                    party.insert(m.guid);
                    role[m.guid] = m.role;
                    if (m.human)
                        humans.insert(m.guid);
                }
            }

            bool IsParty(std::uint64_t g) const { return g && party.count(g); }
            bool IsHuman(std::uint64_t g) const { return humans.count(g) != 0; }
            std::string Role(std::uint64_t g) const
            {
                auto it = role.find(g);
                return it == role.end() ? std::string() : it->second;
            }

            static BotSample const* Bot(Frame const& f, std::uint64_t g)
            {
                for (BotSample const& b : f.bots)
                    if (b.guid == g)
                        return &b;
                return nullptr;
            }
            static UnitSample const* UnitAt(Frame const& f, std::uint64_t g)
            {
                for (UnitSample const& u : f.units)
                    if (u.guid == g)
                        return &u;
                return nullptr;
            }

            // A creature engaged with the party: in combat and its victim or top
            // threat is a party member.
            bool OnParty(UnitSample const& u) const
            {
                return u.alive && u.inCombat && (IsParty(u.victim) || IsParty(u.threat));
            }

            // Last frame at or before t (nullptr when t precedes the first frame).
            Frame const* FrameAt(std::uint32_t t) const
            {
                Frame const* best = nullptr;
                for (Frame const& f : tr.frames)
                {
                    if (f.t > t)
                        break;
                    best = &f;
                }
                return best;
            }

            // The tank's most recent out->in combat edge at or before t (frames).
            bool TankCombatStart(std::uint32_t t, std::uint32_t& out) const
            {
                bool prev = false;
                bool found = false;
                for (Frame const& f : tr.frames)
                {
                    if (f.t > t)
                        break;
                    BotSample const* b = Bot(f, tank);
                    bool const in = b && b->inCombat;
                    if (in && !prev)
                    {
                        out = f.t;
                        found = true;
                    }
                    if (!in)
                        found = false;
                    prev = in;
                }
                return found;
            }
        };

        OracleResult Make(char const* id, char const* name)
        {
            OracleResult r;
            r.id = id;
            r.name = name;
            r.verdict = Verdict::Pass;
            return r;
        }

        void Violate(OracleResult& r, std::uint32_t t, std::uint64_t unit, std::string const& detail)
        {
            if (r.verdict != Verdict::Fail)
            {
                r.verdict = Verdict::Fail;
                r.firstMs = t;
                r.unit = unit;
                r.detail = detail;
            }
            ++r.count;
        }

        std::string UnitName(Index const& ix, std::uint64_t g)
        {
            if (DcLab::Unit const* u = ix.tr.FindUnit(g))
                return u->name.empty() ? DcLabJson::HexGuid(g) : u->name;
            if (Member const* m = ix.tr.FindMember(g))
                return m->name;
            return DcLabJson::HexGuid(g);
        }

        // ---------------------------------------------------------------- O1
        OracleResult O1(Index const& ix, OracleConfig const& c)
        {
            OracleResult r = Make("O1", "aggro ownership");
            std::unordered_map<std::uint64_t, std::uint32_t> since;  // unattended since
            std::unordered_map<std::uint64_t, float> lastTankDist;
            std::unordered_set<std::uint64_t> reported;
            bool any = false;
            for (Frame const& f : ix.tr.frames)
            {
                BotSample const* tank = Index::Bot(f, ix.tank);
                for (UnitSample const& u : f.units)
                {
                    if (!ix.OnParty(u) || u.evading)
                    {
                        since.erase(u.guid);
                        continue;
                    }
                    any = true;
                    bool attended = false;
                    for (BotSample const& b : f.bots)
                        if (b.alive && (b.victim == u.guid || (b.guid == ix.tank && b.target == u.guid)))
                            attended = true;
                    float const td = tank ? Dist2d(tank->x, tank->y, u.x, u.y) : 1e9f;
                    if (tank && tank->alive)
                    {
                        if (td <= c.o1TankNearYd)
                            attended = true;
                        auto it = lastTankDist.find(u.guid);
                        if (it != lastTankDist.end() && td < it->second - 0.5f)
                            attended = true;  // tank closing on it
                    }
                    // A mob walking in on the tank (a dragged caster coming into
                    // camp) is his too: the gap is closing from its side.
                    if (tank && tank->alive && u.moving && (u.victim == ix.tank || u.threat == ix.tank))
                    {
                        auto it2 = lastTankDist.find(u.guid);
                        if (it2 != lastTankDist.end() && td < it2->second - 0.3f)
                            attended = true;
                    }
                    lastTankDist[u.guid] = td;
                    // A mob on the tank is the tank's: he holds its aggro whether he
                    // is dragging it home or tanking a melee mob while a caster
                    // shoots him from range. The failure O1 hunts is a mob on
                    // SOMEONE ELSE that nobody deals with.
                    if (u.victim == ix.tank || u.threat == ix.tank || ix.Role(u.victim) == "tank" ||
                        ix.Role(u.threat) == "tank")
                        attended = true;  // an off-tank holding it counts too
                    if (attended)
                    {
                        since.erase(u.guid);
                        continue;
                    }
                    auto [it, fresh] = since.emplace(u.guid, f.t);
                    (void)fresh;
                    float vicDist = 0.0f;
                    if (BotSample const* v = Index::Bot(f, u.victim))
                        vicDist = Dist2d(v->x, v->y, u.x, u.y);
                    bool const parked = !u.moving && vicDist > c.o1ParkedRangeYd;
                    std::uint32_t const limit =
                        (parked || (f.dc.valid && f.dc.losPull)) ? c.o1ParkedRangedMs : c.o1UnattendedMs;
                    if (f.t - it->second > limit && !reported.count(u.guid))
                    {
                        reported.insert(u.guid);
                        Violate(r, it->second + limit, u.guid,
                                UnitName(ix, u.guid) + " on " + UnitName(ix, u.victim ? u.victim : u.threat) +
                                    " unattended " + Sec(f.t - it->second) + (parked ? " (parked ranged)" : "") +
                                    (td < 1e8f ? ", tank " + std::to_string(static_cast<int>(td)) + "yd" : ""));
                    }
                }
            }
            if (!any)
                r.verdict = Verdict::NotApplicable;
            return r;
        }

        // ---------------------------------------------------------------- O2
        OracleResult O2(Index const& ix, OracleConfig const& c)
        {
            OracleResult r = Make("O2", "premature dps");
            std::unordered_map<std::uint64_t, std::uint32_t> tankFirst;
            for (Event const& e : ix.tr.events)
                if (e.ev == Ev::FirstDmg && e.a == ix.tank && !ix.IsParty(e.b))
                    if (!tankFirst.count(e.b))
                        tankFirst[e.b] = e.t;

            std::uint32_t const delay = ix.tr.header.releaseDelayMs;
            auto premature = [&](std::uint64_t attacker, std::uint64_t mob, std::uint32_t t,
                                 char const* how) -> bool
            {
                // Any tank-role member opens freely: an off-tank's taunt or Shield
                // Slam IS threat, not a DPS opener (Karazhan 10-mans).
                if (attacker == ix.tank || ix.Role(attacker) == "tank" || ix.IsHuman(attacker) ||
                    !ix.IsParty(attacker) || ix.IsParty(mob))
                    return false;
                Frame const* f = ix.FrameAt(t);
                if (f && f->dc.valid && f->dc.partyReleased)
                    return false;
                if (c.o2SelfDefenseExempt && f)
                    if (UnitSample const* u = Index::UnitAt(*f, mob))
                        if (u->victim == attacker || u->threat == attacker)
                            return false;
                auto it = tankFirst.find(mob);
                std::uint32_t start = 0;
                bool const tankHit = it != tankFirst.end() && it->second <= t + c.o2ToleranceMs;
                bool const started = ix.TankCombatStart(t, start);
                // A mob already fighting the party (attacking the tank, or a
                // healer it must be peeled off) is fair game once the release
                // delay has run: hitting it is the intended assist. What O2
                // catches is a follower OPENING on a mob nobody is fighting —
                // pulling it — or any hit inside the release window. A dragged
                // pack chasing the tank home is the common in-window case.
                bool mobOnTank = false;
                if (f)
                    if (UnitSample const* u = Index::UnitAt(*f, mob))
                        mobOnTank = ix.IsParty(u->victim) || ix.IsParty(u->threat);
                // The threat lead runs from the tank's FIRST HIT in this fight
                // (the combat flag can come up seconds before he lands one); if he
                // has hit nothing yet, from the flag.
                std::uint32_t windowStart = start;
                if (started)
                    for (Event const& e : ix.tr.events)
                        if (e.ev == Ev::FirstDmg && e.a == ix.tank && !ix.IsParty(e.b) && e.t >= start && e.t <= t)
                        {
                            windowStart = std::max(windowStart, e.t);
                            break;
                        }
                bool const pastWindow = started && t + c.o2ToleranceMs >= windowStart + delay;
                if ((tankHit || mobOnTank) && pastWindow)
                    return false;
                // The tank briefly out of combat while a mob still fights the
                // party is no opening window at all; there is nothing to pre-empt.
                if (mobOnTank && !started)
                    return false;
                std::string why = !tankHit && !mobOnTank ? "before anyone in the party was fighting it"
                                           : "only " + Sec(t - windowStart) + " after the tank's first hit (release " +
                                                 Sec(delay) + ")";
                Violate(r, t, attacker,
                        UnitName(ix, attacker) + " " + how + " " + UnitName(ix, mob) + " " + why);
                return true;
            };

            for (Event const& e : ix.tr.events)
                if (e.ev == Ev::FirstDmg)
                    premature(e.a, e.b, e.t, "hit");

            if (c.o2CountAttackStart)
            {
                std::set<std::pair<std::uint64_t, std::uint64_t>> seen;
                for (Frame const& f : ix.tr.frames)
                    for (BotSample const& b : f.bots)
                        if (b.alive && b.victim && !ix.IsParty(b.victim) &&
                            seen.insert({b.guid, b.victim}).second)
                            premature(b.guid, b.victim, f.t, "started attacking");
            }
            bool anyFight = !tankFirst.empty();
            if (!anyFight && r.verdict == Verdict::Pass)
                r.verdict = Verdict::NotApplicable;
            return r;
        }

        // ---------------------------------------------------------------- O3
        OracleResult O3(Index const& ix, OracleConfig const& c)
        {
            OracleResult r = Make("O3", "held member hurt");
            std::unordered_map<std::uint64_t, std::uint8_t> hpAtHold;
            std::unordered_set<std::uint64_t> reported;
            bool anyHold = false;
            for (Frame const& f : ix.tr.frames)
                for (BotSample const& b : f.bots)
                {
                    if (b.guid == ix.tank || ix.IsHuman(b.guid))
                        continue;
                    bool const held = (b.passive || b.stay) && !(f.dc.valid && f.dc.partyReleased);
                    if (!held || !b.alive)
                    {
                        hpAtHold.erase(b.guid);
                        continue;
                    }
                    anyHold = true;
                    auto [it, fresh] = hpAtHold.emplace(b.guid, b.hpPct);
                    (void)fresh;
                    if (it->second > b.hpPct && it->second - b.hpPct > c.o3HpDropPct &&
                        reported.insert(b.guid).second)
                        Violate(r, f.t, b.guid,
                                UnitName(ix, b.guid) + " held at " + std::to_string(it->second) + "% fell to " +
                                    std::to_string(b.hpPct) + "%");
                }
            if (!anyHold)
                r.verdict = Verdict::NotApplicable;
            return r;
        }

        // ---------------------------------------------------------------- O4
        OracleResult O4(Index const& ix, OracleConfig const& c)
        {
            OracleResult r = Make("O4", "idle while tank fights");
            std::uint32_t tankSince = 0;
            bool tankIn = false;
            std::unordered_map<std::uint64_t, std::uint32_t> idleSince;
            std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::uint32_t>> lastDmg;  // dmg, t changed
            std::unordered_set<std::uint64_t> reported;
            bool applicable = false;
            for (Frame const& f : ix.tr.frames)
            {
                BotSample const* tank = Index::Bot(f, ix.tank);
                bool const tIn = tank && tank->alive && tank->inCombat;
                if (tIn && !tankIn)
                    tankSince = f.t;
                tankIn = tIn;
                bool fightOn = false;
                for (UnitSample const& u : f.units)
                    if (ix.OnParty(u))
                        fightOn = true;
                for (BotSample const& b : f.bots)
                {
                    auto ld = lastDmg.find(b.guid);
                    if (ld == lastDmg.end() || ld->second.first != b.dmgDone)
                        lastDmg[b.guid] = {b.dmgDone, f.t};
                    if (ix.Role(b.guid) != "dps" || ix.IsHuman(b.guid))
                        continue;
                    bool const held = b.passive || b.stay ||
                                      (f.dc.valid && (Holding(f.dc.phase) || f.dc.scoutAggro) && !f.dc.partyReleased);
                    bool const gate = tIn && fightOn && f.t - tankSince >= c.o4TankFightMs && b.alive && !held;
                    if (!gate)
                    {
                        idleSince.erase(b.guid);
                        continue;
                    }
                    applicable = true;
                    bool const noDamage = f.t - lastDmg[b.guid].second >= c.o4IdleMs;
                    // A caster mid-cast is acting even before its first hit lands.
                    bool const idle = !b.inCombat || (noDamage && !b.casting);
                    if (!idle)
                    {
                        idleSince.erase(b.guid);
                        continue;
                    }
                    auto [it, fresh] = idleSince.emplace(b.guid, f.t);
                    (void)fresh;
                    if (f.t - it->second >= c.o4IdleMs && reported.insert(b.guid).second)
                    {
                        float const d = tank ? Dist2d(tank->x, tank->y, b.x, b.y) : 0.0f;
                        Violate(r, it->second, b.guid,
                                UnitName(ix, b.guid) + (b.inCombat ? " in combat but dealt no damage"
                                                                   : " out of combat") +
                                    " for " + Sec(f.t - it->second) + " while the tank fought, " +
                                    std::to_string(static_cast<int>(d)) + "yd from it");
                    }
                }
            }
            if (!applicable && r.verdict == Verdict::Pass)
                r.verdict = Verdict::NotApplicable;
            return r;
        }

        // ---------------------------------------------------------------- O5
        OracleResult O5(Index const& ix, OracleConfig const& c)
        {
            OracleResult r = Make("O5", "tick starvation");
            auto exempt = [&](std::string const& a)
            {
                for (std::string const& e : c.o5Exempt)
                    if (a.find(e) != std::string::npos)
                        return true;
                return false;
            };
            // Is the pull "live" at t: anyone in combat, or a pull phase running.
            auto live = [&](Frame const& f)
            {
                if (f.dc.valid && f.dc.phase != static_cast<std::uint32_t>(Phase::Idle))
                    return true;
                for (BotSample const& b : f.bots)
                    if (b.inCombat)
                        return true;
                return false;
            };
            std::unordered_map<std::uint64_t, std::vector<ActionRec const*>> byBot;
            for (ActionRec const& a : ix.tr.actions)
                if (a.executed)
                    byBot[a.guid].push_back(&a);
            if (byBot.empty())
            {
                r.verdict = Verdict::NotApplicable;
                return r;
            }
            for (auto const& [guid, acts] : byBot)
            {
                if (ix.IsHuman(guid))
                    continue;
                std::size_t i = 0;
                bool reported = false;
                while (i < acts.size() && !reported)
                {
                    std::size_t j = i;
                    while (j + 1 < acts.size() && acts[j + 1]->action == acts[i]->action)
                        ++j;
                    std::uint32_t const t0 = acts[i]->t, t1 = acts[j]->t;
                    // Three executions minimum: two lone ticks 20s apart are no
                    // streak (an "xp gain" at each kill read as a tick thief).
                    if (t1 - t0 >= c.o5StreakMs && j - i + 1 >= 3 && !exempt(acts[i]->action))
                    {
                        // Did anything about the bot change across the streak?
                        BotSample const* first = nullptr;
                        bool changed = false, allLive = true, held = true;
                        for (Frame const& f : ix.tr.frames)
                        {
                            if (f.t < t0 || f.t > t1)
                                continue;
                            BotSample const* b = Index::Bot(f, guid);
                            if (!b)
                                continue;
                            if (!live(f))
                                allLive = false;
                            if (!b->passive && !b->stay)
                                held = false;
                            if (!first)
                            {
                                first = b;
                                continue;
                            }
                            if (Dist2d(first->x, first->y, b->x, b->y) > c.o5MoveYd ||
                                b->victim != first->victim || b->target != first->target ||
                                b->dmgDone != first->dmgDone || b->healDone != first->healDone ||
                                b->casting || b->alive != first->alive)
                                changed = true;
                        }
                        if (first && !changed && allLive && !held)
                        {
                            reported = true;
                            Violate(r, t0, guid,
                                    UnitName(ix, guid) + " executed '" + acts[i]->action + "' for " +
                                        Sec(t1 - t0) + " (" + std::to_string(j - i + 1) +
                                        " ticks) with no movement, target, cast or damage change");
                        }
                    }
                    i = j + 1;
                }
            }
            return r;
        }

        // ---------------------------------------------------------------- O6
        OracleResult O6(Index const& ix, OracleConfig const& c)
        {
            OracleResult r = Make("O6", "phase dwell");
            bool any = false;
            std::uint32_t phase = 0, since = 0;
            bool have = false, reportedThis = false;
            std::uint32_t idleCombatSince = 0;
            bool inIdleCombat = false, reportedIdle = false;
            std::vector<std::uint32_t> maneuverTicks;
            for (ActionRec const& a : ix.tr.actions)
                if (a.guid == ix.tank && a.executed && a.action == c.maneuverAction)
                    maneuverTicks.push_back(a.t);
            auto maneuverIn = [&](std::uint32_t a, std::uint32_t b)
            {
                auto it = std::lower_bound(maneuverTicks.begin(), maneuverTicks.end(), a);
                return it != maneuverTicks.end() && *it <= b;
            };
            for (Frame const& f : ix.tr.frames)
            {
                if (!f.dc.valid || !f.dc.enabled)
                {
                    have = false;
                    inIdleCombat = false;
                    continue;
                }
                any = true;
                if (!have || f.dc.phase != phase)
                {
                    phase = f.dc.phase;
                    since = f.t;
                    have = true;
                    reportedThis = false;
                }
                std::uint32_t limit = 0;
                switch (static_cast<Phase>(phase))
                {
                    case Phase::Forming: limit = c.o6FormingMs; break;
                    case Phase::Advancing: limit = c.o6AdvancingMs; break;
                    case Phase::Returning: limit = c.o6ReturningMs; break;
                    default: break;
                }
                if (limit && !f.dc.paused && f.t - since > limit && !reportedThis)
                {
                    reportedThis = true;
                    Violate(r, since + limit, ix.tank,
                            std::string(PhaseName(phase)) + " held " + Sec(f.t - since) + " (budget " +
                                Sec(limit) + ")");
                }
                BotSample const* tank = Index::Bot(f, ix.tank);
                bool const idleCombat = tank && tank->inCombat && phase == static_cast<std::uint32_t>(Phase::Idle) &&
                                        f.dc.decision == 2 && !f.dc.paused;
                if (!idleCombat)
                {
                    inIdleCombat = false;
                    continue;
                }
                if (!inIdleCombat)
                {
                    inIdleCombat = true;
                    idleCombatSince = f.t;
                }
                if (f.t - idleCombatSince > c.o6IdleCombatMs && !reportedIdle &&
                    !maneuverIn(idleCombatSince, f.t))
                {
                    reportedIdle = true;
                    Violate(r, idleCombatSince, ix.tank,
                            "tank in combat at Idle under an Advanced verdict for " + Sec(f.t - idleCombatSince) +
                                " with no maneuver tick");
                }
            }
            if (!any)
                r.verdict = Verdict::NotApplicable;
            return r;
        }

        // ---------------------------------------------------------------- O7
        OracleResult O7(Index const& ix, OracleConfig const& c)
        {
            OracleResult r = Make("O7", "joiners");
            // When each creature first engaged the party (frames, or the exact
            // hook event when the recorder had one).
            // A summon the pack itself brought (no DB spawn, no scenario pack —
            // a necromancer's skeleton, a warlock mob's imp) is part of that
            // pack's fight, not an outside joiner, and the governor never counts
            // it either. Synthetic scenario actors carry a pack and stay counted.
            auto counted = [&](std::uint64_t g)
            {
                DcLab::Unit const* u = ix.tr.FindUnit(g);
                return !(u && u->spawnId == 0 && u->pack.empty());
            };
            std::map<std::uint64_t, std::uint32_t> engagedAt;
            for (Event const& e : ix.tr.events)
                if ((e.ev == "engage" || e.ev == Ev::CombatOn) && ix.IsParty(e.b) && !ix.IsParty(e.a) &&
                    !engagedAt.count(e.a) && counted(e.a))
                    engagedAt[e.a] = e.t;
            for (Frame const& f : ix.tr.frames)
                for (UnitSample const& u : f.units)
                    if (ix.OnParty(u) && !engagedAt.count(u.guid) && counted(u.guid))
                        engagedAt[u.guid] = f.t;
            if (engagedAt.empty())
            {
                r.verdict = Verdict::NotApplicable;
                return r;
            }

            // Pull windows by decisionSeq.
            struct Window
            {
                std::uint32_t seq = 0, from = 0, to = UINT32_MAX, predicted = 0;
                std::uint64_t target = 0;
            };
            // A new pull window opens on a new verdict (decisionSeq) AND on every
            // commit: two pulls taken under one standing verdict are two pulls.
            std::vector<std::uint32_t> commits;
            for (Event const& e : ix.tr.events)
                if (e.ev == Ev::Commit)
                    commits.push_back(e.t);
            std::size_t nextCommit = 0;
            std::vector<Window> wins;
            for (Frame const& f : ix.tr.frames)
            {
                if (!f.dc.valid)
                    continue;
                bool commitHere = false;
                while (nextCommit < commits.size() && commits[nextCommit] <= f.t)
                {
                    commitHere = !wins.empty();
                    ++nextCommit;
                }
                if (wins.empty() || wins.back().seq != f.dc.decisionSeq || commitHere)
                {
                    if (!wins.empty())
                        wins.back().to = f.t;
                    Window w;
                    w.seq = f.dc.decisionSeq;
                    w.from = f.t;
                    wins.push_back(w);
                }
                wins.back().predicted = std::max(wins.back().predicted, f.dc.predicted);
                if (f.dc.pullTarget)
                    wins.back().target = f.dc.pullTarget;
            }
            if (wins.empty())
            {
                Window w;
                w.from = 0;
                wins.push_back(w);
            }

            std::uint32_t worstOver = 0, outside = 0;
            std::map<std::string, std::uint32_t> why;
            for (Window const& w : wins)
            {
                std::vector<std::uint64_t> mobs;
                for (auto const& [g, t] : engagedAt)
                    if (t >= w.from && t < w.to)
                        mobs.push_back(g);
                if (mobs.empty())
                    continue;
                std::uint32_t const n = static_cast<std::uint32_t>(mobs.size());
                if (w.predicted > 0 && static_cast<std::int32_t>(n) - static_cast<std::int32_t>(w.predicted) >
                                           c.o7JoinersMax)
                {
                    worstOver = std::max(worstOver, n - w.predicted);
                    Violate(r, w.from, w.target,
                            "pull #" + std::to_string(w.seq) + " engaged " + std::to_string(n) + " vs predicted " +
                                std::to_string(w.predicted));
                }
                DcLab::Unit const* tu = ix.tr.FindUnit(w.target);
                std::string const pack = tu ? tu->pack : std::string();
                if (pack.empty())
                    continue;
                for (std::uint64_t g : mobs)
                {
                    DcLab::Unit const* u = ix.tr.FindUnit(g);
                    if (!u || u->pack == pack)
                        continue;
                    ++outside;
                    // Attribute the join from the frame it engaged on.
                    std::uint32_t const t = engagedAt[g];
                    Frame const* f = ix.FrameAt(t);
                    Frame const* before = ix.FrameAt(t >= kFrameMs * 2 ? t - kFrameMs * 2 : 0);
                    std::string cause = "unknown";
                    UnitSample const* us = f ? Index::UnitAt(*f, g) : nullptr;
                    UnitSample const* ub = before ? Index::UnitAt(*before, g) : nullptr;
                    if (ub && !ub->inCombat && (ub->moving || ub->motion == 2))
                        cause = "patrol";
                    else if (us && f)
                    {
                        for (UnitSample const& o : f->units)
                            if (o.guid != g && ix.OnParty(o) && engagedAt.count(o.guid) &&
                                engagedAt[o.guid] < t &&
                                Dist2d(o.x, o.y, us->x, us->y) <= c.o7CallForHelpYd)
                                cause = "call-for-help";
                        if (cause == "unknown")
                            for (BotSample const& b : f->bots)
                                if (Dist2d(b.x, b.y, us->x, us->y) <= c.o7ProximityYd)
                                    cause = "proximity";
                    }
                    ++why[cause];
                    if (c.o7OutsideMax >= 0 && static_cast<std::int32_t>(outside) > c.o7OutsideMax)
                        Violate(r, t, g,
                                "outside-pack joiner " + UnitName(ix, g) + " (pack '" + u->pack + "', " + cause +
                                    ") on the pull of pack '" + pack + "'");
                }
            }
            std::string tally;
            for (auto const& [k, n] : why)
                tally += (tally.empty() ? "" : ", ") + k + "=" + std::to_string(n);
            std::string const sum = std::to_string(engagedAt.size()) + " engaged, " + std::to_string(outside) +
                                    " outside-pack" + (tally.empty() ? "" : " (" + tally + ")");
            r.detail = r.detail.empty() ? sum : r.detail + "; " + sum;
            return r;
        }

        // ---------------------------------------------------------------- O8
        OracleResult O8(Index const& ix, OracleConfig const& c)
        {
            OracleResult r = Make("O8", "oscillation");
            std::deque<std::uint32_t> flips;
            std::uint32_t lastDec = 0;
            bool haveDec = false, reportedFlips = false;
            for (Frame const& f : ix.tr.frames)
            {
                if (!f.dc.valid)
                    continue;
                if (haveDec && f.dc.decision != lastDec)
                {
                    flips.push_back(f.t);
                    while (!flips.empty() && f.t - flips.front() > c.o8WindowMs)
                        flips.pop_front();
                    if (flips.size() > c.o8MaxFlips && !reportedFlips)
                    {
                        reportedFlips = true;
                        Violate(r, flips.front(), ix.tank,
                                std::to_string(flips.size()) + " verdict flips inside " + Sec(c.o8WindowMs));
                    }
                }
                lastDec = f.dc.decision;
                haveDec = true;
            }
            struct Mv
            {
                bool have = false;
                float x = 0, y = 0, dx = 0, dy = 0;
                bool haveDir = false;
                std::deque<std::uint32_t> rev;
                bool reported = false;
            };
            std::unordered_map<std::uint64_t, Mv> mv;
            for (Frame const& f : ix.tr.frames)
                for (BotSample const& b : f.bots)
                {
                    Mv& m = mv[b.guid];
                    if (!m.have)
                    {
                        m.have = true;
                        m.x = b.x;
                        m.y = b.y;
                        continue;
                    }
                    // Melee jockeying around a live victim (strafe, behind-target,
                    // Killing Spree) reverses by design; ping-pong is a bot with
                    // nothing to fight walking back and forth.
                    if (b.inCombat && b.victim)
                    {
                        m.x = b.x;
                        m.y = b.y;
                        m.haveDir = false;
                        continue;
                    }
                    float const dx = b.x - m.x, dy = b.y - m.y;
                    float const len = std::sqrt(dx * dx + dy * dy);
                    if (len < c.o8StepYd)
                        continue;
                    m.x = b.x;
                    m.y = b.y;
                    if (m.haveDir)
                    {
                        float const plen = std::sqrt(m.dx * m.dx + m.dy * m.dy);
                        if (dx * m.dx + dy * m.dy < -0.5f * len * plen)
                        {
                            m.rev.push_back(f.t);
                            while (!m.rev.empty() && f.t - m.rev.front() > c.o8WindowMs)
                                m.rev.pop_front();
                            if (m.rev.size() > c.o8MaxReversals && !m.reported && !ix.IsHuman(b.guid))
                            {
                                m.reported = true;
                                Violate(r, m.rev.front(), b.guid,
                                        UnitName(ix, b.guid) + " reversed direction " + std::to_string(m.rev.size()) +
                                            " times inside " + Sec(c.o8WindowMs));
                            }
                        }
                    }
                    m.dx = dx;
                    m.dy = dy;
                    m.haveDir = true;
                }
            return r;
        }

        // ---------------------------------------------------------------- O9
        OracleResult O9(Index const& ix, OracleConfig const& c)
        {
            OracleResult r = Make("O9", "human interference");
            if (ix.humans.empty())
            {
                r.verdict = Verdict::NotApplicable;
                return r;
            }
            std::uint64_t const human = *ix.humans.begin();
            std::uint32_t const lead = ix.tr.header.releaseDelayMs + 1000;
            std::uint32_t tankStart = 0;
            bool tankIn = false;
            std::unordered_map<std::uint64_t, float> distAtStart;
            std::unordered_set<std::uint64_t> reportedToward;
            std::uint32_t waitSince = 0;
            bool waiting = false, reportedWait = false;
            float wx = 0, wy = 0;
            std::uint32_t humanFightSince = 0;
            bool humanFight = false, reportedFight = false;
            for (Frame const& f : ix.tr.frames)
            {
                BotSample const* tank = Index::Bot(f, ix.tank);
                BotSample const* hu = Index::Bot(f, human);
                if (!tank || !hu)
                    continue;
                bool const tIn = tank->inCombat;
                if (tIn && !tankIn)
                {
                    tankStart = f.t;
                    distAtStart.clear();
                    for (BotSample const& b : f.bots)
                        distAtStart[b.guid] = Dist2d(b.x, b.y, hu->x, hu->y);
                }
                tankIn = tIn;
                // (a) followers drawn to the human during the threat lead.
                if (tIn && f.t - tankStart <= lead && Dist2d(tank->x, tank->y, hu->x, hu->y) > 10.0f)
                    for (BotSample const& b : f.bots)
                    {
                        if (b.guid == ix.tank || b.guid == human || reportedToward.count(b.guid))
                            continue;
                        auto it = distAtStart.find(b.guid);
                        if (it != distAtStart.end() &&
                            it->second - Dist2d(b.x, b.y, hu->x, hu->y) > c.o9TowardYd)
                        {
                            reportedToward.insert(b.guid);
                            Violate(r, f.t, b.guid,
                                    UnitName(ix, b.guid) + " moved toward the human during the threat lead");
                        }
                    }
                // (b) the tank parked waiting on a far human.
                bool const parked = !tIn && f.dc.valid && f.dc.enabled &&
                                    Dist2d(tank->x, tank->y, hu->x, hu->y) > 20.0f;
                if (parked && waiting && Dist2d(tank->x, tank->y, wx, wy) < 1.0f)
                {
                    if (f.t - waitSince > c.o9WaitMs && !reportedWait)
                    {
                        reportedWait = true;
                        Violate(r, waitSince, ix.tank,
                                "tank stood " + Sec(f.t - waitSince) + " waiting with the human " +
                                    std::to_string(static_cast<int>(Dist2d(tank->x, tank->y, hu->x, hu->y))) +
                                    "yd away");
                    }
                }
                else if (parked)
                {
                    waiting = true;
                    waitSince = f.t;
                    wx = tank->x;
                    wy = tank->y;
                }
                else
                    waiting = false;
                // (c) the human fights alone.
                // A bot mid-cast or with a victim is engaged even before its
                // combat flag comes up (a Frostbolt in flight, a stealth opener).
                std::uint32_t outBots = 0;
                for (BotSample const& b : f.bots)
                    if (b.guid != human && b.alive && !b.inCombat && !b.victim && !b.casting)
                        ++outBots;
                bool const alone = hu->inCombat && outBots >= 2;
                if (alone && !humanFight)
                {
                    humanFight = true;
                    humanFightSince = f.t;
                }
                else if (!alone)
                    humanFight = false;
                if (humanFight && f.t - humanFightSince > c.o9HumanFightMs && !reportedFight)
                {
                    reportedFight = true;
                    Violate(r, humanFightSince, human,
                            "human fought " + Sec(f.t - humanFightSince) + " with " + std::to_string(outBots) +
                                " party members out of combat");
                }
            }
            return r;
        }

        // ---------------------------------------------------------------- O10
        OracleResult O10(Index const& ix, OracleConfig const& c)
        {
            OracleResult r = Make("O10", "outcome");
            if (ix.tr.frames.empty())
            {
                r.verdict = Verdict::NotApplicable;
                return r;
            }
            Frame const& last = ix.tr.frames.back();
            std::vector<std::string> const& goals = ix.tr.header.goalPacks;
            auto isGoal = [&](DcLab::Unit const& u)
            {
                if (goals.empty())
                    return !u.pack.empty();
                for (std::string const& g : goals)
                    if (g == "*" ? !u.pack.empty() : u.pack == g)
                        return true;
                return false;
            };
            std::uint32_t goalTotal = 0, goalAlive = 0;
            std::string aliveName;
            for (DcLab::Unit const& u : ix.tr.units)
            {
                if (!isGoal(u))
                    continue;
                ++goalTotal;
                UnitSample const* s = Index::UnitAt(last, u.guid);
                if (s && s->alive)
                {
                    ++goalAlive;
                    if (aliveName.empty())
                        aliveName = UnitName(ix, u.guid);
                }
            }
            std::unordered_set<std::uint64_t> died;
            for (Frame const& f : ix.tr.frames)
                for (BotSample const& b : f.bots)
                    if (!b.alive && !died.count(b.guid))
                    {
                        died.insert(b.guid);
                        if (c.o10NoDeaths)
                            Violate(r, f.t, b.guid, UnitName(ix, b.guid) + " died");
                    }
            if (c.o10RequireGoal && !goalTotal && !ix.tr.header.goalPacks.empty())
                Violate(r, ix.tr.endMs, 0, "goal pack(s) declared but no unit was ever registered in them");
            if (c.o10RequireGoal && goalTotal && goalAlive)
                Violate(r, ix.tr.endMs, 0,
                        std::to_string(goalAlive) + "/" + std::to_string(goalTotal) + " goal mobs alive at end (" +
                            aliveName + "), end: " + ix.tr.endReason);
            bool wiped = !last.bots.empty();
            for (BotSample const& b : last.bots)
                if (b.alive)
                    wiped = false;
            std::string const sum = std::to_string(goalTotal - goalAlive) + "/" + std::to_string(goalTotal) +
                                    " goal killed, " + std::to_string(died.size()) + " deaths" +
                                    (wiped ? ", WIPE" : "") + ", end " + ix.tr.endReason;
            r.detail = r.detail.empty() ? sum : r.detail + "; " + sum;
            return r;
        }
    }

    std::vector<OracleResult> Evaluate(Trace const& trace, OracleConfig const& cfg)
    {
        Index const ix(trace);
        return {O1(ix, cfg), O2(ix, cfg), O3(ix, cfg), O4(ix, cfg), O5(ix, cfg),
                O6(ix, cfg), O7(ix, cfg), O8(ix, cfg), O9(ix, cfg), O10(ix, cfg)};
    }

    std::string Summary(std::vector<OracleResult> const& results)
    {
        std::string out;
        for (OracleResult const& o : results)
        {
            if (!out.empty())
                out += ' ';
            out += o.id + ":" + VerdictName(o.verdict);
            if (o.verdict == Verdict::Fail)
                out += "@" + Sec(o.firstMs);
        }
        return out;
    }

    void ApplyOverrides(OracleConfig& c, DcLabJson::Value const& oracles, std::string* unknown)
    {
        if (!oracles.IsObject())
            return;
        auto miss = [&](std::string const& k)
        {
            if (unknown)
                *unknown += (unknown->empty() ? "" : ",") + k;
        };
        for (auto const& [id, body] : oracles.obj)
        {
            if (!body.IsObject())
                continue;  // "pass"/"fail" expectations are read by the scenario layer
            for (auto const& kv : body.obj)
            {
                std::string const& k = kv.first;
                DcLabJson::Value const& v = kv.second;
                std::string const key = id + "." + k;
                auto u32 = [&](std::uint32_t& dst) { dst = static_cast<std::uint32_t>(v.AsNumber(dst)); };
                auto i32 = [&](std::int32_t& dst) { dst = static_cast<std::int32_t>(v.AsNumber(dst)); };
                auto f = [&](float& dst) { dst = static_cast<float>(v.AsNumber(dst)); };
                auto ms = [&](std::uint32_t& dst) { dst = static_cast<std::uint32_t>(v.AsNumber(dst / 1000.0) * 1000.0); };
                if (k == "expect")
                    continue;
                if (key == "O1.unattendedS") ms(c.o1UnattendedMs);
                else if (key == "O1.parkedRangedS") ms(c.o1ParkedRangedMs);
                else if (key == "O1.tankNearYd") f(c.o1TankNearYd);
                else if (key == "O2.toleranceMs") u32(c.o2ToleranceMs);
                else if (key == "O2.selfDefenseExempt") c.o2SelfDefenseExempt = v.AsBool(c.o2SelfDefenseExempt);
                else if (key == "O2.countAttackStart") c.o2CountAttackStart = v.AsBool(c.o2CountAttackStart);
                else if (key == "O3.hpDropPct") f(c.o3HpDropPct);
                else if (key == "O4.tankFightS") ms(c.o4TankFightMs);
                else if (key == "O4.idleS") ms(c.o4IdleMs);
                else if (key == "O5.streakS") ms(c.o5StreakMs);
                else if (key == "O6.formingS") ms(c.o6FormingMs);
                else if (key == "O6.advancingS") ms(c.o6AdvancingMs);
                else if (key == "O6.returningS") ms(c.o6ReturningMs);
                else if (key == "O6.idleCombatS") ms(c.o6IdleCombatMs);
                else if (key == "O7.joinersMax") i32(c.o7JoinersMax);
                else if (key == "O7.outsideMax") i32(c.o7OutsideMax);
                else if (key == "O8.maxFlips") u32(c.o8MaxFlips);
                else if (key == "O8.maxReversals") u32(c.o8MaxReversals);
                else if (key == "O9.waitS") ms(c.o9WaitMs);
                else if (key == "O9.humanFightS") ms(c.o9HumanFightMs);
                else if (key == "O10.requireGoal") c.o10RequireGoal = v.AsBool(c.o10RequireGoal);
                else if (key == "O10.noDeaths") c.o10NoDeaths = v.AsBool(c.o10NoDeaths);
                else miss(key);
            }
        }
    }
}
