#ifndef MOD_BOT_PERSONALITY_BOT_GROUP_DIALOGUE_COORDINATOR_H
#define MOD_BOT_PERSONALITY_BOT_GROUP_DIALOGUE_COORDINATOR_H

#include "BotProactiveDialogueEvent.h"
#include "Define.h"

#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

struct BotGroupDialogueConfig
{
    bool debugLogging = false;

    uint32 groupCooldownMs = 12000;
    uint32 botCooldownMs = 30000;
    uint32 eventTypeCooldownMs = 60000;
    uint32 maxMessagesPerGroupPerMinute = 4;
    uint32 maxMessagesPerBotPerMinute = 2;

    uint32 postWipeQuietMs = 20000;
    uint32 postCompletionQuietMs = 10000;
    uint32 postBossQuietMs = 8000;
};

struct BotSpeakerCandidate
{
    Player* bot = nullptr;
    uint32 relatedPlayerGuid = 0;
    uint32 preferredBotGuid = 0;
};

struct BotGroupDialogueStateView
{
    uint32 groupId = 0;
    uint32 lastSpeakerGuid = 0;
    std::string lastSpeakerName;
    BotProactiveDialogueEvent lastEvent =
        BotProactiveDialogueEvent::GroupJoined;
    uint32 lastMessageAgeSeconds = 0;
    uint32 banterReplies = 0;
    uint32 queuedEvents = 0;
    uint32 quietRemainingSeconds = 0;
};

struct BotGroupDialogueCoordinatorStats
{
    std::size_t groupStates = 0;
    std::size_t botCooldowns = 0;
    std::size_t groupCooldowns = 0;
    std::size_t eventCooldowns = 0;
};

class BotGroupDialogueCoordinator
{
public:
    void SetConfig(BotGroupDialogueConfig const& config);

    Player* SelectSpeaker(
        uint32 groupId,
        BotProactiveDialogueEvent event,
        BotDialoguePriority priority,
        std::vector<BotSpeakerCandidate> const& candidates,
        uint32 nowMs,
        bool ignoreCooldowns);

    bool CanSpeak(
        uint32 groupId,
        uint32 botGuid,
        BotProactiveDialogueEvent event,
        BotDialoguePriority priority,
        uint32 nowMs,
        bool ignoreCooldowns) const;

    void RecordMessage(
        uint32 groupId,
        Player* bot,
        BotProactiveDialogueEvent event,
        uint32 nowMs,
        bool isBanterReply);

    bool CanQueueTopic(
        uint32 groupId,
        BotProactiveDialogueEvent event,
        BotDialoguePriority priority,
        uint32 nowMs) const;

    uint32 GetBanterReplies(uint32 groupId) const;
    uint32 GetLastSpeakerGuid(uint32 groupId) const;
    std::string GetLastSpeakerName(uint32 groupId) const;

    void NoteQueuedEvent(uint32 groupId);
    void NoteDequeuedEvent(uint32 groupId);
    void ClearCooldowns();
    void ClearGroup(uint32 groupId);
    void Cleanup(uint32 nowMs);

    BotGroupDialogueStateView GetGroupState(
        uint32 groupId,
        uint32 nowMs) const;
    BotGroupDialogueCoordinatorStats GetStats() const;

private:
    struct GroupState
    {
        uint32 lastSpeakerGuid = 0;
        std::string lastSpeakerName;
        BotProactiveDialogueEvent lastEvent =
            BotProactiveDialogueEvent::GroupJoined;
        uint32 lastMessageMs = 0;
        uint32 quietUntilMs = 0;
        uint32 banterReplies = 0;
        uint32 queuedEvents = 0;
        std::deque<uint32> messageWindow;
    };

    uint64 MakeEventCooldownKey(
        uint32 groupId,
        BotProactiveDialogueEvent event) const;
    int32 ScoreCandidate(
        BotSpeakerCandidate const& candidate,
        uint32 groupId,
        BotProactiveDialogueEvent event,
        uint32 nowMs) const;
    bool IsRateLimited(
        std::deque<uint32> const& window,
        uint32 maxMessages,
        uint32 nowMs) const;

    BotGroupDialogueConfig _config;
    std::unordered_map<uint32, GroupState> _groupStates;
    std::unordered_map<uint32, uint32> _groupCooldowns;
    std::unordered_map<uint32, uint32> _botCooldowns;
    std::unordered_map<uint64, uint32> _eventCooldowns;
    std::unordered_map<uint32, std::deque<uint32>> _botMessageWindows;
};

#endif
