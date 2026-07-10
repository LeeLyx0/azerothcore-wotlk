#ifndef MOD_BOT_PERSONALITY_BOT_MOOD_MGR_H
#define MOD_BOT_PERSONALITY_BOT_MOOD_MGR_H

#include "BotMood.h"
#include "ObjectGuid.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

struct BotMoodCacheStats
{
    bool enabled = false;
    std::size_t cachedBots = 0;
    std::size_t eventDedupEntries = 0;
};

struct BotMoodListEntry
{
    uint32 botGuid = 0;
    std::string botName;
    BotMood mood;
    BotMood baseline;
    BotDominantMood dominantMood = BotDominantMood::Calm;
    uint8 intensity = 0;
    uint32 lastAccessTime = 0;
};

struct BotMoodDebugResult
{
    bool success = false;
    std::string error;
    BotMoodEvent event = BotMoodEvent::PlayerGreeting;
    BotMoodDelta baseDelta;
    BotMoodDelta adjustedDelta;
    BotMood before;
    BotMood after;
    BotMood baseline;
};

class BotMoodMgr
{
public:
    static BotMoodMgr& instance()
    {
        static BotMoodMgr instance;
        return instance;
    }

    void LoadConfig(bool reload);
    void Update(uint32 diff);
    void ClearCache();

    bool IsEnabled() const;
    bool IsDebugLoggingEnabled() const { return _debugLogging; }

    BotMood const* GetMood(Player* bot);
    BotMood const* GetCachedMood(uint32 botGuid) const;
    BotMood GetBaseline(Player* bot) const;

    bool ApplyMoodEvent(
        Player* bot,
        BotMoodEvent event,
        uint32 sourceKey = 0,
        uint32 dedupeMs = 0);
    BotMoodDebugResult PreviewMoodEvent(
        Player* bot,
        BotMoodEvent event,
        bool apply);

    bool SetMoodField(Player* bot, std::string const& field, int32 value);
    bool AdjustMoodField(Player* bot, std::string const& field, int32 amount);
    bool ResetMood(Player* bot);
    bool GetMoodField(BotMood const& mood, std::string const& field, int32& value)
        const;

    BotDominantMood GetDominantMoodFor(Player* bot);
    uint8 GetMoodIntensityFor(Player* bot);

    std::vector<BotMoodListEntry> ListMoods(uint32 limit) const;
    BotMoodCacheStats GetStats() const;

private:
    struct CachedBotMood
    {
        BotMood mood;
        uint32 lastAccessTime = 0;
    };

    struct EventDedupRecord
    {
        uint32 lastSeenMs = 0;
    };

    BotMoodMgr() = default;
    ~BotMoodMgr() = default;

    BotMoodMgr(BotMoodMgr const&) = delete;
    BotMoodMgr& operator=(BotMoodMgr const&) = delete;

    CachedBotMood* GetOrCreateEntry(Player* bot, uint32 nowMs);
    BotMood CreateBaselineMood(Player* bot, uint32 nowMs) const;
    void ApplyDelta(BotMood& mood, BotMoodDelta const& delta);
    void ClampMood(BotMood& mood) const;
    void DecayMood(Player* bot, CachedBotMood& entry, uint32 nowMs);
    void Cleanup(uint32 nowMs);

    bool ShouldSuppressDuplicate(
        uint32 botGuid,
        BotMoodEvent event,
        uint32 sourceKey,
        uint32 nowMs,
        uint32 dedupeMs);
    uint64 MakeDedupKey(
        uint32 botGuid,
        BotMoodEvent event,
        uint32 sourceKey) const;

    bool SetMoodField(BotMood& mood, std::string const& field, int32 value)
        const;

    bool _enabled = true;
    bool _debugLogging = false;

    int32 _minimum = 0;
    int32 _maximum = 100;

    uint32 _decayIntervalMs = 30000;
    int32 _happinessDecay = 1;
    int32 _frustrationDecay = 2;
    int32 _confidenceDecay = 1;
    int32 _fearDecay = 2;
    int32 _excitementDecay = 2;
    int32 _boredomDecay = 1;

    uint32 _cacheExpiryMs = 60 * 60 * 1000;
    uint32 _maxCachedBots = 5000;
    uint32 _decayBatchSize = 200;

    std::unordered_map<uint32, CachedBotMood> _cache;
    std::unordered_map<uint64, EventDedupRecord> _eventDedup;

    uint32 _lastCleanupMs = 0;
};

#define sBotMoodMgr BotMoodMgr::instance()

#endif
