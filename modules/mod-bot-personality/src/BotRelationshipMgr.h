#ifndef MOD_BOT_PERSONALITY_BOT_RELATIONSHIP_MGR_H
#define MOD_BOT_PERSONALITY_BOT_RELATIONSHIP_MGR_H

#include "BotRelationship.h"
#include "ObjectGuid.h"

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

class BotRelationshipMgr
{
public:
    static BotRelationshipMgr& instance()
    {
        static BotRelationshipMgr instance;
        return instance;
    }

    void LoadConfig(bool reload);
    void Update(uint32 diff);
    void OnShutdown();

    bool IsEnabled() const { return _enabled; }
    bool IsUpdateFromChatEnabled() const { return _updateFromChat; }
    bool IsDebugLoggingEnabled() const { return _debugLogging; }

    BotRelationship const* GetCachedRelationship(
        uint32 botGuid,
        uint32 playerGuid) const;
    bool GetExistingRelationship(
        Player const* bot,
        Player const* player,
        BotRelationship& relationship);
    BotRelationship* GetOrCreateRelationship(
        Player const* bot,
        Player const* player);

    bool ApplyEvent(
        Player const* bot,
        Player const* player,
        BotRelationshipEvent event);
    bool PreviewEventDelta(
        Player const* bot,
        BotRelationshipEvent event,
        BotRelationshipDelta& baseDelta,
        BotRelationshipDelta& adjustedDelta) const;

    bool SetValue(
        Player const* bot,
        Player const* player,
        std::string const& field,
        int32 value);
    bool AdjustValue(
        Player const* bot,
        Player const* player,
        std::string const& field,
        int32 amount,
        int32& oldValue,
        int32& newValue);
    bool ResetRelationship(Player const* bot, Player const* player);
    bool ReloadRelationship(Player const* bot, Player const* player);

    BotRelationshipSaveStats SaveDirtyRelationships();
    BotRelationshipSaveStats SaveRelationshipsForPlayer(
        ObjectGuid const& guid);
    void ClearCache();

    std::vector<BotRelationshipListEntry> ListRelationships(
        uint32 botGuid,
        uint32 limit);

    uint32 GetDirtyCount() const;
    BotRelationshipDelta GetBaseDelta(BotRelationshipEvent event) const;
    int32 ClampValue(int32 value) const;

private:
    enum class LoadResult
    {
        Error,
        NotFound,
        Loaded
    };

    struct CachedBotRelationship
    {
        BotRelationship relationship;
        bool dirty = false;
        uint32 lastAccessTime = 0;
        uint32 lastSaveTime = 0;
    };

    struct RecentRelationshipEvent
    {
        BotRelationshipEvent event = BotRelationshipEvent::Conversation;
        uint32 timestamp = 0;
    };

    struct ChatCapRecord
    {
        uint32 timestamp = 0;
        uint32 positive = 0;
        uint32 negative = 0;
        uint32 familiarity = 0;
    };

    struct TransientRelationshipState
    {
        std::deque<RecentRelationshipEvent> recentEvents;
        std::deque<ChatCapRecord> capRecords;
        uint32 lastAccessTime = 0;
    };

    BotRelationshipMgr() = default;
    ~BotRelationshipMgr() = default;

    BotRelationshipMgr(BotRelationshipMgr const&) = delete;
    BotRelationshipMgr& operator=(BotRelationshipMgr const&) = delete;

    bool IsValidPair(
        Player const* bot,
        Player const* player,
        bool logFailure) const;
    BotPlayerRelationshipKey MakeKey(
        uint32 botGuid,
        uint32 playerGuid) const;

    CachedBotRelationship* GetExistingEntry(
        uint32 botGuid,
        uint32 playerGuid);
    CachedBotRelationship* GetOrCreateEntry(
        uint32 botGuid,
        uint32 playerGuid,
        uint32 nowSeconds);

    LoadResult LoadRelationship(
        uint32 botGuid,
        uint32 playerGuid,
        BotRelationship& relationship,
        bool& corrected);
    void SaveRelationship(CachedBotRelationship& entry);
    void DeleteRelationship(uint32 botGuid, uint32 playerGuid);

    bool ValidateLoadedRelationship(BotRelationship& relationship);
    BotRelationship CreateNeutralRelationship(
        uint32 botGuid,
        uint32 playerGuid,
        uint32 nowSeconds) const;

    int16 ClampStorageValue(int32 value) const;
    bool SetRelationshipField(
        BotRelationship& relationship,
        std::string const& field,
        int32 value,
        int32* oldValue);

    uint32 GetRepeatPercent(
        BotPlayerRelationshipKey const& key,
        BotRelationshipEvent event,
        uint32 nowMs);
    void RecordRecentEvent(
        BotPlayerRelationshipKey const& key,
        BotRelationshipEvent event,
        uint32 nowMs);
    void ApplyDiminishingReturns(
        BotPlayerRelationshipKey const& key,
        BotRelationshipEvent event,
        BotRelationshipDelta& delta,
        uint32 nowMs);
    void ApplyChatCaps(
        BotPlayerRelationshipKey const& key,
        BotRelationshipDelta& delta,
        uint32 nowSeconds);
    void RecordChatCaps(
        BotPlayerRelationshipKey const& key,
        BotRelationshipDelta const& delta,
        uint32 nowSeconds);

    void ApplyDelta(
        CachedBotRelationship& entry,
        BotRelationshipDelta const& delta,
        uint32 nowSeconds);
    void MarkDirty(CachedBotRelationship& entry, uint32 nowMs);
    void Cleanup(uint32 nowMs);

    bool _enabled = true;
    bool _updateFromChat = true;
    bool _debugLogging = false;

    int32 _minimum = -1000;
    int32 _maximum = 1000;

    uint32 _saveIntervalMs = 60000;
    uint32 _cacheExpiryMs = 30 * 60 * 1000;
    uint32 _maxCachedEntries = 10000;

    uint32 _repeatWindowMs = 600 * 1000;
    uint32 _secondRepeatPercent = 50;
    uint32 _thirdRepeatPercent = 25;
    uint32 _furtherRepeatPercent = 0;

    uint32 _maxPositiveChatGainPerDay = 20;
    uint32 _maxNegativeChatLossPerDay = 40;
    uint32 _maxFamiliarityGainPerDay = 30;

    BotRelationshipDelta _greetingDelta = { 1, 0, 0, 2 };
    BotRelationshipDelta _thanksDelta = { 2, 1, 0, 1 };
    BotRelationshipDelta _praiseDelta = { 2, 0, 1, 1 };
    BotRelationshipDelta _apologyDelta = { 1, 2, 0, 1 };
    BotRelationshipDelta _insultDelta = { -5, -2, -1, 1 };
    BotRelationshipDelta _helpDelta = { 0, 0, 0, 1 };
    BotRelationshipDelta _conversationDelta = { 0, 0, 0, 1 };
    BotRelationshipDelta _spamDelta = { -2, -1, -1, 0 };

    std::unordered_map<
        BotPlayerRelationshipKey,
        CachedBotRelationship,
        BotPlayerRelationshipKeyHash> _cache;
    std::unordered_map<
        BotPlayerRelationshipKey,
        TransientRelationshipState,
        BotPlayerRelationshipKeyHash> _transientState;

    uint32 _lastSaveMs = 0;
    uint32 _lastCleanupMs = 0;
};

#define sBotRelationshipMgr BotRelationshipMgr::instance()

#endif
