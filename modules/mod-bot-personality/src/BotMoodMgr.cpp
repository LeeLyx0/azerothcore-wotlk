#include "BotMoodMgr.h"

#include "BotPersonalityMgr.h"
#include "CharacterCache.h"
#include "Config.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Timer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace
{
constexpr uint32 CLEANUP_INTERVAL_MS = 60000;
constexpr uint32 DEFAULT_DEDUPE_MS = 5000;
constexpr uint32 MAX_DEDUP_ENTRIES = 20000;

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
            "module.botpersonality.mood",
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

std::string NormalizeField(std::string text)
{
    text = BotPersonalityToLower(text);
    text.erase(
        std::remove_if(
            text.begin(),
            text.end(),
            [](unsigned char c)
            {
                return c == '_' || c == '-' || std::isspace(c);
            }),
        text.end());

    return text;
}

void MoveToward(int16& value, int16 baseline, int32 amount)
{
    if (amount <= 0 || value == baseline)
        return;

    if (value > baseline)
        value = static_cast<int16>(std::max<int32>(baseline, value - amount));
    else
        value = static_cast<int16>(std::min<int32>(baseline, value + amount));
}

bool IsValidOnlineBot(Player const* player)
{
    return player &&
        player->GetSession() &&
        player->IsInWorld() &&
        !player->IsDuringRemoveFromWorld() &&
        !player->IsBeingTeleported() &&
        sBotPersonalityMgr.IsPlayerbot(player);
}
}

void BotMoodMgr::LoadConfig(bool reload)
{
    _enabled = sConfigMgr->GetOption<bool>(
        "BotPersonality.Mood.Enable",
        true);
    _debugLogging = sConfigMgr->GetOption<bool>(
        "BotPersonality.Mood.DebugLogging",
        false);

    _minimum = ReadIntConfig(
        "BotPersonality.Mood.Minimum",
        0,
        0,
        100,
        _debugLogging);
    _maximum = ReadIntConfig(
        "BotPersonality.Mood.Maximum",
        100,
        1,
        100,
        _debugLogging);
    if (_minimum >= _maximum)
    {
        if (_debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.mood",
                "Mood min {} was not below max {}, using 0/100",
                _minimum,
                _maximum);
        }

        _minimum = 0;
        _maximum = 100;
    }

    _decayIntervalMs = ReadUIntConfig(
        "BotPersonality.Mood.DecayIntervalSeconds",
        30,
        1,
        3600,
        _debugLogging) * IN_MILLISECONDS;
    _happinessDecay = ReadIntConfig(
        "BotPersonality.Mood.HappinessDecay",
        1,
        0,
        100,
        _debugLogging);
    _frustrationDecay = ReadIntConfig(
        "BotPersonality.Mood.FrustrationDecay",
        2,
        0,
        100,
        _debugLogging);
    _confidenceDecay = ReadIntConfig(
        "BotPersonality.Mood.ConfidenceDecay",
        1,
        0,
        100,
        _debugLogging);
    _fearDecay = ReadIntConfig(
        "BotPersonality.Mood.FearDecay",
        2,
        0,
        100,
        _debugLogging);
    _excitementDecay = ReadIntConfig(
        "BotPersonality.Mood.ExcitementDecay",
        2,
        0,
        100,
        _debugLogging);
    _boredomDecay = ReadIntConfig(
        "BotPersonality.Mood.BoredomDecay",
        1,
        0,
        100,
        _debugLogging);

    _cacheExpiryMs = ReadUIntConfig(
        "BotPersonality.Mood.CacheExpiryMinutes",
        60,
        1,
        1440,
        _debugLogging) * 60 * IN_MILLISECONDS;
    _maxCachedBots = ReadUIntConfig(
        "BotPersonality.Mood.MaxCachedBots",
        5000,
        100,
        100000,
        _debugLogging);
    _decayBatchSize = ReadUIntConfig(
        "BotPersonality.Mood.DecayBatchSize",
        200,
        10,
        5000,
        _debugLogging);

    for (auto& pair : _cache)
        ClampMood(pair.second.mood);

    if (!_enabled)
        ClearCache();

    LOG_INFO(
        "server.loading",
        "Bot Personality Phase 5 mood {}{}",
        IsEnabled() ? "enabled" : "disabled",
        reload ? " after config reload" : "");
}

void BotMoodMgr::Update(uint32 /*diff*/)
{
    if (!IsEnabled())
        return;

    uint32 const nowMs = getMSTime();
    uint32 processed = 0;
    for (auto& pair : _cache)
    {
        if (processed >= _decayBatchSize)
            break;

        ObjectGuid const guid =
            ObjectGuid::Create<HighGuid::Player>(pair.first);
        Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
        if (!IsValidOnlineBot(bot))
            continue;

        DecayMood(bot, pair.second, nowMs);
        ++processed;
    }

    Cleanup(nowMs);
}

void BotMoodMgr::ClearCache()
{
    _cache.clear();
    _eventDedup.clear();
}

bool BotMoodMgr::IsEnabled() const
{
    return sBotPersonalityMgr.IsEnabled() && _enabled;
}

BotMood const* BotMoodMgr::GetMood(Player* bot)
{
    if (!IsEnabled())
        return nullptr;

    CachedBotMood* entry = GetOrCreateEntry(bot, getMSTime());
    return entry ? &entry->mood : nullptr;
}

BotMood const* BotMoodMgr::GetCachedMood(uint32 botGuid) const
{
    auto const itr = _cache.find(botGuid);
    if (itr == _cache.end())
        return nullptr;

    return &itr->second.mood;
}

BotMood BotMoodMgr::GetBaseline(Player* bot) const
{
    if (!bot)
        return {};

    BotPersonality const* personality =
        sBotPersonalityMgr.GetOrCreatePersonality(bot);
    if (!personality)
        return {};

    BotMood baseline = GetBaselineMood(*personality);
    baseline.lastUpdatedTime = CurrentGameTimeSeconds();
    baseline.lastDecayTime = 0;
    return baseline;
}

bool BotMoodMgr::ApplyMoodEvent(
    Player* bot,
    BotMoodEvent event,
    uint32 sourceKey,
    uint32 dedupeMs)
{
    if (!IsEnabled() || !IsValidOnlineBot(bot))
        return false;

    uint32 const nowMs = getMSTime();
    if (ShouldSuppressDuplicate(
            bot->GetGUID().GetCounter(),
            event,
            sourceKey,
            nowMs,
            dedupeMs ? dedupeMs : DEFAULT_DEDUPE_MS))
        return false;

    BotMoodDebugResult result = PreviewMoodEvent(bot, event, true);
    return result.success;
}

BotMoodDebugResult BotMoodMgr::PreviewMoodEvent(
    Player* bot,
    BotMoodEvent event,
    bool apply)
{
    BotMoodDebugResult result;
    result.event = event;

    if (!IsEnabled())
    {
        result.error = "Mood is disabled.";
        return result;
    }

    if (!IsValidOnlineBot(bot))
    {
        result.error = "Target must be an online Playerbot.";
        return result;
    }

    BotPersonality const* personality =
        sBotPersonalityMgr.GetOrCreatePersonality(bot);
    if (!personality)
    {
        result.error = "Unable to load bot personality.";
        return result;
    }

    CachedBotMood* entry = GetOrCreateEntry(bot, getMSTime());
    if (!entry)
    {
        result.error = "Unable to create mood cache entry.";
        return result;
    }

    result.baseline = GetBaselineMood(*personality);
    result.before = entry->mood;
    result.baseDelta = GetBaseMoodDelta(event);
    result.adjustedDelta = CalculateMoodDelta(
        *personality,
        event,
        result.baseDelta);
    result.after = entry->mood;
    ApplyDelta(result.after, result.adjustedDelta);
    result.after.lastUpdatedTime = CurrentGameTimeSeconds();

    if (apply)
        entry->mood = result.after;

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.mood",
            "Mood event {} bot {} base h:{} f:{} c:{} fear:{} e:{} b:{} "
            "final h:{} f:{} c:{} fear:{} e:{} b:{}",
            BotMoodEventToString(event),
            personality->botGuid,
            result.baseDelta.happiness,
            result.baseDelta.frustration,
            result.baseDelta.moodConfidence,
            result.baseDelta.fear,
            result.baseDelta.excitement,
            result.baseDelta.boredom,
            result.adjustedDelta.happiness,
            result.adjustedDelta.frustration,
            result.adjustedDelta.moodConfidence,
            result.adjustedDelta.fear,
            result.adjustedDelta.excitement,
            result.adjustedDelta.boredom);
    }

    result.success = true;
    return result;
}

bool BotMoodMgr::SetMoodField(
    Player* bot,
    std::string const& field,
    int32 value)
{
    if (!IsEnabled() || !IsValidOnlineBot(bot))
        return false;

    CachedBotMood* entry = GetOrCreateEntry(bot, getMSTime());
    if (!entry)
        return false;

    if (!SetMoodField(entry->mood, field, value))
        return false;

    entry->mood.lastUpdatedTime = CurrentGameTimeSeconds();
    return true;
}

bool BotMoodMgr::AdjustMoodField(
    Player* bot,
    std::string const& field,
    int32 amount)
{
    if (!IsEnabled() || !IsValidOnlineBot(bot))
        return false;

    CachedBotMood* entry = GetOrCreateEntry(bot, getMSTime());
    if (!entry)
        return false;

    int32 current = 0;
    if (!GetMoodField(entry->mood, field, current))
        return false;

    return SetMoodField(bot, field, current + amount);
}

bool BotMoodMgr::ResetMood(Player* bot)
{
    if (!IsEnabled() || !IsValidOnlineBot(bot))
        return false;

    CachedBotMood* entry = GetOrCreateEntry(bot, getMSTime());
    if (!entry)
        return false;

    entry->mood = GetBaseline(bot);
    ClampMood(entry->mood);
    entry->lastAccessTime = getMSTime();
    return true;
}

bool BotMoodMgr::GetMoodField(
    BotMood const& mood,
    std::string const& field,
    int32& value) const
{
    std::string const normalized = NormalizeField(field);

    if (normalized == "happiness")
        value = mood.happiness;
    else if (normalized == "frustration")
        value = mood.frustration;
    else if (normalized == "confidence" ||
             normalized == "moodconfidence")
        value = mood.moodConfidence;
    else if (normalized == "fear")
        value = mood.fear;
    else if (normalized == "excitement")
        value = mood.excitement;
    else if (normalized == "boredom")
        value = mood.boredom;
    else
        return false;

    return true;
}

BotDominantMood BotMoodMgr::GetDominantMoodFor(Player* bot)
{
    BotMood const* mood = GetMood(bot);
    if (!mood)
        return BotDominantMood::Calm;

    BotMood const baseline = GetBaseline(bot);
    return GetDominantMood(*mood, baseline);
}

uint8 BotMoodMgr::GetMoodIntensityFor(Player* bot)
{
    BotMood const* mood = GetMood(bot);
    if (!mood)
        return 0;

    BotMood const baseline = GetBaseline(bot);
    return GetMoodIntensity(*mood, baseline);
}

std::vector<BotMoodListEntry> BotMoodMgr::ListMoods(uint32 limit) const
{
    std::vector<BotMoodListEntry> entries;
    if (!limit)
        return entries;

    for (auto const& pair : _cache)
    {
        if (entries.size() >= limit)
            break;

        ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(
            pair.first);
        Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
        BotPersonality const* personality =
            bot ? sBotPersonalityMgr.GetCachedPersonality(pair.first) : nullptr;

        BotMood baseline;
        if (personality)
            baseline = GetBaselineMood(*personality);

        BotMoodListEntry entry;
        entry.botGuid = pair.first;
        if (bot)
            entry.botName = bot->GetName();
        else
        {
            std::string name;
            if (sCharacterCache->GetCharacterNameByGuid(guid, name))
                entry.botName = name;
            else
                entry.botName = "#" + std::to_string(pair.first);
        }

        entry.mood = pair.second.mood;
        entry.baseline = baseline;
        entry.dominantMood = GetDominantMood(entry.mood, baseline);
        entry.intensity = GetMoodIntensity(entry.mood, baseline);
        entry.lastAccessTime = pair.second.lastAccessTime;
        entries.push_back(entry);
    }

    return entries;
}

BotMoodCacheStats BotMoodMgr::GetStats() const
{
    BotMoodCacheStats stats;
    stats.enabled = IsEnabled();
    stats.cachedBots = _cache.size();
    stats.eventDedupEntries = _eventDedup.size();
    return stats;
}

BotMoodMgr::CachedBotMood* BotMoodMgr::GetOrCreateEntry(
    Player* bot,
    uint32 nowMs)
{
    if (!IsValidOnlineBot(bot))
        return nullptr;

    uint32 const botGuid = bot->GetGUID().GetCounter();
    auto itr = _cache.find(botGuid);
    if (itr != _cache.end())
    {
        itr->second.lastAccessTime = nowMs;
        return &itr->second;
    }

    CachedBotMood entry;
    entry.mood = CreateBaselineMood(bot, nowMs);
    entry.lastAccessTime = nowMs;
    auto inserted = _cache.emplace(botGuid, entry);

    if (_debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.mood",
            "Created mood cache entry for bot {}",
            botGuid);
    }

    while (_cache.size() > _maxCachedBots)
        _cache.erase(_cache.begin());

    return &inserted.first->second;
}

BotMood BotMoodMgr::CreateBaselineMood(Player* bot, uint32 nowMs) const
{
    (void)nowMs;

    BotMood mood = GetBaseline(bot);
    mood.lastDecayTime = nowMs;
    ClampMood(mood);
    return mood;
}

void BotMoodMgr::ApplyDelta(BotMood& mood, BotMoodDelta const& delta)
{
    mood.happiness =
        static_cast<int16>(mood.happiness + delta.happiness);
    mood.frustration =
        static_cast<int16>(mood.frustration + delta.frustration);
    mood.moodConfidence =
        static_cast<int16>(mood.moodConfidence + delta.moodConfidence);
    mood.fear = static_cast<int16>(mood.fear + delta.fear);
    mood.excitement =
        static_cast<int16>(mood.excitement + delta.excitement);
    mood.boredom = static_cast<int16>(mood.boredom + delta.boredom);

    ClampMood(mood);
}

void BotMoodMgr::ClampMood(BotMood& mood) const
{
    auto clampField = [this](int16& value)
    {
        value = static_cast<int16>(
            std::clamp<int32>(value, _minimum, _maximum));
    };

    clampField(mood.happiness);
    clampField(mood.frustration);
    clampField(mood.moodConfidence);
    clampField(mood.fear);
    clampField(mood.excitement);
    clampField(mood.boredom);
}

void BotMoodMgr::DecayMood(Player* bot, CachedBotMood& entry, uint32 nowMs)
{
    if (entry.mood.lastDecayTime &&
        getMSTimeDiff(entry.mood.lastDecayTime, nowMs) < _decayIntervalMs)
        return;

    BotMood const before = entry.mood;
    BotMood const baseline = GetBaseline(bot);

    MoveToward(entry.mood.happiness, baseline.happiness, _happinessDecay);
    MoveToward(
        entry.mood.frustration,
        baseline.frustration,
        _frustrationDecay);
    MoveToward(
        entry.mood.moodConfidence,
        baseline.moodConfidence,
        _confidenceDecay);
    MoveToward(entry.mood.fear, baseline.fear, _fearDecay);
    MoveToward(entry.mood.excitement, baseline.excitement, _excitementDecay);
    MoveToward(entry.mood.boredom, baseline.boredom, _boredomDecay);
    entry.mood.lastDecayTime = nowMs;
    ClampMood(entry.mood);

    if (_debugLogging &&
        (before.happiness != entry.mood.happiness ||
            before.frustration != entry.mood.frustration ||
            before.moodConfidence != entry.mood.moodConfidence ||
            before.fear != entry.mood.fear ||
            before.excitement != entry.mood.excitement ||
            before.boredom != entry.mood.boredom))
    {
        LOG_DEBUG(
            "module.botpersonality.mood",
            "Decayed mood for bot {} toward baseline",
            bot->GetGUID().GetCounter());
    }
}

void BotMoodMgr::Cleanup(uint32 nowMs)
{
    if (_lastCleanupMs &&
        getMSTimeDiff(_lastCleanupMs, nowMs) < CLEANUP_INTERVAL_MS)
        return;

    _lastCleanupMs = nowMs;

    for (auto itr = _cache.begin(); itr != _cache.end();)
    {
        if (getMSTimeDiff(itr->second.lastAccessTime, nowMs) >
            _cacheExpiryMs)
            itr = _cache.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _eventDedup.begin(); itr != _eventDedup.end();)
    {
        if (getMSTimeDiff(itr->second.lastSeenMs, nowMs) >
            CLEANUP_INTERVAL_MS)
            itr = _eventDedup.erase(itr);
        else
            ++itr;
    }

    while (_cache.size() > _maxCachedBots)
        _cache.erase(_cache.begin());
    while (_eventDedup.size() > MAX_DEDUP_ENTRIES)
        _eventDedup.erase(_eventDedup.begin());
}

bool BotMoodMgr::ShouldSuppressDuplicate(
    uint32 botGuid,
    BotMoodEvent event,
    uint32 sourceKey,
    uint32 nowMs,
    uint32 dedupeMs)
{
    if (!sourceKey || !dedupeMs)
        return false;

    uint64 const key = MakeDedupKey(botGuid, event, sourceKey);
    auto itr = _eventDedup.find(key);
    if (itr != _eventDedup.end() &&
        getMSTimeDiff(itr->second.lastSeenMs, nowMs) < dedupeMs)
        return true;

    _eventDedup[key] = { nowMs };
    while (_eventDedup.size() > MAX_DEDUP_ENTRIES)
        _eventDedup.erase(_eventDedup.begin());

    return false;
}

uint64 BotMoodMgr::MakeDedupKey(
    uint32 botGuid,
    BotMoodEvent event,
    uint32 sourceKey) const
{
    uint64 key = static_cast<uint64>(botGuid) << 32;
    key ^= static_cast<uint64>(static_cast<uint32>(event)) << 24;
    key ^= sourceKey;
    return key;
}

bool BotMoodMgr::SetMoodField(
    BotMood& mood,
    std::string const& field,
    int32 value) const
{
    value = std::clamp(value, _minimum, _maximum);
    std::string const normalized = NormalizeField(field);

    if (normalized == "happiness")
        mood.happiness = static_cast<int16>(value);
    else if (normalized == "frustration")
        mood.frustration = static_cast<int16>(value);
    else if (normalized == "confidence" ||
             normalized == "moodconfidence")
        mood.moodConfidence = static_cast<int16>(value);
    else if (normalized == "fear")
        mood.fear = static_cast<int16>(value);
    else if (normalized == "excitement")
        mood.excitement = static_cast<int16>(value);
    else if (normalized == "boredom")
        mood.boredom = static_cast<int16>(value);
    else
        return false;

    return true;
}
