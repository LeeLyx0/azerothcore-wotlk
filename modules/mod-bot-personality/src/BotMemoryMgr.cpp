#include "BotMemoryMgr.h"

#include "BotConversationSessionMgr.h"
#include "BotGameplayTracker.h"
#include "BotLlmMgr.h"
#include "BotPersonalityMgr.h"
#include "Config.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"
#include "QueryResult.h"
#include "StringFormat.h"
#include "Timer.h"
#include "Util.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace
{
constexpr uint32 DAY_SECONDS = 24 * 60 * 60;

uint32 CurrentGameTimeSeconds()
{
    return static_cast<uint32>(GameTime::GetGameTime().count());
}

std::string Lower(std::string text)
{
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
    return text;
}

bool StartsWith(std::string const& text, std::string_view prefix)
{
    return text.size() >= prefix.size() &&
        text.compare(0, prefix.size(), prefix) == 0;
}

BotMemory ReadMemory(Field* fields)
{
    BotMemory memory;
    memory.memoryId = fields[0].Get<uint64>();
    memory.botGuid = fields[1].Get<uint32>();
    memory.playerGuid = fields[2].Get<uint32>();
    memory.type = static_cast<BotMemoryType>(fields[3].Get<uint8>());
    memory.source = static_cast<BotMemorySource>(fields[4].Get<uint8>());
    memory.summary = fields[5].Get<std::string>();
    memory.subjectKey = fields[6].Get<std::string>();
    memory.importance = fields[7].Get<int16>();
    memory.confidence = fields[8].Get<int16>();
    memory.reinforcementCount = fields[9].Get<uint16>();
    memory.createdAt = fields[10].Get<uint32>();
    memory.updatedAt = fields[11].Get<uint32>();
    memory.lastRecalledAt = fields[12].Get<uint32>();
    memory.expiresAt = fields[13].Get<uint32>();
    memory.sourceEventId = fields[14].Get<uint32>();
    memory.sourceReference = fields[15].Get<uint64>();
    memory.pinned = fields[16].Get<uint8>() != 0;
    memory.negative = fields[17].Get<uint8>() != 0;
    return memory;
}

std::string MemorySelectColumns()
{
    return "`memory_id`, `bot_guid`, `player_guid`, `memory_type`, "
        "`memory_source`, `summary`, `subject_key`, `importance`, "
        "`confidence`, `reinforcement_count`, `created_at`, `updated_at`, "
        "`last_recalled_at`, `expires_at`, `source_event_id`, "
        "`source_reference`, `pinned`, `negative`";
}

std::string MapName(uint32 mapId)
{
    if (MapEntry const* entry = sMapStore.LookupEntry(mapId))
    {
        if (entry->name[0] && *entry->name[0])
            return entry->name[0];
    }

    return Acore::StringFormat("map {}", mapId);
}
}

void BotMemoryMgr::LoadConfig(bool reload)
{
    _config.enable = sConfigMgr->GetOption<bool>(
        "BotPersonality.Memory.Enable",
        true);
    _config.debugLogging = sConfigMgr->GetOption<bool>(
        "BotPersonality.Memory.DebugLogging",
        false);
    _config.enableGameplayMemories = sConfigMgr->GetOption<bool>(
        "BotPersonality.Memory.EnableGameplayMemories",
        true);
    _config.enableRelationshipMilestones = sConfigMgr->GetOption<bool>(
        "BotPersonality.Memory.EnableRelationshipMilestones",
        true);
    _config.enableConversationSummaries = sConfigMgr->GetOption<bool>(
        "BotPersonality.Memory.EnableConversationSummaries",
        true);
    _config.enablePlayerPreferences = sConfigMgr->GetOption<bool>(
        "BotPersonality.Memory.EnablePlayerPreferences",
        true);
    _config.enableLlmSummarization = sConfigMgr->GetOption<bool>(
        "BotPersonality.Memory.Summarization.EnableLlm",
        true);
    _config.templateContextEnable = sConfigMgr->GetOption<bool>(
        "BotPersonality.Memory.TemplateContext.Enable",
        true);

    _config.minimumImportance = static_cast<int32>(ReadUInt(
        "BotPersonality.Memory.MinimumImportanceToPersist", 30, 0, 100));
    _config.minimumConfidence = static_cast<int32>(ReadUInt(
        "BotPersonality.Memory.MinimumConfidenceToPersist", 60, 0, 100));
    _config.maxMemoriesPerRelationship = ReadUInt(
        "BotPersonality.Memory.MaxMemoriesPerRelationship", 100, 1, 500);
    _config.maxPinnedPerRelationship = ReadUInt(
        "BotPersonality.Memory.MaxPinnedMemoriesPerRelationship", 20, 0,
        500);
    _config.maxPinnedPerRelationship = std::min(
        _config.maxPinnedPerRelationship,
        _config.maxMemoriesPerRelationship);
    _config.maxSummaryCharacters = ReadUInt(
        "BotPersonality.Memory.MaxSummaryCharacters", 300, 32, 512);
    _config.maxSubjectKeyCharacters = ReadUInt(
        "BotPersonality.Memory.MaxSubjectKeyCharacters", 96, 8, 128);
    _config.maxCachedRelationships = ReadUInt(
        "BotPersonality.Memory.Cache.MaxRelationships", 10000, 10, 100000);
    _config.cacheExpiryMs = ReadUInt(
        "BotPersonality.Memory.Cache.ExpiryMinutes", 30, 1, 1440) *
        MINUTE * IN_MILLISECONDS;
    _config.saveIntervalMs = ReadUInt(
        "BotPersonality.Memory.SaveIntervalSeconds", 60, 5, 3600) *
        IN_MILLISECONDS;

    _config.sessionTimeoutMs = ReadUInt(
        "BotPersonality.Memory.Conversation.SessionTimeoutMinutes", 10, 1,
        1440) * MINUTE * IN_MILLISECONDS;
    _config.sessionMaxTurns = ReadUInt(
        "BotPersonality.Memory.Conversation.MaxTurns", 20, 2, 100);
    _config.sessionMaxCharacters = ReadUInt(
        "BotPersonality.Memory.Conversation.MaxCharacters", 5000, 128,
        20000);
    _config.maxActiveSessions = ReadUInt(
        "BotPersonality.Memory.Conversation.MaxActiveSessions", 10000, 10,
        100000);
    _config.minimumTurnsToSummarize = ReadUInt(
        "BotPersonality.Memory.Conversation.MinimumTurnsToSummarize", 4, 2,
        100);
    _config.minimumCharactersToSummarize = ReadUInt(
        "BotPersonality.Memory.Conversation.MinimumCharactersToSummarize",
        120, 16, 20000);
    _config.summaryRequestTimeoutMs = ReadUInt(
        "BotPersonality.Memory.Summarization.RequestTimeoutMs", 10000, 100,
        120000);
    _config.summaryMaxQueueAgeMs = ReadUInt(
        "BotPersonality.Memory.Summarization.MaxQueueAgeSeconds", 60, 1,
        3600) * IN_MILLISECONDS;
    _config.summaryMaxPendingRequests = ReadUInt(
        "BotPersonality.Memory.Summarization.MaxPendingRequests", 50, 1,
        1000);
    _config.summaryMaxTranscriptCharacters = ReadUInt(
        "BotPersonality.Memory.Summarization.MaxTranscriptCharacters", 4000,
        128, 10000);
    _config.retrievalMaxMemories = ReadUInt(
        "BotPersonality.Memory.Retrieval.MaxMemories", 6, 1, 20);
    _config.retrievalMaxCharacters = ReadUInt(
        "BotPersonality.Memory.Retrieval.MaxCharacters", 1200, 64, 4000);
    _config.retrievalMinimumScore = static_cast<int32>(ReadUInt(
        "BotPersonality.Memory.Retrieval.MinimumScore", 30, 0, 200));

    _config.conversationRetentionDays = ReadUInt(
        "BotPersonality.Memory.Retention.ConversationDays", 60, 0, 3650);
    _config.minorRetentionDays = ReadUInt(
        "BotPersonality.Memory.Retention.MinorInteractionDays", 30, 0,
        3650);
    _config.conflictRetentionDays = ReadUInt(
        "BotPersonality.Memory.Retention.ConflictDays", 30, 0, 3650);
    _config.gameplayRetentionDays = ReadUInt(
        "BotPersonality.Memory.Retention.GameplayDays", 365, 0, 3650);
    _config.preferenceRetentionDays = ReadUInt(
        "BotPersonality.Memory.Retention.PreferenceDays", 0, 0, 3650);
    _config.milestoneRetentionDays = ReadUInt(
        "BotPersonality.Memory.Retention.MilestoneDays", 0, 0, 3650);
    _config.storeRawChat = sConfigMgr->GetOption<bool>(
        "BotPersonality.Memory.Privacy.StoreRawChat",
        false);
    _config.storeOffensiveText = sConfigMgr->GetOption<bool>(
        "BotPersonality.Memory.Privacy.StoreOffensiveText",
        false);
    _config.storeSensitiveData = sConfigMgr->GetOption<bool>(
        "BotPersonality.Memory.Privacy.StoreSensitiveData",
        false);

    EnforceCacheLimit();
    LOG_INFO(
        "server.loading",
        "Bot Personality Phase 7 memory {}{}",
        _config.enable ? "enabled" : "disabled",
        reload ? " after config reload" : "");
}

void BotMemoryMgr::Update(uint32 /*diff*/)
{
    if (!_config.enable)
        return;

    uint32 const nowMs = getMSTime();
    if (!_lastSaveMs ||
        getMSTimeDiff(_lastSaveMs, nowMs) >= _config.saveIntervalMs)
    {
        for (auto& pair : _cache)
            SaveSet(pair.second);
        _lastSaveMs = nowMs;
    }

    if (_lastCleanupMs &&
        getMSTimeDiff(_lastCleanupMs, nowMs) < MINUTE * IN_MILLISECONDS)
        return;

    uint32 const nowSeconds = CurrentGameTimeSeconds();
    for (auto itr = _cache.begin(); itr != _cache.end();)
    {
        RemoveExpiredFromSet(itr->second, nowSeconds);
        if (getMSTimeDiff(itr->second.lastAccessMs, nowMs) >=
            _config.cacheExpiryMs)
        {
            SaveSet(itr->second);
            itr = _cache.erase(itr);
            ++_metrics.memoriesEvicted;
        }
        else
            ++itr;
    }

    _lastCleanupMs = nowMs;
    ExpireMemories(100);
    EnforceCacheLimit();
}

void BotMemoryMgr::OnShutdown()
{
    for (auto& pair : _cache)
        SaveSet(pair.second);
    _cache.clear();
}

bool BotMemoryMgr::IsConversationSummaryEnabled() const
{
    return _config.enable && _config.enableConversationSummaries;
}

bool BotMemoryMgr::IsLlmSummarizationEnabled() const
{
    return IsConversationSummaryEnabled() &&
        _config.enableLlmSummarization && sBotLlmMgr.IsEnabled();
}

bool BotMemoryMgr::IsConversationTextSafe(std::string const& text) const
{
    return PassesPrivacyFilter(text);
}

uint32 BotMemoryMgr::GetSessionTimeoutMs() const
{
    return _config.sessionTimeoutMs;
}

uint32 BotMemoryMgr::GetSessionMaxTurns() const
{
    return _config.sessionMaxTurns;
}

uint32 BotMemoryMgr::GetSessionMaxCharacters() const
{
    return _config.sessionMaxCharacters;
}

uint32 BotMemoryMgr::GetMaxActiveSessions() const
{
    return _config.maxActiveSessions;
}

uint32 BotMemoryMgr::GetMinimumTurnsToSummarize() const
{
    return _config.minimumTurnsToSummarize;
}

uint32 BotMemoryMgr::GetMinimumCharactersToSummarize() const
{
    return _config.minimumCharactersToSummarize;
}

uint32 BotMemoryMgr::GetSummaryMaxTranscriptCharacters() const
{
    return _config.summaryMaxTranscriptCharacters;
}

uint32 BotMemoryMgr::GetSummaryRequestTimeoutMs() const
{
    return _config.summaryRequestTimeoutMs;
}

uint32 BotMemoryMgr::GetSummaryMaxQueueAgeMs() const
{
    return _config.summaryMaxQueueAgeMs;
}

uint32 BotMemoryMgr::GetSummaryMaxPendingRequests() const
{
    return _config.summaryMaxPendingRequests;
}

bool BotMemoryMgr::StoreCandidate(BotMemoryCandidate candidate)
{
    ++_metrics.candidatesCreated;
    if (!ValidateCandidate(candidate))
    {
        ++_metrics.candidatesRejected;
        return false;
    }

    CachedMemorySet* set = GetOrLoad(
        candidate.botGuid,
        candidate.playerGuid);
    if (!set)
        return false;

    uint32 const nowSeconds = CurrentGameTimeSeconds();
    RemoveExpiredFromSet(*set, nowSeconds);
    if (ReinforceOrMerge(*set, candidate, nowSeconds))
        return true;

    BotMemory memory;
    memory.botGuid = candidate.botGuid;
    memory.playerGuid = candidate.playerGuid;
    memory.type = candidate.suggestedType;
    memory.source = candidate.source;
    memory.summary = std::move(candidate.deterministicSummary);
    memory.subjectKey = std::move(candidate.subjectKey);
    memory.importance = static_cast<int16>(candidate.importance);
    memory.confidence = static_cast<int16>(candidate.confidence);
    memory.createdAt = nowSeconds;
    memory.updatedAt = nowSeconds;
    uint32 const retention = RetentionDays(memory.type);
    memory.expiresAt = retention ?
        nowSeconds + retention * DAY_SECONDS :
        0;
    memory.sourceEventId = candidate.sourceEventId;
    memory.sourceReference = candidate.sourceReference;
    memory.pinned = candidate.pinned;
    memory.negative = candidate.negative;

    set->memories.push_back(std::move(memory));
    if (!EnforceRelationshipLimit(*set))
    {
        set->memories.pop_back();
        ++_metrics.candidatesRejected;
        return false;
    }

    if (!SaveMemory(set->memories.back()))
    {
        set->memories.pop_back();
        return false;
    }

    if (set->memories.back().source !=
        BotMemorySource::LlmSummarizedConversation)
        ++_metrics.deterministicStored;
    if (_config.debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.memory",
            "Stored {} memory {} for bot {} player {}",
            BotMemoryTypeToString(set->memories.back().type),
            set->memories.back().memoryId,
            candidate.botGuid,
            candidate.playerGuid);
    }

    return true;
}

void BotMemoryMgr::RecordGameplayEvent(
    Player const* bot,
    Player const* player,
    BotGameplayEvent event,
    BotGameplayContext const& context)
{
    if (!_config.enable || !_config.enableGameplayMemories || !bot || !player)
        return;

    BotMemoryCandidate candidate;
    candidate.botGuid = bot->GetGUID().GetCounter();
    candidate.playerGuid = player->GetGUID().GetCounter();
    candidate.source = BotMemorySource::VerifiedGameplayEvent;
    candidate.confidence = 100;
    candidate.sourceEventId = static_cast<uint32>(event);
    candidate.sourceReference =
        (static_cast<uint64>(context.mapId) << 32) | context.sourceEntry;

    std::string const place = MapName(context.mapId);
    std::string const playerName = player->GetName();
    std::string const botName = bot->GetName();
    switch (event)
    {
        case BotGameplayEvent::DungeonCompleted:
            candidate.suggestedType = BotMemoryType::DungeonCompletion;
            candidate.importance = 65;
            candidate.subjectKey = Acore::StringFormat(
                "dungeon:{}:completed", context.mapId);
            candidate.deterministicSummary = Acore::StringFormat(
                "{} and {} completed {} together.",
                playerName, botName, place);
            break;
        case BotGameplayEvent::RaidEncounterCompleted:
            candidate.suggestedType = BotMemoryType::RaidCompletion;
            candidate.importance = 70;
            candidate.subjectKey = Acore::StringFormat(
                "raid:{}:{}:completed", context.mapId, context.sourceEntry);
            candidate.deterministicSummary = Acore::StringFormat(
                "{} and {} completed an encounter in {} together.",
                playerName, botName, place);
            break;
        case BotGameplayEvent::PlayerResurrectedBot:
            candidate.suggestedType = BotMemoryType::Resurrection;
            candidate.importance = 55;
            candidate.subjectKey = "resurrection:player_saved_bot";
            candidate.deterministicSummary = Acore::StringFormat(
                "{} resurrected {} during a shared run.",
                playerName, botName);
            break;
        case BotGameplayEvent::SharedBossKill:
            candidate.suggestedType = BotMemoryType::SharedGameplay;
            candidate.importance = 55;
            candidate.subjectKey = Acore::StringFormat(
                "boss:{}:{}:defeated", context.mapId, context.sourceEntry);
            candidate.deterministicSummary = Acore::StringFormat(
                "{} and {} defeated a boss in {} together.",
                playerName, botName, place);
            break;
        case BotGameplayEvent::GroupWipe:
            candidate.suggestedType = BotMemoryType::Wipe;
            candidate.importance = 50;
            candidate.subjectKey = Acore::StringFormat(
                "wipe:{}:shared", context.mapId);
            candidate.deterministicSummary = Acore::StringFormat(
                "{} and {} went through a group wipe in {}.",
                playerName, botName, place);
            candidate.negative = true;
            break;
        case BotGameplayEvent::RepeatedPlayerDeath:
            candidate.suggestedType = BotMemoryType::RepeatedFailure;
            candidate.importance = 55;
            candidate.subjectKey = Acore::StringFormat(
                "failure:{}:repeated_deaths", context.mapId);
            candidate.deterministicSummary = Acore::StringFormat(
                "{} struggled through repeated deaths while grouped with {}.",
                playerName, botName);
            candidate.negative = true;
            break;
        case BotGameplayEvent::PlayerLeftGroupDuringCombat:
            candidate.suggestedType = BotMemoryType::Conflict;
            candidate.importance = 60;
            candidate.subjectKey = "conflict:left_during_combat";
            candidate.deterministicSummary = Acore::StringFormat(
                "{} previously left {}'s group during combat.",
                playerName, botName);
            candidate.negative = true;
            break;
        case BotGameplayEvent::SustainedTeamwork:
            candidate.suggestedType = BotMemoryType::GroupHistory;
            candidate.importance = 45;
            candidate.subjectKey = "group_history:sustained_teamwork";
            candidate.deterministicSummary = Acore::StringFormat(
                "{} and {} have spent meaningful time adventuring together.",
                playerName, botName);
            break;
        default:
            return;
    }

    StoreCandidate(std::move(candidate));
}

void BotMemoryMgr::RecordRelationshipMilestone(
    Player const* bot,
    Player const* player,
    BotRelationshipLevel oldLevel,
    BotRelationshipLevel newLevel)
{
    if (!_config.enable || !_config.enableRelationshipMilestones ||
        !bot || !player || oldLevel == newLevel)
        return;

    int32 importance = 0;
    switch (newLevel)
    {
        case BotRelationshipLevel::Friendly:
            importance = 60;
            break;
        case BotRelationshipLevel::Trusted:
            importance = 75;
            break;
        case BotRelationshipLevel::Loyal:
            importance = 90;
            break;
        case BotRelationshipLevel::Wary:
            importance = 70;
            break;
        case BotRelationshipLevel::Hostile:
            importance = 90;
            break;
        default:
            return;
    }

    BotMemoryCandidate candidate;
    candidate.botGuid = bot->GetGUID().GetCounter();
    candidate.playerGuid = player->GetGUID().GetCounter();
    candidate.suggestedType = BotMemoryType::RelationshipMilestone;
    candidate.source = BotMemorySource::RelationshipMilestone;
    candidate.importance = importance;
    candidate.confidence = 100;
    candidate.subjectKey = Acore::StringFormat(
        "relationship:{}",
        Lower(RelationshipLevelToString(newLevel)));
    candidate.deterministicSummary = Acore::StringFormat(
        "{} now considers their relationship with {} {}.",
        bot->GetName(),
        player->GetName(),
        Lower(RelationshipLevelToString(newLevel)));
    candidate.negative =
        newLevel == BotRelationshipLevel::Wary ||
        newLevel == BotRelationshipLevel::Hostile;
    StoreCandidate(std::move(candidate));
}

std::vector<BotMemorySelection> BotMemoryMgr::Retrieve(BotMemoryQuery query)
{
    std::vector<BotMemorySelection> selected;
    if (!_config.enable || !query.botGuid || !query.playerGuid)
        return selected;

    CachedMemorySet* set = GetOrLoad(query.botGuid, query.playerGuid);
    if (!set)
        return selected;

    uint32 const nowSeconds = CurrentGameTimeSeconds();
    RemoveExpiredFromSet(*set, nowSeconds);
    if (!query.maximumResults)
        query.maximumResults = _config.retrievalMaxMemories;
    if (!query.maximumCharacters)
        query.maximumCharacters = _config.retrievalMaxCharacters;

    for (BotMemory const& memory : set->memories)
    {
        if (memory.confidence < _config.minimumConfidence)
            continue;

        int32 const score = CalculateRelevance(memory, query, nowSeconds);
        if (score < _config.retrievalMinimumScore)
            continue;

        selected.push_back({ memory, score });
    }

    std::sort(
        selected.begin(),
        selected.end(),
        [](BotMemorySelection const& left, BotMemorySelection const& right)
        {
            if (left.score != right.score)
                return left.score > right.score;
            return left.memory.updatedAt > right.memory.updatedAt;
        });

    uint32 characters = 0;
    std::size_t keep = 0;
    for (; keep < selected.size() && keep < query.maximumResults; ++keep)
    {
        uint32 const next = static_cast<uint32>(
            selected[keep].memory.summary.size());
        if (characters && characters + next > query.maximumCharacters)
            break;
        characters += next;
    }
    selected.resize(keep);
    _metrics.memoriesRetrieved += selected.size();
    return selected;
}

BotMemoryTemplateContext BotMemoryMgr::GetTemplateContext(
    uint32 botGuid,
    uint32 playerGuid)
{
    BotMemoryTemplateContext context;
    if (!_config.templateContextEnable)
        return context;

    BotMemoryQuery query;
    query.botGuid = botGuid;
    query.playerGuid = playerGuid;
    query.maximumResults = _config.maxMemoriesPerRelationship;
    query.maximumCharacters = 4000;
    for (BotMemorySelection const& selection : Retrieve(query))
    {
        BotMemory const& memory = selection.memory;
        context.hasSharedDungeonHistory |=
            memory.type == BotMemoryType::DungeonCompletion;
        context.hasRecentResurrectionMemory |=
            memory.type == BotMemoryType::Resurrection;
        context.hasRecentConflictMemory |=
            memory.type == BotMemoryType::Conflict;
        context.hasTrustedMilestone |=
            memory.subjectKey == "relationship:trusted";
        context.hasLoyalMilestone |=
            memory.subjectKey == "relationship:loyal";
    }
    return context;
}

void BotMemoryMgr::MarkRecalled(std::vector<uint64> const& memoryIds)
{
    if (memoryIds.empty())
        return;

    uint32 const nowSeconds = CurrentGameTimeSeconds();
    for (auto& pair : _cache)
    {
        for (BotMemory& memory : pair.second.memories)
        {
            if (std::find(memoryIds.begin(), memoryIds.end(),
                    memory.memoryId) == memoryIds.end())
                continue;

            memory.lastRecalledAt = nowSeconds;
            pair.second.dirty = true;
        }
    }
}

std::vector<BotMemory> BotMemoryMgr::ListMemories(
    uint32 botGuid,
    uint32 playerGuid,
    uint32 limit)
{
    std::vector<BotMemory> memories;
    CachedMemorySet* set = GetOrLoad(botGuid, playerGuid);
    if (!set || !limit)
        return memories;

    memories = set->memories;
    std::sort(
        memories.begin(),
        memories.end(),
        [](BotMemory const& left, BotMemory const& right)
        {
            if (left.pinned != right.pinned)
                return left.pinned;
            if (left.importance != right.importance)
                return left.importance > right.importance;
            return left.updatedAt > right.updatedAt;
        });
    if (memories.size() > limit)
        memories.resize(limit);
    return memories;
}

std::optional<BotMemory> BotMemoryMgr::GetMemory(uint64 memoryId)
{
    if (!memoryId)
        return std::nullopt;

    for (auto const& pair : _cache)
    {
        for (BotMemory const& memory : pair.second.memories)
        {
            if (memory.memoryId == memoryId)
                return memory;
        }
    }

    QueryResult result = CharacterDatabase.Query(
        "SELECT {} FROM `bot_memory` WHERE `memory_id` = {} LIMIT 1",
        MemorySelectColumns(), memoryId);
    if (!result)
        return std::nullopt;
    return ReadMemory(result->Fetch());
}

bool BotMemoryMgr::AddGmMemory(
    uint32 botGuid,
    uint32 playerGuid,
    BotMemoryType type,
    int32 importance,
    std::string summary,
    bool pinned)
{
    BotMemoryCandidate candidate;
    candidate.botGuid = botGuid;
    candidate.playerGuid = playerGuid;
    candidate.suggestedType = type;
    candidate.source = BotMemorySource::GmCreated;
    candidate.deterministicSummary = std::move(summary);
    candidate.subjectKey = Acore::StringFormat(
        "gm:{}:{}",
        static_cast<uint32>(type),
        CurrentGameTimeSeconds());
    candidate.importance = std::clamp(importance, 0, 100);
    candidate.confidence = 100;
    candidate.pinned = pinned;
    return StoreCandidate(std::move(candidate));
}

bool BotMemoryMgr::DeleteMemory(uint64 memoryId)
{
    if (!memoryId)
        return false;

    bool const exists = GetMemory(memoryId).has_value();
    if (!exists)
        return false;

    for (auto& pair : _cache)
    {
        auto& memories = pair.second.memories;
        auto itr = std::remove_if(
            memories.begin(),
            memories.end(),
            [memoryId](BotMemory const& memory)
            {
                return memory.memoryId == memoryId;
            });
        if (itr != memories.end())
        {
            memories.erase(itr, memories.end());
        }
    }

    CharacterDatabase.DirectExecute(
        "DELETE FROM `bot_memory` WHERE `memory_id` = {}", memoryId);
    return true;
}

uint32 BotMemoryMgr::ClearMemories(uint32 botGuid, uint32 playerGuid)
{
    uint32 count = 0;
    BotPlayerRelationshipKey const key = MakeKey(botGuid, playerGuid);
    auto itr = _cache.find(key);
    if (itr != _cache.end())
    {
        count = static_cast<uint32>(itr->second.memories.size());
        _cache.erase(itr);
    }

    QueryResult result = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `bot_memory` WHERE `bot_guid` = {} "
        "AND `player_guid` = {}",
        botGuid, playerGuid);
    if (result)
        count = result->Fetch()[0].Get<uint32>();
    CharacterDatabase.DirectExecute(
        "DELETE FROM `bot_memory` WHERE `bot_guid` = {} "
        "AND `player_guid` = {}",
        botGuid, playerGuid);
    return count;
}

bool BotMemoryMgr::SetPinned(uint64 memoryId, bool pinned)
{
    std::optional<BotMemory> found = GetMemory(memoryId);
    if (!found)
        return false;

    if (pinned)
    {
        CachedMemorySet* set = GetOrLoad(found->botGuid, found->playerGuid);
        if (!set)
            return false;
        uint32 const pinnedCount = static_cast<uint32>(std::count_if(
            set->memories.begin(),
            set->memories.end(),
            [](BotMemory const& memory) { return memory.pinned; }));
        if (pinnedCount >= _config.maxPinnedPerRelationship)
            return false;
    }

    CharacterDatabase.DirectExecute(
        "UPDATE `bot_memory` SET `pinned` = {}, `expires_at` = {} "
        "WHERE `memory_id` = {}",
        pinned ? 1 : 0,
        pinned ? 0 :
            (RetentionDays(found->type) ?
                CurrentGameTimeSeconds() +
                    RetentionDays(found->type) * DAY_SECONDS :
                0),
        memoryId);
    for (auto& pair : _cache)
    {
        for (BotMemory& memory : pair.second.memories)
        {
            if (memory.memoryId == memoryId)
            {
                memory.pinned = pinned;
                if (pinned)
                    memory.expiresAt = 0;
                else if (uint32 const retention = RetentionDays(memory.type))
                {
                    memory.expiresAt = CurrentGameTimeSeconds() +
                        retention * DAY_SECONDS;
                }
            }
        }
    }
    return true;
}

bool BotMemoryMgr::SetImportance(uint64 memoryId, int32 importance)
{
    if (!GetMemory(memoryId))
        return false;
    importance = std::clamp(importance, 0, 100);
    CharacterDatabase.DirectExecute(
        "UPDATE `bot_memory` SET `importance` = {} WHERE `memory_id` = {}",
        importance, memoryId);
    for (auto& pair : _cache)
    {
        for (BotMemory& memory : pair.second.memories)
        {
            if (memory.memoryId == memoryId)
                memory.importance = static_cast<int16>(importance);
        }
    }
    return true;
}

BotMemoryExpiryStats BotMemoryMgr::ExpireMemories(uint32 limit)
{
    BotMemoryExpiryStats stats;
    uint32 const nowSeconds = CurrentGameTimeSeconds();
    QueryResult pinned = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `bot_memory` WHERE `expires_at` > 0 "
        "AND `expires_at` <= {} AND `pinned` = 1",
        nowSeconds);
    if (pinned)
        stats.pinnedSkipped = pinned->Fetch()[0].Get<uint32>();

    QueryResult result = CharacterDatabase.Query(
        "SELECT `memory_id` FROM `bot_memory` WHERE `expires_at` > 0 "
        "AND `expires_at` <= {} AND `pinned` = 0 "
        "ORDER BY `expires_at` LIMIT {}",
        nowSeconds, std::clamp(limit, 1u, 5000u));
    if (!result)
        return stats;

    std::vector<uint64> ids;
    do
    {
        ids.push_back(result->Fetch()[0].Get<uint64>());
    }
    while (result->NextRow());

    for (uint64 id : ids)
    {
        DeleteMemory(id);
        ++stats.deleted;
    }
    _metrics.memoriesExpired += stats.deleted;
    return stats;
}

void BotMemoryMgr::ClearCache()
{
    for (auto& pair : _cache)
        SaveSet(pair.second);
    _cache.clear();
}

BotMemoryRuntimeStats BotMemoryMgr::GetStats()
{
    BotMemoryRuntimeStats stats;
    stats.enabled = _config.enable;
    stats.gameplayMemories = _config.enableGameplayMemories;
    stats.relationshipMilestones = _config.enableRelationshipMilestones;
    stats.conversationSummaries = _config.enableConversationSummaries;
    stats.llmSummarization = IsLlmSummarizationEnabled();
    stats.cachedRelationships = static_cast<uint32>(_cache.size());
    for (auto const& pair : _cache)
        stats.cachedMemories += pair.second.memories.size();
    stats.activeSessions = static_cast<uint32>(
        sBotConversationSessionMgr.GetSessionCount());
    stats.pendingSummaryRequests = sBotLlmMgr.GetMemorySummaryQueueDepth();
    QueryResult result = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM `bot_memory`");
    if (result)
        stats.persistentMemories = result->Fetch()[0].Get<uint64>();
    stats.metrics = _metrics;
    return stats;
}

void BotMemoryMgr::ResetMetrics()
{
    _metrics = {};
}

void BotMemoryMgr::NoteSummaryQueued()
{
    ++_metrics.summaryRequestsQueued;
}

void BotMemoryMgr::NoteSummaryCompleted()
{
    ++_metrics.summariesCompleted;
}

void BotMemoryMgr::NoteSummaryRejected()
{
    ++_metrics.summariesRejected;
}

BotPlayerRelationshipKey BotMemoryMgr::MakeKey(
    uint32 botGuid,
    uint32 playerGuid) const
{
    return { botGuid, playerGuid };
}

BotMemoryMgr::CachedMemorySet* BotMemoryMgr::GetOrLoad(
    uint32 botGuid,
    uint32 playerGuid)
{
    if (!_config.enable || !botGuid || !playerGuid || botGuid == playerGuid)
        return nullptr;

    BotPlayerRelationshipKey const key = MakeKey(botGuid, playerGuid);
    auto itr = _cache.find(key);
    if (itr != _cache.end())
    {
        itr->second.lastAccessMs = getMSTime();
        return &itr->second;
    }

    CachedMemorySet set;
    if (!LoadSet(botGuid, playerGuid, set))
        return nullptr;
    set.loaded = true;
    set.lastAccessMs = getMSTime();
    auto inserted = _cache.emplace(key, std::move(set));
    EnforceCacheLimit();
    return &inserted.first->second;
}

bool BotMemoryMgr::LoadSet(
    uint32 botGuid,
    uint32 playerGuid,
    CachedMemorySet& set)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT {} FROM `bot_memory` WHERE `bot_guid` = {} "
        "AND `player_guid` = {} ORDER BY `pinned` DESC, "
        "`importance` DESC, `updated_at` DESC LIMIT {}",
        MemorySelectColumns(), botGuid, playerGuid,
        _config.maxMemoriesPerRelationship);
    if (!result)
        return true;

    do
    {
        BotMemory memory = ReadMemory(result->Fetch());
        if (!IsValidBotMemoryType(memory.type) ||
            !IsValidBotMemorySource(memory.source) ||
            !IsSafeSubjectKey(memory.subjectKey) ||
            !PassesPrivacyFilter(memory.summary))
            continue;
        set.memories.push_back(std::move(memory));
    }
    while (result->NextRow());
    RemoveExpiredFromSet(set, CurrentGameTimeSeconds());
    return true;
}

void BotMemoryMgr::SaveSet(CachedMemorySet& set)
{
    if (!set.dirty)
        return;
    for (BotMemory& memory : set.memories)
        SaveMemory(memory);
    set.dirty = false;
}

bool BotMemoryMgr::SaveMemory(BotMemory& memory)
{
    std::string summary = memory.summary;
    std::string subjectKey = memory.subjectKey;
    CharacterDatabase.EscapeString(summary);
    CharacterDatabase.EscapeString(subjectKey);

    if (!memory.memoryId)
    {
        CharacterDatabase.DirectExecute(
            "INSERT INTO `bot_memory` (`bot_guid`, `player_guid`, "
            "`memory_type`, `memory_source`, `summary`, `subject_key`, "
            "`importance`, `confidence`, `reinforcement_count`, "
            "`created_at`, `updated_at`, `last_recalled_at`, `expires_at`, "
            "`source_event_id`, `source_reference`, `pinned`, `negative`) "
            "VALUES ({}, {}, {}, {}, '{}', '{}', {}, {}, {}, {}, {}, {}, "
            "{}, {}, {}, {}, {})",
            memory.botGuid, memory.playerGuid,
            static_cast<uint32>(memory.type),
            static_cast<uint32>(memory.source),
            summary, subjectKey,
            static_cast<int32>(memory.importance),
            static_cast<int32>(memory.confidence),
            memory.reinforcementCount, memory.createdAt, memory.updatedAt,
            memory.lastRecalledAt, memory.expiresAt, memory.sourceEventId,
            memory.sourceReference, memory.pinned ? 1 : 0,
            memory.negative ? 1 : 0);
        QueryResult result = CharacterDatabase.Query(
            "SELECT LAST_INSERT_ID()");
        if (!result)
            return false;
        memory.memoryId = result->Fetch()[0].Get<uint64>();
        return memory.memoryId != 0;
    }

    CharacterDatabase.DirectExecute(
        "UPDATE `bot_memory` SET `memory_type` = {}, `memory_source` = {}, "
        "`summary` = '{}', `subject_key` = '{}', `importance` = {}, "
        "`confidence` = {}, `reinforcement_count` = {}, `updated_at` = {}, "
        "`last_recalled_at` = {}, `expires_at` = {}, `source_event_id` = {}, "
        "`source_reference` = {}, `pinned` = {}, `negative` = {} "
        "WHERE `memory_id` = {}",
        static_cast<uint32>(memory.type),
        static_cast<uint32>(memory.source), summary, subjectKey,
        static_cast<int32>(memory.importance),
        static_cast<int32>(memory.confidence), memory.reinforcementCount,
        memory.updatedAt, memory.lastRecalledAt, memory.expiresAt,
        memory.sourceEventId, memory.sourceReference,
        memory.pinned ? 1 : 0, memory.negative ? 1 : 0, memory.memoryId);
    return true;
}

void BotMemoryMgr::EnforceCacheLimit()
{
    while (_cache.size() > _config.maxCachedRelationships)
    {
        auto oldest = _cache.begin();
        for (auto itr = _cache.begin(); itr != _cache.end(); ++itr)
        {
            if (itr->second.lastAccessMs < oldest->second.lastAccessMs)
                oldest = itr;
        }
        SaveSet(oldest->second);
        _cache.erase(oldest);
        ++_metrics.memoriesEvicted;
    }
}

bool BotMemoryMgr::EnforceRelationshipLimit(CachedMemorySet& set)
{
    while (set.memories.size() > _config.maxMemoriesPerRelationship)
    {
        auto victim = set.memories.end();
        for (auto itr = set.memories.begin(); itr != set.memories.end(); ++itr)
        {
            if (itr->pinned)
                continue;
            if (victim == set.memories.end() ||
                itr->importance < victim->importance ||
                (itr->importance == victim->importance &&
                    itr->confidence < victim->confidence) ||
                (itr->importance == victim->importance &&
                    itr->confidence == victim->confidence &&
                    itr->updatedAt < victim->updatedAt))
                victim = itr;
        }
        if (victim == set.memories.end())
            return false;
        if (victim->memoryId)
        {
            CharacterDatabase.DirectExecute(
                "DELETE FROM `bot_memory` WHERE `memory_id` = {}",
                victim->memoryId);
        }
        set.memories.erase(victim);
        ++_metrics.memoriesEvicted;
    }
    return true;
}

bool BotMemoryMgr::ValidateCandidate(BotMemoryCandidate& candidate)
{
    if (!_config.enable || !candidate.botGuid || !candidate.playerGuid ||
        candidate.botGuid == candidate.playerGuid ||
        !IsValidBotMemoryType(candidate.suggestedType) ||
        !IsValidBotMemorySource(candidate.source))
        return false;

    candidate.importance = std::clamp(candidate.importance, 0, 100);
    candidate.confidence = std::clamp(candidate.confidence, 0, 100);
    if (candidate.importance < _config.minimumImportance ||
        candidate.confidence < _config.minimumConfidence)
        return false;
    if (candidate.suggestedType == BotMemoryType::PlayerPreference &&
        !_config.enablePlayerPreferences)
        return false;

    utf8truncate(candidate.deterministicSummary,
        _config.maxSummaryCharacters);
    utf8truncate(candidate.subjectKey, _config.maxSubjectKeyCharacters);
    if (candidate.deterministicSummary.empty() ||
        candidate.subjectKey.empty() ||
        !IsSafeSubjectKey(candidate.subjectKey))
        return false;

    if (!PassesPrivacyFilter(candidate.deterministicSummary))
    {
        ++_metrics.privacyRejected;
        return false;
    }
    return true;
}

bool BotMemoryMgr::PassesPrivacyFilter(std::string const& text) const
{
    std::string mutableText = text;
    if (!utf8length(mutableText) || mutableText.empty())
        return false;
    for (unsigned char c : text)
    {
        if (c < 0x20 && c != '\n' && c != '\t')
            return false;
    }

    std::string const lower = Lower(text);
    static constexpr std::string_view blocked[] =
    {
        "password", "api key", "apikey", "access token", "private key",
        "session cookie", "authorization:", "ignore all rules",
        "ignore previous", "system prompt", "hidden prompt", "system:",
        "assistant:", "developer:", "drop table", "delete from",
        "insert into", "select * from", "c:\\users\\", "/home/"
    };
    for (std::string_view value : blocked)
    {
        if (lower.find(value) != std::string::npos)
            return false;
    }
    if (StartsWith(lower, ".") || StartsWith(lower, "/") ||
        StartsWith(lower, "#"))
        return false;

    if (_config.storeSensitiveData)
        return true;

    static std::regex const email(
        R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})");
    static std::regex const ipv4(
        R"((\d{1,3}\.){3}\d{1,3})");
    static std::regex const phone(
        R"((\+?\d[\d ()-]{8,}\d))");
    static std::regex const card(R"((\d[ -]?){13,19})");
    static std::regex const secret(
        R"((sk|pk|ghp|xox[baprs])[-_][A-Za-z0-9_-]{12,})",
        std::regex::icase);
    return !std::regex_search(text, email) &&
        !std::regex_search(text, ipv4) &&
        !std::regex_search(text, phone) &&
        !std::regex_search(text, card) &&
        !std::regex_search(text, secret);
}

bool BotMemoryMgr::IsSafeSubjectKey(std::string const& key) const
{
    if (key.empty() || key.size() > _config.maxSubjectKeyCharacters)
        return false;
    return std::all_of(
        key.begin(),
        key.end(),
        [](unsigned char c)
        {
            return std::islower(c) || std::isdigit(c) ||
                c == ':' || c == '_' || c == '-';
        });
}

uint32 BotMemoryMgr::RetentionDays(BotMemoryType type) const
{
    switch (type)
    {
        case BotMemoryType::ConversationSummary:
        case BotMemoryType::PlayerStatement:
        case BotMemoryType::PersonalTopic:
        case BotMemoryType::PromiseOrPlan:
            return _config.conversationRetentionDays;
        case BotMemoryType::PositiveInteraction:
        case BotMemoryType::NegativeInteraction:
        case BotMemoryType::Reconciliation:
            return _config.minorRetentionDays;
        case BotMemoryType::Conflict:
            return _config.conflictRetentionDays;
        case BotMemoryType::PlayerPreference:
            return _config.preferenceRetentionDays;
        case BotMemoryType::RelationshipMilestone:
        case BotMemoryType::CustomGmMemory:
            return _config.milestoneRetentionDays;
        default:
            return _config.gameplayRetentionDays;
    }
}

int32 BotMemoryMgr::CalculateRelevance(
    BotMemory const& memory,
    BotMemoryQuery const& query,
    uint32 nowSeconds) const
{
    int32 score = memory.importance * 55 / 100;
    score += memory.confidence * 15 / 100;
    score += std::min<int32>(15, memory.reinforcementCount * 3);
    score += TypeRelevance(memory.type, query);

    uint32 const ageDays = memory.updatedAt < nowSeconds ?
        (nowSeconds - memory.updatedAt) / DAY_SECONDS : 0;
    score += std::max<int32>(0, 20 - static_cast<int32>(ageDays));
    if (!memory.pinned &&
        memory.type != BotMemoryType::RelationshipMilestone &&
        memory.type != BotMemoryType::PlayerPreference)
        score -= static_cast<int32>(ageDays / 7);
    if (memory.lastRecalledAt &&
        nowSeconds - memory.lastRecalledAt < 5 * MINUTE)
        score -= 12;
    if (memory.pinned)
        score += 10;
    return score;
}

int32 BotMemoryMgr::TypeRelevance(
    BotMemoryType type,
    BotMemoryQuery const& query) const
{
    switch (query.intent)
    {
        case BotChatIntent::Greeting:
        case BotChatIntent::Farewell:
            if (type == BotMemoryType::RelationshipMilestone ||
                type == BotMemoryType::GroupHistory ||
                type == BotMemoryType::Conflict ||
                type == BotMemoryType::Reconciliation)
                return 30;
            break;
        case BotChatIntent::HelpRequest:
            if (type == BotMemoryType::PlayerPreference ||
                type == BotMemoryType::DungeonCompletion ||
                type == BotMemoryType::SharedGameplay ||
                type == BotMemoryType::RepeatedFailure)
                return 35;
            break;
        case BotChatIntent::IdentityQuestion:
            if (type == BotMemoryType::PersonalTopic ||
                type == BotMemoryType::ConversationSummary ||
                type == BotMemoryType::RelationshipMilestone)
                return 35;
            break;
        case BotChatIntent::WellbeingQuestion:
            if (type == BotMemoryType::Conflict ||
                type == BotMemoryType::Wipe ||
                type == BotMemoryType::Resurrection ||
                type == BotMemoryType::DungeonCompletion)
                return 35;
            break;
        default:
            break;
    }

    if (query.hasProactiveEvent)
    {
        if (query.proactiveEvent == BotProactiveDialogueEvent::GroupWipe &&
            (type == BotMemoryType::Wipe ||
                type == BotMemoryType::RepeatedFailure))
            return 40;
        if ((query.proactiveEvent ==
                BotProactiveDialogueEvent::DungeonCompleted ||
                query.proactiveEvent ==
                BotProactiveDialogueEvent::SharedBossKilled) &&
            (type == BotMemoryType::DungeonCompletion ||
                type == BotMemoryType::SharedGameplay))
            return 40;
    }
    return type == BotMemoryType::RelationshipMilestone ? 15 : 0;
}

bool BotMemoryMgr::ReinforceOrMerge(
    CachedMemorySet& set,
    BotMemoryCandidate const& candidate,
    uint32 nowSeconds)
{
    for (BotMemory& memory : set.memories)
    {
        if (memory.subjectKey != candidate.subjectKey)
            continue;
        if (memory.source == BotMemorySource::VerifiedGameplayEvent &&
            candidate.source != BotMemorySource::VerifiedGameplayEvent)
        {
            ++_metrics.duplicatesIgnored;
            return true;
        }
        if (memory.source == BotMemorySource::RelationshipMilestone &&
            candidate.source == BotMemorySource::RelationshipMilestone &&
            nowSeconds >= memory.updatedAt &&
            nowSeconds - memory.updatedAt < 7 * DAY_SECONDS)
        {
            ++_metrics.duplicatesIgnored;
            return true;
        }
        if (memory.source ==
                BotMemorySource::DeterministicConversationRule &&
            candidate.source ==
                BotMemorySource::LlmSummarizedConversation &&
            memory.summary != candidate.deterministicSummary)
        {
            ++_metrics.duplicatesIgnored;
            return true;
        }
        if (candidate.sourceReference &&
            memory.sourceReference == candidate.sourceReference &&
            memory.updatedAt == nowSeconds)
        {
            ++_metrics.duplicatesIgnored;
            return true;
        }

        bool const changedSummary =
            memory.summary != candidate.deterministicSummary;
        if (memory.type == BotMemoryType::PlayerPreference && changedSummary)
        {
            memory.summary = candidate.deterministicSummary;
            memory.source = candidate.source;
            memory.confidence = static_cast<int16>(candidate.confidence);
            ++_metrics.memoriesMerged;
        }
        else
            ++_metrics.memoriesReinforced;

        memory.reinforcementCount = static_cast<uint16>(std::min<uint32>(
            static_cast<uint32>(memory.reinforcementCount) + 1,
            65535));
        memory.importance = static_cast<int16>(std::min<int32>(
            100,
            std::max<int32>(memory.importance, candidate.importance) + 3));
        memory.confidence = static_cast<int16>(std::min<int32>(
            100,
            std::max<int32>(memory.confidence, candidate.confidence)));
        memory.updatedAt = nowSeconds;
        memory.sourceReference = candidate.sourceReference;
        memory.negative = candidate.negative;
        SaveMemory(memory);
        return true;
    }
    return false;
}

void BotMemoryMgr::RemoveExpiredFromSet(
    CachedMemorySet& set,
    uint32 nowSeconds)
{
    auto itr = std::remove_if(
        set.memories.begin(),
        set.memories.end(),
        [nowSeconds](BotMemory const& memory)
        {
            return !memory.pinned && memory.expiresAt &&
                memory.expiresAt <= nowSeconds;
        });
    set.memories.erase(itr, set.memories.end());
}

uint32 BotMemoryMgr::ReadUInt(
    char const* name,
    int32 defaultValue,
    int32 minimum,
    int32 maximum) const
{
    int32 const configured = sConfigMgr->GetOption<int32>(name, defaultValue);
    int32 const value = std::clamp(configured, minimum, maximum);
    if (configured != value)
    {
        LOG_WARN(
            "module.botpersonality.memory",
            "{} value {} corrected to {}",
            name, configured, value);
    }
    return static_cast<uint32>(value);
}
