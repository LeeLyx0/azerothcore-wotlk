#ifndef MOD_BOT_PERSONALITY_BOT_PROACTIVE_DIALOGUE_MGR_H
#define MOD_BOT_PERSONALITY_BOT_PROACTIVE_DIALOGUE_MGR_H

#include "BotGameplayEvent.h"
#include "BotGameplayTracker.h"
#include "BotGroupDialogueCoordinator.h"
#include "BotProactiveDialogueContext.h"
#include "ObjectGuid.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Group;
class Player;

struct BotProactiveDialogueStats
{
    bool enabled = false;
    bool partyChat = false;
    bool raidChat = false;
    bool say = false;
    bool botBanter = false;
    bool requireRealPlayer = true;

    std::size_t queuedEvents = 0;
    std::size_t trackedBots = 0;
    BotGroupDialogueCoordinatorStats coordinator;
};

struct BotProactiveQueueEntry
{
    BotProactiveDialogueEvent event =
        BotProactiveDialogueEvent::GroupJoined;
    BotDialoguePriority priority = BotDialoguePriority::Low;
    uint32 groupId = 0;
    uint32 preferredBotGuid = 0;
    uint32 relatedPlayerGuid = 0;
    uint32 queuedAgeMs = 0;
    uint32 delayRemainingMs = 0;
};

struct BotProactiveDialogueDebugResult
{
    bool success = false;
    bool sent = false;
    std::string error;
    BotProactiveDialogueContext context;
    std::string response;
    uint32 baseChance = 0;
    uint32 adjustedChance = 0;
};

class BotProactiveDialogueMgr
{
public:
    static BotProactiveDialogueMgr& instance()
    {
        static BotProactiveDialogueMgr instance;
        return instance;
    }

    void LoadConfig(bool reload);
    void Update(uint32 diff);
    void ClearQueue();
    void ClearCooldowns();
    void ClearAll();

    bool IsEnabled() const;
    bool IsDebugLoggingEnabled() const { return _config.debugLogging; }

    void TrackBot(Player* bot);
    void OnPlayerLogout(Player* player);
    void OnPlayerMapChanged(Player* player);
    void OnGroupMemberAdded(Group* group, ObjectGuid guid);
    void OnGroupMemberRemoved(Group* group, ObjectGuid guid);

    void RecordGameplayEvent(
        Player* bot,
        Player* player,
        BotGameplayEvent event,
        BotGameplayContext const& gameplayContext);

    bool QueueEvent(
        BotProactiveDialogueEvent event,
        Player* preferredBot,
        Player* relatedPlayer,
        BotGameplayContext const& gameplayContext,
        bool force = false,
        bool isBanterReply = false,
        uint32 sourceKey = 0);

    BotProactiveDialogueDebugResult SimulateDialogue(
        Player* bot,
        Player* player,
        BotProactiveDialogueEvent event,
        bool send,
        bool bypassCooldowns);

    BotProactiveDialogueStats GetStats() const;
    std::vector<BotProactiveQueueEntry> GetQueue(uint32 limit) const;
    BotGroupDialogueStateView GetGroupState(Player* member) const;

private:
    struct Config
    {
        bool enable = true;
        bool debugLogging = false;
        bool requireRealPlayerPresent = true;
        bool enablePartyChat = true;
        bool enableRaidChat = true;
        bool enableSay = false;
        bool enableBotBanter = true;
        bool enableInactivityDialogue = true;
        bool forceDialogueForDebug = false;

        uint32 botBanterChance = 15;
        uint32 botBanterDelayMinMs = 1500;
        uint32 botBanterDelayMaxMs = 4000;
        uint32 maxBanterReplies = 1;

        uint32 delayMinMs = 500;
        uint32 delayMaxMs = 2500;

        uint32 maxQueuedEventsGlobal = 1000;
        uint32 maxQueuedEventsPerGroup = 10;
        uint32 eventExpirySeconds = 20;

        uint32 groupJoinChance = 35;
        uint32 dungeonEnterChance = 40;
        uint32 eliteKillChance = 15;
        uint32 bossKillChance = 75;
        uint32 botHealedChance = 15;
        uint32 botSavedChance = 65;
        uint32 botResurrectedChance = 80;
        uint32 playerDeathChance = 10;
        uint32 repeatedPlayerDeathChance = 45;
        uint32 botDeathChance = 20;
        uint32 wipeChance = 70;
        uint32 dungeonCompleteChance = 90;
        uint32 raidEncounterCompleteChance = 80;
        uint32 dangerousPullChance = 30;
        uint32 abandonmentChance = 75;
        uint32 lowHealthChance = 5;
        uint32 criticalHealthChance = 25;
        uint32 lowManaChance = 8;
        uint32 inactivityChance = 15;
        uint32 teamworkChance = 20;

        uint32 lowHealthPercent = 30;
        uint32 criticalHealthPercent = 15;
        uint32 lowManaPercent = 20;
        uint32 lowHealthCooldownMs = 120000;
        uint32 criticalHealthCooldownMs = 60000;
        uint32 lowManaCooldownMs = 120000;
        uint32 inactivityMinutes = 5;
        uint32 inactivityCooldownMinutes = 15;
    };

    struct PendingEvent
    {
        BotProactiveDialogueEvent event =
            BotProactiveDialogueEvent::GroupJoined;
        BotDialoguePriority priority = BotDialoguePriority::Low;
        ObjectGuid preferredBotGuid;
        ObjectGuid relatedPlayerGuid;
        uint32 groupId = 0;
        uint32 mapId = 0;
        uint32 zoneId = 0;
        uint32 instanceId = 0;
        uint32 sourceEntry = 0;
        uint32 value = 0;
        uint32 queuedAtMs = 0;
        uint32 deliverAtMs = 0;
        uint32 eventTime = 0;
        bool force = false;
        bool bypassCooldowns = false;
        bool isBanterReply = false;
        std::string previousSpeakerName;
    };

    struct BotLiveState
    {
        uint32 lastSeenMs = 0;
        uint32 lastActivityMs = 0;
        uint32 lastLowHealthMs = 0;
        uint32 lastCriticalHealthMs = 0;
        uint32 lastLowManaMs = 0;
        uint32 lastInactivityMs = 0;
        uint32 mapId = 0;
        uint32 instanceId = 0;
        uint8 healthState = 0;
        bool lowManaActive = false;
    };

    BotProactiveDialogueMgr() = default;
    ~BotProactiveDialogueMgr() = default;

    BotProactiveDialogueMgr(BotProactiveDialogueMgr const&) = delete;
    BotProactiveDialogueMgr& operator=(BotProactiveDialogueMgr const&) =
        delete;

    void QueuePendingEvent(PendingEvent event);
    void ProcessPendingEvents(uint32 nowMs);
    void ProcessTrackedBots(uint32 nowMs);
    void Cleanup(uint32 nowMs);

    std::optional<BotProactiveDialogueContext> BuildContext(
        PendingEvent const& event,
        Player* bot,
        Player* relatedPlayer);
    BotProactiveDialogueDebugResult BuildDebugResult(
        PendingEvent const& event,
        Player* bot,
        Player* relatedPlayer,
        bool send);

    std::vector<BotSpeakerCandidate> BuildCandidates(
        PendingEvent const& event,
        Group* group,
        Player* preferredBot,
        Player* relatedPlayer) const;

    bool DeliverEvent(PendingEvent const& event, uint32 nowMs);
    bool SendChat(
        Player* bot,
        Group* group,
        BotProactiveDialogueContext const& context,
        std::string& response);
    void MaybeQueueBanter(
        PendingEvent const& sourceEvent,
        Player* speaker,
        Group* group,
        std::string const& response,
        uint32 nowMs);

    bool IsValidOnlinePlayer(Player const* player) const;
    bool IsRealPlayer(Player const* player) const;
    bool HasRealPlayer(Group const* group) const;
    uint32 GetGroupId(Player const* player) const;
    uint32 GetGroupId(Group const* group) const;
    uint32 GetEventChance(BotProactiveDialogueEvent event) const;
    uint32 AdjustChance(
        BotProactiveDialogueContext const& context,
        uint32 baseChance) const;
    bool RollChance(
        BotProactiveDialogueContext const& context,
        uint32 chance) const;
    uint32 GetSelectionSeed(BotProactiveDialogueContext const& context) const;
    uint32 GetDelayMs(
        BotProactiveDialogueEvent event,
        bool isBanterReply) const;

    bool IsEventStale(PendingEvent const& event, uint32 nowMs) const;
    bool IsSameTopic(PendingEvent const& left, PendingEvent const& right) const;
    void TouchActivity(Player* bot, uint32 nowMs);
    void RemoveEventsForGuid(ObjectGuid guid);
    void RemoveEventsForGroup(uint32 groupId);
    void TruncateResponse(std::string& response) const;
    uint32 MakeSourceKey(
        BotGameplayEvent event,
        BotGameplayContext const& context,
        Player const* player) const;

    Config _config;
    BotGroupDialogueCoordinator _coordinator;

    std::deque<PendingEvent> _queue;
    std::unordered_map<uint32, uint32> _queuedPerGroup;
    std::unordered_map<uint32, BotLiveState> _trackedBots;
    std::unordered_map<uint64, std::string> _recentResponses;

    uint32 _lastCleanupMs = 0;
    uint32 _lastTrackedBotScanMs = 0;
};

#define sBotProactiveDialogueMgr BotProactiveDialogueMgr::instance()

#endif
