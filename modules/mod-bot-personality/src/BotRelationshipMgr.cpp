#include "BotRelationshipMgr.h"

#include "BotPersonalityMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"
#include "QueryResult.h"
#include "Timer.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>

namespace
{
constexpr int32 DEFAULT_RELATIONSHIP_MINIMUM = -1000;
constexpr int32 DEFAULT_RELATIONSHIP_MAXIMUM = 1000;
constexpr uint32 MIN_CACHE_ENTRIES = 100;
constexpr uint32 MAX_CACHE_ENTRIES = 100000;
constexpr uint32 MIN_CACHE_EXPIRY_MINUTES = 1;
constexpr uint32 MAX_CACHE_EXPIRY_MINUTES = 1440;
constexpr uint32 MIN_SAVE_INTERVAL_SECONDS = 5;
constexpr uint32 MAX_SAVE_INTERVAL_SECONDS = 3600;
constexpr uint32 CLEANUP_INTERVAL_MS = 60000;
constexpr uint32 DAY_SECONDS = 24 * 60 * 60;

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
            "module.botpersonality.relationship",
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

bool HasAnyDelta(BotRelationshipDelta const& delta)
{
    return delta.affinity != 0 ||
        delta.trust != 0 ||
        delta.respect != 0 ||
        delta.familiarity != 0;
}

uint32 CurrentGameTimeSeconds()
{
    return static_cast<uint32>(GameTime::GetGameTime().count());
}
}

void BotRelationshipMgr::LoadConfig(bool reload)
{
    _enabled = sConfigMgr->GetOption<bool>(
        "BotPersonality.Relationship.Enable",
        true);
    _updateFromChat = sConfigMgr->GetOption<bool>(
        "BotPersonality.Relationship.UpdateFromChat",
        true);
    _debugLogging = sConfigMgr->GetOption<bool>(
        "BotPersonality.Relationship.DebugLogging",
        false);

    int32 const configuredMinimum = sConfigMgr->GetOption<int32>(
        "BotPersonality.Relationship.Minimum",
        DEFAULT_RELATIONSHIP_MINIMUM);
    int32 const configuredMaximum = sConfigMgr->GetOption<int32>(
        "BotPersonality.Relationship.Maximum",
        DEFAULT_RELATIONSHIP_MAXIMUM);

    if (configuredMinimum >= configuredMaximum)
    {
        _minimum = DEFAULT_RELATIONSHIP_MINIMUM;
        _maximum = DEFAULT_RELATIONSHIP_MAXIMUM;

        LOG_WARN(
            "module.botpersonality.relationship",
            "Invalid relationship range {}..{}, restored defaults {}..{}",
            configuredMinimum,
            configuredMaximum,
            _minimum,
            _maximum);
    }
    else
    {
        _minimum = std::max(configuredMinimum, -30000);
        _maximum = std::min(configuredMaximum, 30000);
    }

    uint32 const saveIntervalSeconds = ReadUIntConfig(
        "BotPersonality.Relationship.SaveIntervalSeconds",
        60,
        MIN_SAVE_INTERVAL_SECONDS,
        MAX_SAVE_INTERVAL_SECONDS,
        _debugLogging);
    _saveIntervalMs = saveIntervalSeconds * 1000;

    uint32 const expiryMinutes = ReadUIntConfig(
        "BotPersonality.Relationship.CacheExpiryMinutes",
        30,
        MIN_CACHE_EXPIRY_MINUTES,
        MAX_CACHE_EXPIRY_MINUTES,
        _debugLogging);
    _cacheExpiryMs = expiryMinutes * 60 * 1000;

    _maxCachedEntries = ReadUIntConfig(
        "BotPersonality.Relationship.MaxCachedEntries",
        10000,
        MIN_CACHE_ENTRIES,
        MAX_CACHE_ENTRIES,
        _debugLogging);

    uint32 const repeatWindowSeconds = ReadUIntConfig(
        "BotPersonality.Relationship.RepeatWindowSeconds",
        600,
        0,
        86400,
        _debugLogging);
    _repeatWindowMs = repeatWindowSeconds * 1000;

    _secondRepeatPercent = ReadUIntConfig(
        "BotPersonality.Relationship.SecondRepeatPercent",
        50,
        0,
        100,
        _debugLogging);
    _thirdRepeatPercent = ReadUIntConfig(
        "BotPersonality.Relationship.ThirdRepeatPercent",
        25,
        0,
        100,
        _debugLogging);
    _furtherRepeatPercent = ReadUIntConfig(
        "BotPersonality.Relationship.FurtherRepeatPercent",
        0,
        0,
        100,
        _debugLogging);

    _maxPositiveChatGainPerDay = ReadUIntConfig(
        "BotPersonality.Relationship.MaxPositiveChatGainPerDay",
        20,
        0,
        10000,
        _debugLogging);
    _maxNegativeChatLossPerDay = ReadUIntConfig(
        "BotPersonality.Relationship.MaxNegativeChatLossPerDay",
        40,
        0,
        10000,
        _debugLogging);
    _maxFamiliarityGainPerDay = ReadUIntConfig(
        "BotPersonality.Relationship.MaxFamiliarityGainPerDay",
        30,
        0,
        10000,
        _debugLogging);

    _greetingDelta.affinity = ReadIntConfig(
        "BotPersonality.Relationship.GreetingAffinity",
        1,
        -100,
        100,
        _debugLogging);
    _greetingDelta.familiarity = ReadIntConfig(
        "BotPersonality.Relationship.GreetingFamiliarity",
        2,
        -100,
        100,
        _debugLogging);

    _thanksDelta.affinity = ReadIntConfig(
        "BotPersonality.Relationship.ThanksAffinity",
        2,
        -100,
        100,
        _debugLogging);
    _thanksDelta.trust = ReadIntConfig(
        "BotPersonality.Relationship.ThanksTrust",
        1,
        -100,
        100,
        _debugLogging);
    _thanksDelta.familiarity = ReadIntConfig(
        "BotPersonality.Relationship.ThanksFamiliarity",
        1,
        -100,
        100,
        _debugLogging);

    _praiseDelta.affinity = ReadIntConfig(
        "BotPersonality.Relationship.PraiseAffinity",
        2,
        -100,
        100,
        _debugLogging);
    _praiseDelta.respect = ReadIntConfig(
        "BotPersonality.Relationship.PraiseRespect",
        1,
        -100,
        100,
        _debugLogging);
    _praiseDelta.familiarity = ReadIntConfig(
        "BotPersonality.Relationship.PraiseFamiliarity",
        1,
        -100,
        100,
        _debugLogging);

    _apologyDelta.affinity = ReadIntConfig(
        "BotPersonality.Relationship.ApologyAffinity",
        1,
        -100,
        100,
        _debugLogging);
    _apologyDelta.trust = ReadIntConfig(
        "BotPersonality.Relationship.ApologyTrust",
        2,
        -100,
        100,
        _debugLogging);
    _apologyDelta.familiarity = ReadIntConfig(
        "BotPersonality.Relationship.ApologyFamiliarity",
        1,
        -100,
        100,
        _debugLogging);

    _insultDelta.affinity = ReadIntConfig(
        "BotPersonality.Relationship.InsultAffinity",
        -5,
        -100,
        100,
        _debugLogging);
    _insultDelta.trust = ReadIntConfig(
        "BotPersonality.Relationship.InsultTrust",
        -2,
        -100,
        100,
        _debugLogging);
    _insultDelta.respect = ReadIntConfig(
        "BotPersonality.Relationship.InsultRespect",
        -1,
        -100,
        100,
        _debugLogging);
    _insultDelta.familiarity = ReadIntConfig(
        "BotPersonality.Relationship.InsultFamiliarity",
        1,
        -100,
        100,
        _debugLogging);

    _helpDelta.familiarity = ReadIntConfig(
        "BotPersonality.Relationship.HelpFamiliarity",
        1,
        -100,
        100,
        _debugLogging);
    _conversationDelta.familiarity = ReadIntConfig(
        "BotPersonality.Relationship.ConversationFamiliarity",
        1,
        -100,
        100,
        _debugLogging);

    _spamDelta.affinity = ReadIntConfig(
        "BotPersonality.Relationship.SpamAffinity",
        -2,
        -100,
        100,
        _debugLogging);
    _spamDelta.trust = ReadIntConfig(
        "BotPersonality.Relationship.SpamTrust",
        -1,
        -100,
        100,
        _debugLogging);
    _spamDelta.respect = ReadIntConfig(
        "BotPersonality.Relationship.SpamRespect",
        -1,
        -100,
        100,
        _debugLogging);

    for (auto& pair : _cache)
    {
        if (ValidateLoadedRelationship(pair.second.relationship))
            MarkDirty(pair.second, getMSTime());
    }

    if (!_enabled)
        ClearCache();

    LOG_INFO(
        "server.loading",
        "Bot Personality relationships {}{}",
        _enabled ? "enabled" : "disabled",
        reload ? " after config reload" : "");
}

void BotRelationshipMgr::Update(uint32 /*diff*/)
{
    if (!_enabled)
        return;

    uint32 const nowMs = getMSTime();
    if (!_lastSaveMs)
        _lastSaveMs = nowMs;

    if (getMSTimeDiff(_lastSaveMs, nowMs) >= _saveIntervalMs)
    {
        SaveDirtyRelationships();
        _lastSaveMs = nowMs;
    }

    Cleanup(nowMs);
}

void BotRelationshipMgr::OnShutdown()
{
    SaveDirtyRelationships();
}

BotRelationship const* BotRelationshipMgr::GetCachedRelationship(
    uint32 botGuid,
    uint32 playerGuid) const
{
    auto const itr = _cache.find(MakeKey(botGuid, playerGuid));
    if (itr == _cache.end())
        return nullptr;

    return &itr->second.relationship;
}

bool BotRelationshipMgr::GetExistingRelationship(
    Player const* bot,
    Player const* player,
    BotRelationship& relationship)
{
    if (!_enabled || !IsValidPair(bot, player, true))
        return false;

    CachedBotRelationship* entry = GetExistingEntry(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter());
    if (!entry)
        return false;

    relationship = entry->relationship;
    return true;
}

BotRelationship* BotRelationshipMgr::GetOrCreateRelationship(
    Player const* bot,
    Player const* player)
{
    if (!_enabled || !IsValidPair(bot, player, true))
        return nullptr;

    CachedBotRelationship* entry = GetOrCreateEntry(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter(),
        CurrentGameTimeSeconds());
    if (!entry)
        return nullptr;

    return &entry->relationship;
}

bool BotRelationshipMgr::ApplyEvent(
    Player const* bot,
    Player const* player,
    BotRelationshipEvent event)
{
    if (!_enabled || !_updateFromChat || !IsValidPair(bot, player, true))
        return false;

    BotPersonality const* personality =
        sBotPersonalityMgr.GetOrCreatePersonality(const_cast<Player*>(bot));
    if (!personality)
        return false;

    uint32 const nowMs = getMSTime();
    uint32 const nowSeconds = CurrentGameTimeSeconds();
    BotPlayerRelationshipKey const key = MakeKey(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter());

    CachedBotRelationship* entry = GetOrCreateEntry(
        key.botGuid,
        key.playerGuid,
        nowSeconds);
    if (!entry)
        return false;

    BotRelationshipDelta baseDelta = GetBaseDelta(event);
    BotRelationshipDelta adjustedDelta = CalculateRelationshipDelta(
        *personality,
        event,
        baseDelta);

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.relationship",
            "Event {} base delta a:{} t:{} r:{} f:{}",
            BotRelationshipEventToString(event),
            baseDelta.affinity,
            baseDelta.trust,
            baseDelta.respect,
            baseDelta.familiarity);
        LOG_DEBUG(
            "module.botpersonality.relationship",
            "Event {} personality delta a:{} t:{} r:{} f:{}",
            BotRelationshipEventToString(event),
            adjustedDelta.affinity,
            adjustedDelta.trust,
            adjustedDelta.respect,
            adjustedDelta.familiarity);
    }

    ApplyDiminishingReturns(key, event, adjustedDelta, nowMs);
    ApplyChatCaps(key, adjustedDelta, nowSeconds);
    RecordRecentEvent(key, event, nowMs);
    RecordChatCaps(key, adjustedDelta, nowSeconds);

    if (!HasAnyDelta(adjustedDelta) &&
        entry->relationship.firstInteraction != 0)
        return true;

    BotRelationshipLevel const oldLevel = GetRelationshipLevel(
        entry->relationship.affinity);
    ApplyDelta(*entry, adjustedDelta, nowSeconds);
    BotRelationshipLevel const newLevel = GetRelationshipLevel(
        entry->relationship.affinity);

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.relationship",
            "Applied delta bot {} player {} a:{} t:{} r:{} f:{}",
            key.botGuid,
            key.playerGuid,
            adjustedDelta.affinity,
            adjustedDelta.trust,
            adjustedDelta.respect,
            adjustedDelta.familiarity);
    }

    if (oldLevel != newLevel)
    {
        if (_debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.relationship",
                "Relationship level bot {} player {} changed {} -> {}",
                key.botGuid,
                key.playerGuid,
                RelationshipLevelToString(oldLevel),
                RelationshipLevelToString(newLevel));
        }

        SaveRelationship(*entry);
    }

    return true;
}

bool BotRelationshipMgr::PreviewEventDelta(
    Player const* bot,
    BotRelationshipEvent event,
    BotRelationshipDelta& baseDelta,
    BotRelationshipDelta& adjustedDelta) const
{
    if (!_enabled || !bot || !sBotPersonalityMgr.IsPlayerbot(bot))
        return false;

    BotPersonality const* personality =
        sBotPersonalityMgr.GetOrCreatePersonality(const_cast<Player*>(bot));
    if (!personality)
        return false;

    baseDelta = GetBaseDelta(event);
    adjustedDelta = CalculateRelationshipDelta(
        *personality,
        event,
        baseDelta);
    return true;
}

bool BotRelationshipMgr::SetValue(
    Player const* bot,
    Player const* player,
    std::string const& field,
    int32 value)
{
    if (!_enabled || !IsValidPair(bot, player, true))
        return false;

    CachedBotRelationship* entry = GetOrCreateEntry(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter(),
        CurrentGameTimeSeconds());
    if (!entry)
        return false;

    if (!SetRelationshipField(entry->relationship, field, value, nullptr))
        return false;

    MarkDirty(*entry, getMSTime());
    SaveRelationship(*entry);
    return true;
}

bool BotRelationshipMgr::AdjustValue(
    Player const* bot,
    Player const* player,
    std::string const& field,
    int32 amount,
    int32& oldValue,
    int32& newValue)
{
    if (!_enabled || !IsValidPair(bot, player, true))
        return false;

    CachedBotRelationship* entry = GetOrCreateEntry(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter(),
        CurrentGameTimeSeconds());
    if (!entry)
        return false;

    std::string const normalized = BotPersonalityToLower(field);
    if (normalized == "affinity")
        oldValue = static_cast<int32>(entry->relationship.affinity);
    else if (normalized == "trust")
        oldValue = static_cast<int32>(entry->relationship.trust);
    else if (normalized == "respect")
        oldValue = static_cast<int32>(entry->relationship.respect);
    else if (normalized == "familiarity")
        oldValue = static_cast<int32>(entry->relationship.familiarity);
    else
        return false;

    int32 const clampedValue = ClampValue(oldValue + amount);
    SetRelationshipField(
        entry->relationship,
        field,
        clampedValue,
        nullptr);
    newValue = clampedValue;

    MarkDirty(*entry, getMSTime());
    SaveRelationship(*entry);
    return true;
}

bool BotRelationshipMgr::ResetRelationship(
    Player const* bot,
    Player const* player)
{
    if (!_enabled || !IsValidPair(bot, player, true))
        return false;

    uint32 const botGuid = bot->GetGUID().GetCounter();
    uint32 const playerGuid = player->GetGUID().GetCounter();
    BotPlayerRelationshipKey const key = MakeKey(botGuid, playerGuid);

    DeleteRelationship(botGuid, playerGuid);
    _cache.erase(key);
    _transientState.erase(key);

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.relationship",
            "Reset relationship bot {} player {}",
            botGuid,
            playerGuid);
    }

    return true;
}

bool BotRelationshipMgr::ReloadRelationship(
    Player const* bot,
    Player const* player)
{
    if (!_enabled || !IsValidPair(bot, player, true))
        return false;

    uint32 const botGuid = bot->GetGUID().GetCounter();
    uint32 const playerGuid = player->GetGUID().GetCounter();
    BotPlayerRelationshipKey const key = MakeKey(botGuid, playerGuid);

    _cache.erase(key);

    CachedBotRelationship* entry = GetExistingEntry(botGuid, playerGuid);
    return entry != nullptr;
}

BotRelationshipSaveStats BotRelationshipMgr::SaveDirtyRelationships()
{
    BotRelationshipSaveStats stats;

    for (auto& pair : _cache)
    {
        CachedBotRelationship& entry = pair.second;
        if (!entry.dirty)
            continue;

        SaveRelationship(entry);
        ++stats.saved;
    }

    stats.remainingDirty = GetDirtyCount();
    return stats;
}

BotRelationshipSaveStats BotRelationshipMgr::SaveRelationshipsForPlayer(
    ObjectGuid const& guid)
{
    BotRelationshipSaveStats stats;
    if (guid.IsEmpty())
        return stats;

    uint32 const lowGuid = guid.GetCounter();
    for (auto& pair : _cache)
    {
        CachedBotRelationship& entry = pair.second;
        if (!entry.dirty)
            continue;

        if (entry.relationship.botGuid != lowGuid &&
            entry.relationship.playerGuid != lowGuid)
            continue;

        SaveRelationship(entry);
        ++stats.saved;
    }

    stats.remainingDirty = GetDirtyCount();
    return stats;
}

void BotRelationshipMgr::ClearCache()
{
    SaveDirtyRelationships();
    _cache.clear();
    _transientState.clear();
}

std::vector<BotRelationshipListEntry> BotRelationshipMgr::ListRelationships(
    uint32 botGuid,
    uint32 limit)
{
    std::vector<BotRelationshipListEntry> entries;
    if (!_enabled || !botGuid || !limit)
        return entries;

    QueryResult result = CharacterDatabase.Query(
        "SELECT `bot_guid`, `player_guid`, `affinity`, `trust`, `respect`, "
        "`familiarity`, `positive_interactions`, `negative_interactions`, "
        "`first_interaction`, `last_interaction` "
        "FROM `bot_relationship` WHERE `bot_guid` = {} "
        "ORDER BY `affinity` DESC, `last_interaction` DESC LIMIT {}",
        botGuid,
        limit);

    if (!result)
        return entries;

    do
    {
        Field* fields = result->Fetch();
        BotRelationship relationship;
        relationship.botGuid = fields[0].Get<uint32>();
        relationship.playerGuid = fields[1].Get<uint32>();
        relationship.affinity = ClampStorageValue(fields[2].Get<int32>());
        relationship.trust = ClampStorageValue(fields[3].Get<int32>());
        relationship.respect = ClampStorageValue(fields[4].Get<int32>());
        relationship.familiarity = ClampStorageValue(fields[5].Get<int32>());
        relationship.positiveInteractions = fields[6].Get<uint32>();
        relationship.negativeInteractions = fields[7].Get<uint32>();
        relationship.firstInteraction = fields[8].Get<uint32>();
        relationship.lastInteraction = fields[9].Get<uint32>();

        if (relationship.botGuid != botGuid || !relationship.playerGuid)
            continue;

        BotRelationshipListEntry entry;
        entry.playerGuid = relationship.playerGuid;
        entry.relationship = relationship;
        entries.push_back(entry);
    }
    while (result->NextRow());

    return entries;
}

uint32 BotRelationshipMgr::GetDirtyCount() const
{
    uint32 count = 0;
    for (auto const& pair : _cache)
    {
        if (pair.second.dirty)
            ++count;
    }

    return count;
}

BotRelationshipDelta BotRelationshipMgr::GetBaseDelta(
    BotRelationshipEvent event) const
{
    switch (event)
    {
        case BotRelationshipEvent::Greeting:
            return _greetingDelta;
        case BotRelationshipEvent::Thanks:
            return _thanksDelta;
        case BotRelationshipEvent::Praise:
            return _praiseDelta;
        case BotRelationshipEvent::Apology:
            return _apologyDelta;
        case BotRelationshipEvent::Insult:
            return _insultDelta;
        case BotRelationshipEvent::HelpRequest:
            return _helpDelta;
        case BotRelationshipEvent::RepeatedSpam:
            return _spamDelta;
        case BotRelationshipEvent::Conversation:
            return _conversationDelta;
    }

    return _conversationDelta;
}

int32 BotRelationshipMgr::ClampValue(int32 value) const
{
    return std::clamp(value, _minimum, _maximum);
}

bool BotRelationshipMgr::IsValidPair(
    Player const* bot,
    Player const* player,
    bool logFailure) const
{
    if (!bot || !player || !bot->GetSession() || !player->GetSession())
    {
        if (logFailure && _debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.relationship",
                "Rejected invalid relationship pair with missing player");
        }

        return false;
    }

    if (!sBotPersonalityMgr.IsPlayerbot(bot))
    {
        if (logFailure && _debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.relationship",
                "Rejected relationship bot {} because target is not a bot",
                bot->GetGUID().GetCounter());
        }

        return false;
    }

    if (sBotPersonalityMgr.IsPlayerbot(player) ||
        player->GetSession()->IsBot())
    {
        if (logFailure && _debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.relationship",
                "Rejected relationship player {} because target is a bot",
                player->GetGUID().GetCounter());
        }

        return false;
    }

    if (bot->GetGUID() == player->GetGUID())
        return false;

    return true;
}

BotPlayerRelationshipKey BotRelationshipMgr::MakeKey(
    uint32 botGuid,
    uint32 playerGuid) const
{
    return { botGuid, playerGuid };
}

BotRelationshipMgr::CachedBotRelationship*
BotRelationshipMgr::GetExistingEntry(uint32 botGuid, uint32 playerGuid)
{
    BotPlayerRelationshipKey const key = MakeKey(botGuid, playerGuid);
    uint32 const nowMs = getMSTime();

    auto itr = _cache.find(key);
    if (itr != _cache.end())
    {
        itr->second.lastAccessTime = nowMs;

        if (_debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.relationship",
                "Relationship cache hit bot {} player {}",
                botGuid,
                playerGuid);
        }

        return &itr->second;
    }

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.relationship",
            "Relationship cache miss bot {} player {}",
            botGuid,
            playerGuid);
    }

    BotRelationship relationship;
    bool corrected = false;
    LoadResult const result = LoadRelationship(
        botGuid,
        playerGuid,
        relationship,
        corrected);
    if (result != LoadResult::Loaded)
        return nullptr;

    CachedBotRelationship& entry = _cache[key];
    entry.relationship = relationship;
    entry.lastAccessTime = nowMs;
    entry.lastSaveTime = nowMs;

    if (corrected)
    {
        entry.dirty = true;
        SaveRelationship(entry);
    }

    return &entry;
}

BotRelationshipMgr::CachedBotRelationship*
BotRelationshipMgr::GetOrCreateEntry(
    uint32 botGuid,
    uint32 playerGuid,
    uint32 nowSeconds)
{
    if (CachedBotRelationship* entry = GetExistingEntry(botGuid, playerGuid))
        return entry;

    BotPlayerRelationshipKey const key = MakeKey(botGuid, playerGuid);
    uint32 const nowMs = getMSTime();

    CachedBotRelationship& entry = _cache[key];
    entry.relationship = CreateNeutralRelationship(
        botGuid,
        playerGuid,
        nowSeconds);
    entry.dirty = true;
    entry.lastAccessTime = nowMs;
    entry.lastSaveTime = 0;

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.relationship",
            "Created neutral relationship bot {} player {}",
            botGuid,
            playerGuid);
    }

    return &entry;
}

BotRelationshipMgr::LoadResult BotRelationshipMgr::LoadRelationship(
    uint32 botGuid,
    uint32 playerGuid,
    BotRelationship& relationship,
    bool& corrected)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT `bot_guid`, `player_guid`, `affinity`, `trust`, `respect`, "
        "`familiarity`, `positive_interactions`, `negative_interactions`, "
        "`first_interaction`, `last_interaction` "
        "FROM `bot_relationship` WHERE `bot_guid` = {} "
        "AND `player_guid` = {} LIMIT 2",
        botGuid,
        playerGuid);

    if (!result)
        return LoadResult::NotFound;

    if (result->GetRowCount() > 1)
    {
        LOG_ERROR(
            "module.botpersonality.relationship",
            "Duplicate relationship rows found for bot {} player {}",
            botGuid,
            playerGuid);
    }

    Field* fields = result->Fetch();
    relationship.botGuid = fields[0].Get<uint32>();
    relationship.playerGuid = fields[1].Get<uint32>();
    relationship.affinity = ClampStorageValue(fields[2].Get<int32>());
    relationship.trust = ClampStorageValue(fields[3].Get<int32>());
    relationship.respect = ClampStorageValue(fields[4].Get<int32>());
    relationship.familiarity = ClampStorageValue(fields[5].Get<int32>());
    relationship.positiveInteractions = fields[6].Get<uint32>();
    relationship.negativeInteractions = fields[7].Get<uint32>();
    relationship.firstInteraction = fields[8].Get<uint32>();
    relationship.lastInteraction = fields[9].Get<uint32>();

    if (relationship.botGuid != botGuid ||
        relationship.playerGuid != playerGuid)
    {
        corrected = true;
        relationship.botGuid = botGuid;
        relationship.playerGuid = playerGuid;
    }

    if (ValidateLoadedRelationship(relationship))
        corrected = true;

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.relationship",
            "Loaded relationship bot {} player {}",
            botGuid,
            playerGuid);
    }

    return LoadResult::Loaded;
}

void BotRelationshipMgr::SaveRelationship(CachedBotRelationship& entry)
{
    BotRelationship const& relationship = entry.relationship;

    CharacterDatabase.DirectExecute(
        "REPLACE INTO `bot_relationship` "
        "(`bot_guid`, `player_guid`, `affinity`, `trust`, `respect`, "
        "`familiarity`, `positive_interactions`, `negative_interactions`, "
        "`first_interaction`, `last_interaction`) VALUES "
        "({}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
        relationship.botGuid,
        relationship.playerGuid,
        static_cast<int32>(relationship.affinity),
        static_cast<int32>(relationship.trust),
        static_cast<int32>(relationship.respect),
        static_cast<int32>(relationship.familiarity),
        relationship.positiveInteractions,
        relationship.negativeInteractions,
        relationship.firstInteraction,
        relationship.lastInteraction);

    entry.dirty = false;
    entry.lastSaveTime = getMSTime();

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.relationship",
            "Saved relationship bot {} player {}",
            relationship.botGuid,
            relationship.playerGuid);
    }
}

void BotRelationshipMgr::DeleteRelationship(uint32 botGuid, uint32 playerGuid)
{
    CharacterDatabase.DirectExecute(
        "DELETE FROM `bot_relationship` WHERE `bot_guid` = {} "
        "AND `player_guid` = {}",
        botGuid,
        playerGuid);
}

bool BotRelationshipMgr::ValidateLoadedRelationship(
    BotRelationship& relationship)
{
    bool changed = false;

    auto validateValue = [this, &changed](int16& value)
    {
        int16 const oldValue = value;
        value = ClampStorageValue(value);
        if (value != oldValue)
            changed = true;
    };

    validateValue(relationship.affinity);
    validateValue(relationship.trust);
    validateValue(relationship.respect);
    validateValue(relationship.familiarity);

    if (!relationship.botGuid || !relationship.playerGuid)
        changed = true;

    if (relationship.firstInteraction &&
        relationship.lastInteraction &&
        relationship.firstInteraction > relationship.lastInteraction)
    {
        relationship.firstInteraction = relationship.lastInteraction;
        changed = true;
    }

    return changed;
}

BotRelationship BotRelationshipMgr::CreateNeutralRelationship(
    uint32 botGuid,
    uint32 playerGuid,
    uint32 nowSeconds) const
{
    BotRelationship relationship;
    relationship.botGuid = botGuid;
    relationship.playerGuid = playerGuid;
    relationship.firstInteraction = nowSeconds;
    relationship.lastInteraction = nowSeconds;
    return relationship;
}

int16 BotRelationshipMgr::ClampStorageValue(int32 value) const
{
    return static_cast<int16>(ClampValue(value));
}

bool BotRelationshipMgr::SetRelationshipField(
    BotRelationship& relationship,
    std::string const& field,
    int32 value,
    int32* oldValue)
{
    std::string const normalized = BotPersonalityToLower(field);

    int16* target = nullptr;
    if (normalized == "affinity")
        target = &relationship.affinity;
    else if (normalized == "trust")
        target = &relationship.trust;
    else if (normalized == "respect")
        target = &relationship.respect;
    else if (normalized == "familiarity")
        target = &relationship.familiarity;
    else
        return false;

    if (oldValue)
        *oldValue = static_cast<int32>(*target);

    *target = ClampStorageValue(value);
    return true;
}

uint32 BotRelationshipMgr::GetRepeatPercent(
    BotPlayerRelationshipKey const& key,
    BotRelationshipEvent event,
    uint32 nowMs)
{
    if (_repeatWindowMs == 0)
        return 100;

    TransientRelationshipState& state = _transientState[key];
    state.lastAccessTime = nowMs;

    while (!state.recentEvents.empty() &&
        getMSTimeDiff(state.recentEvents.front().timestamp, nowMs) >
            _repeatWindowMs)
        state.recentEvents.pop_front();

    uint32 repeatCount = 0;
    for (RecentRelationshipEvent const& recent : state.recentEvents)
    {
        if (recent.event == event)
            ++repeatCount;
    }

    if (repeatCount == 0)
        return 100;
    if (repeatCount == 1)
        return _secondRepeatPercent;
    if (repeatCount == 2)
        return _thirdRepeatPercent;

    return _furtherRepeatPercent;
}

void BotRelationshipMgr::RecordRecentEvent(
    BotPlayerRelationshipKey const& key,
    BotRelationshipEvent event,
    uint32 nowMs)
{
    TransientRelationshipState& state = _transientState[key];
    state.lastAccessTime = nowMs;
    state.recentEvents.push_back({ event, nowMs });

    while (state.recentEvents.size() > 64)
        state.recentEvents.pop_front();
}

void BotRelationshipMgr::ApplyDiminishingReturns(
    BotPlayerRelationshipKey const& key,
    BotRelationshipEvent event,
    BotRelationshipDelta& delta,
    uint32 nowMs)
{
    uint32 const percent = GetRepeatPercent(key, event, nowMs);
    uint32 negativePercent = percent;

    if ((event == BotRelationshipEvent::Insult ||
            event == BotRelationshipEvent::RepeatedSpam) &&
        NegativeMagnitude(delta) > 0)
        negativePercent = std::max<uint32>(negativePercent, 50);

    ScalePositiveValues(delta, percent);
    ScaleNegativeValues(delta, negativePercent);

    if (delta.familiarity > 0)
        delta.familiarity = delta.familiarity * static_cast<int32>(percent) /
            100;

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.relationship",
            "Diminishing returns bot {} player {} event {} percent {}",
            key.botGuid,
            key.playerGuid,
            BotRelationshipEventToString(event),
            percent);
    }
}

void BotRelationshipMgr::ApplyChatCaps(
    BotPlayerRelationshipKey const& key,
    BotRelationshipDelta& delta,
    uint32 nowSeconds)
{
    TransientRelationshipState& state = _transientState[key];

    while (!state.capRecords.empty() &&
        (nowSeconds < state.capRecords.front().timestamp ||
            nowSeconds - state.capRecords.front().timestamp >= DAY_SECONDS))
        state.capRecords.pop_front();

    uint32 usedPositive = 0;
    uint32 usedNegative = 0;
    uint32 usedFamiliarity = 0;
    for (ChatCapRecord const& record : state.capRecords)
    {
        usedPositive += record.positive;
        usedNegative += record.negative;
        usedFamiliarity += record.familiarity;
    }

    int32 const positive = PositiveMagnitude(delta);
    if (positive > 0)
    {
        uint32 const remaining = usedPositive >= _maxPositiveChatGainPerDay ?
            0 :
            _maxPositiveChatGainPerDay - usedPositive;
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
        uint32 const remaining = usedNegative >= _maxNegativeChatLossPerDay ?
            0 :
            _maxNegativeChatLossPerDay - usedNegative;
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
            usedFamiliarity >= _maxFamiliarityGainPerDay ?
                0 :
                _maxFamiliarityGainPerDay - usedFamiliarity;
        if (static_cast<uint32>(delta.familiarity) > remaining)
            delta.familiarity = static_cast<int32>(remaining);
    }

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.relationship",
            "Applied chat caps bot {} player {} used +{} -{} fam {}",
            key.botGuid,
            key.playerGuid,
            usedPositive,
            usedNegative,
            usedFamiliarity);
    }
}

void BotRelationshipMgr::RecordChatCaps(
    BotPlayerRelationshipKey const& key,
    BotRelationshipDelta const& delta,
    uint32 nowSeconds)
{
    ChatCapRecord record;
    record.timestamp = nowSeconds;
    record.positive = static_cast<uint32>(PositiveMagnitude(delta));
    record.negative = static_cast<uint32>(NegativeMagnitude(delta));
    record.familiarity = delta.familiarity > 0 ?
        static_cast<uint32>(delta.familiarity) :
        0;

    if (!record.positive && !record.negative && !record.familiarity)
        return;

    TransientRelationshipState& state = _transientState[key];
    state.capRecords.push_back(record);

    while (state.capRecords.size() > 256)
        state.capRecords.pop_front();
}

void BotRelationshipMgr::ApplyDelta(
    CachedBotRelationship& entry,
    BotRelationshipDelta const& delta,
    uint32 nowSeconds)
{
    BotRelationship& relationship = entry.relationship;

    relationship.affinity = ClampStorageValue(
        static_cast<int32>(relationship.affinity) + delta.affinity);
    relationship.trust = ClampStorageValue(
        static_cast<int32>(relationship.trust) + delta.trust);
    relationship.respect = ClampStorageValue(
        static_cast<int32>(relationship.respect) + delta.respect);
    relationship.familiarity = ClampStorageValue(
        static_cast<int32>(relationship.familiarity) + delta.familiarity);

    if (PositiveMagnitude(delta) > 0 || delta.familiarity > 0)
        ++relationship.positiveInteractions;
    if (NegativeMagnitude(delta) > 0)
        ++relationship.negativeInteractions;

    if (!relationship.firstInteraction)
        relationship.firstInteraction = nowSeconds;

    relationship.lastInteraction = nowSeconds;
    MarkDirty(entry, getMSTime());
}

void BotRelationshipMgr::MarkDirty(
    CachedBotRelationship& entry,
    uint32 nowMs)
{
    entry.dirty = true;
    entry.lastAccessTime = nowMs;
}

void BotRelationshipMgr::Cleanup(uint32 nowMs)
{
    if (_lastCleanupMs &&
        getMSTimeDiff(_lastCleanupMs, nowMs) < CLEANUP_INTERVAL_MS)
        return;

    _lastCleanupMs = nowMs;

    for (auto itr = _cache.begin(); itr != _cache.end();)
    {
        CachedBotRelationship& entry = itr->second;
        if (getMSTimeDiff(entry.lastAccessTime, nowMs) < _cacheExpiryMs)
        {
            ++itr;
            continue;
        }

        if (entry.dirty)
            SaveRelationship(entry);

        if (_debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.relationship",
                "Evicted relationship bot {} player {}",
                entry.relationship.botGuid,
                entry.relationship.playerGuid);
        }

        _transientState.erase(itr->first);
        itr = _cache.erase(itr);
    }

    while (_cache.size() > _maxCachedEntries)
    {
        auto oldest = _cache.begin();
        for (auto itr = _cache.begin(); itr != _cache.end(); ++itr)
        {
            if (itr->second.lastAccessTime < oldest->second.lastAccessTime)
                oldest = itr;
        }

        if (oldest->second.dirty)
            SaveRelationship(oldest->second);

        _transientState.erase(oldest->first);
        _cache.erase(oldest);
    }

    for (auto itr = _transientState.begin(); itr != _transientState.end();)
    {
        if (_cache.find(itr->first) == _cache.end() &&
            getMSTimeDiff(itr->second.lastAccessTime, nowMs) >=
                _cacheExpiryMs)
            itr = _transientState.erase(itr);
        else
            ++itr;
    }
}
