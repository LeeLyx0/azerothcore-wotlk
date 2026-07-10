#include "BotGameplayTracker.h"

#include "BotPersonalityMgr.h"
#include "BotProactiveDialogueMgr.h"
#include "BotRelationshipMgr.h"
#include "Config.h"
#include "Creature.h"
#include "GameTime.h"
#include "Group.h"
#include "GroupReference.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "Unit.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>

namespace
{
constexpr uint32 HOUR_SECONDS = 60 * 60;
constexpr uint32 CLEANUP_INTERVAL_MS = 60000;
constexpr uint32 TEAMWORK_UPDATE_INTERVAL_MS = 30000;
constexpr uint32 MAX_RECENT_EVENTS_PER_PAIR = 20;
constexpr uint32 RECENT_EVENT_EXPIRY_SECONDS = 2 * HOUR_SECONDS;
constexpr uint32 MAX_TRACKED_ENTRIES = 50000;

uint32 CurrentGameTimeSeconds()
{
    return static_cast<uint32>(GameTime::GetGameTime().count());
}

int32 ReadIntConfig(
    char const* name,
    int32 defaultValue,
    int32 minValue,
    int32 maxValue,
    bool debugLogging)
{
    int32 const configured = sConfigMgr->GetOption<int32>(
        name,
        defaultValue);
    int32 const clamped = std::clamp(configured, minValue, maxValue);

    if (debugLogging && configured != clamped)
    {
        LOG_DEBUG(
            "module.botpersonality.gameplay",
            "{} value {} is invalid, clamped to {}",
            name,
            configured,
            clamped);
    }

    return clamped;
}

uint32 ReadUIntConfig(
    char const* name,
    int32 defaultValue,
    int32 minValue,
    int32 maxValue,
    bool debugLogging)
{
    return static_cast<uint32>(
        ReadIntConfig(name, defaultValue, minValue, maxValue, debugLogging));
}

float ReadFloatConfig(
    char const* name,
    float defaultValue,
    float minValue,
    float maxValue,
    bool debugLogging)
{
    float const configured = sConfigMgr->GetOption<float>(
        name,
        defaultValue);
    float const clamped = std::clamp(configured, minValue, maxValue);

    if (debugLogging && configured != clamped)
    {
        LOG_DEBUG(
            "module.botpersonality.gameplay",
            "{} value {} is invalid, clamped to {}",
            name,
            configured,
            clamped);
    }

    return clamped;
}

void ReadDelta(
    char const* prefix,
    BotRelationshipDelta defaultValue,
    BotRelationshipDelta& delta,
    bool debugLogging)
{
    std::string const base(prefix);
    delta.affinity = ReadIntConfig(
        (base + "Affinity").c_str(),
        defaultValue.affinity,
        -100,
        100,
        debugLogging);
    delta.trust = ReadIntConfig(
        (base + "Trust").c_str(),
        defaultValue.trust,
        -100,
        100,
        debugLogging);
    delta.respect = ReadIntConfig(
        (base + "Respect").c_str(),
        defaultValue.respect,
        -100,
        100,
        debugLogging);
    delta.familiarity = ReadIntConfig(
        (base + "Familiarity").c_str(),
        defaultValue.familiarity,
        -100,
        100,
        debugLogging);
}

int32 PositiveMagnitude(BotRelationshipDelta const& delta)
{
    return std::max(delta.affinity, 0) +
        std::max(delta.trust, 0) +
        std::max(delta.respect, 0);
}

int32 NegativeMagnitude(BotRelationshipDelta const& delta)
{
    return std::max(-delta.affinity, 0) +
        std::max(-delta.trust, 0) +
        std::max(-delta.respect, 0);
}

void ScalePositiveValues(BotRelationshipDelta& delta, uint32 percent)
{
    auto scale = [percent](int32& value)
    {
        if (value > 0)
            value = value * static_cast<int32>(percent) / 100;
    };

    scale(delta.affinity);
    scale(delta.trust);
    scale(delta.respect);
}

void ScaleNegativeValues(BotRelationshipDelta& delta, uint32 percent)
{
    auto scale = [percent](int32& value)
    {
        if (value < 0)
            value = value * static_cast<int32>(percent) / 100;
    };

    scale(delta.affinity);
    scale(delta.trust);
    scale(delta.respect);
}

Player* ResolvePlayer(Unit* unit)
{
    if (!unit)
        return nullptr;

    if (Player* player = unit->ToPlayer())
        return player;

    return unit->GetCharmerOrOwnerPlayerOrPlayerItself();
}

BotGameplayContext MakeContext(
    Map const* map,
    uint32 sourceEntry,
    uint32 value)
{
    BotGameplayContext context;
    if (map)
    {
        context.mapId = map->GetId();
        context.instanceId = map->GetInstanceId();
        context.isDungeon = map->IsDungeon();
        context.isRaid = map->IsRaid();
    }

    context.sourceEntry = sourceEntry;
    context.value = value;
    return context;
}
}

std::size_t BotGameplayTracker::CooldownKeyHash::operator()(
    CooldownKey const& key) const
{
    std::size_t hash = BotPlayerRelationshipKeyHash{}(key.relationship);
    hash ^= static_cast<std::size_t>(key.event) + 0x9e3779b9u +
        (hash << 6) + (hash >> 2);
    return hash;
}

void BotGameplayTracker::LoadConfig(bool reload)
{
    _enabled = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.Enable",
        true);
    _updateRelationships = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.UpdateRelationships",
        true);
    _debugLogging = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.DebugLogging",
        false);

    _trackNormalKills = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.TrackNormalKills",
        true);
    _trackEliteKills = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.TrackEliteKills",
        true);
    _trackBossKills = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.TrackBossKills",
        true);
    _trackHealing = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.TrackHealing",
        true);
    _trackResurrection = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.TrackResurrection",
        true);
    _trackDeaths = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.TrackDeaths",
        true);
    _trackGroupWipes = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.TrackGroupWipes",
        true);
    _trackGroupLeaves = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.TrackGroupLeaves",
        true);
    _trackEncounterCompletion = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.TrackEncounterCompletion",
        true);
    _trackSustainedTeamwork = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.TrackSustainedTeamwork",
        true);
    _allowPvPEvents = sConfigMgr->GetOption<bool>(
        "BotPersonality.Gameplay.AllowPvPEvents",
        false);

    _participationDistance = ReadFloatConfig(
        "BotPersonality.Gameplay.ParticipationDistance",
        120.0f,
        10.0f,
        500.0f,
        _debugLogging);
    _normalKillBatchSize = ReadUIntConfig(
        "BotPersonality.Gameplay.NormalKillBatchSize",
        3,
        1,
        50,
        _debugLogging);
    _normalKillWindowMs = ReadUIntConfig(
        "BotPersonality.Gameplay.NormalKillWindowSeconds",
        300,
        5,
        3600,
        _debugLogging) * 1000;
    _healThreshold = ReadUIntConfig(
        "BotPersonality.Gameplay.HealThreshold",
        500,
        1,
        100000,
        _debugLogging);
    _healWindowMs = ReadUIntConfig(
        "BotPersonality.Gameplay.HealWindowSeconds",
        20,
        1,
        300,
        _debugLogging) * 1000;
    _deathWindowMs = ReadUIntConfig(
        "BotPersonality.Gameplay.DeathWindowSeconds",
        120,
        5,
        3600,
        _debugLogging) * 1000;
    _repeatedDeathThreshold = ReadUIntConfig(
        "BotPersonality.Gameplay.RepeatedDeathThreshold",
        3,
        2,
        20,
        _debugLogging);
    _sustainedTeamworkSeconds = ReadUIntConfig(
        "BotPersonality.Gameplay.SustainedTeamworkSeconds",
        20 * 60,
        60,
        86400,
        _debugLogging);
    _minimumWipeMembers = ReadUIntConfig(
        "BotPersonality.Gameplay.MinimumWipeMembers",
        3,
        2,
        40,
        _debugLogging);

    _maxPositiveGainPerHour = ReadUIntConfig(
        "BotPersonality.Gameplay.MaxPositiveGainPerHour",
        40,
        0,
        10000,
        _debugLogging);
    _maxNegativeLossPerHour = ReadUIntConfig(
        "BotPersonality.Gameplay.MaxNegativeLossPerHour",
        50,
        0,
        10000,
        _debugLogging);
    _maxFamiliarityGainPerHour = ReadUIntConfig(
        "BotPersonality.Gameplay.MaxFamiliarityGainPerHour",
        60,
        0,
        10000,
        _debugLogging);

    _normalKillCooldownMs = ReadUIntConfig(
        "BotPersonality.Gameplay.NormalKillCooldownSeconds",
        60,
        0,
        3600,
        _debugLogging) * 1000;
    _eliteKillCooldownMs = ReadUIntConfig(
        "BotPersonality.Gameplay.EliteKillCooldownSeconds",
        90,
        0,
        3600,
        _debugLogging) * 1000;
    _bossKillCooldownMs = ReadUIntConfig(
        "BotPersonality.Gameplay.BossKillCooldownSeconds",
        300,
        0,
        7200,
        _debugLogging) * 1000;
    _healCooldownMs = ReadUIntConfig(
        "BotPersonality.Gameplay.HealCooldownSeconds",
        45,
        0,
        3600,
        _debugLogging) * 1000;
    _resurrectionCooldownMs = ReadUIntConfig(
        "BotPersonality.Gameplay.ResurrectionCooldownSeconds",
        300,
        0,
        7200,
        _debugLogging) * 1000;
    _deathCooldownMs = ReadUIntConfig(
        "BotPersonality.Gameplay.DeathCooldownSeconds",
        90,
        0,
        3600,
        _debugLogging) * 1000;
    _wipeCooldownMs = ReadUIntConfig(
        "BotPersonality.Gameplay.WipeCooldownSeconds",
        300,
        0,
        7200,
        _debugLogging) * 1000;
    _groupLeaveCooldownMs = ReadUIntConfig(
        "BotPersonality.Gameplay.GroupLeaveCooldownSeconds",
        300,
        0,
        7200,
        _debugLogging) * 1000;
    _encounterCooldownMs = ReadUIntConfig(
        "BotPersonality.Gameplay.EncounterCooldownSeconds",
        600,
        0,
        7200,
        _debugLogging) * 1000;
    _teamworkCooldownMs = ReadUIntConfig(
        "BotPersonality.Gameplay.TeamworkCooldownSeconds",
        20 * 60,
        60,
        86400,
        _debugLogging) * 1000;

    ReadDelta(
        "BotPersonality.Gameplay.SharedNormalKill",
        { 1, 0, 1, 1 },
        _sharedNormalKillDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.SharedEliteKill",
        { 2, 1, 2, 2 },
        _sharedEliteKillDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.SharedBossKill",
        { 5, 3, 5, 3 },
        _sharedBossKillDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.PlayerHealedBot",
        { 3, 3, 1, 2 },
        _playerHealedBotDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.PlayerResurrectedBot",
        { 6, 5, 2, 4 },
        _playerResurrectedBotDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.BotHealedPlayer",
        { 1, 1, 1, 2 },
        _botHealedPlayerDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.BotResurrectedPlayer",
        { 2, 2, 1, 3 },
        _botResurrectedPlayerDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.PlayerDied",
        { -1, -1, -2, 1 },
        _playerDiedDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.BotDied",
        { -2, -2, -1, 1 },
        _botDiedDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.SharedDeath",
        { -1, 0, -1, 2 },
        _sharedDeathDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.GroupWipe",
        { -3, -3, -3, 2 },
        _groupWipeDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.PlayerLeftGroupDuringCombat",
        { -8, -6, -4, 0 },
        _playerLeftGroupDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.DungeonCompleted",
        { 8, 4, 8, 6 },
        _dungeonCompletedDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.RaidEncounterCompleted",
        { 6, 3, 6, 4 },
        _raidEncounterDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.SustainedTeamwork",
        { 2, 2, 1, 4 },
        _sustainedTeamworkDelta,
        _debugLogging);
    ReadDelta(
        "BotPersonality.Gameplay.RepeatedPlayerDeath",
        { -3, -2, -4, 1 },
        _repeatedPlayerDeathDelta,
        _debugLogging);

    if (!_enabled)
        ClearTrackers();

    LOG_INFO(
        "server.loading",
        "Bot Personality gameplay tracking {}{}",
        _enabled ? "enabled" : "disabled",
        reload ? " after config reload" : "");
}

void BotGameplayTracker::Update(uint32 diff)
{
    if (!_enabled)
        return;

    uint32 const nowMs = getMSTime();
    uint32 const nowSeconds = CurrentGameTimeSeconds();

    if (!_lastTeamworkUpdateMs)
        _lastTeamworkUpdateMs = nowMs;

    if (getMSTimeDiff(_lastTeamworkUpdateMs, nowMs) >=
        TEAMWORK_UPDATE_INTERVAL_MS)
    {
        CheckTeamwork(nowSeconds);
        _lastTeamworkUpdateMs = nowMs;
    }

    (void)diff;
    Cleanup(nowMs, nowSeconds);
}

void BotGameplayTracker::ClearTrackers()
{
    _cooldowns.clear();
    _normalKillAccumulators.clear();
    _healAccumulators.clear();
    _recentEvents.clear();
    _deathWindows.clear();
    _capRecords.clear();
    _teamworkStates.clear();
}

BotGameplayTrackerStats BotGameplayTracker::GetStats() const
{
    BotGameplayTrackerStats stats;
    stats.enabled = _enabled;
    stats.updateRelationships = _updateRelationships;
    stats.recentPairs = static_cast<uint32>(_recentEvents.size());
    stats.cooldowns = static_cast<uint32>(_cooldowns.size());
    stats.normalKillAccumulators =
        static_cast<uint32>(_normalKillAccumulators.size());
    stats.healAccumulators = static_cast<uint32>(_healAccumulators.size());
    stats.deathWindows = static_cast<uint32>(_deathWindows.size());
    stats.capPairs = static_cast<uint32>(_capRecords.size());
    stats.teamworkPairs = static_cast<uint32>(_teamworkStates.size());
    return stats;
}

std::vector<BotGameplayRecentEvent> BotGameplayTracker::GetRecentEvents(
    uint32 botGuid,
    uint32 playerGuid,
    uint32 limit) const
{
    std::vector<BotGameplayRecentEvent> events;
    if (!botGuid || !playerGuid || !limit)
        return events;

    auto const itr = _recentEvents.find(MakeKey(botGuid, playerGuid));
    if (itr == _recentEvents.end())
        return events;

    std::deque<BotGameplayRecentEvent> const& stored = itr->second;
    uint32 count = 0;
    for (auto recent = stored.rbegin();
        recent != stored.rend() && count < limit;
        ++recent, ++count)
        events.push_back(*recent);

    return events;
}

BotRelationshipDelta BotGameplayTracker::GetBaseDelta(
    BotGameplayEvent event) const
{
    switch (event)
    {
        case BotGameplayEvent::SharedNormalKill:
            return _sharedNormalKillDelta;
        case BotGameplayEvent::SharedEliteKill:
            return _sharedEliteKillDelta;
        case BotGameplayEvent::SharedBossKill:
            return _sharedBossKillDelta;
        case BotGameplayEvent::PlayerHealedBot:
            return _playerHealedBotDelta;
        case BotGameplayEvent::PlayerResurrectedBot:
            return _playerResurrectedBotDelta;
        case BotGameplayEvent::BotHealedPlayer:
            return _botHealedPlayerDelta;
        case BotGameplayEvent::BotResurrectedPlayer:
            return _botResurrectedPlayerDelta;
        case BotGameplayEvent::PlayerDied:
            return _playerDiedDelta;
        case BotGameplayEvent::BotDied:
            return _botDiedDelta;
        case BotGameplayEvent::SharedDeath:
            return _sharedDeathDelta;
        case BotGameplayEvent::GroupWipe:
            return _groupWipeDelta;
        case BotGameplayEvent::PlayerLeftGroupDuringCombat:
            return _playerLeftGroupDelta;
        case BotGameplayEvent::DungeonCompleted:
            return _dungeonCompletedDelta;
        case BotGameplayEvent::RaidEncounterCompleted:
            return _raidEncounterDelta;
        case BotGameplayEvent::SustainedTeamwork:
            return _sustainedTeamworkDelta;
        case BotGameplayEvent::RepeatedPlayerDeath:
            return _repeatedPlayerDeathDelta;
    }

    return _sharedNormalKillDelta;
}

BotGameplayDebugResult BotGameplayTracker::SimulateEvent(
    Player const* bot,
    Player const* player,
    BotGameplayEvent event,
    bool apply)
{
    BotGameplayDebugResult result;
    result.event = event;

    if (!_enabled)
    {
        result.error = "Gameplay tracking is disabled.";
        return result;
    }

    if (!IsValidPair(bot, player))
    {
        result.error = "Invalid bot/player pair.";
        return result;
    }

    BotGameplayContext context = MakeContext(
        bot->GetMap(),
        0,
        0);

    ApplyGameplayEvent(
        const_cast<Player*>(bot),
        const_cast<Player*>(player),
        event,
        context,
        apply,
        true,
        &result);

    return result;
}

BotPlayerRelationshipKey BotGameplayTracker::MakeKey(
    uint32 botGuid,
    uint32 playerGuid) const
{
    return { botGuid, playerGuid };
}

bool BotGameplayTracker::IsRealPlayer(Player const* player) const
{
    if (!player || !player->GetSession())
        return false;

    return !sBotPersonalityMgr.IsPlayerbot(player) &&
        !player->GetSession()->IsBot();
}

bool BotGameplayTracker::IsValidPair(
    Player const* bot,
    Player const* player) const
{
    if (!bot || !player || bot == player)
        return false;

    if (!bot->GetSession() || !player->GetSession())
        return false;

    if (!sBotPersonalityMgr.IsPlayerbot(bot) || !IsRealPlayer(player))
        return false;

    return IsMapAllowed(bot->GetMap()) && IsMapAllowed(player->GetMap());
}

bool BotGameplayTracker::SameGroup(
    Player const* bot,
    Player const* player) const
{
    if (!bot || !player)
        return false;

    Group const* botGroup = bot->GetGroup();
    return botGroup && botGroup == player->GetGroup();
}

bool BotGameplayTracker::IsMapAllowed(Map const* map) const
{
    if (!map)
        return false;

    if (!_allowPvPEvents && map->IsBattlegroundOrArena())
        return false;

    return true;
}

bool BotGameplayTracker::IsNear(
    Player const* player,
    Unit const* reference) const
{
    if (!player || !reference)
        return false;

    if (!player->IsInMap(reference))
        return false;

    return player->IsWithinDist(reference, _participationDistance);
}

void BotGameplayTracker::ApplyGameplayEvent(
    Player* bot,
    Player* player,
    BotGameplayEvent event,
    BotGameplayContext const& context,
    bool applyRelationship,
    bool bypassAntiFarm,
    BotGameplayDebugResult* debugResult)
{
    if (debugResult)
        debugResult->event = event;

    if (!_enabled || !IsValidPair(bot, player))
    {
        if (debugResult)
            debugResult->error = "Invalid bot/player pair.";
        return;
    }

    uint32 const nowMs = getMSTime();
    uint32 const nowSeconds = CurrentGameTimeSeconds();
    BotPlayerRelationshipKey const key = MakeKey(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter());

    if (!bypassAntiFarm && IsEventOnCooldown(key, event, nowMs))
    {
        if (debugResult)
            debugResult->error = "Gameplay event is on cooldown.";
        return;
    }

    BotPersonality const* personality =
        sBotPersonalityMgr.GetOrCreatePersonality(bot);
    if (!personality)
    {
        if (debugResult)
            debugResult->error = "Unable to load bot personality.";
        return;
    }

    BotRelationshipDelta baseDelta = GetBaseDelta(event);
    BotRelationshipDelta adjustedDelta = CalculateGameplayRelationshipDelta(
        *personality,
        event,
        baseDelta);

    if (!bypassAntiFarm)
        ApplyGameplayCaps(key, adjustedDelta, nowSeconds);

    bool applied = false;
    if (applyRelationship && _updateRelationships)
    {
        applied = sBotRelationshipMgr.ApplyGameplayEvent(
            bot,
            player,
            event,
            adjustedDelta,
            true);
    }

    if (applyRelationship && !bypassAntiFarm)
    {
        StartEventCooldown(key, event, nowMs);
        if (applied)
            RecordGameplayCaps(key, adjustedDelta, nowSeconds);
    }

    BotGameplayRecentEvent recent;
    recent.event = event;
    recent.context = context;
    recent.baseDelta = baseDelta;
    recent.adjustedDelta = adjustedDelta;
    recent.timestamp = nowSeconds;
    recent.applied = applied;

    if (!applyRelationship)
        recent.note = "preview";
    else if (!_updateRelationships)
        recent.note = "relationship updates disabled";
    else if (!applied)
        recent.note = "relationship manager rejected event";
    else
        recent.note = "applied";

    RecordRecent(key, recent);

    if (applied)
        TouchTeamwork(bot, player, nowSeconds);

    if (applyRelationship)
    {
        sBotProactiveDialogueMgr.RecordGameplayEvent(
            bot,
            player,
            event,
            context);
    }

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.gameplay",
            "{} bot {} player {} base a:{} t:{} r:{} f:{} final a:{} "
            "t:{} r:{} f:{} {}",
            BotGameplayEventToString(event),
            key.botGuid,
            key.playerGuid,
            baseDelta.affinity,
            baseDelta.trust,
            baseDelta.respect,
            baseDelta.familiarity,
            adjustedDelta.affinity,
            adjustedDelta.trust,
            adjustedDelta.respect,
            adjustedDelta.familiarity,
            recent.note);
    }

    if (debugResult)
    {
        debugResult->success = true;
        debugResult->applied = applied;
        debugResult->baseDelta = baseDelta;
        debugResult->adjustedDelta = adjustedDelta;
    }
}

void BotGameplayTracker::ApplyGameplayCaps(
    BotPlayerRelationshipKey const& key,
    BotRelationshipDelta& delta,
    uint32 nowSeconds)
{
    std::deque<GameplayCapRecord>& records = _capRecords[key];

    while (!records.empty() &&
        (nowSeconds < records.front().timestamp ||
            nowSeconds - records.front().timestamp >= HOUR_SECONDS))
        records.pop_front();

    uint32 usedPositive = 0;
    uint32 usedNegative = 0;
    uint32 usedFamiliarity = 0;
    for (GameplayCapRecord const& record : records)
    {
        usedPositive += record.positive;
        usedNegative += record.negative;
        usedFamiliarity += record.familiarity;
    }

    int32 const positive = PositiveMagnitude(delta);
    if (positive > 0)
    {
        uint32 const remaining =
            usedPositive >= _maxPositiveGainPerHour ?
                0 :
                _maxPositiveGainPerHour - usedPositive;
        if (static_cast<uint32>(positive) > remaining)
        {
            uint32 const percent = remaining * 100 /
                static_cast<uint32>(positive);
            ScalePositiveValues(delta, percent);
        }
    }

    int32 const negative = NegativeMagnitude(delta);
    if (negative > 0)
    {
        uint32 const remaining =
            usedNegative >= _maxNegativeLossPerHour ?
                0 :
                _maxNegativeLossPerHour - usedNegative;
        if (static_cast<uint32>(negative) > remaining)
        {
            uint32 const percent = remaining * 100 /
                static_cast<uint32>(negative);
            ScaleNegativeValues(delta, percent);
        }
    }

    if (delta.familiarity > 0)
    {
        uint32 const remaining =
            usedFamiliarity >= _maxFamiliarityGainPerHour ?
                0 :
                _maxFamiliarityGainPerHour - usedFamiliarity;
        if (static_cast<uint32>(delta.familiarity) > remaining)
            delta.familiarity = static_cast<int32>(remaining);
    }
}

void BotGameplayTracker::RecordGameplayCaps(
    BotPlayerRelationshipKey const& key,
    BotRelationshipDelta const& delta,
    uint32 nowSeconds)
{
    GameplayCapRecord record;
    record.timestamp = nowSeconds;
    record.positive = static_cast<uint32>(PositiveMagnitude(delta));
    record.negative = static_cast<uint32>(NegativeMagnitude(delta));
    record.familiarity = delta.familiarity > 0 ?
        static_cast<uint32>(delta.familiarity) :
        0;

    if (!record.positive && !record.negative && !record.familiarity)
        return;

    std::deque<GameplayCapRecord>& records = _capRecords[key];
    records.push_back(record);

    while (records.size() > 128)
        records.pop_front();
}

bool BotGameplayTracker::IsEventOnCooldown(
    BotPlayerRelationshipKey const& key,
    BotGameplayEvent event,
    uint32 nowMs) const
{
    auto const itr = _cooldowns.find({ key, event });
    if (itr == _cooldowns.end())
        return false;

    return static_cast<int32>(itr->second - nowMs) > 0;
}

void BotGameplayTracker::StartEventCooldown(
    BotPlayerRelationshipKey const& key,
    BotGameplayEvent event,
    uint32 nowMs)
{
    uint32 const cooldownMs = GetCooldownMs(event);
    if (!cooldownMs)
        return;

    _cooldowns[{ key, event }] = nowMs + cooldownMs;
}

uint32 BotGameplayTracker::GetCooldownMs(BotGameplayEvent event) const
{
    switch (event)
    {
        case BotGameplayEvent::SharedNormalKill:
            return _normalKillCooldownMs;
        case BotGameplayEvent::SharedEliteKill:
            return _eliteKillCooldownMs;
        case BotGameplayEvent::SharedBossKill:
            return _bossKillCooldownMs;
        case BotGameplayEvent::PlayerHealedBot:
        case BotGameplayEvent::BotHealedPlayer:
            return _healCooldownMs;
        case BotGameplayEvent::PlayerResurrectedBot:
        case BotGameplayEvent::BotResurrectedPlayer:
            return _resurrectionCooldownMs;
        case BotGameplayEvent::PlayerDied:
        case BotGameplayEvent::BotDied:
        case BotGameplayEvent::SharedDeath:
        case BotGameplayEvent::RepeatedPlayerDeath:
            return _deathCooldownMs;
        case BotGameplayEvent::GroupWipe:
            return _wipeCooldownMs;
        case BotGameplayEvent::PlayerLeftGroupDuringCombat:
            return _groupLeaveCooldownMs;
        case BotGameplayEvent::DungeonCompleted:
        case BotGameplayEvent::RaidEncounterCompleted:
            return _encounterCooldownMs;
        case BotGameplayEvent::SustainedTeamwork:
            return _teamworkCooldownMs;
    }

    return 0;
}

void BotGameplayTracker::RecordRecent(
    BotPlayerRelationshipKey const& key,
    BotGameplayRecentEvent const& recent)
{
    std::deque<BotGameplayRecentEvent>& events = _recentEvents[key];
    events.push_back(recent);

    while (events.size() > MAX_RECENT_EVENTS_PER_PAIR)
        events.pop_front();
}

void BotGameplayTracker::TouchTeamwork(
    Player* bot,
    Player* player,
    uint32 nowSeconds)
{
    if (!_trackSustainedTeamwork || !IsValidPair(bot, player) ||
        !SameGroup(bot, player))
        return;

    BotPlayerRelationshipKey const key = MakeKey(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter());

    TeamworkState& state = _teamworkStates[key];
    if (!state.startedSeconds)
        state.startedSeconds = nowSeconds;

    state.lastSeenSeconds = nowSeconds;
}

void BotGameplayTracker::RecordCreatureKill(Player* killer, Creature* killed)
{
    if (!_enabled || !killer || !killed || !IsMapAllowed(killed->GetMap()))
        return;

    if (!killer->GetGroup() || !IsNear(killer, killed))
        return;

    if (killed->IsTrigger() || killed->IsPet())
        return;

    BotGameplayEvent event = BotGameplayEvent::SharedNormalKill;
    uint32 const rank = killed->GetCreatureTemplate()->rank;

    if (killed->IsDungeonBoss() || killed->isWorldBoss() ||
        rank == CREATURE_ELITE_WORLDBOSS)
    {
        if (!_trackBossKills)
            return;

        event = BotGameplayEvent::SharedBossKill;
    }
    else if (killed->isElite())
    {
        if (!_trackEliteKills)
            return;

        event = BotGameplayEvent::SharedEliteKill;
    }
    else
    {
        if (!_trackNormalKills)
            return;
    }

    BotGameplayContext context = MakeContext(
        killed->GetMap(),
        killed->GetEntry(),
        1);
    Group* group = killer->GetGroup();
    uint32 const nowSeconds = CurrentGameTimeSeconds();

    if (sBotPersonalityMgr.IsPlayerbot(killer))
    {
        for (GroupReference* itr = group->GetFirstMember();
            itr != nullptr;
            itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!IsRealPlayer(member) || !IsNear(member, killed))
                continue;

            if (event == BotGameplayEvent::SharedNormalKill)
                RecordNormalKill(killer, member, event, context);
            else
                ApplyGameplayEvent(killer, member, event, context, true, false,
                    nullptr);

            TouchTeamwork(killer, member, nowSeconds);
        }
    }
    else if (IsRealPlayer(killer))
    {
        for (GroupReference* itr = group->GetFirstMember();
            itr != nullptr;
            itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!sBotPersonalityMgr.IsPlayerbot(member) ||
                !IsNear(member, killed))
                continue;

            if (event == BotGameplayEvent::SharedNormalKill)
                RecordNormalKill(member, killer, event, context);
            else
                ApplyGameplayEvent(member, killer, event, context, true, false,
                    nullptr);

            TouchTeamwork(member, killer, nowSeconds);
        }
    }
}

void BotGameplayTracker::RecordNormalKill(
    Player* bot,
    Player* player,
    BotGameplayEvent event,
    BotGameplayContext const& context)
{
    if (!IsValidPair(bot, player) || !SameGroup(bot, player))
        return;

    uint32 const nowMs = getMSTime();
    BotPlayerRelationshipKey const key = MakeKey(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter());

    if (IsEventOnCooldown(key, event, nowMs))
        return;

    TimedAccumulator& accumulator = _normalKillAccumulators[key];
    if (!accumulator.firstMs ||
        getMSTimeDiff(accumulator.firstMs, nowMs) > _normalKillWindowMs)
    {
        accumulator = {};
        accumulator.firstMs = nowMs;
    }

    accumulator.lastMs = nowMs;
    accumulator.sourceEntry = context.sourceEntry;
    ++accumulator.value;

    if (accumulator.value < _normalKillBatchSize)
        return;

    BotGameplayContext appliedContext = context;
    appliedContext.value = accumulator.value;
    accumulator = {};

    ApplyGameplayEvent(
        bot,
        player,
        event,
        appliedContext,
        true,
        false,
        nullptr);
}

void BotGameplayTracker::RecordHeal(
    Unit* healer,
    Unit* receiver,
    uint32 gain)
{
    if (!_enabled || !_trackHealing || !gain || healer == receiver)
        return;

    Player* healerPlayer = ResolvePlayer(healer);
    Player* receiverPlayer = ResolvePlayer(receiver);
    if (!healerPlayer || !receiverPlayer || healerPlayer == receiverPlayer)
        return;

    if (!healerPlayer->IsInMap(receiverPlayer) ||
        !healerPlayer->IsWithinDist(receiverPlayer, _participationDistance))
        return;

    BotGameplayContext context = MakeContext(
        receiverPlayer->GetMap(),
        healer ? healer->GetEntry() : 0,
        gain);

    if (IsRealPlayer(healerPlayer) &&
        sBotPersonalityMgr.IsPlayerbot(receiverPlayer) &&
        SameGroup(receiverPlayer, healerPlayer))
    {
        RecordHealEvent(
            receiverPlayer,
            healerPlayer,
            BotGameplayEvent::PlayerHealedBot,
            gain,
            context);
    }
    else if (sBotPersonalityMgr.IsPlayerbot(healerPlayer) &&
        IsRealPlayer(receiverPlayer) &&
        SameGroup(healerPlayer, receiverPlayer))
    {
        RecordHealEvent(
            healerPlayer,
            receiverPlayer,
            BotGameplayEvent::BotHealedPlayer,
            gain,
            context);
    }
}

void BotGameplayTracker::RecordHealEvent(
    Player* bot,
    Player* player,
    BotGameplayEvent event,
    uint32 amount,
    BotGameplayContext const& context)
{
    if (!IsValidPair(bot, player) || !SameGroup(bot, player))
        return;

    uint32 const nowMs = getMSTime();
    BotPlayerRelationshipKey const key = MakeKey(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter());
    CooldownKey const cooldownKey = { key, event };

    if (IsEventOnCooldown(key, event, nowMs))
        return;

    TimedAccumulator& accumulator = _healAccumulators[cooldownKey];
    if (!accumulator.firstMs ||
        getMSTimeDiff(accumulator.firstMs, nowMs) > _healWindowMs)
    {
        accumulator = {};
        accumulator.firstMs = nowMs;
    }

    accumulator.lastMs = nowMs;
    accumulator.value += amount;
    accumulator.sourceEntry = context.sourceEntry;

    if (accumulator.value < _healThreshold)
        return;

    BotGameplayContext appliedContext = context;
    appliedContext.value = accumulator.value;
    accumulator = {};

    ApplyGameplayEvent(
        bot,
        player,
        event,
        appliedContext,
        true,
        false,
        nullptr);
}

void BotGameplayTracker::RecordResurrectionCast(
    Player* caster,
    Player* target,
    uint32 spellId)
{
    if (!_enabled || !_trackResurrection || !caster || !target ||
        caster == target)
        return;

    if (!caster->IsInMap(target) ||
        !caster->IsWithinDist(target, _participationDistance))
        return;

    BotGameplayContext context = MakeContext(
        caster->GetMap(),
        spellId,
        1);

    if (IsRealPlayer(caster) &&
        sBotPersonalityMgr.IsPlayerbot(target) &&
        SameGroup(target, caster))
    {
        ApplyGameplayEvent(
            target,
            caster,
            BotGameplayEvent::PlayerResurrectedBot,
            context,
            true,
            false,
            nullptr);
    }
    else if (sBotPersonalityMgr.IsPlayerbot(caster) &&
        IsRealPlayer(target) &&
        SameGroup(caster, target))
    {
        ApplyGameplayEvent(
            caster,
            target,
            BotGameplayEvent::BotResurrectedPlayer,
            context,
            true,
            false,
            nullptr);
    }
}

void BotGameplayTracker::RecordPlayerDeath(Player* player, Unit* killer)
{
    if (!_enabled || !_trackDeaths || !player || !player->GetGroup())
        return;

    if (!IsMapAllowed(player->GetMap()))
        return;

    if (!_allowPvPEvents && ResolvePlayer(killer))
        return;

    BotGameplayContext context = MakeContext(
        player->GetMap(),
        killer ? killer->GetEntry() : 0,
        1);
    Group* group = player->GetGroup();

    if (IsRealPlayer(player))
    {
        for (GroupReference* itr = group->GetFirstMember();
            itr != nullptr;
            itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!sBotPersonalityMgr.IsPlayerbot(member) ||
                !IsNear(member, player))
                continue;

            ApplyGameplayEvent(
                member,
                player,
                BotGameplayEvent::PlayerDied,
                context,
                true,
                false,
                nullptr);
            RecordDeathWindows(
                member,
                player,
                BotGameplayEvent::PlayerDied,
                context);
        }
    }
    else if (sBotPersonalityMgr.IsPlayerbot(player))
    {
        for (GroupReference* itr = group->GetFirstMember();
            itr != nullptr;
            itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!IsRealPlayer(member) || !IsNear(member, player))
                continue;

            ApplyGameplayEvent(
                player,
                member,
                BotGameplayEvent::BotDied,
                context,
                true,
                false,
                nullptr);
            RecordDeathWindows(
                player,
                member,
                BotGameplayEvent::BotDied,
                context);
        }
    }

    TryRecordGroupWipe(group, player, context);
}

void BotGameplayTracker::RecordDeathWindows(
    Player* bot,
    Player* player,
    BotGameplayEvent deathEvent,
    BotGameplayContext const& context)
{
    if (!IsValidPair(bot, player))
        return;

    uint32 const nowMs = getMSTime();
    BotPlayerRelationshipKey const key = MakeKey(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter());
    DeathWindow& window = _deathWindows[key];
    window.lastAccessMs = nowMs;

    auto prune = [this, nowMs](std::deque<uint32>& deaths)
    {
        while (!deaths.empty() &&
            getMSTimeDiff(deaths.front(), nowMs) > _deathWindowMs)
            deaths.pop_front();
    };

    prune(window.playerDeaths);
    prune(window.botDeaths);

    if (deathEvent == BotGameplayEvent::PlayerDied)
    {
        window.playerDeaths.push_back(nowMs);
        if (window.playerDeaths.size() >= _repeatedDeathThreshold)
        {
            ApplyGameplayEvent(
                bot,
                player,
                BotGameplayEvent::RepeatedPlayerDeath,
                context,
                true,
                false,
                nullptr);
            window.playerDeaths.clear();
        }

        if (!window.botDeaths.empty())
            ApplyGameplayEvent(
                bot,
                player,
                BotGameplayEvent::SharedDeath,
                context,
                true,
                false,
                nullptr);
    }
    else if (deathEvent == BotGameplayEvent::BotDied)
    {
        window.botDeaths.push_back(nowMs);
        if (!window.playerDeaths.empty())
            ApplyGameplayEvent(
                bot,
                player,
                BotGameplayEvent::SharedDeath,
                context,
                true,
                false,
                nullptr);
    }
}

void BotGameplayTracker::TryRecordGroupWipe(
    Group* group,
    Player* deadPlayer,
    BotGameplayContext const& context)
{
    if (!_trackGroupWipes || !group || !deadPlayer)
        return;

    std::vector<Player*> bots;
    std::vector<Player*> players;
    uint32 total = 0;
    uint32 dead = 0;

    for (GroupReference* itr = group->GetFirstMember();
        itr != nullptr;
        itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || !member->IsInMap(deadPlayer))
            continue;

        if (!member->IsWithinDist(deadPlayer, _participationDistance))
            continue;

        ++total;
        if (!member->IsAlive())
            ++dead;

        if (sBotPersonalityMgr.IsPlayerbot(member))
            bots.push_back(member);
        else if (IsRealPlayer(member))
            players.push_back(member);
    }

    if (total < _minimumWipeMembers || dead < total ||
        bots.empty() || players.empty())
        return;

    for (Player* bot : bots)
        for (Player* player : players)
            ApplyGameplayEvent(
                bot,
                player,
                BotGameplayEvent::GroupWipe,
                context,
                true,
                false,
                nullptr);
}

void BotGameplayTracker::RecordGroupMemberAdded(
    Group* group,
    ObjectGuid guid)
{
    if (!_enabled || !_trackSustainedTeamwork || !group || guid.IsEmpty())
        return;

    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (!player || !IsMapAllowed(player->GetMap()))
        return;

    uint32 const nowSeconds = CurrentGameTimeSeconds();
    if (sBotPersonalityMgr.IsPlayerbot(player))
    {
        for (GroupReference* itr = group->GetFirstMember();
            itr != nullptr;
            itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (IsRealPlayer(member) && player->IsInMap(member))
                TouchTeamwork(player, member, nowSeconds);
        }
    }
    else if (IsRealPlayer(player))
    {
        for (GroupReference* itr = group->GetFirstMember();
            itr != nullptr;
            itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (sBotPersonalityMgr.IsPlayerbot(member) &&
                player->IsInMap(member))
                TouchTeamwork(member, player, nowSeconds);
        }
    }
}

void BotGameplayTracker::RecordGroupMemberRemoved(
    Group* group,
    ObjectGuid guid,
    RemoveMethod method)
{
    if (!_enabled || !_trackGroupLeaves || !group || guid.IsEmpty())
        return;

    if (method != GROUP_REMOVEMETHOD_LEAVE)
        return;

    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (!IsRealPlayer(player) || !IsMapAllowed(player->GetMap()))
        return;

    bool combatRelevant = player->IsInCombat();
    for (GroupReference* itr = group->GetFirstMember();
        itr != nullptr && !combatRelevant;
        itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (member && member->IsInCombat() && member->IsInMap(player))
            combatRelevant = true;
    }

    if (!combatRelevant)
        return;

    BotGameplayContext context = MakeContext(player->GetMap(), 0, 1);
    for (GroupReference* itr = group->GetFirstMember();
        itr != nullptr;
        itr = itr->next())
    {
        Player* bot = itr->GetSource();
        if (!sBotPersonalityMgr.IsPlayerbot(bot) || !bot->IsInMap(player))
            continue;

        ApplyGameplayEvent(
            bot,
            player,
            BotGameplayEvent::PlayerLeftGroupDuringCombat,
            context,
            true,
            false,
            nullptr);
    }
}

void BotGameplayTracker::RecordEncounterCompleted(
    Map* map,
    Unit* source,
    uint32 creditEntry,
    uint32 dungeonCompleted,
    bool updated)
{
    if (!_enabled || !_trackEncounterCompletion || !updated ||
        !IsMapAllowed(map))
        return;

    BotGameplayEvent event = BotGameplayEvent::DungeonCompleted;
    if (map->IsRaid())
        event = BotGameplayEvent::RaidEncounterCompleted;
    else if (map->IsDungeon() && dungeonCompleted)
        event = BotGameplayEvent::DungeonCompleted;
    else
        return;

    uint32 const sourceEntry = source ? source->GetEntry() : creditEntry;
    BotGameplayContext context = MakeContext(map, sourceEntry, 1);
    std::vector<Player*> bots;
    std::vector<Player*> players;

    Map::PlayerList const& mapPlayers = map->GetPlayers();
    for (Map::PlayerList::const_iterator itr = mapPlayers.begin();
        itr != mapPlayers.end();
        ++itr)
    {
        Player* member = itr->GetSource();
        if (!member || !member->GetGroup())
            continue;

        if (sBotPersonalityMgr.IsPlayerbot(member))
            bots.push_back(member);
        else if (IsRealPlayer(member))
            players.push_back(member);
    }

    for (Player* bot : bots)
    {
        for (Player* player : players)
        {
            if (!SameGroup(bot, player))
                continue;

            ApplyGameplayEvent(
                bot,
                player,
                event,
                context,
                true,
                false,
                nullptr);
        }
    }
}

void BotGameplayTracker::CheckTeamwork(uint32 nowSeconds)
{
    if (!_trackSustainedTeamwork)
        return;

    struct DuePair
    {
        Player* bot = nullptr;
        Player* player = nullptr;
        BotGameplayContext context;
        BotPlayerRelationshipKey key;
    };

    std::vector<DuePair> duePairs;

    for (auto itr = _teamworkStates.begin();
        itr != _teamworkStates.end();)
    {
        BotPlayerRelationshipKey const key = itr->first;
        TeamworkState& state = itr->second;
        ObjectGuid const botGuid =
            ObjectGuid::Create<HighGuid::Player>(key.botGuid);
        ObjectGuid const playerGuid =
            ObjectGuid::Create<HighGuid::Player>(key.playerGuid);

        Player* bot = ObjectAccessor::FindConnectedPlayer(botGuid);
        Player* player = ObjectAccessor::FindConnectedPlayer(playerGuid);
        if (!IsValidPair(bot, player) || !SameGroup(bot, player) ||
            !bot->IsInMap(player))
        {
            itr = _teamworkStates.erase(itr);
            continue;
        }

        state.lastSeenSeconds = nowSeconds;
        if (!state.startedSeconds)
            state.startedSeconds = nowSeconds;

        if (nowSeconds >= state.startedSeconds &&
            nowSeconds - state.startedSeconds >= _sustainedTeamworkSeconds)
        {
            DuePair pair;
            pair.bot = bot;
            pair.player = player;
            pair.context = MakeContext(bot->GetMap(), 0, 1);
            pair.key = key;
            duePairs.push_back(pair);
            state.startedSeconds = nowSeconds;
        }

        ++itr;
    }

    for (DuePair const& pair : duePairs)
    {
        ApplyGameplayEvent(
            pair.bot,
            pair.player,
            BotGameplayEvent::SustainedTeamwork,
            pair.context,
            true,
            false,
            nullptr);
    }
}

void BotGameplayTracker::Cleanup(uint32 nowMs, uint32 nowSeconds)
{
    if (_lastCleanupMs &&
        getMSTimeDiff(_lastCleanupMs, nowMs) < CLEANUP_INTERVAL_MS)
        return;

    _lastCleanupMs = nowMs;

    for (auto itr = _cooldowns.begin(); itr != _cooldowns.end();)
    {
        if (static_cast<int32>(itr->second - nowMs) <= 0)
            itr = _cooldowns.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _normalKillAccumulators.begin();
        itr != _normalKillAccumulators.end();)
    {
        if (!itr->second.lastMs ||
            getMSTimeDiff(itr->second.lastMs, nowMs) > _normalKillWindowMs)
            itr = _normalKillAccumulators.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _healAccumulators.begin();
        itr != _healAccumulators.end();)
    {
        if (!itr->second.lastMs ||
            getMSTimeDiff(itr->second.lastMs, nowMs) > _healWindowMs)
            itr = _healAccumulators.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _recentEvents.begin(); itr != _recentEvents.end();)
    {
        std::deque<BotGameplayRecentEvent>& events = itr->second;
        while (!events.empty() &&
            (nowSeconds < events.front().timestamp ||
                nowSeconds - events.front().timestamp >
                    RECENT_EVENT_EXPIRY_SECONDS))
            events.pop_front();

        if (events.empty())
            itr = _recentEvents.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _deathWindows.begin(); itr != _deathWindows.end();)
    {
        DeathWindow& window = itr->second;
        auto prune = [this, nowMs](std::deque<uint32>& deaths)
        {
            while (!deaths.empty() &&
                getMSTimeDiff(deaths.front(), nowMs) > _deathWindowMs)
                deaths.pop_front();
        };

        prune(window.playerDeaths);
        prune(window.botDeaths);

        if (window.playerDeaths.empty() && window.botDeaths.empty() &&
            getMSTimeDiff(window.lastAccessMs, nowMs) > _deathWindowMs)
            itr = _deathWindows.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _capRecords.begin(); itr != _capRecords.end();)
    {
        std::deque<GameplayCapRecord>& records = itr->second;
        while (!records.empty() &&
            (nowSeconds < records.front().timestamp ||
                nowSeconds - records.front().timestamp >= HOUR_SECONDS))
            records.pop_front();

        if (records.empty())
            itr = _capRecords.erase(itr);
        else
            ++itr;
    }

    while (_recentEvents.size() > MAX_TRACKED_ENTRIES)
        _recentEvents.erase(_recentEvents.begin());
    while (_cooldowns.size() > MAX_TRACKED_ENTRIES)
        _cooldowns.erase(_cooldowns.begin());
    while (_teamworkStates.size() > MAX_TRACKED_ENTRIES)
        _teamworkStates.erase(_teamworkStates.begin());
}
