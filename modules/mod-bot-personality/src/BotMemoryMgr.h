#ifndef MOD_BOT_PERSONALITY_BOT_MEMORY_MGR_H
#define MOD_BOT_PERSONALITY_BOT_MEMORY_MGR_H

#include "BotMemory.h"

#include <optional>
#include <unordered_map>

class Player;
struct BotGameplayContext;

class BotMemoryMgr
{
public:
    static BotMemoryMgr& instance()
    {
        static BotMemoryMgr instance;
        return instance;
    }

    void LoadConfig(bool reload);
    void Update(uint32 diff);
    void OnShutdown();

    bool IsEnabled() const { return _config.enable; }
    bool IsConversationSummaryEnabled() const;
    bool IsLlmSummarizationEnabled() const;
    bool IsDebugLoggingEnabled() const { return _config.debugLogging; }
    bool IsConversationTextSafe(std::string const& text) const;

    uint32 GetSessionTimeoutMs() const;
    uint32 GetSessionMaxTurns() const;
    uint32 GetSessionMaxCharacters() const;
    uint32 GetMaxActiveSessions() const;
    uint32 GetMinimumTurnsToSummarize() const;
    uint32 GetMinimumCharactersToSummarize() const;
    uint32 GetSummaryMaxTranscriptCharacters() const;
    uint32 GetSummaryRequestTimeoutMs() const;
    uint32 GetSummaryMaxQueueAgeMs() const;
    uint32 GetSummaryMaxPendingRequests() const;

    bool StoreCandidate(BotMemoryCandidate candidate);
    void RecordGameplayEvent(
        Player const* bot,
        Player const* player,
        BotGameplayEvent event,
        BotGameplayContext const& context);
    void RecordRelationshipMilestone(
        Player const* bot,
        Player const* player,
        BotRelationshipLevel oldLevel,
        BotRelationshipLevel newLevel);

    std::vector<BotMemorySelection> Retrieve(BotMemoryQuery query);
    BotMemoryTemplateContext GetTemplateContext(
        uint32 botGuid,
        uint32 playerGuid);
    void MarkRecalled(std::vector<uint64> const& memoryIds);

    std::vector<BotMemory> ListMemories(
        uint32 botGuid,
        uint32 playerGuid,
        uint32 limit);
    std::optional<BotMemory> GetMemory(uint64 memoryId);
    bool AddGmMemory(
        uint32 botGuid,
        uint32 playerGuid,
        BotMemoryType type,
        int32 importance,
        std::string summary,
        bool pinned);
    bool DeleteMemory(uint64 memoryId);
    uint32 ClearMemories(uint32 botGuid, uint32 playerGuid);
    bool SetPinned(uint64 memoryId, bool pinned);
    bool SetImportance(uint64 memoryId, int32 importance);

    BotMemoryExpiryStats ExpireMemories(uint32 limit = 500);
    void ClearCache();
    BotMemoryRuntimeStats GetStats();
    void ResetMetrics();

    void NoteSummaryQueued();
    void NoteSummaryCompleted();
    void NoteSummaryRejected();

private:
    struct Config
    {
        bool enable = true;
        bool debugLogging = false;
        bool enableGameplayMemories = true;
        bool enableRelationshipMilestones = true;
        bool enableConversationSummaries = true;
        bool enablePlayerPreferences = true;
        bool enableLlmSummarization = true;
        bool templateContextEnable = true;
        int32 minimumImportance = 30;
        int32 minimumConfidence = 60;
        uint32 maxMemoriesPerRelationship = 100;
        uint32 maxPinnedPerRelationship = 20;
        uint32 maxSummaryCharacters = 300;
        uint32 maxSubjectKeyCharacters = 96;
        uint32 maxCachedRelationships = 10000;
        uint32 cacheExpiryMs = 30 * 60 * 1000;
        uint32 saveIntervalMs = 60000;
        uint32 sessionTimeoutMs = 10 * 60 * 1000;
        uint32 sessionMaxTurns = 20;
        uint32 sessionMaxCharacters = 5000;
        uint32 maxActiveSessions = 10000;
        uint32 minimumTurnsToSummarize = 4;
        uint32 minimumCharactersToSummarize = 120;
        uint32 summaryRequestTimeoutMs = 10000;
        uint32 summaryMaxQueueAgeMs = 60000;
        uint32 summaryMaxPendingRequests = 50;
        uint32 summaryMaxTranscriptCharacters = 4000;
        uint32 retrievalMaxMemories = 6;
        uint32 retrievalMaxCharacters = 1200;
        int32 retrievalMinimumScore = 30;
        uint32 conversationRetentionDays = 60;
        uint32 minorRetentionDays = 30;
        uint32 conflictRetentionDays = 30;
        uint32 gameplayRetentionDays = 365;
        uint32 preferenceRetentionDays = 0;
        uint32 milestoneRetentionDays = 0;
        bool storeRawChat = false;
        bool storeOffensiveText = false;
        bool storeSensitiveData = false;
    };

    struct CachedMemorySet
    {
        std::vector<BotMemory> memories;
        bool loaded = false;
        bool dirty = false;
        uint32 lastAccessMs = 0;
    };

    BotMemoryMgr() = default;
    ~BotMemoryMgr() = default;

    BotPlayerRelationshipKey MakeKey(
        uint32 botGuid,
        uint32 playerGuid) const;
    CachedMemorySet* GetOrLoad(uint32 botGuid, uint32 playerGuid);
    bool LoadSet(
        uint32 botGuid,
        uint32 playerGuid,
        CachedMemorySet& set);
    void SaveSet(CachedMemorySet& set);
    bool SaveMemory(BotMemory& memory);
    void EnforceCacheLimit();
    bool EnforceRelationshipLimit(CachedMemorySet& set);

    bool ValidateCandidate(BotMemoryCandidate& candidate);
    bool PassesPrivacyFilter(std::string const& text) const;
    bool IsSafeSubjectKey(std::string const& key) const;
    uint32 RetentionDays(BotMemoryType type) const;
    int32 CalculateRelevance(
        BotMemory const& memory,
        BotMemoryQuery const& query,
        uint32 nowSeconds) const;
    int32 TypeRelevance(
        BotMemoryType type,
        BotMemoryQuery const& query) const;
    bool ReinforceOrMerge(
        CachedMemorySet& set,
        BotMemoryCandidate const& candidate,
        uint32 nowSeconds);
    void RemoveExpiredFromSet(CachedMemorySet& set, uint32 nowSeconds);
    uint32 ReadUInt(
        char const* name,
        int32 defaultValue,
        int32 minimum,
        int32 maximum) const;

    Config _config;
    BotMemoryMetrics _metrics;
    std::unordered_map<
        BotPlayerRelationshipKey,
        CachedMemorySet,
        BotPlayerRelationshipKeyHash> _cache;
    uint32 _lastSaveMs = 0;
    uint32 _lastCleanupMs = 0;
};

#define sBotMemoryMgr BotMemoryMgr::instance()

#endif
