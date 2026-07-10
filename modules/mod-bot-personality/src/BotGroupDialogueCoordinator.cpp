#include "BotGroupDialogueCoordinator.h"

#include "BotMoodMgr.h"
#include "BotPersonalityMgr.h"
#include "BotRelationshipMgr.h"
#include "Log.h"
#include "Player.h"
#include "Timer.h"

#include <algorithm>

namespace
{
constexpr uint32 RATE_WINDOW_MS = 60000;
constexpr uint32 CLEANUP_AFTER_MS = 10 * 60 * 1000;

uint32 Mix(uint32 value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}
}

void BotGroupDialogueCoordinator::SetConfig(
    BotGroupDialogueConfig const& config)
{
    _config = config;
}

Player* BotGroupDialogueCoordinator::SelectSpeaker(
    uint32 groupId,
    BotProactiveDialogueEvent event,
    BotDialoguePriority priority,
    std::vector<BotSpeakerCandidate> const& candidates,
    uint32 nowMs,
    bool ignoreCooldowns)
{
    Player* selected = nullptr;
    int32 selectedScore = -1000000;

    for (BotSpeakerCandidate const& candidate : candidates)
    {
        if (!candidate.bot)
            continue;

        uint32 const botGuid = candidate.bot->GetGUID().GetCounter();
        if (!CanSpeak(
                groupId,
                botGuid,
                event,
                priority,
                nowMs,
                ignoreCooldowns))
            continue;

        int32 const score = ScoreCandidate(candidate, groupId, event, nowMs);
        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.proactive",
                "Speaker candidate bot {} event {} score {}",
                botGuid,
                BotProactiveDialogueEventToString(event),
                score);
        }

        if (!selected || score > selectedScore)
        {
            selected = candidate.bot;
            selectedScore = score;
        }
    }

    if (selected && _config.debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.proactive",
            "Selected speaker bot {} for group {} event {}",
            selected->GetGUID().GetCounter(),
            groupId,
            BotProactiveDialogueEventToString(event));
    }

    return selected;
}

bool BotGroupDialogueCoordinator::CanSpeak(
    uint32 groupId,
    uint32 botGuid,
    BotProactiveDialogueEvent event,
    BotDialoguePriority priority,
    uint32 nowMs,
    bool ignoreCooldowns) const
{
    if (ignoreCooldowns)
        return true;

    auto stateItr = _groupStates.find(groupId);
    if (stateItr != _groupStates.end())
    {
        GroupState const& state = stateItr->second;
        if (state.quietUntilMs &&
            static_cast<int32>(state.quietUntilMs - nowMs) > 0 &&
            priority < BotDialoguePriority::High)
            return false;

        if (IsRateLimited(
                state.messageWindow,
                _config.maxMessagesPerGroupPerMinute,
                nowMs))
            return false;
    }

    auto groupCooldown = _groupCooldowns.find(groupId);
    if (groupCooldown != _groupCooldowns.end() &&
        static_cast<int32>(groupCooldown->second - nowMs) > 0 &&
        priority < BotDialoguePriority::Critical)
        return false;

    auto botCooldown = _botCooldowns.find(botGuid);
    if (botCooldown != _botCooldowns.end() &&
        static_cast<int32>(botCooldown->second - nowMs) > 0)
        return false;

    auto eventCooldown = _eventCooldowns.find(
        MakeEventCooldownKey(groupId, event));
    if (eventCooldown != _eventCooldowns.end() &&
        static_cast<int32>(eventCooldown->second - nowMs) > 0 &&
        priority < BotDialoguePriority::Critical)
        return false;

    auto botWindow = _botMessageWindows.find(botGuid);
    if (botWindow != _botMessageWindows.end() &&
        IsRateLimited(
            botWindow->second,
            _config.maxMessagesPerBotPerMinute,
            nowMs))
        return false;

    return true;
}

void BotGroupDialogueCoordinator::RecordMessage(
    uint32 groupId,
    Player* bot,
    BotProactiveDialogueEvent event,
    uint32 nowMs,
    bool isBanterReply)
{
    if (!bot)
        return;

    uint32 const botGuid = bot->GetGUID().GetCounter();
    GroupState& state = _groupStates[groupId];
    state.lastSpeakerGuid = botGuid;
    state.lastSpeakerName = bot->GetName();
    state.lastEvent = event;
    state.lastMessageMs = nowMs;
    state.messageWindow.push_back(nowMs);
    if (isBanterReply)
        ++state.banterReplies;
    else
        state.banterReplies = 0;

    while (!state.messageWindow.empty() &&
        getMSTimeDiff(state.messageWindow.front(), nowMs) >= RATE_WINDOW_MS)
        state.messageWindow.pop_front();

    uint32 quietMs = 0;
    switch (event)
    {
        case BotProactiveDialogueEvent::GroupWipe:
            quietMs = _config.postWipeQuietMs;
            break;
        case BotProactiveDialogueEvent::DungeonCompleted:
        case BotProactiveDialogueEvent::RaidEncounterCompleted:
            quietMs = _config.postCompletionQuietMs;
            break;
        case BotProactiveDialogueEvent::SharedBossKilled:
            quietMs = _config.postBossQuietMs;
            break;
        default:
            break;
    }

    if (quietMs)
        state.quietUntilMs = nowMs + quietMs;

    _groupCooldowns[groupId] = nowMs + _config.groupCooldownMs;
    _botCooldowns[botGuid] = nowMs + _config.botCooldownMs;
    _eventCooldowns[MakeEventCooldownKey(groupId, event)] =
        nowMs + _config.eventTypeCooldownMs;

    std::deque<uint32>& botWindow = _botMessageWindows[botGuid];
    botWindow.push_back(nowMs);
    while (!botWindow.empty() &&
        getMSTimeDiff(botWindow.front(), nowMs) >= RATE_WINDOW_MS)
        botWindow.pop_front();
}

bool BotGroupDialogueCoordinator::CanQueueTopic(
    uint32 groupId,
    BotProactiveDialogueEvent event,
    BotDialoguePriority priority,
    uint32 nowMs) const
{
    if (priority >= BotDialoguePriority::Critical)
        return true;

    auto eventCooldown = _eventCooldowns.find(
        MakeEventCooldownKey(groupId, event));
    if (eventCooldown != _eventCooldowns.end() &&
        static_cast<int32>(eventCooldown->second - nowMs) > 0)
        return false;

    return true;
}

uint32 BotGroupDialogueCoordinator::GetBanterReplies(uint32 groupId) const
{
    auto itr = _groupStates.find(groupId);
    return itr == _groupStates.end() ? 0 : itr->second.banterReplies;
}

uint32 BotGroupDialogueCoordinator::GetLastSpeakerGuid(uint32 groupId) const
{
    auto itr = _groupStates.find(groupId);
    return itr == _groupStates.end() ? 0 : itr->second.lastSpeakerGuid;
}

std::string BotGroupDialogueCoordinator::GetLastSpeakerName(
    uint32 groupId) const
{
    auto itr = _groupStates.find(groupId);
    return itr == _groupStates.end() ? std::string() :
        itr->second.lastSpeakerName;
}

void BotGroupDialogueCoordinator::NoteQueuedEvent(uint32 groupId)
{
    ++_groupStates[groupId].queuedEvents;
}

void BotGroupDialogueCoordinator::NoteDequeuedEvent(uint32 groupId)
{
    GroupState& state = _groupStates[groupId];
    if (state.queuedEvents)
        --state.queuedEvents;
}

void BotGroupDialogueCoordinator::ClearCooldowns()
{
    _groupCooldowns.clear();
    _botCooldowns.clear();
    _eventCooldowns.clear();
    _botMessageWindows.clear();

    for (auto& pair : _groupStates)
    {
        pair.second.messageWindow.clear();
        pair.second.quietUntilMs = 0;
        pair.second.banterReplies = 0;
    }
}

void BotGroupDialogueCoordinator::ClearGroup(uint32 groupId)
{
    _groupStates.erase(groupId);
    _groupCooldowns.erase(groupId);

    for (auto itr = _eventCooldowns.begin(); itr != _eventCooldowns.end();)
    {
        if ((itr->first >> 32) == groupId)
            itr = _eventCooldowns.erase(itr);
        else
            ++itr;
    }
}

void BotGroupDialogueCoordinator::Cleanup(uint32 nowMs)
{
    for (auto itr = _groupCooldowns.begin(); itr != _groupCooldowns.end();)
    {
        if (static_cast<int32>(itr->second - nowMs) <= 0)
            itr = _groupCooldowns.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _botCooldowns.begin(); itr != _botCooldowns.end();)
    {
        if (static_cast<int32>(itr->second - nowMs) <= 0)
            itr = _botCooldowns.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _eventCooldowns.begin(); itr != _eventCooldowns.end();)
    {
        if (static_cast<int32>(itr->second - nowMs) <= 0)
            itr = _eventCooldowns.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _botMessageWindows.begin();
        itr != _botMessageWindows.end();)
    {
        std::deque<uint32>& window = itr->second;
        while (!window.empty() &&
            getMSTimeDiff(window.front(), nowMs) >= RATE_WINDOW_MS)
            window.pop_front();

        if (window.empty())
            itr = _botMessageWindows.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _groupStates.begin(); itr != _groupStates.end();)
    {
        GroupState& state = itr->second;
        while (!state.messageWindow.empty() &&
            getMSTimeDiff(state.messageWindow.front(), nowMs) >=
                RATE_WINDOW_MS)
            state.messageWindow.pop_front();

        if (!state.queuedEvents &&
            state.messageWindow.empty() &&
            (!state.lastMessageMs ||
                getMSTimeDiff(state.lastMessageMs, nowMs) >
                    CLEANUP_AFTER_MS))
            itr = _groupStates.erase(itr);
        else
            ++itr;
    }
}

BotGroupDialogueStateView BotGroupDialogueCoordinator::GetGroupState(
    uint32 groupId,
    uint32 nowMs) const
{
    BotGroupDialogueStateView view;
    view.groupId = groupId;

    auto itr = _groupStates.find(groupId);
    if (itr == _groupStates.end())
        return view;

    GroupState const& state = itr->second;
    view.lastSpeakerGuid = state.lastSpeakerGuid;
    view.lastSpeakerName = state.lastSpeakerName;
    view.lastEvent = state.lastEvent;
    view.banterReplies = state.banterReplies;
    view.queuedEvents = state.queuedEvents;

    if (state.lastMessageMs)
        view.lastMessageAgeSeconds =
            getMSTimeDiff(state.lastMessageMs, nowMs) / IN_MILLISECONDS;

    if (state.quietUntilMs &&
        static_cast<int32>(state.quietUntilMs - nowMs) > 0)
        view.quietRemainingSeconds =
            (state.quietUntilMs - nowMs) / IN_MILLISECONDS;

    return view;
}

BotGroupDialogueCoordinatorStats
BotGroupDialogueCoordinator::GetStats() const
{
    BotGroupDialogueCoordinatorStats stats;
    stats.groupStates = _groupStates.size();
    stats.botCooldowns = _botCooldowns.size();
    stats.groupCooldowns = _groupCooldowns.size();
    stats.eventCooldowns = _eventCooldowns.size();
    return stats;
}

uint64 BotGroupDialogueCoordinator::MakeEventCooldownKey(
    uint32 groupId,
    BotProactiveDialogueEvent event) const
{
    return (static_cast<uint64>(groupId) << 32) |
        static_cast<uint64>(static_cast<uint32>(event));
}

int32 BotGroupDialogueCoordinator::ScoreCandidate(
    BotSpeakerCandidate const& candidate,
    uint32 groupId,
    BotProactiveDialogueEvent event,
    uint32 nowMs) const
{
    Player* bot = candidate.bot;
    if (!bot)
        return -1000000;

    uint32 const botGuid = bot->GetGUID().GetCounter();
    BotPersonality const* personality =
        sBotPersonalityMgr.GetOrCreatePersonality(bot);
    if (!personality)
        return -1000000;

    int32 score = 50;
    score += static_cast<int32>(personality->talkativeness) / 4;

    if (candidate.preferredBotGuid &&
        candidate.preferredBotGuid == botGuid)
        score += 80;

    auto stateItr = _groupStates.find(groupId);
    if (stateItr != _groupStates.end() &&
        stateItr->second.lastSpeakerGuid == botGuid)
        score -= 35;

    if (personality->archetype == BotPersonalityArchetype::Stoic)
        score -= 15;
    else if (personality->archetype == BotPersonalityArchetype::Helpful &&
        (event == BotProactiveDialogueEvent::BotHealed ||
            event == BotProactiveDialogueEvent::BotResurrected ||
            event == BotProactiveDialogueEvent::SustainedTeamwork))
        score += 10;
    else if (personality->archetype ==
        BotPersonalityArchetype::Competitive &&
        (event == BotProactiveDialogueEvent::SharedBossKilled ||
            event == BotProactiveDialogueEvent::DungeonCompleted ||
            event == BotProactiveDialogueEvent::RaidEncounterCompleted))
        score += 12;

    if (candidate.relatedPlayerGuid)
    {
        BotRelationship const* relationship =
            sBotRelationshipMgr.GetCachedRelationship(
                botGuid,
                candidate.relatedPlayerGuid);
        if (relationship)
            score += relationship->affinity / 100;
    }

    score += sBotMoodMgr.GetMoodIntensityFor(bot) / 8;
    score += static_cast<int32>(Mix(botGuid ^ nowMs) % 7);
    return score;
}

bool BotGroupDialogueCoordinator::IsRateLimited(
    std::deque<uint32> const& window,
    uint32 maxMessages,
    uint32 nowMs) const
{
    if (!maxMessages)
        return true;

    uint32 count = 0;
    for (uint32 timestamp : window)
    {
        if (getMSTimeDiff(timestamp, nowMs) < RATE_WINDOW_MS)
            ++count;
    }

    return count >= maxMessages;
}
