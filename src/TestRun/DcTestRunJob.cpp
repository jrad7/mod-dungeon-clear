/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcTestRunJob.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <limits>
#include <optional>
#include <set>

#include "CharacterCache.h"
#include "DBCStores.h"
#include "Chat.h"
#include "Creature.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "InstanceSaveMgr.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "Util/DcPlayerbotsConfig.h"
#include "World.h"

#include "AiFactory.h"
#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "Random.h"

#include "DcStrategyGate.h"
#include "Util/DcBotProvisioning.h"
#include "Util/DcDungeonAccess.h"
#include "Util/DcPoolBots.h"
#include "Util/DcProvisionBudget.h"
#include "Ai/Dungeon/DungeonClear/Action/DcActionShared.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonWingRegistry.h"
#include "Ai/Dungeon/DungeonClear/DcPullContext.h"
#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRunWing.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "TestRun/DcDiagSnapshot.h"
#include "TestRun/DcTestComp.h"
#include "Lab/DcLabOracles.h"
#include "Lab/DcLabPaths.h"
#include "Lab/DcLabRecorder.h"

namespace
{
    // Stage timeouts: a setup stage overrunning these is itself the failure.
    // Setup-stage bases; the per-run bound is Scaled(base) = base + 3s per
    // member (raid-support Plan D): spawn/provision/teleport work is issued a
    // bot at a time (provisioning literally 1 bot/tick globally), so a 25-man
    // legitimately needs ~5x a 5-man's clock while a 5-man keeps roughly the
    // old bounds.
    constexpr uint32 SPAWN_TIMEOUT_MS = 60 * 1000;
    constexpr uint32 PROVISION_TIMEOUT_MS = 60 * 1000;
    constexpr uint32 GROUP_TIMEOUT_MS = 30 * 1000;
    // Covers BOTH teleport waves (leader, then the rest — see TickTeleporting),
    // and the stage clock does not restart between them.
    constexpr uint32 TELEPORT_TIMEOUT_MS = 45 * 1000;
    constexpr uint32 START_TIMEOUT_MS = 20 * 1000;

    constexpr uint32 MONITOR_STEP_MS = 1000;

    // How much closer the party must get to its target for the sample to count
    // as ground gained. Above the idle jitter of a bot settling on a spot, so a
    // parked tank drifting a few centimetres can't hold the watchdog open
    // forever; far below any real leg of travel.
    constexpr float DC_TESTRUN_PROGRESS_EPSILON_YD = 1.0f;

    uint64 NowUnixMs()
    {
        return static_cast<uint64>(std::time(nullptr)) * 1000;
    }

    std::string MakeRunId()
    {
        static uint32 counter = 0;
        std::time_t const now = std::time(nullptr);
        std::tm tmBuf{};
        localtime_r(&now, &tmBuf);
        char buf[32];
        std::strftime(buf, sizeof(buf), "tr-%Y%m%d-%H%M%S", &tmBuf);
        return std::string(buf) + "-" + std::to_string(++counter);
    }

}

DcTestRunJob::~DcTestRunJob() = default;

void DcTestRunJob::ArmTrace(Player* tank, AiObjectContext* ctx)
{
    if (!_traceRequested || _trace || !tank || !ctx)
        return;
    DcLab::Header h;
    h.runId = _record.runId;
    h.seed = _record.compSeed;
    h.mapId = _mapId;
    h.instanceId = tank->GetInstanceId();
    h.pullSetting = ctx->GetValue<uint32>(DcKey::PullSetting)->Get();
    h.releaseDelayMs = static_cast<uint32>(DcSettings::GetFloat(_tankGuid, "PullPlayerReleaseDelay") * 1000.0f);
    _trace = std::make_unique<DcLabRecorder>(std::move(h));
    for (std::size_t i = 0; i < _slots.size(); ++i)
        if (Player* bot = ObjectAccessor::FindPlayer(_slots[i].guid))
            _trace->AddMember(bot, _slots[i].role, false, i == 0, "fast");
    _trace->Arm();
    LOG_INFO("playerbots.dungeonclear", "TESTRUN {} trace armed (Pull Lab recorder, {} members)",
             _record.runId, _slots.size());
}

void DcTestRunJob::FinishTrace()
{
    if (!_trace)
        return;
    _trace->End(_record.result.empty() ? std::string("teardown") : _record.result);
    DcLab::Trace& tr = _trace->TraceRef();
    tr.oracles = DcLabOracles::Evaluate(tr, DcLabOracles::OracleConfig{});
    _record.oracles = DcLabOracles::Summary(tr.oracles);
    _record.traceFile = _trace->WriteFile(DcLabPaths::TraceDir());
    LOG_INFO("playerbots.dungeonclear", "TESTRUN TRACE {} {} -> {}", _record.runId, _record.oracles,
             _record.traceFile);
    for (DcLab::OracleResult const& o : tr.oracles)
        if (o.verdict == DcLab::Verdict::Fail)
            LOG_INFO("playerbots.dungeonclear", "TESTRUN TRACE {} {} {} @{}ms: {}", _record.runId, o.id, o.name,
                     o.firstMs, o.detail);
    _trace.reset();
}

char const* DcTestRunJob::StageName(Stage s)
{
    switch (s)
    {
        case Stage::SpawningBots: return "spawning_bots";
        case Stage::Provisioning: return "provisioning";
        case Stage::Grouping:     return "grouping";
        case Stage::Teleporting:  return "teleporting";
        case Stage::Starting:     return "starting";
        case Stage::Monitoring:   return "monitoring";
        case Stage::Lab:          return "lab";
        case Stage::TearingDown:  return "tearing_down";
    }
    return "?";
}

void DcTestRunJob::EnterStage(Stage s)
{
    _stage.store(s);
    _stageMs = 0;
}

Player* DcTestRunJob::FindGm() const
{
    // FindConnectedPlayer, NOT FindPlayer: the GM teleporting into the
    // instance to watch sits on a loading screen for a few ticks — not in
    // world, but very much still logged in. FindPlayer() returned null there,
    // and the liveness check below read it as a logout and aborted every run
    // the moment the GM zoned in. Session-based lookup survives the loading
    // screen; a real logout still goes null as soon as the session closes.
    return ObjectAccessor::FindConnectedPlayer(_gmGuid);
}

Player* DcTestRunJob::FindTank() const
{
    return ObjectAccessor::FindPlayer(_tankGuid);
}

void DcTestRunJob::ReassertMaster()
{
    Player* const gm = FindGm();
    if (!gm)
        return;

    for (Slot const& slot : _slots)
    {
        Player* const bot = ObjectAccessor::FindPlayer(slot.guid);
        if (!bot)
            continue;
        PlayerbotAI* const botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || botAI->GetMaster() == gm)
            continue;

        // Name it once. WHICH member loses its master, and how far into the run,
        // is the missing half of the diagnosis — the rate of the message is the
        // answer to "is it lost once at setup, or repeatedly?".
        if (!_masterRepairLogged)
        {
            _masterRepairLogged = true;
            Player* const had = botAI->GetMaster();
            LOG_INFO("playerbots.dungeonclear",
                     "TESTRUN {} react-delay repair: {}'s playerbots master was {} "
                     "({}s into monitoring) — reinstating {}. Until this fires the "
                     "bot thinks on GetReactDelay()'s slow path (up to 3s per tick).",
                     RunId(), bot->GetName(), had ? had->GetName() : "cleared",
                     _monitorMs / 1000, gm->GetName());
        }
        botAI->SetMaster(gm);
        // Whatever cleared the master very likely ran ResetStrategies() with it
        // (that is the shape of every master-clearing path in stock), which puts
        // stock follow-master back. Re-strip it on the repair path exactly as
        // Grouping does, so a reinstated GM master can never start a bot jogging
        // toward the invisible driver parked outside the instance.
        botAI->ChangeStrategy("-follow", BOT_STATE_NON_COMBAT);
        botAI->ChangeStrategy("-follow", BOT_STATE_COMBAT);
    }
}

void DcTestRunJob::InitIdentity(Player* gm, DcTestDungeonRegistry::Row const& row, uint32 level,
                                bool heroic, uint32 seed, std::string const& planId)
{
    _dungeonToken = row.token;
    _mapId = row.mapId;
    _x = row.x;
    _y = row.y;
    _z = row.z;
    _o = row.o;
    _heroic = heroic;
    _level = level;
    _gmGuid = gm->GetGUID();
    MapEntry const* mapEntry = sMapStore.LookupEntry(_mapId);
    _isRaidMap = mapEntry && mapEntry->IsRaid();

    _limits.pauseGraceMs = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.PauseGraceS") * 1000;
    _limits.stallGraceMs = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.StallGraceS") * 1000;
    _limits.noProgressMs = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.NoProgressS") * 1000;
    _limits.overallTimeoutMs = DcSettings::GetUInt(ObjectGuid::Empty, "TestRun.OverallTimeoutS") * 1000;
    // A row may carry its own watchdog budget (a long scripted event — chess
    // plays for up to half an hour with long quiet stretches); 0 keeps the
    // global value. Resolved here so the record's `watchdog` block shows what
    // the run was actually held to.
    if (row.overallTimeoutS)
        _limits.overallTimeoutMs = row.overallTimeoutS * 1000;
    if (row.noProgressS)
        _limits.noProgressMs = row.noProgressS * 1000;

    // A map whose wing is picked per run (Blackrock Spire) takes it from the
    // row: the wing whose token is the row's. Proximity maps (DM, SM) leave it
    // to where the party lands, exactly as before.
    _runWing.clear();
    _wingMask = 0;
    if (DungeonWingLayout const* layout = DungeonWingRegistry::Get(row.mapId);
        layout && layout->isolated && layout->select == WingSelect::Explicit)
        if (DungeonWing const* wing = DungeonWingRegistry::FindWing(*layout, row.token))
        {
            _runWing = wing->token;
            _wingMask = wing->encounterMask;
        }

    _isScenario = DcTestDungeonRegistry::IsScenario(row);
    _focus = row.focusEntries;
    _success = row.success;
    _successGraceMs = row.successGraceS * 1000;
    _grace = DcTestRun::ScenarioGrace{};
    _progressClock = DcTestRun::ProgressClock{};

    _record = DcTestRunRecord::Record{};
    if (_isScenario)
    {
        _record.scenario = row.token;
        _record.scenarioOf = row.scenarioOf;
        _record.focus = row.focusEntries;
    }
    _record.successPredicate = DcTestDungeonRegistry::DescribePredicate(row.success);
    _record.runId = MakeRunId();
    _record.planId = planId;
    _record.dungeon = row.token;
    _record.dungeonName = row.name;
    _record.wing = row.wing;
    _record.mapId = row.mapId;
    _record.level = _level;
    _record.heroic = heroic;
    _record.compSeed = seed;
    _record.startedAtMs = NowUnixMs();
    _record.pauseGraceS = _limits.pauseGraceMs / 1000;
    _record.stallGraceS = _limits.stallGraceMs / 1000;
    _record.noProgressS = _limits.noProgressMs / 1000;
    _record.overallS = _limits.overallTimeoutMs / 1000;
}

std::unique_ptr<DcTestRunJob> DcTestRunJob::Create(Player* gm, DcTestDungeonRegistry::Row const& row,
                                                   uint32 levelOverride, uint32 seed, uint32 size,
                                                   bool heroic,
                                                   DcTestGearTiers::Spec const& gear,
                                                   std::unordered_set<ObjectGuid> const& reservedGuids,
                                                   std::string const& planId, std::string* err)
{
    // Caller (DcTestRunManager::Start) has already validated the registry row
    // (including that heroic is only requested where heroicLevel is set) and
    // that gm has a playerbot manager.

    // seed 0 = "roll one" — pick a nonzero seed so the comp varies per run yet
    // is recorded for exact replay via `.dc test start <d> seed=N`.
    if (seed == 0)
        seed = rand32() | 1u;

    uint32 const level = levelOverride ? std::min<uint32>(levelOverride, 80u)
                                       : (heroic ? row.heroicLevel : row.recommendedLevel);

    // Death knights are a Wrath class whose kit starts at 55, so they are on
    // the table only for a WotLK row — and only when the run's LEVEL actually
    // reaches the floor, because `level=N` can drop a WotLK run under it and
    // the factory levels every test bot to whatever the run asked for.
    DcTestComp::Roster const roster = RosterFor(row, level);

    // size 0 keeps the classic 5-man draw (distinct classes) bit-for-bit; a
    // requested size uses the quota'd raid comp (duplicates spread evenly).
    std::vector<DcTestComp::Slot> comp;
    if (size == 0)
    {
        auto const classic = DcTestComp::BuildComp(seed, roster);
        comp.assign(classic.begin(), classic.end());
    }
    else
        comp = DcTestComp::BuildComp(seed, size, roster);
    return CreateWithComp(gm, row, level, seed, heroic, gear, comp, reservedGuids, planId, err);
}

DcTestComp::Roster DcTestRunJob::RosterFor(DcTestDungeonRegistry::Row const& row, uint32 level)
{
    return DcTestDungeonRegistry::ExpansionOf(row) >= DcTestDungeonRegistry::kExpansionWrath &&
                   level >= DcTestComp::kDeathKnightMinLevel
               ? DcTestComp::Roster::WithDeathKnights
               : DcTestComp::Roster::NoDeathKnights;
}

std::unique_ptr<DcTestRunJob> DcTestRunJob::CreateWithComp(Player* gm, DcTestDungeonRegistry::Row const& row,
                                                           uint32 level, uint32 seed, bool heroic,
                                                           DcTestGearTiers::Spec const& gear,
                                                           std::vector<DcTestComp::Slot> const& comp,
                                                           std::unordered_set<ObjectGuid> const& reservedGuids,
                                                           std::string const& planId, std::string* err)
{
    std::unique_ptr<DcTestRunJob> job(new DcTestRunJob());
    job->InitIdentity(gm, row, level, heroic, seed, planId);

    // Resolve the gear ceiling against the conf once, here: a run that took 40
    // minutes must be reproducible from its record even if somebody reloaded
    // the config while it was in the dungeon.
    job->_gear = DcTestGearTiers::Resolve(gear, DC_PB_CONFIG(AutoGearScoreLimit, autoGearScoreLimit),
                                          DC_PB_CONFIG(AutoGearQualityLimit, autoGearQualityLimit));
    job->_record.gearIlvl = job->_gear.ilvl;
    job->_record.gearQuality = job->_gear.quality;
    DcTestComp::Roster const roster = RosterFor(row, level);

    job->_record.size = static_cast<uint32>(comp.size());
    for (DcTestComp::Slot const& c : comp)
    {
        Slot s;
        s.classId = c.classId;
        s.specName = c.specName;
        s.fallbackSpec = c.fallbackSpec;
        s.role = c.role;
        job->_slots.push_back(s);
    }

    PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(gm);
    bool const isAlliance = gm->GetTeamId(true) == TEAM_ALLIANCE;

    // Claim one offline addclass-pool character of `classId` (the `addclass`
    // command's own selection rules), or Empty if none is free.
    auto claim = [&](uint8 classId) -> ObjectGuid
    {
        auto const& pool = sRandomPlayerbotMgr.addclassCache[
            RandomPlayerbotMgr::GetTeamClassIdx(isAlliance, classId)];
        for (ObjectGuid const& guid : pool)
        {
            // Skip guids already claimed by this job's earlier slots and by any
            // other live run (cross-run reservation set, world-thread only).
            bool taken = false;
            for (Slot const& other : job->_slots)
                if (other.guid == guid)
                    taken = true;
            if (taken)
                continue;
            if (reservedGuids.find(guid) != reservedGuids.end())
                continue;
            // The queue fills draw from this same pool. Their claims are
            // invisible here for the tick between picking a guid and
            // AddPlayerBot landing, so ask them rather than relying on the
            // "already online" test below.
            if (DcPoolBots::IsClaimedByFill(guid))
                continue;
            // (No botLoading check — it is protected on PlayerbotHolder;
            // AddPlayerBot itself no-ops on an in-flight load, and a char
            // loading for someone else just times the spawn stage out.)
            if (ObjectAccessor::FindConnectedPlayer(guid))
                continue;
            uint32 const guildId = sCharacterCache->GetCharacterGuildIdByGuid(guid);
            if (guildId && PlayerbotGuildMgr::instance().IsRealGuild(guildId))
                continue;
            return guid;
        }
        return ObjectGuid::Empty;
    };

    // Fill each slot with its drawn class, then start its async login. When the
    // drawn class has no free pool character, substitute another class of the
    // same role that does (and isn't already in the party) rather than fail —
    // randomisation shouldn't abort a run just because one class is un-seeded.
    // Only a role with no fillable class at all is fatal.
    std::set<uint8> usedClasses;
    for (Slot& slot : job->_slots)
    {
        ObjectGuid guid = claim(slot.classId);
        if (!guid)
        {
            // Distinct-class substitution first; past 9 slots the distinct rule
            // is impossible anyway, so a raid-sized run may substitute a class
            // already in the party rather than fail the role.
            bool const allowDuplicates = job->_slots.size() > 9;
            for (DcTestComp::Slot const& alt : DcTestComp::RolePool(slot.role, roster))
            {
                if (alt.classId == slot.classId)
                    continue;
                if (!allowDuplicates && usedClasses.count(alt.classId))
                    continue;
                guid = claim(alt.classId);
                if (guid)
                {
                    LOG_INFO("playerbots.dungeonclear",
                             "TESTRUN {} substituting {} for unavailable {} ({})",
                             job->_record.runId, DcBotProvisioning::ClassToken(alt.classId),
                             DcBotProvisioning::ClassToken(slot.classId), slot.role);
                    slot.classId = alt.classId;
                    slot.specName = alt.specName;
                    slot.fallbackSpec = alt.fallbackSpec;
                    break;
                }
            }
        }
        if (!guid)
        {
            if (err)
                *err = std::string("no available ") + slot.role +
                       " class in the addclass pool — pre-seed with `.playerbots addclass`";
            return nullptr;
        }
        slot.guid = guid;
        usedClasses.insert(slot.classId);
        // Harness adds are exempt from Playerbots.MaxAddedBots. That cap is
        // playerbots' per-account limit on hand-added bots, and every run's
        // party is added under the one issuing GM (usually the test driver),
        // so it capped the whole harness — all concurrent runs together — at
        // MaxAddedBots bots. AddPlayerBot reads the cap synchronously, so
        // lifting it for this one call on the world thread leaves a player's
        // own `.playerbot add` limit exactly as configured. What bounds the
        // harness is the addclass pool (a launch without free characters
        // backs off) and the machine.
        int32 const addedCap = DC_PB_CONFIG(MaxAddedBots, maxAddedBots);
        DC_PB_CONFIG(MaxAddedBots, maxAddedBots) = std::numeric_limits<int32>::max();
        mgr->AddPlayerBot(slot.guid, gm->GetSession()->GetAccountId());
        DC_PB_CONFIG(MaxAddedBots, maxAddedBots) = addedCap;
    }

    LOG_INFO("playerbots.dungeonclear",
             "TESTRUN START {} dungeon={} map={} level={} heroic={} seed={} gm={}",
             job->_record.runId, job->_record.dungeon, job->_mapId, job->_level,
             heroic ? 1 : 0, seed, gm->GetName());

    job->EnterStage(Stage::SpawningBots);
    return job;
}

std::unique_ptr<DcTestRunJob> DcTestRunJob::CreateLab(Player* gm, DcTestDungeonRegistry::Row const& row,
                                                      uint32 level, uint32 seed, bool heroic,
                                                      DcTestGearTiers::Spec const& gear,
                                                      std::vector<DcTestComp::Slot> const& comp,
                                                      std::shared_ptr<LabDriver> driver,
                                                      std::unordered_set<ObjectGuid> const& reservedGuids,
                                                      std::string* err)
{
    if (!driver)
    {
        if (err)
            *err = "no lab driver";
        return nullptr;
    }
    std::unique_ptr<DcTestRunJob> job =
        CreateWithComp(gm, row, level, seed ? seed : 1u, heroic, gear, comp, reservedGuids, "", err);
    if (job)
        job->_lab = std::move(driver);
    return job;
}

DcTestRunJob::LabParty DcTestRunJob::MakeLabParty() const
{
    LabParty p;
    p.gm = _gmGuid;
    for (Slot const& slot : _slots)
    {
        p.members.push_back(slot.guid);
        p.roles.push_back(slot.role);
    }
    p.mapId = _mapId;
    p.instanceId = _destInstanceId;
    p.runId = _record.runId;
    return p;
}

void DcTestRunJob::TickLab(uint32 diff)
{
    {
        std::lock_guard<std::mutex> lock(_obsMutex);
        if (_abortRequested)
        {
            _record.result = "aborted";
            _record.failReason = _abortReason;
        }
    }
    if (_record.result == "aborted")
    {
        Teardown();
        return;
    }
    if (_lab->KeepGmMaster())
        ReassertMaster();
    if (!_lab->Tick(MakeLabParty(), diff))
    {
        _record.result = "lab_done";
        Teardown();
    }
}

std::unique_ptr<DcTestRunJob> DcTestRunJob::CreateFromRoster(Player* gm,
                                                             DcTestDungeonRegistry::Row const& row,
                                                             bool heroic,
                                                             std::vector<RosterEntry> const& roster,
                                                             std::string const& planId,
                                                             std::string* err)
{
    if (roster.size() < DcTestComp::kMinPartySize ||
        roster.size() > DcTestComp::kMaxPartySize)
    {
        if (err)
            *err = "a roster must be " + std::to_string(DcTestComp::kMinPartySize) + "-" +
                   std::to_string(DcTestComp::kMaxPartySize) + " characters";
        return nullptr;
    }

    std::unique_ptr<DcTestRunJob> job(new DcTestRunJob());
    job->_realChars = true;

    // Level is whatever the characters are. The highest of the five stands in
    // for the run until provisioning reads the live values, so the status line
    // and the dungeon's own level expectations have something sane to show; the
    // record's per-member levels are the real answer.
    uint32 level = 0;
    for (RosterEntry const& e : roster)
        level = std::max<uint32>(level, sCharacterCache->GetCharacterLevelByGuid(e.guid));

    // seed 0: a roster IS the comp, so there is nothing to replay from a seed.
    job->InitIdentity(gm, row, level, heroic, /*seed*/ 0, planId);
    job->_record.roster = true;

    for (RosterEntry const& e : roster)
    {
        Slot s;
        s.guid = e.guid;
        s.role = e.role;
        s.rosterName = e.name;
        // Snapshot the guild BEFORE the login: stock playerbots guilds a guildless
        // bot on login, and this is the only moment the original state is knowable.
        s.guildBefore = sCharacterCache->GetCharacterGuildIdByGuid(e.guid);
        // classId off the cache so the record and the live map overlay have it
        // before the character is in world; spec templates are never applied to
        // a real character, so specName/fallbackSpec stay empty.
        if (CharacterCacheEntry const* cache = sCharacterCache->GetCharacterCacheByGuid(e.guid))
            s.classId = cache->Class;
        job->_slots.push_back(std::move(s));
    }

    // MASTERLESS login. AddPlayerBot's ownership gate (PlayerbotMgr.cpp) clears
    // only for same-account / same-guild / addclass-pool / linked characters —
    // a hand-picked party is none of those, and passing the GM's account id would
    // see every slot refused with "not allowed to control bot". masterAccountId 0
    // takes the isRndbot branch, which skips the gate; it is the same call the
    // headless test driver logs itself in with. The party therefore lands in
    // sRandomPlayerbotMgr rather than the GM's PlayerbotMgr, which LogoutBots
    // already handles, and Grouping still installs the GM as playerbots master
    // so HasRealPlayerMaster (and the react-delay fast path) is unaffected.
    //
    // Landing in sRandomPlayerbotMgr does NOT enrol the character in the
    // random-bot rotation — the thing that would periodically re-Randomize (i.e.
    // regear) or relocate it. That rotation walks `currentBots`, populated purely
    // from the playerbots DB's own enrolment rows (RandomPlayerbotMgr::GetBots),
    // and IsRandomBot additionally demands the account be in
    // Playerbots.RandomBotAccounts. A real player's character satisfies neither,
    // so the holder only owns its login/logout here.
    for (Slot const& slot : job->_slots)
        sRandomPlayerbotMgr.AddPlayerBot(slot.guid, 0);

    std::string names;
    for (Slot const& slot : job->_slots)
    {
        if (!names.empty())
            names += ",";
        names += slot.rosterName + "(" + slot.role + ")";
    }
    LOG_INFO("playerbots.dungeonclear",
             "TESTRUN START {} dungeon={} map={} heroic={} roster={} gm={}",
             job->_record.runId, job->_record.dungeon, job->_mapId, heroic ? 1 : 0,
             names, gm->GetName());

    job->EnterStage(Stage::SpawningBots);
    return job;
}

void DcTestRunJob::RequestAbort(std::string const& reason)
{
    std::lock_guard<std::mutex> lock(_obsMutex);
    _abortRequested = true;
    _abortReason = reason;
}

void DcTestRunJob::AbortSetup(std::string const& reason)
{
    FailSetup(reason);
}

std::string DcTestRunJob::StatusLine() const
{
    Stage const stage = _stage.load();
    std::string out = _record.runId + " " + _record.dungeon + (_heroic ? " (heroic)" : "") +
                      " [" + StageName(stage) + "] elapsed " + std::to_string(_totalMs / 1000) + "s";
    // The gear ceiling belongs in the start confirmation: it is the one run
    // parameter with no visible effect until the party is already fighting.
    if (!_realChars)
        out += ", gear " +
               (_gear.ilvl ? "ilvl<=" + std::to_string(_gear.ilvl) : std::string("unlimited")) +
               " " + DcTestGearTiers::QualityName(_gear.quality);
    if (stage == Stage::Lab && _lab)
        out += ", lab " + _lab->Status();
    if (stage == Stage::Monitoring)
    {
        out += ", bosses " + std::to_string(_record.bossesKilled) + "/" +
               std::to_string(_record.bossesTotal);
        std::lock_guard<std::mutex> lock(_obsMutex);
        if (!_lastStatusState.empty())
            out += ", state " + _lastStatusState;
    }
    return out;
}

DcTestRunLive::RunSnapshot DcTestRunJob::Snapshot() const
{
    DcTestRunLive::RunSnapshot s;
    s.runId = _record.runId;
    s.planId = _record.planId;
    s.dungeon = _record.dungeon;
    s.dungeonName = _record.dungeonName;
    s.stage = StageName(_stage.load());
    s.level = _level;
    s.heroic = _heroic;
    s.elapsedS = _totalMs / 1000;
    s.bossesKilled = _record.bossesKilled;
    s.bossesTotal = _record.bossesTotal;

    // Live positions for the dashboard map overlay. Snapshot() is called from
    // the manager's world-thread tick (WriteLiveStatus), so resolving guids to
    // players and reading their transform is safe here. The run's mapId is the
    // tank's (all members share one instance); a member still loading resolves
    // to null and is simply omitted this heartbeat.
    for (Slot const& slot : _slots)
    {
        if (!slot.guid)
            continue;
        Player* p = ObjectAccessor::FindPlayer(slot.guid);
        if (!p)
            continue;
        if (s.mapId < 0)
            s.mapId = static_cast<std::int32_t>(p->GetMapId());
        DcTestRunLive::BotPos bp;
        bp.role = slot.role;
        // The slot only knows class/spec/role; the name belongs to the char the
        // pool handed out, so it has to come off the resolved player.
        bp.name = p->GetName();
        bp.classId = slot.classId;
        bp.x = p->GetPositionX();
        bp.y = p->GetPositionY();
        bp.z = p->GetPositionZ();
        bp.alive = p->IsAlive();
        bp.hp = static_cast<std::uint8_t>(bp.alive ? p->GetHealthPct() : 0.f);
        // Gate on the mana POOL, not on getPowerType(): a druid shifted into
        // bear/cat reports RAGE/ENERGY while still holding the mana it has to
        // shift back and drink for. Keying off the active power type would blank
        // the bar for exactly the member whose mana the run is waiting on.
        // Non-mana classes have maxMana 0 and stay at the -1 "no bar" default.
        if (bp.alive && p->GetMaxPower(POWER_MANA) > 0)
            bp.mp = static_cast<std::int16_t>(p->GetPowerPct(POWER_MANA));
        bp.inCombat = p->IsInCombat();
        if (bp.inCombat)
            s.inCombat = true;
        s.bots.push_back(std::move(bp));
    }

    // Why the run is sitting still, read live off the tank rather than from the
    // status timeline: the timeline only records state CHANGES, so a run that
    // has been wedged in one state for ten minutes has nothing recent in it.
    if (Player* tank = FindTank())
        if (PlayerbotAI* tankAI = GET_PLAYERBOT_AI(tank))
        {
            AiObjectContext* ctx = tankAI->GetAiObjectContext();
            s.stall = ctx->GetValue<std::string&>(DcKey::StallReason)->Get();
            std::optional<DungeonBossInfo> const next =
                ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();
            if (next.has_value())
                s.bossName = next->name;
        }

    // Not under _obsMutex: the watchdog writes _sinceProgressMs outside it too,
    // and both that write and this read happen on the world thread.
    s.sinceProgressS = _sinceProgressMs / 1000;

    // A wipe holds the run for the grace window (a battle rez can still save
    // it), during which the live card would otherwise show a party doing
    // nothing at all. Name the killer as soon as the harness sees the wipe
    // rather than only in the finished record.
    if (_wipedForMs > 0)
    {
        DcTestRun::Engagement const blame = DeathBlame();
        s.wiped = true;
        s.wipeOnBoss = blame.isBoss;
        s.wipeOpponent = blame.name;
    }

    std::lock_guard<std::mutex> lock(_obsMutex);
    s.state = _lastStatusState;
    std::vector<DcTestRunRecord::StatusEntry> const& st = _record.statusTimeline;
    std::size_t const from = st.size() > 8 ? st.size() - 8 : 0;
    for (std::size_t i = from; i < st.size(); ++i)
        s.recent.push_back({st[i].t, st[i].state, st[i].detail});
    return s;
}

std::vector<ObjectGuid> DcTestRunJob::BotGuids() const
{
    std::vector<ObjectGuid> out;
    out.reserve(_slots.size());
    for (Slot const& slot : _slots)
        if (slot.guid)
            out.push_back(slot.guid);
    return out;
}

void DcTestRunJob::Tick(uint32 diff)
{
    Stage const stage = _stage.load();
    if (stage == Stage::TearingDown || _done)
        return;

    _stageMs += diff;
    _totalMs += diff;

    // The GM's session anchors the bots (their logout logs the party out);
    // without it the run cannot finish cleanly whatever stage it is in. One
    // GM's logout kills only their runs — the check is per-job.
    if (!FindGm())
    {
        if (stage == Stage::Monitoring)
            Finish(DcTestRun::Verdict::FailAborted, "GM logged out mid-run");
        else if (stage == Stage::Lab)
        {
            _record.result = "aborted";
            _record.failReason = "GM logged out mid-lab";
            Teardown();
        }
        else
            FailSetup("GM logged out during setup");
        return;
    }

    switch (stage)
    {
        case Stage::SpawningBots:
            TickSpawning();
            break;
        case Stage::Provisioning:
            TickProvisioning();
            break;
        case Stage::Grouping:
            TickGrouping();
            break;
        case Stage::Teleporting:
            TickTeleporting();
            break;
        case Stage::Starting:
            TickStarting();
            break;
        case Stage::Lab:
            TickLab(diff);
            break;
        case Stage::Monitoring:
            _monitorMs += diff;
            // Every tick, NOT on the monitor's 1s step: a bot crosses a small
            // trigger box in well under a second, and a relay that samples at
            // 1Hz would silently skip the set-piece it exists to fire.
            _areaTriggers.Tick(FindTank());
            if (_trace)
                _trace->Tick(diff);
            _monitorAccumMs += diff;
            if (_monitorAccumMs >= MONITOR_STEP_MS)
            {
                uint32 const dt = _monitorAccumMs;
                _monitorAccumMs = 0;
                TickMonitoring(dt);
            }
            break;
        default:
            break;
    }
}

void DcTestRunJob::TickSpawning()
{
    bool allIn = true;
    for (Slot const& slot : _slots)
    {
        Player* bot = ObjectAccessor::FindPlayer(slot.guid);
        if (!bot || !bot->IsInWorld() || !GET_PLAYERBOT_AI(bot))
        {
            allIn = false;
            break;
        }
    }

    if (allIn)
    {
        EnterStage(Stage::Provisioning);
        return;
    }

    if (_stageMs >= Scaled(SPAWN_TIMEOUT_MS))
    {
        if (_realChars)
        {
            // Name the character that never arrived: for a hand-picked party the
            // usual cause is somebody logging in on it between the pre-flight
            // check and the login, which the pool path cannot experience.
            std::string missing;
            for (Slot const& slot : _slots)
            {
                Player* bot = ObjectAccessor::FindPlayer(slot.guid);
                if (bot && bot->IsInWorld() && GET_PLAYERBOT_AI(bot))
                    continue;
                if (!missing.empty())
                    missing += ", ";
                missing += slot.rosterName;
            }
            FailSetup("roster characters did not finish logging in (" + missing +
                      ") — logged in as a real player, or a login failure (see server log)");
            return;
        }
        FailSetup("bots did not finish logging in (addclass pool empty, "
                  "maxAddedBots cap, or login failure — see server log)");
    }
}

void DcTestRunJob::TickProvisioning()
{
    if (_stageMs >= Scaled(PROVISION_TIMEOUT_MS))
    {
        FailSetup("provisioning timed out");
        return;
    }

    // Real characters are never rolled — read them out and move on.
    if (_realChars)
    {
        TickProvisioningRoster();
        return;
    }

    if (_provisionIdx >= _slots.size())
    {
        EnterStage(Stage::Grouping);
        return;
    }

    Slot& slot = _slots[_provisionIdx];
    Player* bot = ObjectAccessor::FindPlayer(slot.guid);
    PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!bot || !bot->IsInWorld() || !botAI)
        return;  // transient (mid world-add/teleport) — retry; stage timeout bounds it

    std::string pickedSpec;
    int const specNo = DcBotProvisioning::ResolveSpecNo(slot.classId, slot.specName, slot.fallbackSpec, &pickedSpec);
    if (specNo < 0 && (slot.role == std::string("tank") || slot.role == std::string("heal")))
    {
        // A random-rolled tank/healer spec would invalidate the whole run.
        FailSetup(std::string("no premade spec template matching '") + slot.specName +
                  "' for " + DcBotProvisioning::ClassToken(slot.classId) +
                  " (Playerbots.PremadeSpecName.*) — cannot force the " + slot.role);
        return;
    }

    // One factory roll per world tick REALM-WIDE — Randomize is heavyweight
    // (full gear/spell/talent roll) and several in one tick would be a visible
    // stall. If another run, or the RDF queue-fill subsystem, already spent
    // this tick's ration, retry next tick; the stage timeout bounds the wait.
    if (!DcProvisionBudget::Take())
        return;

    // Gear ceiling: the run's own (_gear, resolved at Create from the `ilvl=` /
    // `quality=` options or the AutoGear* conf values), applied exactly the way
    // the `autogear` chat command applies the server-wide ones.
    //
    // This used to be a hardcoded ITEM_QUALITY_EPIC with no gear-score argument,
    // which silently opted every test bot out of BOTH gear limits: the factory
    // only falls back to the RandomGear* settings when itemQuality is left at 0,
    // so passing a quality also left gearScoreLimit at 0 — i.e. "no ilvl cap".
    // Runs were fought in uncapped epics no matter what AutoGearScoreLimit said.
    uint32 const gearScoreLimit =
        _gear.ilvl == 0 ? 0 : PlayerbotFactory::CalcMixedGearScore(_gear.ilvl, _gear.quality);
    std::string const gearCap = _gear.ilvl == 0 ? std::string("unlimited") : std::to_string(_gear.ilvl);

    // The shared factory sequence (strip, Randomize at the run level, force
    // the spec and re-gear/re-glyph/re-enchant for it, pet, ammo,
    // ResetStrategies). Shared with the RDF queue fill so the two subsystems
    // cannot drift apart on an order-sensitive pass that cost live runs to get
    // right — see Util/DcBotProvisioning.h.
    DcBotProvisioning::Roll(bot, _level, _gear.quality, gearScoreLimit, specNo);

    DcTestRunRecord::CompEntry entry;
    entry.name = bot->GetName();
    entry.className = DcBotProvisioning::ClassToken(slot.classId);
    entry.spec = specNo >= 0 ? pickedSpec : "(random)";
    entry.role = slot.role;
    entry.guid = slot.guid.GetRawValue();
    entry.level = bot->GetLevel();
    _record.comp.push_back(entry);

    LOG_INFO("playerbots.dungeonclear",
             "TESTRUN {} provisioned {} ({} {}, level {}, gear <= ilvl {} quality {})",
             _record.runId, bot->GetName(), entry.spec, entry.role, entry.level, gearCap,
             DcTestGearTiers::QualityName(_gear.quality));

    slot.provisioned = true;
    ++_provisionIdx;
}

// The whole of "provisioning" for a hand-picked party: describe the characters,
// change nothing about them.
//
// Deliberately absent, and none of it may come back: PlayerbotFactory::Randomize
// (re-rolls gear AND talents AND level), InitTalentsBySpecNo / InitEquipment /
// InitGlyphs / ApplyEnchantAndGemsNew (the `autogear`/`maintenance` pass), and
// InitPet/InitAmmo (a hunter's real pet is not ours to replace). A character
// marked for a run accepts dying, looting, and durability loss — it does not
// accept coming back a different character.
//
// ResetStrategies stays: it reloads strategies from config (which is how the
// dungeon-clear stack gets installed) without touching the character sheet.
//
// All five slots are read in one tick — there is no factory roll to spend the
// shared per-tick provision budget on.
// Stock playerbots joins any guildless bot to a random bot guild the moment it
// logs in: RandomPlayerbotMgr::OnBotLoginInternal calls
// PlayerbotFactory::InitGuild whenever Playerbots.RandomBotGuildCount > 0, with
// no check that the character actually IS a random bot. A roster member logs in
// through that same holder (the masterless path is the only one whose ownership
// gate a hand-picked character clears), so it gets caught too — and a real
// character must come out of a test run in the guild it went in with.
//
// So put it back. The decisive signal is that the character was guildless when
// the run claimed it (captured in CreateFromRoster, moments before the login) and
// is guilded now: that change provably happened inside our window, and the only
// actor in that window is playerbots' own InitGuild.
//
// Deliberately NOT gated on PlayerbotGuildMgr::IsRealGuild. That flag is computed
// from the guild LEADER's account, and InitGuild's create path makes the drafted
// character itself the leader — so a guild freshly conjured around a real
// character is classified "real" and the gate would refuse to undo exactly the
// case that needs undoing. The classification is logged instead of obeyed.
//
// A character that already had a guild is never touched (InitGuild returns early
// on those anyway). Guild::DeleteMember handles the leader case properly: it
// promotes the next-ranked member, or disbands when this was the only one, which
// is right for a guild that exists solely because of this bug.
void DcTestRunJob::UndoUnwantedGuild(Player* bot, Slot const& slot) const
{
    if (slot.guildBefore)
        return;  // came in with a guild — not ours to touch
    uint32 const now = bot->GetGuildId();
    if (!now)
        return;  // still guildless — nothing happened

    Guild* guild = sGuildMgr->GetGuildById(now);
    if (!guild)
        return;

    bool const classedReal = PlayerbotGuildMgr::instance().IsRealGuild(now);
    std::string const guildName = guild->GetName();
    bool const wasLeader = guild->GetLeaderGUID() == bot->GetGUID();

    guild->DeleteMember(bot->GetGUID(), /*isDisbanding*/ false, /*isKicked*/ false,
                        /*canDeleteGuild*/ true);
    LOG_INFO("playerbots.dungeonclear",
             "TESTRUN {} removed {} from guild '{}' ({}) it was auto-joined to at login — "
             "character was guildless (leader={}, playerbots classed it {}); "
             "stock RandomBotGuildCount behaviour",
             _record.runId, bot->GetName(), guildName, now, wasLeader ? "yes" : "no",
             classedReal ? "real" : "bot");
}

void DcTestRunJob::TickProvisioningRoster()
{
    for (Slot& slot : _slots)
    {
        if (slot.provisioned)
            continue;

        Player* bot = ObjectAccessor::FindPlayer(slot.guid);
        PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        if (!bot || !bot->IsInWorld() || !botAI)
            return;  // transient — retry next tick; the stage timeout bounds it

        UndoUnwantedGuild(bot, slot);
        botAI->ResetStrategies();

        // What the character's talents actually say, next to what the human
        // marked it as. A wrong marking is human error at roster time and the run
        // proceeds anyway (that is the stated policy), but it is the first thing
        // anyone will want to see in the post-mortem of a run where the "tank"
        // died in six seconds.
        char const* detected = PlayerbotAI::IsTank(bot, /*bySpec*/ true)   ? "tank"
                               : PlayerbotAI::IsHeal(bot, /*bySpec*/ true) ? "heal"
                                                                          : "dps";

        DcTestRunRecord::CompEntry entry;
        entry.name = bot->GetName();
        entry.className = DcBotProvisioning::ClassToken(bot->getClass());
        entry.spec = AiFactory::GetPlayerSpecName(bot);
        entry.role = slot.role;
        entry.detectedRole = detected;
        entry.roleMismatch = slot.role != std::string(detected);
        entry.guid = slot.guid.GetRawValue();
        entry.level = bot->GetLevel();
        entry.fromMap = bot->GetMapId();
        entry.fromX = bot->GetPositionX();
        entry.fromY = bot->GetPositionY();
        entry.fromZ = bot->GetPositionZ();
        entry.fromO = bot->GetOrientation();
        _record.comp.push_back(entry);

        // classId was taken from the character cache at Create; trust the live
        // character over the cache now that it is resolvable.
        slot.classId = bot->getClass();
        slot.provisioned = true;

        LOG_INFO("playerbots.dungeonclear",
                 "TESTRUN {} roster member {} ({} {}, level {}){}",
                 _record.runId, entry.name, entry.spec, entry.role, entry.level,
                 entry.roleMismatch ? std::string(" — WARNING: spec reads as ") + detected : "");
    }

    // Highest level present is the run's headline level (Create only had the
    // cache's view; this is the live one).
    for (DcTestRunRecord::CompEntry const& e : _record.comp)
        _level = std::max(_level, e.level);
    _record.level = _level;

    EnterStage(Stage::Grouping);
}

void DcTestRunJob::TickGrouping()
{
    if (!_groupFormed)
    {
        Player* tank = ObjectAccessor::FindPlayer(_slots[0].guid);
        if (!tank)
        {
            if (_stageMs >= Scaled(GROUP_TIMEOUT_MS))
                FailSetup("tank vanished before grouping");
            return;
        }

        for (Slot const& slot : _slots)
            if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
                if (bot->GetGroup())
                    bot->RemoveFromGroup();

        // Direct group formation (the LFGMgr pattern) — no invite/accept
        // packet round-trips, and Create() makes the tank the leader.
        Group* group = new Group();
        if (!group->Create(tank))
        {
            delete group;
            FailSetup("group creation failed");
            return;
        }
        sGroupMgr->AddGroup(group);
        // RAID party (raid-support Plan D): convert BEFORE the sixth member —
        // AddMember refuses on a full 5-man party group. Raid entry also
        // REQUIRES a raid group (MapMgr enforces it), so any raid-map run
        // converts even at size <= 5.
        bool const raidGroup = _slots.size() > 5 || IsRaidMap();
        if (raidGroup)
            group->ConvertToRaid();
        for (std::size_t i = 1; i < _slots.size(); ++i)
        {
            Player* bot = ObjectAccessor::FindPlayer(_slots[i].guid);
            if (!bot || !group->AddMember(bot))
            {
                FailSetup(std::string("could not add ") + DcBotProvisioning::ClassToken(_slots[i].classId) +
                          " to the group");
                return;
            }
        }
        if (raidGroup)
        {
            // Sub-groups round-robin (8 x 5 slots), and the leader flagged MAIN
            // TANK so DC's leader election and playerbots' GetMainTankGuid agree
            // by construction.
            uint8 subGroup = 0;
            for (std::size_t i = 0; i < _slots.size(); ++i)
            {
                group->ChangeMembersGroup(_slots[i].guid, subGroup);
                subGroup = (subGroup + 1) % MAX_RAID_SUBGROUPS;
            }
            group->SetGroupMemberFlag(tank->GetGUID(), true, MEMBER_FLAG_MAINTANK);
        }
        if (IsRaidMap())
            // Classic raids run at raid difficulty 0 (10-man normal); the
            // heroic flag is a dungeon-mode concept and never reaches here for
            // a raid row (the registry validation refuses it).
            group->SetRaidDifficulty(RAID_DIFFICULTY_10MAN_NORMAL);
        else
            group->SetDungeonDifficulty(_heroic ? DUNGEON_DIFFICULTY_HEROIC
                                                : DUNGEON_DIFFICULTY_NORMAL);
        _tankGuid = tank->GetGUID();
        _groupFormed = true;
    }

    Player* tank = FindTank();
    Group* group = tank ? tank->GetGroup() : nullptr;
    if (group && group->GetMembersCount() == _slots.size() && group->IsLeader(_tankGuid))
    {
        // Keep the GM (a real human Player) as each bot's playerbots MASTER, and
        // strip the stock follow-master strategy instead of nulling the master.
        //
        // Why not masterless: the real-player-master gate governs the whole "a
        // human is driving me" fast path in stock playerbots (pre-PR-2592 the
        // removed HasRealPlayerMaster(); now the master satisfying
        // IsRealPlayer(master) || IsSelfBot(master)). With no real-player master,
        // GetReactDelay() returns base*10 (1000ms vs 100ms) out of combat, so the
        // party thinks on a 1s beat between packs — test runs looked far slower
        // and sloppier than a real human-led run. That gate has no
        // map/distance/visibility check (it only checks the master pointer is a
        // non-bot Player or a self-bot), so keeping the GM as master holds the
        // fast path even
        // though the GM is invisible and outside the instance — the test now
        // mirrors a real run in every master-gated respect (react delay, AoE
        // avoidance, wait-for-attack), which is the point of a regression harness.
        //
        // The only reason the old code went masterless was to stop stock
        // follow-master from dragging the party toward the GM. Follow-master is
        // just the "follow" strategy, so we remove it outright here. DC's own
        // follow-tank redirect (a DcMovementAction, not a FollowAction) drives
        // the followers, and DungeonClearMultiplier additionally zeroes
        // FollowAction for every active-run member once the run is enabled —
        // this strip covers the pre-enable / paused window. A GM master sticks:
        // FindNewMaster() only re-resolves a null or bot master.
        Player* gm = FindGm();
        for (Slot const& slot : _slots)
            if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                {
                    if (gm)
                        botAI->SetMaster(gm);
                    botAI->ChangeStrategy("-follow", BOT_STATE_NON_COMBAT);
                    botAI->ChangeStrategy("-follow", BOT_STATE_COMBAT);
                }

        EnterStage(Stage::Teleporting);
        return;
    }

    if (_stageMs >= Scaled(GROUP_TIMEOUT_MS))
        FailSetup("group did not form");
}

void DcTestRunJob::UnbindFromMap() const
{
    // Guid-keyed and offline-safe, which is why the teardown copy can run after
    // the party has logged out.
    for (Slot const& slot : _slots)
    {
        if (!slot.guid)
            continue;
        sInstanceSaveMgr->PlayerUnbindInstance(slot.guid, _mapId, DUNGEON_DIFFICULTY_NORMAL,
                                               /*deleteFromDB*/ true);
        sInstanceSaveMgr->PlayerUnbindInstance(slot.guid, _mapId, DUNGEON_DIFFICULTY_HEROIC,
                                               /*deleteFromDB*/ true);
        // Raid saves are PERMANENT once a boss dies (non-resettable; "reset all
        // instances" skips raids), so one leaked bind poisons every later run
        // of the map for that character. Cover the two raid-only difficulty
        // values as well — raw 0/1 are already covered by the dungeon pair —
        // deleteFromDB both. No-ops for maps/difficulties with no bind.
        sInstanceSaveMgr->PlayerUnbindInstance(slot.guid, _mapId, RAID_DIFFICULTY_10MAN_HEROIC,
                                               /*deleteFromDB*/ true);
        sInstanceSaveMgr->PlayerUnbindInstance(slot.guid, _mapId, RAID_DIFFICULTY_25MAN_HEROIC,
                                               /*deleteFromDB*/ true);
    }
}

bool DcTestRunJob::CheckInstanceBudget()
{
    // Player::CheckInstanceCount is exactly the gate MapMgr::PlayerCannotEnter
    // applies, and reads the same in-memory per-account table the core loads at
    // login (account_instance_times) and prunes as entries expire — so asking the
    // logged-in character is both cheaper and more faithful than re-deriving the
    // count in SQL. Instance id 0 = "a brand-new instance", which is what an
    // unbound party is about to create.
    uint32 const perHour = sWorld->getIntConfig(CONFIG_MAX_INSTANCES_PER_HOUR);
    if (perHour == 0)
        return true;

    for (Slot const& slot : _slots)
    {
        Player* bot = ObjectAccessor::FindPlayer(slot.guid);
        if (!bot)
            continue;  // not resolvable yet; the stage timeout covers it

        // Mirror MapMgr::PlayerCannotEnter exactly, including its escape hatch:
        // a member still bound to an instance of this map is re-entering one it
        // has already paid for, so the count check passes on the found id. Every
        // run now unbinds before this point, so in practice the lookup misses and
        // the id is 0 ("a brand-new instance") — the hatch is kept rather than
        // hard-coding 0 so this stays a faithful mirror of the core's gate if a
        // bind ever does survive.
        uint32 idToCheck = 0;
        if (InstanceSave* save = sInstanceSaveMgr->PlayerGetInstanceSave(
                bot->GetGUID(), _mapId, bot->GetDifficulty(IsRaidMap())))
            idToCheck = save->GetInstanceId();

        if (bot->CheckInstanceCount(idToCheck))
            continue;

        FailSetup(Acore::StringFormat(
            "{} has entered {} instances in the last hour (AccountInstancesPerHour) — "
            "the core would refuse the teleport. Wait for a slot to free, use different "
            "characters, or raise AccountInstancesPerHour.",
            bot->GetName(), perHour));
        return false;
    }
    return true;
}

void DcTestRunJob::TickTeleporting()
{
    if (!_teleportIssued)
    {
        for (Slot const& slot : _slots)
        {
            Player* bot = ObjectAccessor::FindPlayer(slot.guid);
            if (!bot)
            {
                if (_stageMs >= Scaled(TELEPORT_TIMEOUT_MS))
                    FailSetup("bot vanished before teleport");
                return;  // transient — retry next tick
            }
        }

        // Nobody may enter this dungeon FROM a dungeon. Two separate core
        // behaviours make an instance-to-instance entry unsafe, and one trip out
        // to the bind point defuses both:
        //
        //   * A same-map TeleportTo is a NEAR teleport (Player::TeleportTo keys
        //     the branch on map id alone). It moves the body but never
        //     re-resolves which COPY the body is in, so a bot left inside a
        //     stale instance of this very dungeon — which is exactly what a
        //     worldserver restart mid-plan leaves behind — would "arrive"
        //     without ever leaving that copy, dragging its cleared bosses into
        //     our verdict.
        //
        //   * `m_InstanceValid` is cleared by Group::_homebindIfInstance for any
        //     member pulled out of a group while standing in a dungeon — which
        //     TickGrouping's pre-formation sweep does to every recycled pool bot
        //     that logged in inside one. The ONLY place that ever sets it back is
        //     HandleMoveWorldportAck, and only when the DESTINATION is not
        //     instanced ("except if going to an instance inside an instance").
        //     Carry it into the run and Player::UpdateHomebindTime repops the
        //     member at the entrance graveyard exactly 60s later — the whole
        //     party ends up outside, the DC strategy gate sees a bot no longer in
        //     a dungeon, and the run dies as "Left the dungeon" barely a minute
        //     in.
        bool evicting = false;
        for (Slot const& slot : _slots)
            if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
                if (Map const* map = bot->FindMap())
                    if (map->IsDungeon())
                    {
                        evicting = true;
                        if (!bot->IsBeingTeleported())
                            bot->TeleportTo(bot->m_homebindMapId, bot->m_homebindX,
                                            bot->m_homebindY, bot->m_homebindZ,
                                            bot->GetOrientation());
                    }
        if (evicting)
        {
            if (_stageMs >= Scaled(TELEPORT_TIMEOUT_MS))
                FailSetup("could not clear the party out of the dungeon it was already in");
            return;  // retry next tick
        }

        // Every member sheds every bind for this map, both difficulties, on
        // every run. A bind is what decides WHICH COPY of the map a teleport
        // lands in (InstanceSaveMgr::PlayerGetDestinationInstanceId), so a
        // leftover one drags the party into an instance somebody already
        // cleared: the verdict's GetCompletedEncounterMask baseline starts with
        // bosses dead and TickStarting refuses the run as stale.
        //
        // This used to be a roster-only sweep, with pool runs shedding only the
        // permanent heroic saves on the theory that normal 5-man saves are
        // non-permanent and reset themselves when the map empties. They do —
        // eventually, and only if the map ever empties. A worldserver restart
        // in the middle of a plan leaves the interrupted runs' rows sitting in
        // character_instance, and the next plan's recycled pool bots walk
        // straight back into those half-cleared copies. Unbinding
        // unconditionally is a few guid-keyed deletes and removes the whole
        // class of failure.
        //
        // The cost is real but small: re-entering a bound instance is free
        // against AccountInstancesPerHour, and a fresh one is not, so a bot
        // recycled through more runs than that cap in an hour will now be
        // refused by name in CheckInstanceBudget rather than silently reusing a
        // copy. The pool is far larger than any one plan, so that is a
        // theoretical cost against a defect we have actually been paying.
        UnbindFromMap();

        // Now that no member is bound, entering costs each account one of its
        // AccountInstancesPerHour slots — refuse by name here rather than let the
        // core silently abort the transfer and time this stage out.
        if (!CheckInstanceBudget())
            return;

        // Attunement, not permission. Nothing in the pool has played the chain
        // that gates a dungeon on a quest, so the two Frozen Halls sequels
        // (Pit of Saron on "Echoes of Tortured Souls", Halls of Reflection on
        // "Deliverance from the Pit") and every TBC heroic keyed to an
        // achievement refused their tank's teleport outright — silently, with
        // the run dying 68s later as "tank did not arrive at the dungeon
        // entrance" and the party still standing where it was provisioned. The
        // same silent refusal pins a recycled death knight to Acherus.
        //
        // Grant every member what the destination actually asks for rather
        // than teleporting with TELE_TO_GM_MODE: the level bound, the
        // instance-per-hour budget, a disabled map and a copy mid-reset all
        // stay in force, and only the attunement is handed over.
        for (Slot const& slot : _slots)
            if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
                DcDungeonAccess::GrantEntry(bot, _mapId, bot->GetDifficulty(IsRaidMap()));

        // LEADER FIRST, ALONE. The destination copy is resolved per member at
        // worldport-ack time from the GROUP LEADER's bind; with nobody bound
        // that lookup returns 0, which means "mint a brand-new instance". Fire
        // all five teleports at once and every member that resolves before the
        // first one has actually been added to a map mints a copy of its own —
        // we have watched ten parties scatter across twenty-four copies of the
        // same map in a nine-second window, each tank alone or nearly so, the
        // run then stalling forever on teammates it can never reach.
        //
        // Sending the tank by itself closes the window: once it is inside, its
        // bind names the copy, and every later arrival resolves to that same id
        // no matter how the acks interleave.
        Player* const leader = FindTank();
        if (!leader)
        {
            if (_stageMs >= Scaled(TELEPORT_TIMEOUT_MS))
                FailSetup("tank vanished before teleport");
            return;
        }
        leader->TeleportTo(_mapId, _x, _y, _z, _o);
        _teleportIssued = true;
    }

    // Wave 1 — wait for the leader to be standing in a copy of its own.
    Player* const tank = FindTank();
    if (!tank || tank->GetMapId() != _mapId || !tank->IsInWorld() ||
        tank->IsBeingTeleported() || !tank->GetInstanceId())
    {
        if (_stageMs >= Scaled(TELEPORT_TIMEOUT_MS))
            FailSetup("tank did not arrive at the dungeon entrance");
        return;
    }

    // Wave 2 — the rest of the party, now that there is a copy to join.
    if (!_followersTeleported)
    {
        _destInstanceId = tank->GetInstanceId();
        for (std::size_t i = 1; i < _slots.size(); ++i)
            if (Player* bot = ObjectAccessor::FindPlayer(_slots[i].guid))
                bot->TeleportTo(_mapId, _x, _y, _z, _o);
        _followersTeleported = true;
    }

    // Arrival is the INSTANCE id, not the map id. Two members of one party can
    // both be "on map 601" and never see each other, and the distance helpers
    // do not check the instance either — a member in the wrong copy reports a
    // plausible 30-yard distToTank and the run reads as a party that is merely
    // lagging. Refusing here turns a twenty-minute run that could never have
    // progressed into a setup failure that names the members and their copies.
    std::string stranded;
    bool allThere = true;
    for (Slot const& slot : _slots)
    {
        Player* bot = ObjectAccessor::FindPlayer(slot.guid);
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
        {
            allThere = false;
            continue;
        }
        if (bot->GetMapId() != _mapId || bot->GetInstanceId() != _destInstanceId)
        {
            allThere = false;
            if (!stranded.empty())
                stranded += ", ";
            stranded += Acore::StringFormat("{} (map {} instance {})", bot->GetName(),
                                            bot->GetMapId(), bot->GetInstanceId());
        }
    }

    if (allThere)
    {
        // A Lab party never starts a clear: the driver stages it from here.
        EnterStage(_lab ? Stage::Lab : Stage::Starting);
        return;
    }

    if (_stageMs >= Scaled(TELEPORT_TIMEOUT_MS))
    {
        if (stranded.empty())
            FailSetup("party did not arrive at the dungeon entrance");
        else
            FailSetup(Acore::StringFormat(
                "party split across instance copies — the tank is in instance {} but {}",
                _destInstanceId, stranded));
    }
}

void DcTestRunJob::TickStarting()
{
    Player* tank = FindTank();
    PlayerbotAI* tankAI = tank ? GET_PLAYERBOT_AI(tank) : nullptr;
    if (!tank || !tankAI)
    {
        if (_stageMs >= START_TIMEOUT_MS)
            FailSetup("tank vanished before start");
        return;
    }

    // Freshly-teleported bots may not have the DC strategies installed yet
    // (contexts register on world ticks) — re-assert every tick; idempotent.
    for (Slot const& slot : _slots)
        if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
            DcStrategyGate::Reconcile(bot);

    AiObjectContext* ctx = tankAI->GetAiObjectContext();

    // Run wing first (idempotent; re-asserted each tick until `dc on` takes): it
    // decides which boss list the roster read below — and the whole run — sees.
    // Explicit beats the fallback a bot may already have computed at the portal.
    if (!_runWing.empty())
        if (DungeonWingLayout const* layout = DungeonWingRegistry::Get(_mapId))
            if (DungeonWing const* wing = DungeonWingRegistry::FindWing(*layout, _runWing))
                DcRunWing::Set(tank, *wing, DcRunWing::Source::Explicit);

    // A reused instance with dead bosses would flash an instant (false)
    // all-clear — refuse it rather than record a fake success. A wing-scoped run
    // only cares about its own wing's bits.
    if (InstanceScript* inst = DcTargeting::GetInstanceScript(tank))
        if (uint32 const done = inst->GetCompletedEncounterMask() & (_wingMask ? _wingMask : ~0u))
        {
            FailSetup("stale instance: encounters already completed (mask " +
                      std::to_string(done) + ")");
            return;
        }

    // The boss roster needs a tick or two to populate after the teleport;
    // retry inside the stage timeout.
    std::vector<DungeonBossInfo> const bosses =
        ctx->GetValue<std::vector<DungeonBossInfo>>(DcKey::DungeonBosses)->Get();
    if (bosses.empty())
    {
        if (_stageMs >= START_TIMEOUT_MS)
            FailSetup("no boss roster for this map");
        return;
    }

    if (!_dcOnIssued)
    {
        _record.instanceId = tank->GetMap()->GetInstanceId();
        _roster.clear();
        for (DungeonBossInfo const& b : bosses)
        {
            BossRef ref;
            ref.entry = b.entry;
            ref.encounterIndex = b.encounterIndex;
            ref.name = b.name;
            ref.isBoss = b.kind == DungeonAnchorKind::Boss;
            _roster.push_back(ref);
            // Objectives (anchor kinds) carry no name worth aggregating across a
            // plan — only real bosses go into the reported roster.
            if (ref.isBoss)
                _record.bossRoster.push_back(ref.name);
        }
        _record.bossesTotal = static_cast<uint32>(_roster.size());

        // SCENARIO: the run is scoped to its focus. Every focus entry must be
        // on this map's live roster — the registry gtest can only check the
        // row's shape, not the roster BossSpawnIndex derives at runtime — and a
        // focus the roster lacks would otherwise skip EVERYTHING and flash an
        // instant all-cleared. Then the reported roster/total count the focus
        // only, so a scenario's "1/1" means its objective, not 1/11 of the map.
        if (_isScenario)
        {
            std::vector<uint32> rosterEntries;
            for (BossRef const& ref : _roster)
                rosterEntries.push_back(ref.entry);
            std::vector<DcTestDungeonRegistry::Row> const& rows = DcTestDungeonRegistry::All();
            if (DcTestDungeonRegistry::Row const* row = DcTestDungeonRegistry::Find(_dungeonToken, rows))
            {
                std::string const bad =
                    DcTestDungeonRegistry::ValidateScenario(*row, rows, &rosterEntries);
                if (!bad.empty())
                {
                    FailSetup("scenario '" + _dungeonToken + "': " + bad);
                    return;
                }
            }
            // A focus the roster marks skip-by-design (Karazhan's Chess and
            // Prince are, until their events land) is never selected, so with
            // everything else skipped the run would read ALL-CLEARED on its
            // first tick — a fake success. Refuse it instead.
            bool anyRunnable = false;
            for (DungeonBossInfo const& b : bosses)
                if (!b.skipByDesign &&
                    std::find(_focus.begin(), _focus.end(), b.entry) != _focus.end())
                    anyRunnable = true;
            if (!anyRunnable)
            {
                FailSetup("scenario '" + _dungeonToken +
                          "': every focus entry is skip-by-design on this map's roster — "
                          "nothing to run");
                return;
            }
            _record.bossRoster.clear();
            uint32 focused = 0;
            for (BossRef const& ref : _roster)
                if (std::find(_focus.begin(), _focus.end(), ref.entry) != _focus.end())
                {
                    ++focused;
                    // Focus objectives are named too: a scenario's roster IS its
                    // objective list, even when that objective is not a boss.
                    _record.bossRoster.push_back(ref.name);
                }
            _record.bossesTotal = focused;
        }

        // The event-telemetry block on the leader survives DcRunState::Reset()
        // (see DcRunState) — so it has to be cleared HERE, before `dc on`, or a
        // previous run's extras and progress count would leak into this one.
        DcRun::Of(ctx).ClearTestTelemetry();

        // RAID runs lean on the playerbots raid strategies for the boss fights
        // (DC stands down during encounters), and those attach by mapId only
        // when Playerbots.ApplyInstanceStrategies is on. A raid run with the
        // knob off would test nothing but bots auto-attacking a raid boss —
        // fail loudly at start instead of 40 minutes later.
        if (IsRaidMap() && !DC_PB_CONFIG(ApplyInstanceStrategies, applyInstanceStrategies))
        {
            FailSetup("Playerbots.ApplyInstanceStrategies is off — raid runs need the "
                      "playerbots raid strategies to fight bosses; enable it and retry");
            return;
        }

        // The run must never wait for a human: kill the WaitAtBoss pre-pull
        // hold for this run whatever the conf says.
        DcSettings::SetOverride(_tankGuid, "WaitAtBoss", 0.0);
        _dcOnIssued = true;
    }

    // Retry `dc on` each tick until the enabled flag sticks (roster/context
    // timing) or the stage times out.
    tankAI->DoSpecificAction("dc on", Event("dc", "", FindGm()), true);
    if (DcRun::Of(ctx).enabled)
    {
        if (_isScenario)
        {
            // Take the run-instance transition NOW, before filling Skipped. It
            // wipes Skipped, and left to itself it fires on the first
            // NextDungeonBoss evaluation — i.e. just AFTER this fill, which would
            // point the raid at the map's first boss until the next monitor tick
            // re-asserted the scope. Taking it here (idempotent: it only acts on
            // an instance change) makes the fill stick from the first AI tick.
            DcTargeting::ResetCompletionLatchesForNewInstance(tank, ctx);
            ApplyScenarioSkips(tank, ctx);
            LOG_INFO("playerbots.dungeonclear",
                     "TESTRUN {} scenario {} (of {}): focus {} of {} roster entries, success {}{}",
                     _record.runId, _dungeonToken, _record.scenarioOf, _focus.size(),
                     _roster.size(),
                     _record.successPredicate.empty() ? "all-cleared" : _record.successPredicate,
                     _success.IsSet() ? " + " + std::to_string(_successGraceMs / 1000) + "s grace"
                                      : std::string());
        }
        if (InstanceScript* inst = DcTargeting::GetInstanceScript(tank))
            _lastMask = inst->GetCompletedEncounterMask();
        _lastAnchors =
            ctx->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get().size();
        LOG_INFO("playerbots.dungeonclear",
                 "TESTRUN {} running: instance {} with {} bosses/objectives",
                 _record.runId, _record.instanceId, _record.bossesTotal);
        // Nobody in this party has a game client, so nobody will ever send a
        // CMSG_AREATRIGGER and no `at_*` script would run for the whole clear.
        // Armed here rather than at Teleporting so it covers exactly the window
        // the party is actually walking the dungeon.
        _areaTriggers.Arm(_mapId);
        ArmTrace(tank, ctx);
        EnterStage(Stage::Monitoring);
        return;
    }

    if (_stageMs >= START_TIMEOUT_MS)
        FailSetup("dc on did not take (look for 'DC command refused' in the DC log)");
}

void DcTestRunJob::ApplyScenarioSkips(Player* tank, AiObjectContext* ctx)
{
    if (!_isScenario || !tank || !ctx)
        return;
    std::unordered_set<uint32>& skipped =
        ctx->GetValue<std::unordered_set<uint32>&>(DcKey::Skipped)->Get();
    for (BossRef const& ref : _roster)
        if (std::find(_focus.begin(), _focus.end(), ref.entry) == _focus.end())
            skipped.insert(ref.entry);
}

void DcTestRunJob::CaptureExtras(Player* tank)
{
    PlayerbotAI* tankAI = tank ? GET_PLAYERBOT_AI(tank) : nullptr;
    if (!tankAI)
        return;
    std::vector<DcTestExtra> const& live = DcRun::Of(tankAI->GetAiObjectContext()).testExtras;
    _record.extras.clear();
    _record.extras.reserve(live.size());
    for (DcTestExtra const& e : live)
        _record.extras.push_back({e.key, e.value, e.numeric});
}

// File a death for every member who went from standing to a corpse since the
// last sample, stamped with what the party was fighting at the time.
//
// MUST run before TrackEngagement folds this tick's sample: `_engaged` still
// holds the picture from the last tick in which the victim was alive, which is
// exactly the mob to blame. Folding first would attribute the death to whatever
// survives the fold — nothing at all, once the survivors drop combat.
//
// Only members on the leader's map are watched. A bot who left the instance (or
// logged out) is not a casualty, and its absence must not be read as a death.
void DcTestRunJob::TrackDeaths(Player* tank)
{
    if (!tank)
        return;

    auto observe = [this](Player* member)
    {
        if (!member || !member->IsInWorld())
            return;
        bool const alive = member->IsAlive();
        auto const [it, fresh] = _aliveLast.emplace(member->GetGUID(), alive);
        if (fresh)
            return;  // seeding this member — no edge to report yet

        bool const wasAlive = it->second;
        it->second = alive;
        if (wasAlive == alive || alive)
            return;

        DcTestRunRecord::DeathEntry death;
        death.t = _totalMs / 1000;
        death.name = member->GetName();
        death.opponent = _engaged.name;
        death.opponentEntry = _engaged.entry;
        death.onBoss = _engaged.isBoss;
        _record.deaths.push_back(death);
        // Deliberately overwritten even when the latch is empty: this means
        // "what the LAST death was to", not "the last death that had a killer".
        // A member who dropped combat and then fell off a ledge must not be
        // filed against the boss the party disengaged from ten minutes earlier.
        _lastDeathEngaged = _engaged;
    };

    auto onTankMap = [tank](Player* member)
    { return member && member->IsInWorld() && member->GetMapId() == tank->GetMapId(); };

    if (onTankMap(tank))
        observe(tank);
    if (Group* group = tank->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member != tank && onTankMap(member))
                observe(member);
}

// Who to blame for a run people died in. DcTestRun::BlameFor owns the rule.
DcTestRun::Engagement DcTestRunJob::DeathBlame() const
{
    return DcTestRun::BlameFor(_engaged, _lastDeathEngaged);
}

// Gather this sample's combat picture off the party and fold it into the
// engagement latch (DcTestRun::UpdateEngagement owns the rule).
//
// Only members that are alive and on the leader's map are read: a corpse holds
// neither a victim nor an attacker list, so a party being wiped simply stops
// contributing and the last engagement seen while somebody was standing is what
// the wipe verdict reports.
//
// The run's own roster is the authority on what counts as a boss — it is what
// every other line of the report names bosses by — and the creature-side flags
// are the fallback for a summoned or phase-swapped boss the roster never listed
// (Anzu, a Tuten'kash-style event spawn).
void DcTestRunJob::TrackEngagement(Player* tank)
{
    if (!tank)
        return;

    DcTestRun::EngagementSample sample;

    auto rosterName = [this](uint32 entry) -> std::string const*
    {
        for (BossRef const& ref : _roster)
            if (ref.entry == entry && ref.isBoss)
                return &ref.name;
        return nullptr;
    };

    auto consider = [&](Unit const* u)
    {
        if (!u || !u->IsAlive())
            return;
        Creature const* creature = u->ToCreature();
        if (!creature)
            return;
        std::string const* named = rosterName(creature->GetEntry());
        if (sample.bossName.empty() &&
            (named || creature->IsDungeonBoss() || creature->isWorldBoss()))
        {
            sample.bossEntry = creature->GetEntry();
            sample.bossName = named ? *named : creature->GetName();
        }
        if (sample.trashName.empty())
        {
            sample.trashEntry = creature->GetEntry();
            sample.trashName = creature->GetName();
        }
    };

    auto scan = [&](Player* member)
    {
        if (!member || !member->IsAlive() || !member->IsInWorld() ||
            member->GetMapId() != tank->GetMapId())
            return;
        sample.anyAlive = true;
        if (!member->IsInCombat())
            return;
        sample.anyAliveInCombat = true;
        // The victim covers whoever the member is swinging at; the attacker set
        // covers the mob beating on a member with no target of its own (a
        // healer, or anyone the pack peeled onto).
        consider(member->GetVictim());
        for (Unit const* attacker : member->getAttackers())
            consider(attacker);
    };

    scan(tank);
    if (Group* group = tank->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member && member != tank)
                scan(member);

    _engaged = DcTestRun::UpdateEngagement(_engaged, sample);
}

// File the pull observation currently in flight, if any. `wipedHere` marks the
// pull the run died on — the single most useful row in the log, since it names
// the pack that ended the run alongside what the governor thought it was.
void DcTestRunJob::ClosePull(bool wipedHere)
{
    if (!_pullOpen)
        return;
    _pullOpen = false;
    _pullEntry.wipedHere = wipedHere;
    if (_record.pulls.size() < DcTestRunRecord::kPullLog)
        _record.pulls.push_back(_pullEntry);
    else
        ++_record.pullsElided;
}

// Pair each Dynamic-pull verdict with the number of mobs that actually turned
// up for it. See DcTestRunRecord::PullEntry for why the pairing (not either
// number alone) is what diagnoses an over-pulling tank.
//
// The observation runs on the leader's DcPullContext:
//
//   decisionSeq changed  -> a NEW pack was latched: close the old record, open
//                           one stamped with this verdict's prediction.
//   decision == None     -> the governor dropped its verdict (pack dead, target
//                           lost past the grace); once the party is also out of
//                           combat the fight is over and the record closes.
//
// The observed count is the union, over every alive on-map member, of what it is
// swinging at and what is swinging at it — the same reach TrackEngagement uses,
// widened from "name one opponent" to "count them all". Sampled at MONITOR_STEP_MS,
// so it is a floor on what was fought: a mob that joined and died inside one
// second never appears.
void DcTestRunJob::TrackPulls(Player* tank)
{
    if (!tank)
        return;
    PlayerbotAI* ai = GET_PLAYERBOT_AI(tank);
    AiObjectContext* ctx = ai ? ai->GetAiObjectContext() : nullptr;
    if (!ctx)
        return;

    DcPullContext const& pull = ctx->GetValue<DcPullContext&>(DcKey::PullContext)->Get();

    if (pull.decisionSeq != _pullSeq)
    {
        ClosePull();
        _pullSeq = pull.decisionSeq;
        _pullEntry = DcTestRunRecord::PullEntry{};
        _pullEntry.t = _totalMs / 1000;
        _pullEntry.targetEntry = pull.decisionTargetEntry;
        _pullOpen = true;
    }

    if (!_pullOpen)
        return;

    // Refresh the prediction every sample while the record is open: the governor
    // re-checks a standing Leeroy on a throttle and may UPGRADE it to Advanced,
    // which re-estimates the pack. What we want on file is the estimate the
    // COMMITTED verdict was taken from, i.e. the last one before the pull ends.
    _pullEntry.predictedCount = pull.predictedCount;
    _pullEntry.predictedThirds = pull.predictedThirds;
    _pullEntry.ceilingThirds = pull.predictedCeiling;
    if (pull.decision == DcPullDecisionCode::Advanced)
        _pullEntry.advanced = true;

    std::set<ObjectGuid> engaged;
    std::uint32_t elites = 0;
    auto consider = [&](Unit const* u)
    {
        if (!u || !u->IsAlive())
            return;
        Creature const* c = u->ToCreature();
        if (!c || c->IsCritter() || c->IsTotem())
            return;
        if (engaged.insert(c->GetGUID()).second && c->isElite())
            ++elites;
    };
    auto scan = [&](Player* member)
    {
        if (!member || !member->IsAlive() || !member->IsInWorld() ||
            member->GetMapId() != tank->GetMapId() || !member->IsInCombat())
            return;
        consider(member->GetVictim());
        for (Unit const* attacker : member->getAttackers())
            consider(attacker);
    };
    scan(tank);
    if (Group* group = tank->GetGroup())
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member && member != tank)
                scan(member);

    if (engaged.size() > _pullEntry.observedMax)
    {
        _pullEntry.observedMax = static_cast<std::uint32_t>(engaged.size());
        _pullEntry.observedElites = elites;
    }

    // Verdict dropped AND the fight resolved: this pull is history. Holding the
    // record open until combat clears is what keeps a Leeroy on file — its
    // verdict drops the moment the pack dies, which is also when the fight ends.
    if (pull.decision == DcPullDecisionCode::None && engaged.empty())
        ClosePull();
}

// Is anyone in the party on the leader's map a corpse right now? Separates a
// run that ended because people died (worth the wipe post-mortem) from one that
// ended for any other reason.
bool DcTestRunJob::AnyMemberDead(Player* tank)
{
    if (!tank)
        return false;
    if (!tank->IsAlive())
        return true;
    Group* group = tank->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || member == tank || member->GetMapId() != tank->GetMapId())
            continue;
        if (!member->IsAlive())
            return true;
    }
    return false;
}

void DcTestRunJob::TickMonitoring(uint32 dt)
{
    DcTestRun::Observation obs;
    obs.elapsedMs = _monitorMs;
    obs.gmOnline = true;  // GM absence is handled in Tick()

    {
        std::lock_guard<std::mutex> lock(_obsMutex);
        obs.abortRequested = _abortRequested;
        obs.disableFired = _disableFired;
        obs.disableAllCleared = _disableAllCleared;
    }

    // Connected-lookup + explicit teleport tolerance: DC's own scripted
    // events teleport the party mid-run (Underbog hops, Old Hillsbrad's
    // muster) — a sample landing in that window must wait, not read the
    // leader as gone.
    Player* tank = ObjectAccessor::FindConnectedPlayer(_tankGuid);
    PlayerbotAI* tankAI = tank ? GET_PLAYERBOT_AI(tank) : nullptr;
    std::string pauseReason;
    std::string stallReason;

    if (tank && tankAI && (!tank->IsInWorld() || tank->IsBeingTeleported()))
        return;  // mid-teleport — skip this sample, timers resume next tick

    // Keep the react-delay fast path alive for the WHOLE run, not just from
    // Grouping (see ReassertMaster — the leader was observed losing its master and
    // dropping to a 1-3 second think interval for the entire clear).
    ReassertMaster();

    // A roster member vanishing from the world mid-run means its owner logged in:
    // playerbots' secure-login hook force-logs-out an active altbot when a real
    // CMSG_PLAYER_LOGIN arrives for it, so the human cleanly takes their character
    // back. Name that outcome. Without this the run limps on a member short and
    // eventually files a no-progress or wipe failure — a real regression report
    // for something that was never the AI's doing. Session-based lookup, so DC's
    // own mid-run teleports (handled above) cannot trip it.
    if (_realChars)
    {
        for (Slot const& slot : _slots)
        {
            if (!slot.guid || ObjectAccessor::FindConnectedPlayer(slot.guid))
                continue;
            Finish(DcTestRun::Verdict::FailAborted,
                   "roster member " + slot.rosterName +
                       " left the run (owner logged in, or the character was logged out)");
            return;
        }
    }

    if (!tank || !tankAI)
    {
        obs.leaderMissing = true;
    }
    else
    {
        AiObjectContext* ctx = tankAI->GetAiObjectContext();
        DcRunState const& rs = DcRun::Of(ctx);

        // Scenario scope + success. The skip fill is re-asserted every tick (see
        // ApplyScenarioSkips); the predicate is read straight off the instance
        // and latched by the pure grace clock, whose verdict the kernel folds in.
        if (_isScenario)
        {
            ApplyScenarioSkips(tank, ctx);
            // No predicate = all-cleared is the only success, as on any row.
            if (_success.IsSet())
            {
                bool const wasHeld = _grace.held;
                bool holds = wasHeld;  // latched: no need to re-read once met
                if (!wasHeld)
                    if (InstanceScript* inst = DcTargeting::GetInstanceScript(tank))
                        holds = DcTestDungeonRegistry::PredicateHolds(
                            _success, inst->GetData(_success.dataId));
                _grace.Step(holds, dt, _totalMs);
                if (!wasHeld && _grace.held)
                    LOG_INFO("playerbots.dungeonclear",
                             "TESTRUN {} scenario success predicate {} holds at {}s — {}s grace "
                             "for the event's tail",
                             _record.runId, _record.successPredicate, _totalMs / 1000,
                             _successGraceMs / 1000);
                obs.scenarioSuccess = _grace.held;
                obs.graceExpired = _grace.Expired(_successGraceMs);
            }
        }
        CaptureExtras(tank);

        // The disable funnel notifies OnRunDisabled; this catches any path
        // that somehow bypassed it (belt and braces — enabled off without a
        // callback still ends the run).
        if (!obs.disableFired && !rs.enabled)
        {
            obs.disableFired = true;
            std::lock_guard<std::mutex> lock(_obsMutex);
            if (_disableReason.empty())
                _disableReason = "run disabled (no reason captured)";
        }

        // The anchor the run is currently heading for, for the closing-distance
        // signal below.
        std::optional<DungeonBossInfo> const next =
            ctx->GetValue<std::optional<DungeonBossInfo>>(DcKey::NextDungeonBoss)->Get();

        uint32 mask = _lastMask;
        if (InstanceScript* inst = DcTargeting::GetInstanceScript(tank))
            mask = inst->GetCompletedEncounterMask();
        std::size_t const anchors =
            ctx->GetValue<std::unordered_set<uint32>&>(DcKey::ClearedAnchors)->Get().size();

        bool progressed = false;
        // A wing-scoped run counts its own wing's bits only: the other wing's
        // kills (a UBRS pack pulled from the shared hall) are not its progress.
        if (uint32 const fresh = mask & ~_lastMask & (_wingMask ? _wingMask : ~0u))
        {
            for (uint32 bit = 0; bit < 32; ++bit)
            {
                if (!(fresh & (1u << bit)))
                    continue;
                DcTestRunRecord::BossKill kill;
                kill.t = _totalMs / 1000;
                kill.via = "mask";
                // Prefer the boss: an objective sharing the bit (the Iron Clad
                // Door on Mr. Smite's 3) sorts first by the objective-before-boss
                // tie-break and would otherwise take the boss's kill. A bit that
                // only an objective owns (Stratholme's Dathrohan 6, Slaughterhouse
                // 11) keeps the objective's name.
                for (BossRef const& ref : _roster)
                    if (ref.encounterIndex == bit && (ref.isBoss || kill.name.empty()))
                    {
                        kill.entry = ref.entry;
                        kill.name = ref.name;
                        if (ref.isBoss)
                            break;
                    }      
                if (kill.name.empty())
                    kill.name = "encounter #" + std::to_string(bit);
                _record.bossTimeline.push_back(kill);
                progressed = true;
            }
        }
        _lastMask = mask;
        if (anchors > _lastAnchors)
        {
            for (std::size_t i = _lastAnchors; i < anchors; ++i)
            {
                DcTestRunRecord::BossKill kill;
                kill.t = _totalMs / 1000;
                kill.name = "objective";
                kill.via = "anchor";
                _record.bossTimeline.push_back(kill);
            }
            _lastAnchors = anchors;
            progressed = true;
        }
        if (progressed)
            _record.bossesKilled =
                std::min<uint32>(static_cast<uint32>(_record.bossTimeline.size()),
                                 _record.bossesTotal);

        // A kill is not the only evidence a run is alive. Keyed on kills alone
        // the watchdog fails runs that are working perfectly: a party can
        // legitimately spend more than NoProgressS on the trash between two
        // bosses — or on the way to the first one, where the clock starts at
        // zero and nothing has been killed yet.
        //
        // What counts is that something CHANGES, never that some state holds.
        // "In combat" held true is the signature of the phantom-combat
        // ghost-flag deadlock, so counting combat itself as progress would make
        // that bug undetectable by construction — the run would sit in a dead
        // fight forever and the watchdog would call it healthy the whole time.
        //
        // So compare this tick's combat picture against last tick's:
        //
        //   entering or leaving combat — a pack pulled, or a pack died.
        //   a different victim         — one mob down, on to the next.
        //   the victim's health moved  — the fight itself is progressing,
        //                                which is what carries a long single
        //                                target where nothing else changes.
        //
        // A real fight moves at least one of those every sample. A wedged one
        // moves none: stuck flag, no victim or a stale one, health frozen.
        bool const inCombat = tank->IsInCombat();
        Unit const* victim = tank->GetVictim();
        ObjectGuid const victimGuid = victim ? victim->GetGUID() : ObjectGuid::Empty;
        uint32 const victimHpPct =
            victim ? static_cast<uint32>(victim->GetHealthPct()) : 0u;

        if (inCombat != _lastInCombat || victimGuid != _lastVictim ||
            victimHpPct != _lastVictimHpPct)
            progressed = true;

        _lastInCombat = inCombat;
        _lastVictim = victimGuid;
        _lastVictimHpPct = victimHpPct;

        // Sampled every monitor step while somebody is still standing, so the
        // wipe verdict below can name what took the party down. Deaths are read
        // first, against the previous tick's latch — see TrackDeaths.
        TrackDeaths(tank);
        TrackEngagement(tank);
        TrackPulls(tank);

        if (next.has_value() && next->mapId == tank->GetMapId())
        {
            // Re-arm on target change: distance to a NEW anchor is unrelated to
            // the best held for the old one, and would otherwise read as an
            // instant regression that never recovers.
            if (next->entry != _bestDistEntry)
            {
                _bestDistEntry = next->entry;
                _bestDist = -1.f;
            }
            float const dist = tank->GetDistance(next->x, next->y, next->z);
            if (_bestDist < 0.f || dist < _bestDist - DC_TESTRUN_PROGRESS_EPSILON_YD)
            {
                _bestDist = dist;
                progressed = true;
            }
        }

        // The event progress sequence (DcRunState::eventProgressSeq) is the one
        // signal the harness cannot derive itself: a long scripted event (chess)
        // bumps it on its own headway, and a change counts like a kill.
        if (_progressClock.Step(dt, progressed, rs.eventProgressSeq))
            _frozenDumpLogged = false;
        _sinceProgressMs = _progressClock.sinceMs;

        // THE FREEZE DUMP. Fired once, halfway into the no-progress window, on a
        // run that is going to fail its watchdog unless something changes — and
        // fired HERE rather than at teardown because the state that explains a
        // stuck-in-combat freeze does not survive to teardown: the units holding
        // the party evade, leash home or despawn in the minutes between, and the
        // record ends up naming nothing. Halfway is the compromise: late enough
        // that a slow pull or a long rez is not reported as a wedge, early enough
        // that the holders are still standing where they wedged it.
        //
        // Read-only (DcDiag::Capture is). Deliberately NOT gated on the tank's
        // own combat flag: in both MGT freezes this was written for, the members
        // held in combat were the tank AND one follower, and a variant where the
        // tank is clean while a follower is pinned wedges the run just as hard —
        // gating on `inCombat` here would report nothing on exactly that case.
        // The combat line is what stays conditional, so a run frozen on
        // navigation does not carry a blame line with nothing to blame.
        if (!_frozenDumpLogged && _limits.noProgressMs &&
            _sinceProgressMs >= _limits.noProgressMs / 2)
        {
            _frozenDumpLogged = true;
            DcDiag::Snapshot const frozen = DcDiag::Capture(tank, "frozen");
            LOG_INFO("playerbots.dungeonclear",
                     "TESTRUN FROZEN {} no progress for {}s (limit {}s) — {}",
                     _record.runId, _sinceProgressMs / 1000,
                     _limits.noProgressMs / 1000, DcDiag::Summarize(frozen));
            if (frozen.inCombatCount)
                LOG_INFO("playerbots.dungeonclear", "TESTRUN FROZEN {} combat blame: {}",
                         _record.runId, DcDiag::SummarizeCombat(frozen));
        }

        // Pause / stall trackers.
        if (rs.paused)
        {
            pauseReason = rs.pauseReason;
            if (!_wasPaused)
            {
                _record.pauses.push_back({_totalMs / 1000, pauseReason});
                _pausedForMs = 0;
            }
            else
                _pausedForMs += dt;
            _wasPaused = true;
        }
        else
        {
            _wasPaused = false;
            _pausedForMs = 0;
        }
        obs.paused = rs.paused;
        obs.pausedForMs = _pausedForMs;

        // Wipe tracker. The in-run death bailout (DungeonClearPartyDiedTrigger)
        // covers every case where SOMEONE is left alive to notice — it fires and
        // the disable funnel above ends the run with the death reason. It cannot
        // cover a full wipe: the trigger lives in the `dungeon clear` strategy,
        // which is on the non-combat and combat engines only, and a corpse runs
        // BOT_STATE_DEAD. So the harness has to see this one for itself.
        //
        // Dead = dead OR ghost (a released bot is still a wiped bot). Members off
        // the leader's map are ignored, matching the trigger's own convention:
        // someone alive back in town is not a party that is still fighting.
        bool anyAlive = tank->IsAlive();
        if (anyAlive)
            _lastAliveMember = tank->GetName();
        else if (Group* group = tank->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || member == tank || member->GetMapId() != tank->GetMapId())
                    continue;
                if (member->IsAlive())
                {
                    anyAlive = true;
                    _lastAliveMember = member->GetName();
                    break;
                }
            }
        }

        if (anyAlive)
            _wipedForMs = 0;
        else
            _wipedForMs += dt;
        obs.partyWiped = !anyAlive;
        obs.wipedForMs = _wipedForMs;

        stallReason = ctx->GetValue<std::string&>(DcKey::StallReason)->Get();
        if (!stallReason.empty())
            _stalledForMs += dt;
        else
            _stalledForMs = 0;
        obs.stalled = !stallReason.empty();
        obs.stalledForMs = _stalledForMs;

        obs.sinceProgressMs = _sinceProgressMs;
    }

    DcTestRun::Verdict const verdict = DcTestRun::Classify(obs, _limits);
    if (!DcTestRun::IsTerminal(verdict))
        return;

    if (DcTestRun::IsSuccess(verdict))
    {
        _record.successBy = DcTestRun::SuccessBy(obs, verdict);
        _record.tailPending = _record.successBy != "allCleared";
        if (_isScenario)
            LOG_INFO("playerbots.dungeonclear", "TESTRUN {} scenario success by {}{}",
                     _record.runId, _record.successBy,
                     _record.tailPending ? " (event tail still pending)" : "");
    }

    std::string failReason;
    switch (verdict)
    {
        case DcTestRun::Verdict::Success:
            break;
        case DcTestRun::Verdict::FailDisabled:
        {
            std::string reason;
            {
                std::lock_guard<std::mutex> lock(_obsMutex);
                reason = _disableReason;
            }
            failReason = "run disabled: " + DcTestRun::StripResumeHint(reason);
            // The rez-recovery bailouts ("no one left alive can resurrect",
            // "couldn't get X resurrected in time") name the corpse but never
            // what put it there — and both fire after combat has ended, so the
            // blame has to come off the death log.
            if (AnyMemberDead(FindTank()))
                failReason += DcTestRun::BlameSuffix(DeathBlame());
            break;
        }
        case DcTestRun::Verdict::FailPartyWiped:
        {
            std::string const who =
                _lastAliveMember.empty() ? std::string()
                                         : " (last standing: " + _lastAliveMember + ")";
            DcTestRun::Engagement const blame = DeathBlame();
            if (blame.Empty())
                failReason = "party wiped out of combat" + who;
            else if (blame.isBoss)
                failReason = "party wiped on " + blame.name + who;
            else
                failReason = "party wiped to trash: " + blame.name + who;
            break;
        }
        case DcTestRun::Verdict::FailPausedTimeout:
            failReason = "paused for over " + std::to_string(_limits.pauseGraceMs / 1000) +
                         "s: " + (pauseReason.empty() ? "(no reason)" : pauseReason);
            break;
        case DcTestRun::Verdict::FailStalledTimeout:
            failReason = "stalled for over " + std::to_string(_limits.stallGraceMs / 1000) +
                         "s: " + (stallReason.empty() ? "(no reason)" : stallReason);
            break;
        case DcTestRun::Verdict::FailNoProgress:
            failReason = "no boss/objective progress for " +
                         std::to_string(_limits.noProgressMs / 1000) + "s";
            break;
        case DcTestRun::Verdict::FailOverallTimeout:
            failReason = "exceeded the overall time limit (" +
                         std::to_string(_limits.overallTimeoutMs / 1000) + "s)";
            break;
        case DcTestRun::Verdict::FailAborted:
        {
            std::lock_guard<std::mutex> lock(_obsMutex);
            failReason = obs.leaderMissing ? "leader tank vanished"
                         : (_abortReason.empty() ? "aborted" : _abortReason);
            break;
        }
        default:
            break;
    }

    Finish(verdict, failReason);
}

void DcTestRunJob::FailSetup(std::string const& why)
{
    _record.setupStage = StageName(_stage.load());
    _record.result = "setup_failed";
    _record.failReason = why;
    LOG_INFO("playerbots.dungeonclear", "TESTRUN {} setup failed at {}: {}",
             _record.runId, _record.setupStage, why);
    Teardown();
}

void DcTestRunJob::Finish(DcTestRun::Verdict verdict, std::string const& failReason)
{
    _record.result = DcTestRun::VerdictName(verdict);
    _record.failReason = failReason;
    // Last look at the event's extras: an event that published its final
    // counters in the tick the run ended has not been sampled yet.
    CaptureExtras(FindTank());

    // Wipe post-mortem. Also filled for a death bailout — the run ends as
    // "disabled" with a corpse in the party, and "what killed us" is the same
    // question there. Left empty for every outcome nobody died in, so a
    // successful run that lost (and rezzed) a member carries no stale opponent.
    bool const diedHere =
        verdict == DcTestRun::Verdict::FailPartyWiped || AnyMemberDead(FindTank());
    if (diedHere)
    {
        DcTestRun::Engagement const blame = DeathBlame();
        _record.wipeOnBoss = blame.isBoss;
        _record.wipeOpponentEntry = blame.entry;
        _record.wipeOpponent = blame.name;
    }
    // File the pull still in flight, flagged when the run ended on corpses: that
    // row is the pull the party did not survive, complete with what the governor
    // predicted for it.
    ClosePull(diedHere);
    {
        std::lock_guard<std::mutex> lock(_obsMutex);
        _record.disableReason = _disableReason;
    }
    Teardown();
}

// Put the party back the way the run found it: alive, and out of the instance.
//
// The bug this closes is a wipe. Every logout path below ends in
// WorldSession::LogoutPlayer, which repops a character that is still dead
// (BuildPlayerRepop -> RepopAtGraveyard) and then saves it — so a wiped party
// was written to the DB as five ghosts at the instance graveyard, and somebody's
// real character came out of a test run dead in a dungeon they never chose to
// enter. Being resurrected here is what stops LogoutPlayer taking that branch at
// all; the recall is the second half of the same promise.
//
// Ordering is load-bearing in two places:
//   * this must run BEFORE LogoutBots, for the reason above;
//   * the teleport may nonetheless be issued in the same tick as the logout,
//     because LogoutPlayer drains a pending far transfer first
//     (`while (IsBeingTeleportedFar()) HandleMoveWorldportAck()`), and until it
//     does, TeleportTo has already stored the destination for SaveToDB to use
//     in place of the live position.
//
// Nothing happens at all if the run never teleported the party in: a setup
// failure before Teleporting leaves the characters exactly where they were
// standing, which for a hand-picked real character is the only defensible
// outcome — the run never moved it, so the run does not get to move it.
void DcTestRunJob::ReviveAndSendHome()
{
    if (!_teleportIssued)
        return;

    for (Slot const& slot : _slots)
    {
        if (!slot.guid)
            continue;

        // Pool slots have no rosterName; the guid is the only handle that exists
        // before the character resolves, and the only one left if it never does.
        std::string const who = slot.rosterName.empty() ? slot.guid.ToString() : slot.rosterName;

        Player* bot = ObjectAccessor::FindPlayer(slot.guid);
        if (!bot || !bot->IsInWorld())
        {
            LOG_WARN("playerbots.dungeonclear",
                     "TESTRUN {} member {} is not in world at teardown — cannot revive or recall it",
                     _record.runId, who);
            continue;
        }

        bot->CombatStop(true);

        // Dead OR a released ghost: DeathState covers the un-released corpse,
        // PLAYER_FLAGS_GHOST (from the ghost aura, spell 8326) covers the member
        // that already ran back.
        if (!bot->IsAlive() || bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
        {
            // Spirit of Redemption is a death the aura is only postponing —
            // LogoutPlayer kills the character outright when it sees this aura,
            // so it has to come off before the resurrect, not after.
            bot->RemoveAurasDueToSpell(27827);
            // No resurrection sickness. The character did not choose this death,
            // and a regression harness must not hand somebody's raider ten
            // minutes of it as the price of being volunteered.
            bot->ResurrectPlayer(1.0f, /*applySickness*/ false);
            // Turn the corpse to bones, or the character keeps a resurrectable
            // corpse inside an instance that is about to be unbound and reset.
            // Resurrect + SpawnCorpseBones is exactly what `.revive` does.
            bot->SpawnCorpseBones();
            LOG_INFO("playerbots.dungeonclear", "TESTRUN {} resurrected {} at teardown",
                     _record.runId, bot->GetName());
        }

        // A transfer already in flight owns the destination; issuing a second one
        // would be refused anyway (TeleportTo returns false while a semaphore is
        // set), so say where it landed instead of pretending it went home.
        if (bot->IsBeingTeleported())
        {
            LOG_WARN("playerbots.dungeonclear",
                     "TESTRUN {} {} is mid-transfer at teardown — left wherever that transfer lands",
                     _record.runId, bot->GetName());
            continue;
        }

        // The bind point: the innkeeper/hearthstone destination, read from the
        // same homebind fields the hearthstone spell itself uses. Deliberately
        // NOT the recorded entry position — that can be another instance, or a
        // spot the character can no longer legally be in an hour later, whereas
        // the bind point is by construction somewhere the character may stand.
        // The entry position stays in the record either way, so a manual recall
        // to it is still possible.
        if (!bot->TeleportTo(bot->m_homebindMapId, bot->m_homebindX, bot->m_homebindY,
                             bot->m_homebindZ, bot->GetOrientation()))
            LOG_WARN("playerbots.dungeonclear",
                     "TESTRUN {} could not send {} home to map {} — left in the instance",
                     _record.runId, bot->GetName(), bot->m_homebindMapId);
    }
}

// Log the party out, whichever holder owns it.
//
// A bot's login is filed under the holder that owns its master *at callback
// time*, resolved by sWorldSessionMgr->FindSession(masterAccountId)
// (PlayerbotMgr.cpp AddPlayerBot / PlayerbotOperations.h OnBotLoginOperation).
// The test driver logs in through the masterless fake-session path, and those
// sessions are constructed directly — never handed to WorldSessionMgr::AddSession
// — so that lookup misses and every driver-run party lands in
// sRandomPlayerbotMgr instead of the driver's own PlayerbotMgr. The run itself
// never noticed (it resolves bots through ObjectAccessor), but LogoutPlayerBot
// keys off the holder's map and returned silently: the group disbanded, the
// bots left the instance, and then sat in the world logged in forever.
// A real GM's session *is* in WorldSessionMgr, so their runs took the first
// branch — which is why this only ever showed up on driver/dashboard runs.
void DcTestRunJob::LogoutBots(Player* gm)
{
    PlayerbotMgr* mgr = gm ? GET_PLAYERBOT_MGR(gm) : nullptr;
    for (Slot const& slot : _slots)
    {
        if (!slot.guid)
            continue;

        if (mgr && mgr->GetPlayerBot(slot.guid))
            mgr->LogoutPlayerBot(slot.guid);
        else if (sRandomPlayerbotMgr.GetPlayerBot(slot.guid))
            sRandomPlayerbotMgr.LogoutPlayerBot(slot.guid);
        else if (ObjectAccessor::FindConnectedPlayer(slot.guid))
            LOG_WARN("playerbots.dungeonclear",
                     "TESTRUN {} bot {} is online but owned by no holder — left logged in",
                     _record.runId, slot.guid.ToString());
    }
}

void DcTestRunJob::Teardown()
{
    // gates the observers off before DisableDungeonClear; the exchange also
    // makes a second Teardown (racing abort / double .dc test stop in one tick
    // gap) a no-op, so the record is appended and the bots logged out once.
    if (_stage.exchange(Stage::TearingDown) == Stage::TearingDown)
        return;

    // Report the relay's tally before Disarm() drops the volume list. Logged
    // whenever the map had anything to relay, INCLUDING zero fires — "0 of 1"
    // is the party never reaching the volume, which is a different failure from
    // a set-piece that fired and did nothing.
    if (uint32 const armed = _areaTriggers.Armed())
        LOG_INFO("playerbots.dungeonclear",
                 "TESTRUN {} areatrigger relay: {} packet(s) over {} volume(s)", _record.runId,
                 _areaTriggers.Relayed(), armed);
    _areaTriggers.Disarm();

    // Before anything below disables the run or moves the party: the trace's
    // last frame should show the party where the run actually ended.
    FinishTrace();
    if (_lab)
        _lab->OnTeardown(MakeLabParty(), _record.result.empty() ? std::string("teardown") : _record.result);

    Player* tank = FindTank();
    if (tank)
    {
        _record.finalMap = tank->GetMapId();
        _record.finalX = tank->GetPositionX();
        _record.finalY = tank->GetPositionY();
        _record.finalZ = tank->GetPositionZ();

        // Capture the full picture BEFORE anything below tears the party down:
        // DisableDungeonClear resets the run state, Group::Disband drops every
        // member, and LogoutBots removes them from the world. Afterwards there
        // is nothing left to describe why the run failed.
        if (!_lab)
            _record.diag = DcDiag::Capture(tank, "teardown");
        _record.stallAtEnd = _record.diag.stallReason;
        _record.phaseAtEnd = _record.diag.phase;
        if (_record.diag.valid && _record.result != "success")
        {
            LOG_INFO("playerbots.dungeonclear", "TESTRUN DIAG {} {}",
                     _record.runId, DcDiag::Summarize(_record.diag));
            // Second line, only when it has something to say. The freeze dump
            // above is the authoritative one (it sampled while the party was
            // still wedged); this is the same question asked at teardown, and
            // the pair together show whether the holders changed in between —
            // which is itself the answer on a run that recovered and re-froze.
            if (_record.diag.inCombatCount)
                LOG_INFO("playerbots.dungeonclear", "TESTRUN DIAG {} combat blame: {}",
                         _record.runId, DcDiag::SummarizeCombat(_record.diag));
        }

        if (PlayerbotAI* tankAI = GET_PLAYERBOT_AI(tank))
            if (DcRun::Of(tankAI).enabled)
                DcActionShared::DisableDungeonClear(tankAI, "test run teardown");
    }

    if (_tankGuid)
        DcSettings::ClearRun(_tankGuid);

    if (tank)
        if (Group* group = tank->GetGroup())
            group->Disband(true);

    // Second guild sweep, while the party is still in world. Provisioning undoes
    // the join stock playerbots makes at LOGIN; this catches anything that guilded
    // a member later in the run (a guild strategy/task acting mid-run), so the
    // guarantee is about the state a character comes OUT with, not just the moment
    // after it logged in. No-op in the normal case.
    if (_realChars)
        for (Slot const& slot : _slots)
            if (Player* bot = ObjectAccessor::FindPlayer(slot.guid))
                UndoUnwantedGuild(bot, slot);

    // Alive and out of the dungeon before anything logs the party out — a dead
    // member reaching LogoutPlayer is saved as a ghost at the instance graveyard.
    ReviveAndSendHome();

    Player* gm = FindGm();
    LogoutBots(gm);

    // Shed the saves the run just created so the characters go back clean (and
    // the instance can reset) — the mirror of the pre-teleport unbind, and
    // unconditional for the same reason: a bind left behind here is a bind the
    // next run has to teleport around. Guid-keyed, so it works after the logout.
    // Note this does NOT give back the AccountInstancesPerHour slot the entry
    // consumed; that is time-based. A run killed by a worldserver restart never
    // reaches this at all, which is exactly why the pre-teleport sweep cannot
    // rely on it.
    UnbindFromMap();

    _record.endedAtMs = NowUnixMs();
    _record.durationS = static_cast<uint32>((_record.endedAtMs - _record.startedAtMs) / 1000);
    // A Lab party's runs are recorded by the Lab (dc_labruns.jsonl); the host
    // job itself is plumbing, not a dungeon run, and stays out of the analytics.
    if (!_lab)
        DcTestRunRecord::Append(_record);

    LOG_INFO("playerbots.dungeonclear", "TESTRUN END {} result={} reason={} bosses={}/{} duration={}s",
             _record.runId, _record.result, _record.failReason,
             _record.bossesKilled, _record.bossesTotal, _record.durationS);

    if (gm && _lab)
        ChatHandler(gm->GetSession()).SendSysMessage(Acore::StringFormat(
            "Lab party {} released: {} ({}s){}", _record.runId, _record.result, _record.durationS,
            _record.failReason.empty() ? "" : (" — " + _record.failReason)));
    else if (gm)
        ChatHandler(gm->GetSession()).SendSysMessage(Acore::StringFormat(
            "Test run {}: {} ({}/{} bosses, {}s){}", _record.dungeon, _record.result,
            _record.bossesKilled, _record.bossesTotal, _record.durationS,
            _record.failReason.empty() ? "" : (" — " + _record.failReason)));

    _done = true;  // manager erases this job (and releases its reservations) next tick
}

void DcTestRunJob::OnRunDisabled(std::string const& reason)
{
    if (_stage.load() != Stage::Monitoring)
        return;

    std::lock_guard<std::mutex> lock(_obsMutex);
    if (_disableFired)
        return;  // first reason wins
    _disableFired = true;
    _disableReason = reason;
    _disableAllCleared = reason == DcActionShared::kReasonAllCleared;
}

void DcTestRunJob::OnStatusPayload(std::string const& payload)
{
    Stage const stage = _stage.load();
    if (stage != Stage::Monitoring && stage != Stage::Starting)
        return;

    // Payload: STATUS \t enabled \t bossEntry \t bossName \t stall \t skipped
    //          \t state \t detail \t pullSetting \t pullDecision
    std::vector<std::string> parts;
    std::size_t from = 0;
    while (from <= payload.size())
    {
        std::size_t const tab = payload.find('\t', from);
        if (tab == std::string::npos)
        {
            parts.push_back(payload.substr(from));
            break;
        }
        parts.push_back(payload.substr(from, tab - from));
        from = tab + 1;
    }
    if (parts.size() < 8 || parts[0] != "STATUS")
        return;

    std::lock_guard<std::mutex> lock(_obsMutex);
    if (parts[6] == _lastStatusState)
        return;
    _lastStatusState = parts[6];
    _record.statusTimeline.push_back({_totalMs / 1000, parts[6], parts[7]});
}
