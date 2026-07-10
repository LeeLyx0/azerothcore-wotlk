#include "BotProactiveDialogueMgr.h"

#include "BotMoodMgr.h"
#include "BotPersonalityMgr.h"
#include "BotProactiveTemplates.h"
#include "BotRelationshipMgr.h"
#include "Chat.h"
#include "Config.h"
#include "GameTime.h"
#include "Group.h"
#include "GroupReference.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "Util.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <limits>

namespace
{
constexpr uint32 CHAT_LENGTH_LIMIT = 255;
constexpr uint32 CLEANUP_INTERVAL_MS = 60000;
constexpr uint32 TRACKED_BOT_SCAN_INTERVAL_MS = 5000;
constexpr uint32 TRACKED_BOT_BATCH = 100;
constexpr uint32 TRACKED_BOT_EXPIRY_MS = 2 * 60 * 60 * 1000;

uint32 CurrentGameTimeSeconds()
{
    return static_cast<uint32>(GameTime::GetGameTime().count());
}

uint32 Mix(uint32 value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
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
            "module.botpersonality.proactive",
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

uint32 ReadMsConfig(
    char const* name,
    int32 defaultValue,
    int32 minValue,
    int32 maxValue,
    bool debugLogging)
{
    return ReadUIntConfig(
        name,
        defaultValue,
        minValue,
        maxValue,
        debugLogging);
}

BotMoodEvent MoodEventForProactive(BotProactiveDialogueEvent event)
{
    switch (event)
    {
        case BotProactiveDialogueEvent::GroupJoined:
            return BotMoodEvent::JoinedGroup;
        case BotProactiveDialogueEvent::DungeonEntered:
            return BotMoodEvent::EnteredDungeon;
        case BotProactiveDialogueEvent::SharedEliteKilled:
            return BotMoodEvent::SharedEliteKill;
        case BotProactiveDialogueEvent::SharedBossKilled:
            return BotMoodEvent::SharedBossKill;
        case BotProactiveDialogueEvent::BotHealed:
            return BotMoodEvent::BotWasHealed;
        case BotProactiveDialogueEvent::BotSaved:
            return BotMoodEvent::BotWasSaved;
        case BotProactiveDialogueEvent::BotResurrected:
            return BotMoodEvent::BotWasResurrected;
        case BotProactiveDialogueEvent::PlayerDied:
            return BotMoodEvent::PlayerDied;
        case BotProactiveDialogueEvent::RepeatedPlayerDeath:
            return BotMoodEvent::RepeatedPlayerDeath;
        case BotProactiveDialogueEvent::BotDied:
            return BotMoodEvent::BotDied;
        case BotProactiveDialogueEvent::GroupWipe:
            return BotMoodEvent::GroupWipe;
        case BotProactiveDialogueEvent::DungeonCompleted:
            return BotMoodEvent::DungeonCompleted;
        case BotProactiveDialogueEvent::RaidEncounterCompleted:
            return BotMoodEvent::RaidEncounterCompleted;
        case BotProactiveDialogueEvent::DangerousPull:
            return BotMoodEvent::DangerousPull;
        case BotProactiveDialogueEvent::PlayerAbandonedCombat:
            return BotMoodEvent::PlayerAbandonedCombat;
        case BotProactiveDialogueEvent::BotLowHealth:
            return BotMoodEvent::LowHealth;
        case BotProactiveDialogueEvent::BotCriticalHealth:
            return BotMoodEvent::CriticalHealth;
        case BotProactiveDialogueEvent::BotLowMana:
            return BotMoodEvent::LowMana;
        case BotProactiveDialogueEvent::LongInactivity:
            return BotMoodEvent::LongInactivity;
        case BotProactiveDialogueEvent::SustainedTeamwork:
        case BotProactiveDialogueEvent::RelationshipImproved:
            return BotMoodEvent::SustainedTeamwork;
        case BotProactiveDialogueEvent::RelationshipWorsened:
            return BotMoodEvent::PlayerInsultsBot;
        case BotProactiveDialogueEvent::BotReplyToBot:
            return BotMoodEvent::PlayerGreeting;
    }

    return BotMoodEvent::PlayerGreeting;
}

std::optional<BotProactiveDialogueEvent> ProactiveEventForGameplay(
    BotGameplayEvent event)
{
    switch (event)
    {
        case BotGameplayEvent::SharedEliteKill:
            return BotProactiveDialogueEvent::SharedEliteKilled;
        case BotGameplayEvent::SharedBossKill:
            return BotProactiveDialogueEvent::SharedBossKilled;
        case BotGameplayEvent::PlayerHealedBot:
            return BotProactiveDialogueEvent::BotHealed;
        case BotGameplayEvent::PlayerResurrectedBot:
            return BotProactiveDialogueEvent::BotResurrected;
        case BotGameplayEvent::PlayerDied:
            return BotProactiveDialogueEvent::PlayerDied;
        case BotGameplayEvent::BotDied:
            return BotProactiveDialogueEvent::BotDied;
        case BotGameplayEvent::GroupWipe:
            return BotProactiveDialogueEvent::GroupWipe;
        case BotGameplayEvent::PlayerLeftGroupDuringCombat:
            return BotProactiveDialogueEvent::PlayerAbandonedCombat;
        case BotGameplayEvent::DungeonCompleted:
            return BotProactiveDialogueEvent::DungeonCompleted;
        case BotGameplayEvent::RaidEncounterCompleted:
            return BotProactiveDialogueEvent::RaidEncounterCompleted;
        case BotGameplayEvent::SustainedTeamwork:
            return BotProactiveDialogueEvent::SustainedTeamwork;
        case BotGameplayEvent::RepeatedPlayerDeath:
            return BotProactiveDialogueEvent::RepeatedPlayerDeath;
        case BotGameplayEvent::SharedNormalKill:
        case BotGameplayEvent::BotHealedPlayer:
        case BotGameplayEvent::BotResurrectedPlayer:
        case BotGameplayEvent::SharedDeath:
            return std::nullopt;
    }

    return std::nullopt;
}

bool IsGroupLevelEvent(BotProactiveDialogueEvent event)
{
    switch (event)
    {
        case BotProactiveDialogueEvent::SharedEliteKilled:
        case BotProactiveDialogueEvent::SharedBossKilled:
        case BotProactiveDialogueEvent::GroupWipe:
        case BotProactiveDialogueEvent::DungeonCompleted:
        case BotProactiveDialogueEvent::RaidEncounterCompleted:
        case BotProactiveDialogueEvent::LongInactivity:
        case BotProactiveDialogueEvent::SustainedTeamwork:
            return true;
        default:
            return false;
    }
}

uint64 MakeRecentResponseKey(
    uint32 botGuid,
    BotProactiveDialogueEvent event)
{
    return (static_cast<uint64>(botGuid) << 32) |
        static_cast<uint64>(static_cast<uint32>(event));
}
}

void BotProactiveDialogueMgr::LoadConfig(bool reload)
{
    _config.enable = sConfigMgr->GetOption<bool>(
        "BotPersonality.ProactiveChat.Enable",
        true);
    _config.debugLogging = sConfigMgr->GetOption<bool>(
        "BotPersonality.ProactiveChat.DebugLogging",
        false);
    _config.requireRealPlayerPresent = sConfigMgr->GetOption<bool>(
        "BotPersonality.ProactiveChat.RequireRealPlayerPresent",
        true);
    _config.enablePartyChat = sConfigMgr->GetOption<bool>(
        "BotPersonality.ProactiveChat.EnablePartyChat",
        true);
    _config.enableRaidChat = sConfigMgr->GetOption<bool>(
        "BotPersonality.ProactiveChat.EnableRaidChat",
        true);
    _config.enableSay = sConfigMgr->GetOption<bool>(
        "BotPersonality.ProactiveChat.EnableSay",
        false);
    _config.enableBotBanter = sConfigMgr->GetOption<bool>(
        "BotPersonality.ProactiveChat.EnableBotBanter",
        true);
    _config.enableInactivityDialogue = sConfigMgr->GetOption<bool>(
        "BotPersonality.ProactiveChat.EnableInactivityDialogue",
        true);
    _config.forceDialogueForDebug = sConfigMgr->GetOption<bool>(
        "BotPersonality.ProactiveChat.ForceDialogueForDebug",
        false);

    _config.botBanterChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.BotBanterChance",
        15,
        0,
        100,
        _config.debugLogging);
    _config.botBanterDelayMinMs = ReadMsConfig(
        "BotPersonality.ProactiveChat.BotBanterDelayMinMs",
        1500,
        0,
        60000,
        _config.debugLogging);
    _config.botBanterDelayMaxMs = ReadMsConfig(
        "BotPersonality.ProactiveChat.BotBanterDelayMaxMs",
        4000,
        0,
        60000,
        _config.debugLogging);
    if (_config.botBanterDelayMaxMs < _config.botBanterDelayMinMs)
        _config.botBanterDelayMaxMs = _config.botBanterDelayMinMs;
    _config.maxBanterReplies = ReadUIntConfig(
        "BotPersonality.ProactiveChat.MaxBanterReplies",
        1,
        0,
        1,
        _config.debugLogging);

    _config.delayMinMs = ReadMsConfig(
        "BotPersonality.ProactiveChat.DelayMinMs",
        500,
        0,
        60000,
        _config.debugLogging);
    _config.delayMaxMs = ReadMsConfig(
        "BotPersonality.ProactiveChat.DelayMaxMs",
        2500,
        0,
        60000,
        _config.debugLogging);
    if (_config.delayMaxMs < _config.delayMinMs)
        _config.delayMaxMs = _config.delayMinMs;

    BotGroupDialogueConfig coordinatorConfig;
    coordinatorConfig.debugLogging = _config.debugLogging;
    coordinatorConfig.groupCooldownMs = ReadMsConfig(
        "BotPersonality.ProactiveChat.GroupCooldownMs",
        12000,
        0,
        600000,
        _config.debugLogging);
    coordinatorConfig.botCooldownMs = ReadMsConfig(
        "BotPersonality.ProactiveChat.BotCooldownMs",
        30000,
        0,
        600000,
        _config.debugLogging);
    coordinatorConfig.eventTypeCooldownMs = ReadMsConfig(
        "BotPersonality.ProactiveChat.EventTypeCooldownMs",
        60000,
        0,
        3600000,
        _config.debugLogging);
    coordinatorConfig.maxMessagesPerGroupPerMinute = ReadUIntConfig(
        "BotPersonality.ProactiveChat.MaxMessagesPerGroupPerMinute",
        4,
        1,
        60,
        _config.debugLogging);
    coordinatorConfig.maxMessagesPerBotPerMinute = ReadUIntConfig(
        "BotPersonality.ProactiveChat.MaxMessagesPerBotPerMinute",
        2,
        1,
        60,
        _config.debugLogging);
    coordinatorConfig.postWipeQuietMs = ReadUIntConfig(
        "BotPersonality.ProactiveChat.PostWipeQuietSeconds",
        20,
        0,
        3600,
        _config.debugLogging) * IN_MILLISECONDS;
    coordinatorConfig.postCompletionQuietMs = ReadUIntConfig(
        "BotPersonality.ProactiveChat.PostCompletionQuietSeconds",
        10,
        0,
        3600,
        _config.debugLogging) * IN_MILLISECONDS;
    coordinatorConfig.postBossQuietMs = ReadUIntConfig(
        "BotPersonality.ProactiveChat.PostBossQuietSeconds",
        8,
        0,
        3600,
        _config.debugLogging) * IN_MILLISECONDS;
    _coordinator.SetConfig(coordinatorConfig);

    _config.maxQueuedEventsGlobal = ReadUIntConfig(
        "BotPersonality.ProactiveChat.MaxQueuedEventsGlobal",
        1000,
        1,
        10000,
        _config.debugLogging);
    _config.maxQueuedEventsPerGroup = ReadUIntConfig(
        "BotPersonality.ProactiveChat.MaxQueuedEventsPerGroup",
        10,
        1,
        100,
        _config.debugLogging);
    _config.eventExpirySeconds = ReadUIntConfig(
        "BotPersonality.ProactiveChat.EventExpirySeconds",
        20,
        1,
        300,
        _config.debugLogging);

    _config.groupJoinChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.GroupJoinChance",
        35,
        0,
        100,
        _config.debugLogging);
    _config.dungeonEnterChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.DungeonEnterChance",
        40,
        0,
        100,
        _config.debugLogging);
    _config.eliteKillChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.EliteKillChance",
        15,
        0,
        100,
        _config.debugLogging);
    _config.bossKillChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.BossKillChance",
        75,
        0,
        100,
        _config.debugLogging);
    _config.botHealedChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.BotHealedChance",
        15,
        0,
        100,
        _config.debugLogging);
    _config.botSavedChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.BotSavedChance",
        65,
        0,
        100,
        _config.debugLogging);
    _config.botResurrectedChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.BotResurrectedChance",
        80,
        0,
        100,
        _config.debugLogging);
    _config.playerDeathChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.PlayerDeathChance",
        10,
        0,
        100,
        _config.debugLogging);
    _config.repeatedPlayerDeathChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.RepeatedPlayerDeathChance",
        45,
        0,
        100,
        _config.debugLogging);
    _config.botDeathChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.BotDeathChance",
        20,
        0,
        100,
        _config.debugLogging);
    _config.wipeChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.WipeChance",
        70,
        0,
        100,
        _config.debugLogging);
    _config.dungeonCompleteChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.DungeonCompleteChance",
        90,
        0,
        100,
        _config.debugLogging);
    _config.raidEncounterCompleteChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.RaidEncounterCompleteChance",
        80,
        0,
        100,
        _config.debugLogging);
    _config.dangerousPullChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.DangerousPullChance",
        30,
        0,
        100,
        _config.debugLogging);
    _config.abandonmentChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.AbandonmentChance",
        75,
        0,
        100,
        _config.debugLogging);
    _config.lowHealthChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.LowHealthChance",
        5,
        0,
        100,
        _config.debugLogging);
    _config.criticalHealthChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.CriticalHealthChance",
        25,
        0,
        100,
        _config.debugLogging);
    _config.lowManaChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.LowManaChance",
        8,
        0,
        100,
        _config.debugLogging);
    _config.inactivityChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.InactivityChance",
        15,
        0,
        100,
        _config.debugLogging);
    _config.teamworkChance = ReadUIntConfig(
        "BotPersonality.ProactiveChat.TeamworkChance",
        20,
        0,
        100,
        _config.debugLogging);

    _config.lowHealthPercent = ReadUIntConfig(
        "BotPersonality.ProactiveChat.LowHealthPercent",
        30,
        1,
        99,
        _config.debugLogging);
    _config.criticalHealthPercent = ReadUIntConfig(
        "BotPersonality.ProactiveChat.CriticalHealthPercent",
        15,
        1,
        99,
        _config.debugLogging);
    if (_config.criticalHealthPercent >= _config.lowHealthPercent)
        _config.criticalHealthPercent = _config.lowHealthPercent - 1;
    _config.lowManaPercent = ReadUIntConfig(
        "BotPersonality.ProactiveChat.LowManaPercent",
        20,
        1,
        99,
        _config.debugLogging);
    _config.lowHealthCooldownMs = ReadUIntConfig(
        "BotPersonality.ProactiveChat.LowHealthCooldownSeconds",
        120,
        0,
        3600,
        _config.debugLogging) * IN_MILLISECONDS;
    _config.criticalHealthCooldownMs = ReadUIntConfig(
        "BotPersonality.ProactiveChat.CriticalHealthCooldownSeconds",
        60,
        0,
        3600,
        _config.debugLogging) * IN_MILLISECONDS;
    _config.lowManaCooldownMs = ReadUIntConfig(
        "BotPersonality.ProactiveChat.LowManaCooldownSeconds",
        120,
        0,
        3600,
        _config.debugLogging) * IN_MILLISECONDS;
    _config.inactivityMinutes = ReadUIntConfig(
        "BotPersonality.ProactiveChat.InactivityMinutes",
        5,
        1,
        1440,
        _config.debugLogging);
    _config.inactivityCooldownMinutes = ReadUIntConfig(
        "BotPersonality.ProactiveChat.InactivityCooldownMinutes",
        15,
        1,
        1440,
        _config.debugLogging);

    if (!IsEnabled())
        ClearAll();

    LOG_INFO(
        "server.loading",
        "Bot Personality Phase 5 proactive chat {}{}",
        IsEnabled() ? "enabled" : "disabled",
        reload ? " after config reload" : "");
}

void BotProactiveDialogueMgr::Update(uint32 /*diff*/)
{
    if (!IsEnabled())
        return;

    uint32 const nowMs = getMSTime();
    ProcessPendingEvents(nowMs);
    ProcessTrackedBots(nowMs);
    Cleanup(nowMs);
}

void BotProactiveDialogueMgr::ClearQueue()
{
    _queue.clear();
    _queuedPerGroup.clear();
}

void BotProactiveDialogueMgr::ClearCooldowns()
{
    _coordinator.ClearCooldowns();
    _recentResponses.clear();
}

void BotProactiveDialogueMgr::ClearAll()
{
    ClearQueue();
    ClearCooldowns();
    _trackedBots.clear();
}

bool BotProactiveDialogueMgr::IsEnabled() const
{
    return sBotPersonalityMgr.IsEnabled() && _config.enable;
}

void BotProactiveDialogueMgr::TrackBot(Player* bot)
{
    if (!IsEnabled() || !IsValidOnlinePlayer(bot) ||
        !sBotPersonalityMgr.IsPlayerbot(bot))
        return;

    uint32 const nowMs = getMSTime();
    uint32 const botGuid = bot->GetGUID().GetCounter();
    BotLiveState& state = _trackedBots[botGuid];
    if (!state.lastActivityMs)
        state.lastActivityMs = nowMs;

    state.lastSeenMs = nowMs;
    state.mapId = bot->GetMapId();
    state.instanceId = bot->GetInstanceId();
    sBotMoodMgr.GetMood(bot);
}

void BotProactiveDialogueMgr::OnPlayerLogout(Player* player)
{
    if (!player)
        return;

    ObjectGuid const guid = player->GetGUID();
    RemoveEventsForGuid(guid);
    if (sBotPersonalityMgr.IsPlayerbot(player))
        _trackedBots.erase(guid.GetCounter());
}

void BotProactiveDialogueMgr::OnPlayerMapChanged(Player* player)
{
    if (!player)
        return;

    RemoveEventsForGuid(player->GetGUID());
    if (sBotPersonalityMgr.IsPlayerbot(player))
    {
        TrackBot(player);
        BotLiveState& state = _trackedBots[player->GetGUID().GetCounter()];
        state.mapId = player->GetMapId();
        state.instanceId = player->GetInstanceId();
        state.healthState = 0;
        state.lowManaActive = false;

        if (player->GetMap() && player->GetMap()->IsDungeon())
        {
            BotGameplayContext context;
            context.mapId = player->GetMapId();
            context.instanceId = player->GetInstanceId();
            context.isDungeon = player->GetMap()->IsDungeon();
            context.isRaid = player->GetMap()->IsRaid();
            QueueEvent(
                BotProactiveDialogueEvent::DungeonEntered,
                player,
                nullptr,
                context);
        }
    }
}

void BotProactiveDialogueMgr::OnGroupMemberAdded(
    Group* group,
    ObjectGuid guid)
{
    if (!IsEnabled() || !group || guid.IsEmpty())
        return;

    Player* member = ObjectAccessor::FindConnectedPlayer(guid);
    if (!member)
        return;

    if (sBotPersonalityMgr.IsPlayerbot(member))
    {
        TrackBot(member);
        sBotMoodMgr.ApplyMoodEvent(
            member,
            BotMoodEvent::JoinedGroup,
            GetGroupId(group));

        BotGameplayContext context;
        if (Map const* map = member->GetMap())
        {
            context.mapId = map->GetId();
            context.instanceId = member->GetInstanceId();
            context.isDungeon = map->IsDungeon();
            context.isRaid = map->IsRaid();
        }

        QueueEvent(
            BotProactiveDialogueEvent::GroupJoined,
            member,
            nullptr,
            context,
            false,
            false,
            GetGroupId(group));
    }
    else if (IsRealPlayer(member))
    {
        for (GroupReference* itr = group->GetFirstMember();
            itr != nullptr;
            itr = itr->next())
        {
            Player* bot = itr->GetSource();
            if (sBotPersonalityMgr.IsPlayerbot(bot))
                TrackBot(bot);
        }
    }
}

void BotProactiveDialogueMgr::OnGroupMemberRemoved(
    Group* group,
    ObjectGuid guid)
{
    if (!group)
        return;

    RemoveEventsForGuid(guid);
    if (group->GetMembersCount() <= 1)
        RemoveEventsForGroup(GetGroupId(group));
}

void BotProactiveDialogueMgr::RecordGameplayEvent(
    Player* bot,
    Player* player,
    BotGameplayEvent event,
    BotGameplayContext const& gameplayContext)
{
    if (!IsEnabled() || !bot)
        return;

    TrackBot(bot);
    TouchActivity(bot, getMSTime());

    uint32 const sourceKey = MakeSourceKey(event, gameplayContext, player);
    BotMoodEvent const moodEvent =
        event == BotGameplayEvent::SharedNormalKill ?
            BotMoodEvent::SharedNormalKill :
            MoodEventForProactive(
                ProactiveEventForGameplay(event).value_or(
                    BotProactiveDialogueEvent::SustainedTeamwork));
    sBotMoodMgr.ApplyMoodEvent(bot, moodEvent, sourceKey);

    std::optional<BotProactiveDialogueEvent> proactiveEvent =
        ProactiveEventForGameplay(event);
    if (!proactiveEvent)
        return;

    QueueEvent(
        *proactiveEvent,
        bot,
        player,
        gameplayContext,
        false,
        false,
        sourceKey);
}

bool BotProactiveDialogueMgr::QueueEvent(
    BotProactiveDialogueEvent event,
    Player* preferredBot,
    Player* relatedPlayer,
    BotGameplayContext const& gameplayContext,
    bool force,
    bool isBanterReply,
    uint32 sourceKey)
{
    if (!IsEnabled() && !force)
        return false;

    Player* anchor = preferredBot ? preferredBot : relatedPlayer;
    if (!IsValidOnlinePlayer(anchor))
        return false;

    Group* group = anchor->GetGroup();
    uint32 const groupId = GetGroupId(group);
    if (!group && !_config.enableSay && !force)
        return false;

    if (group && _config.requireRealPlayerPresent && !HasRealPlayer(group))
        return false;

    BotDialoguePriority const priority = GetDialoguePriority(event);
    uint32 const nowMs = getMSTime();
    if (!force && !_coordinator.CanQueueTopic(
            groupId,
            event,
            priority,
            nowMs))
        return false;

    PendingEvent pending;
    pending.event = event;
    pending.priority = priority;
    pending.preferredBotGuid =
        preferredBot ? preferredBot->GetGUID() : ObjectGuid::Empty;
    pending.relatedPlayerGuid =
        relatedPlayer ? relatedPlayer->GetGUID() : ObjectGuid::Empty;
    pending.groupId = groupId;
    pending.mapId = gameplayContext.mapId;
    pending.zoneId = preferredBot ? preferredBot->GetZoneId() : 0;
    pending.instanceId = gameplayContext.instanceId;
    pending.sourceEntry = gameplayContext.sourceEntry;
    pending.value = gameplayContext.value;
    pending.queuedAtMs = nowMs;
    pending.deliverAtMs = nowMs + GetDelayMs(event, isBanterReply);
    pending.eventTime = CurrentGameTimeSeconds();
    pending.force = force;
    pending.bypassCooldowns = force;
    pending.isBanterReply = isBanterReply;
    pending.previousSpeakerName = _coordinator.GetLastSpeakerName(groupId);

    if (sourceKey)
        pending.sourceEntry ^= sourceKey;

    QueuePendingEvent(pending);
    return true;
}

BotProactiveDialogueDebugResult
BotProactiveDialogueMgr::SimulateDialogue(
    Player* bot,
    Player* player,
    BotProactiveDialogueEvent event,
    bool send,
    bool bypassCooldowns)
{
    BotProactiveDialogueDebugResult result;

    if (!bot || !sBotPersonalityMgr.IsPlayerbot(bot))
    {
        result.error = "Target bot must be an online Playerbot.";
        return result;
    }

    BotGameplayContext gameplayContext;
    if (Map const* map = bot->GetMap())
    {
        gameplayContext.mapId = map->GetId();
        gameplayContext.instanceId = bot->GetInstanceId();
        gameplayContext.isDungeon = map->IsDungeon();
        gameplayContext.isRaid = map->IsRaid();
    }

    PendingEvent pending;
    pending.event = event;
    pending.priority = GetDialoguePriority(event);
    pending.preferredBotGuid = bot->GetGUID();
    pending.relatedPlayerGuid = player ? player->GetGUID() : ObjectGuid::Empty;
    pending.groupId = GetGroupId(bot);
    pending.mapId = gameplayContext.mapId;
    pending.zoneId = bot->GetZoneId();
    pending.instanceId = gameplayContext.instanceId;
    pending.queuedAtMs = getMSTime();
    pending.deliverAtMs = pending.queuedAtMs;
    pending.eventTime = CurrentGameTimeSeconds();
    pending.force = true;
    pending.bypassCooldowns = bypassCooldowns;

    return BuildDebugResult(pending, bot, player, send);
}

BotProactiveDialogueStats BotProactiveDialogueMgr::GetStats() const
{
    BotProactiveDialogueStats stats;
    stats.enabled = IsEnabled();
    stats.partyChat = _config.enablePartyChat;
    stats.raidChat = _config.enableRaidChat;
    stats.say = _config.enableSay;
    stats.botBanter = _config.enableBotBanter;
    stats.requireRealPlayer = _config.requireRealPlayerPresent;
    stats.queuedEvents = _queue.size();
    stats.trackedBots = _trackedBots.size();
    stats.coordinator = _coordinator.GetStats();
    return stats;
}

std::vector<BotProactiveQueueEntry> BotProactiveDialogueMgr::GetQueue(
    uint32 limit) const
{
    std::vector<BotProactiveQueueEntry> entries;
    uint32 const nowMs = getMSTime();
    for (PendingEvent const& pending : _queue)
    {
        if (entries.size() >= limit)
            break;

        BotProactiveQueueEntry entry;
        entry.event = pending.event;
        entry.priority = pending.priority;
        entry.groupId = pending.groupId;
        entry.preferredBotGuid = pending.preferredBotGuid.GetCounter();
        entry.relatedPlayerGuid = pending.relatedPlayerGuid.GetCounter();
        entry.queuedAgeMs = getMSTimeDiff(pending.queuedAtMs, nowMs);
        if (static_cast<int32>(pending.deliverAtMs - nowMs) > 0)
            entry.delayRemainingMs = pending.deliverAtMs - nowMs;
        entries.push_back(entry);
    }

    return entries;
}

BotGroupDialogueStateView BotProactiveDialogueMgr::GetGroupState(
    Player* member) const
{
    return _coordinator.GetGroupState(GetGroupId(member), getMSTime());
}

void BotProactiveDialogueMgr::QueuePendingEvent(PendingEvent event)
{
    uint32 const groupId = event.groupId;
    uint32& queuedForGroup = _queuedPerGroup[groupId];

    for (auto itr = _queue.begin(); itr != _queue.end(); ++itr)
    {
        if (!IsSameTopic(*itr, event))
            continue;

        if (event.priority > itr->priority || event.force)
        {
            *itr = event;
            if (_config.debugLogging)
            {
                LOG_DEBUG(
                    "module.botpersonality.proactive",
                    "Replaced queued event for group {} with {}",
                    groupId,
                    BotProactiveDialogueEventToString(event.event));
            }
        }

        return;
    }

    while (_queue.size() >= _config.maxQueuedEventsGlobal)
    {
        PendingEvent const& old = _queue.front();
        if (_queuedPerGroup[old.groupId])
            --_queuedPerGroup[old.groupId];
        _coordinator.NoteDequeuedEvent(old.groupId);
        _queue.pop_front();
    }

    if (queuedForGroup >= _config.maxQueuedEventsPerGroup)
    {
        for (auto itr = _queue.begin(); itr != _queue.end(); ++itr)
        {
            if (itr->groupId != groupId)
                continue;

            if (event.priority > itr->priority || event.force)
            {
                *itr = event;
                if (_config.debugLogging)
                {
                    LOG_DEBUG(
                        "module.botpersonality.proactive",
                        "Replaced low-priority queued event for group {}",
                        groupId);
                }
            }

            return;
        }

        return;
    }

    _queue.push_back(event);
    ++queuedForGroup;
    _coordinator.NoteQueuedEvent(groupId);

    if (_config.debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.proactive",
            "Queued proactive event {} for group {}",
            BotProactiveDialogueEventToString(event.event),
            groupId);
    }
}

void BotProactiveDialogueMgr::ProcessPendingEvents(uint32 nowMs)
{
    for (auto itr = _queue.begin(); itr != _queue.end();)
    {
        if (IsEventStale(*itr, nowMs))
        {
            if (_queuedPerGroup[itr->groupId])
                --_queuedPerGroup[itr->groupId];
            _coordinator.NoteDequeuedEvent(itr->groupId);
            itr = _queue.erase(itr);
            continue;
        }

        if (static_cast<int32>(itr->deliverAtMs - nowMs) > 0)
        {
            ++itr;
            continue;
        }

        DeliverEvent(*itr, nowMs);
        if (_queuedPerGroup[itr->groupId])
            --_queuedPerGroup[itr->groupId];
        _coordinator.NoteDequeuedEvent(itr->groupId);
        itr = _queue.erase(itr);
    }
}

void BotProactiveDialogueMgr::ProcessTrackedBots(uint32 nowMs)
{
    if (_lastTrackedBotScanMs &&
        getMSTimeDiff(_lastTrackedBotScanMs, nowMs) <
            TRACKED_BOT_SCAN_INTERVAL_MS)
        return;

    _lastTrackedBotScanMs = nowMs;
    uint32 processed = 0;

    for (auto& pair : _trackedBots)
    {
        if (processed >= TRACKED_BOT_BATCH)
            break;

        ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(
            pair.first);
        Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
        if (!IsValidOnlinePlayer(bot) || !sBotPersonalityMgr.IsPlayerbot(bot))
            continue;

        ++processed;
        BotLiveState& state = pair.second;
        state.lastSeenMs = nowMs;

        if (!bot->GetGroup() ||
            (_config.requireRealPlayerPresent &&
                !HasRealPlayer(bot->GetGroup())))
            continue;

        if (bot->IsInCombat())
            state.lastActivityMs = nowMs;

        uint8 healthState = 0;
        float const healthPct = bot->GetHealthPct();
        if (healthPct <= _config.criticalHealthPercent)
            healthState = 2;
        else if (healthPct <= _config.lowHealthPercent)
            healthState = 1;

        BotGameplayContext context;
        if (Map const* map = bot->GetMap())
        {
            context.mapId = map->GetId();
            context.instanceId = bot->GetInstanceId();
            context.isDungeon = map->IsDungeon();
            context.isRaid = map->IsRaid();
        }
        context.value = static_cast<uint32>(healthPct);

        if (healthState == 2 && state.healthState < 2 &&
            getMSTimeDiff(state.lastCriticalHealthMs, nowMs) >=
                _config.criticalHealthCooldownMs)
        {
            state.lastCriticalHealthMs = nowMs;
            sBotMoodMgr.ApplyMoodEvent(
                bot,
                BotMoodEvent::CriticalHealth,
                bot->GetMapId() ^ bot->GetInstanceId());
            QueueEvent(
                BotProactiveDialogueEvent::BotCriticalHealth,
                bot,
                nullptr,
                context);
        }
        else if (healthState == 1 && state.healthState == 0 &&
            getMSTimeDiff(state.lastLowHealthMs, nowMs) >=
                _config.lowHealthCooldownMs)
        {
            state.lastLowHealthMs = nowMs;
            sBotMoodMgr.ApplyMoodEvent(
                bot,
                BotMoodEvent::LowHealth,
                bot->GetMapId() ^ bot->GetInstanceId());
            QueueEvent(
                BotProactiveDialogueEvent::BotLowHealth,
                bot,
                nullptr,
                context);
        }

        if (healthPct > _config.lowHealthPercent + 10)
            healthState = 0;
        state.healthState = healthState;

        uint32 const maxMana = bot->GetMaxPower(POWER_MANA);
        if (maxMana)
        {
            uint32 const manaPct = bot->GetPower(POWER_MANA) * 100 / maxMana;
            bool const lowMana = manaPct <= _config.lowManaPercent;
            if (lowMana && !state.lowManaActive &&
                getMSTimeDiff(state.lastLowManaMs, nowMs) >=
                    _config.lowManaCooldownMs)
            {
                state.lastLowManaMs = nowMs;
                context.value = manaPct;
                sBotMoodMgr.ApplyMoodEvent(
                    bot,
                    BotMoodEvent::LowMana,
                    bot->GetMapId() ^ bot->GetInstanceId());
                QueueEvent(
                    BotProactiveDialogueEvent::BotLowMana,
                    bot,
                    nullptr,
                    context);
            }

            state.lowManaActive = lowMana;
            if (manaPct > _config.lowManaPercent + 20)
                state.lowManaActive = false;
        }

        if (_config.enableInactivityDialogue &&
            state.lastActivityMs &&
            !bot->IsInCombat() &&
            !bot->isMoving() &&
            getMSTimeDiff(state.lastActivityMs, nowMs) >=
                _config.inactivityMinutes * 60 * IN_MILLISECONDS &&
            getMSTimeDiff(state.lastInactivityMs, nowMs) >=
                _config.inactivityCooldownMinutes * 60 * IN_MILLISECONDS)
        {
            state.lastInactivityMs = nowMs;
            sBotMoodMgr.ApplyMoodEvent(
                bot,
                BotMoodEvent::LongInactivity,
                GetGroupId(bot));
            QueueEvent(
                BotProactiveDialogueEvent::LongInactivity,
                bot,
                nullptr,
                context);
        }
    }
}

void BotProactiveDialogueMgr::Cleanup(uint32 nowMs)
{
    if (_lastCleanupMs &&
        getMSTimeDiff(_lastCleanupMs, nowMs) < CLEANUP_INTERVAL_MS)
        return;

    _lastCleanupMs = nowMs;
    _coordinator.Cleanup(nowMs);

    for (auto itr = _trackedBots.begin(); itr != _trackedBots.end();)
    {
        if (getMSTimeDiff(itr->second.lastSeenMs, nowMs) >
            TRACKED_BOT_EXPIRY_MS)
            itr = _trackedBots.erase(itr);
        else
            ++itr;
    }

    while (_recentResponses.size() > 4096)
        _recentResponses.erase(_recentResponses.begin());
}

std::optional<BotProactiveDialogueContext>
BotProactiveDialogueMgr::BuildContext(
    PendingEvent const& event,
    Player* bot,
    Player* relatedPlayer)
{
    if (!IsValidOnlinePlayer(bot) || !sBotPersonalityMgr.IsPlayerbot(bot))
        return std::nullopt;

    BotPersonality const* personality =
        sBotPersonalityMgr.GetOrCreatePersonality(bot);
    BotMood const* mood = sBotMoodMgr.GetMood(bot);
    if (!personality || !mood)
        return std::nullopt;

    BotProactiveDialogueContext context;
    context.event = event.event;
    context.priority = event.priority;
    context.botGuid = bot->GetGUID().GetCounter();
    context.relatedPlayerGuid =
        relatedPlayer ? relatedPlayer->GetGUID().GetCounter() : 0;
    context.groupId = event.groupId;
    context.botName = bot->GetName();
    context.playerName = relatedPlayer ? relatedPlayer->GetName() : "";
    context.previousSpeakerName = event.previousSpeakerName;
    context.personality = *personality;
    context.mood = *mood;
    context.baselineMood = GetBaselineMood(*personality);
    context.dominantMood = GetDominantMood(context.mood, context.baselineMood);
    context.moodIntensity = GetMoodIntensity(
        context.mood,
        context.baselineMood);

    if (relatedPlayer)
    {
        context.hasExistingRelationship =
            sBotRelationshipMgr.GetExistingRelationship(
                bot,
                relatedPlayer,
                context.relationship);
        if (context.hasExistingRelationship)
            context.relationshipLevel = GetRelationshipLevel(
                context.relationship.affinity);
    }

    context.mapId = event.mapId ? event.mapId : bot->GetMapId();
    context.zoneId = event.zoneId ? event.zoneId : bot->GetZoneId();
    context.instanceId = event.instanceId ? event.instanceId :
        bot->GetInstanceId();
    context.sourceEntry = event.sourceEntry;
    context.eventValue = event.value;
    context.inCombat = bot->IsInCombat();
    context.isBanterReply = event.isBanterReply;
    context.eventTime = event.eventTime;

    if (Map const* map = bot->GetMap())
    {
        context.inDungeon = map->IsDungeon();
        context.inRaid = map->IsRaid();
    }

    if (relatedPlayer)
    {
        context.recentGameplay = sBotGameplayTracker.GetRecentEvents(
            context.botGuid,
            context.relatedPlayerGuid,
            5);
    }

    context.tone = DetermineProactiveTone(context);
    context.selectionSeed = GetSelectionSeed(context);

    auto recent = _recentResponses.find(
        MakeRecentResponseKey(context.botGuid, context.event));
    if (recent != _recentResponses.end())
        context.previousResponse = recent->second;

    return context;
}

BotProactiveDialogueDebugResult
BotProactiveDialogueMgr::BuildDebugResult(
    PendingEvent const& event,
    Player* bot,
    Player* relatedPlayer,
    bool send)
{
    BotProactiveDialogueDebugResult result;
    std::optional<BotProactiveDialogueContext> context =
        BuildContext(event, bot, relatedPlayer);
    if (!context)
    {
        result.error = "Unable to build proactive dialogue context.";
        return result;
    }

    result.baseChance = GetEventChance(event.event);
    result.adjustedChance = AdjustChance(*context, result.baseChance);
    std::optional<std::string> response = GenerateProactiveResponse(*context);
    if (!response || response->empty())
    {
        result.error = "No proactive template was available.";
        return result;
    }

    result.response = *response;
    TruncateResponse(result.response);
    if (result.response.empty())
    {
        result.error = "Response was empty after truncation.";
        return result;
    }

    result.context = *context;
    result.success = true;

    if (send)
    {
        Group* group = bot->GetGroup();
        if (!SendChat(bot, group, result.context, result.response))
        {
            result.success = false;
            result.error = "Unable to send proactive chat.";
            return result;
        }

        result.sent = true;
        _coordinator.RecordMessage(
            GetGroupId(group),
            bot,
            event.event,
            getMSTime(),
            event.isBanterReply);
    }

    return result;
}

std::vector<BotSpeakerCandidate> BotProactiveDialogueMgr::BuildCandidates(
    PendingEvent const& event,
    Group* group,
    Player* preferredBot,
    Player* relatedPlayer) const
{
    std::vector<BotSpeakerCandidate> candidates;
    uint32 const relatedPlayerGuid =
        relatedPlayer ? relatedPlayer->GetGUID().GetCounter() : 0;
    uint32 const preferredBotGuid =
        preferredBot ? preferredBot->GetGUID().GetCounter() : 0;

    if (!group)
    {
        if (preferredBot)
            candidates.push_back({ preferredBot, relatedPlayerGuid,
                preferredBotGuid });
        return candidates;
    }

    for (GroupReference* itr = group->GetFirstMember();
        itr != nullptr;
        itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!IsValidOnlinePlayer(member) ||
            !sBotPersonalityMgr.IsPlayerbot(member))
            continue;

        if (event.mapId && member->GetMapId() != event.mapId)
            continue;

        if (event.instanceId && member->GetInstanceId() != event.instanceId)
            continue;

        candidates.push_back({ member, relatedPlayerGuid, preferredBotGuid });
    }

    return candidates;
}

bool BotProactiveDialogueMgr::DeliverEvent(
    PendingEvent const& event,
    uint32 nowMs)
{
    Player* preferredBot = event.preferredBotGuid.IsEmpty() ?
        nullptr :
        ObjectAccessor::FindConnectedPlayer(event.preferredBotGuid);
    Player* relatedPlayer = event.relatedPlayerGuid.IsEmpty() ?
        nullptr :
        ObjectAccessor::FindConnectedPlayer(event.relatedPlayerGuid);
    Player* anchor = preferredBot ? preferredBot : relatedPlayer;
    if (!IsValidOnlinePlayer(anchor))
        return false;

    Group* group = anchor->GetGroup();
    if (group && GetGroupId(group) != event.groupId)
        return false;

    if (group && _config.requireRealPlayerPresent && !HasRealPlayer(group))
        return false;

    std::vector<BotSpeakerCandidate> candidates =
        BuildCandidates(event, group, preferredBot, relatedPlayer);
    Player* speaker = _coordinator.SelectSpeaker(
        event.groupId,
        event.event,
        event.priority,
        candidates,
        nowMs,
        event.bypassCooldowns);
    if (!speaker)
        return false;

    if (IsGroupLevelEvent(event.event))
        relatedPlayer = nullptr;
    else if (!relatedPlayer && group)
    {
        for (GroupReference* itr = group->GetFirstMember();
            itr != nullptr;
            itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (IsRealPlayer(member))
            {
                relatedPlayer = member;
                break;
            }
        }
    }

    BotProactiveDialogueDebugResult result =
        BuildDebugResult(event, speaker, relatedPlayer, false);
    if (!result.success)
        return false;

    if (!event.force && !_config.forceDialogueForDebug &&
        !RollChance(result.context, result.adjustedChance))
    {
        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.proactive",
                "Proactive event {} suppressed by chance {}",
                BotProactiveDialogueEventToString(event.event),
                result.adjustedChance);
        }

        return false;
    }

    if (!SendChat(speaker, group, result.context, result.response))
        return false;

    _recentResponses[MakeRecentResponseKey(
        result.context.botGuid,
        result.context.event)] = result.response;
    _coordinator.RecordMessage(
        event.groupId,
        speaker,
        event.event,
        nowMs,
        event.isBanterReply);
    MaybeQueueBanter(event, speaker, group, result.response, nowMs);
    return true;
}

bool BotProactiveDialogueMgr::SendChat(
    Player* bot,
    Group* group,
    BotProactiveDialogueContext const& context,
    std::string& response)
{
    if (!IsValidOnlinePlayer(bot))
        return false;

    TruncateResponse(response);
    if (response.empty())
        return false;

    if (group)
    {
        if (group->isRaidGroup())
        {
            if (!_config.enableRaidChat)
                return false;
        }
        else if (!_config.enablePartyChat)
            return false;

        ChatMsg const chatType = group->isRaidGroup() ?
            CHAT_MSG_RAID :
            CHAT_MSG_PARTY;
        WorldPacket data;
        ChatHandler::BuildChatPacket(
            data,
            chatType,
            response,
            LANG_UNIVERSAL,
            CHAT_TAG_NONE,
            bot->GetGUID(),
            bot->GetName());

        bool sent = false;
        for (GroupReference* itr = group->GetFirstMember();
            itr != nullptr;
            itr = itr->next())
        {
            Player* receiver = itr->GetSource();
            if (!IsRealPlayer(receiver))
                continue;

            receiver->GetSession()->SendPacket(&data);
            sent = true;
        }

        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.proactive",
                "Sent proactive {} chat for bot {} event {}: {}",
                group->isRaidGroup() ? "raid" : "party",
                context.botGuid,
                BotProactiveDialogueEventToString(context.event),
                sent ? "ok" : "no real receivers");
        }

        return sent;
    }

    if (!_config.enableSay)
        return false;

    bot->Say(response, LANG_UNIVERSAL);
    return true;
}

void BotProactiveDialogueMgr::MaybeQueueBanter(
    PendingEvent const& sourceEvent,
    Player* speaker,
    Group* group,
    std::string const& /*response*/,
    uint32 nowMs)
{
    if (!_config.enableBotBanter ||
        sourceEvent.isBanterReply ||
        !group ||
        _config.maxBanterReplies == 0 ||
        _coordinator.GetBanterReplies(sourceEvent.groupId) >=
            _config.maxBanterReplies ||
        (_config.requireRealPlayerPresent && !HasRealPlayer(group)))
        return;

    uint32 const rollSeed = Mix(
        sourceEvent.groupId ^
        speaker->GetGUID().GetCounter() ^
        nowMs ^
        static_cast<uint32>(sourceEvent.event));
    if (rollSeed % 100 >= _config.botBanterChance)
        return;

    Player* replyBot = nullptr;
    for (GroupReference* itr = group->GetFirstMember();
        itr != nullptr;
        itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!IsValidOnlinePlayer(member) ||
            !sBotPersonalityMgr.IsPlayerbot(member) ||
            member->GetGUID() == speaker->GetGUID())
            continue;

        replyBot = member;
        break;
    }

    if (!replyBot)
        return;

    BotGameplayContext context;
    if (Map const* map = replyBot->GetMap())
    {
        context.mapId = map->GetId();
        context.instanceId = replyBot->GetInstanceId();
        context.isDungeon = map->IsDungeon();
        context.isRaid = map->IsRaid();
    }

    QueueEvent(
        BotProactiveDialogueEvent::BotReplyToBot,
        replyBot,
        nullptr,
        context,
        false,
        true,
        sourceEvent.sourceEntry);
}

bool BotProactiveDialogueMgr::IsValidOnlinePlayer(Player const* player) const
{
    return player &&
        player->GetSession() &&
        player->IsInWorld() &&
        !player->IsDuringRemoveFromWorld() &&
        !player->IsBeingTeleported();
}

bool BotProactiveDialogueMgr::IsRealPlayer(Player const* player) const
{
    return IsValidOnlinePlayer(player) &&
        !sBotPersonalityMgr.IsPlayerbot(player) &&
        !player->GetSession()->IsBot();
}

bool BotProactiveDialogueMgr::HasRealPlayer(Group const* group) const
{
    if (!group)
        return false;

    for (GroupReference const* itr = group->GetFirstMember();
        itr != nullptr;
        itr = itr->next())
    {
        if (IsRealPlayer(itr->GetSource()))
            return true;
    }

    return false;
}

uint32 BotProactiveDialogueMgr::GetGroupId(Player const* player) const
{
    return player ? GetGroupId(player->GetGroup()) : 0;
}

uint32 BotProactiveDialogueMgr::GetGroupId(Group const* group) const
{
    return group ? group->GetGUID().GetCounter() : 0;
}

uint32 BotProactiveDialogueMgr::GetEventChance(
    BotProactiveDialogueEvent event) const
{
    if (_config.forceDialogueForDebug)
        return 100;

    switch (event)
    {
        case BotProactiveDialogueEvent::GroupJoined:
            return _config.groupJoinChance;
        case BotProactiveDialogueEvent::DungeonEntered:
            return _config.dungeonEnterChance;
        case BotProactiveDialogueEvent::SharedEliteKilled:
            return _config.eliteKillChance;
        case BotProactiveDialogueEvent::SharedBossKilled:
            return _config.bossKillChance;
        case BotProactiveDialogueEvent::BotHealed:
            return _config.botHealedChance;
        case BotProactiveDialogueEvent::BotSaved:
            return _config.botSavedChance;
        case BotProactiveDialogueEvent::BotResurrected:
            return _config.botResurrectedChance;
        case BotProactiveDialogueEvent::PlayerDied:
            return _config.playerDeathChance;
        case BotProactiveDialogueEvent::RepeatedPlayerDeath:
            return _config.repeatedPlayerDeathChance;
        case BotProactiveDialogueEvent::BotDied:
            return _config.botDeathChance;
        case BotProactiveDialogueEvent::GroupWipe:
            return _config.wipeChance;
        case BotProactiveDialogueEvent::DungeonCompleted:
            return _config.dungeonCompleteChance;
        case BotProactiveDialogueEvent::RaidEncounterCompleted:
            return _config.raidEncounterCompleteChance;
        case BotProactiveDialogueEvent::DangerousPull:
            return _config.dangerousPullChance;
        case BotProactiveDialogueEvent::PlayerAbandonedCombat:
            return _config.abandonmentChance;
        case BotProactiveDialogueEvent::BotLowHealth:
            return _config.lowHealthChance;
        case BotProactiveDialogueEvent::BotCriticalHealth:
            return _config.criticalHealthChance;
        case BotProactiveDialogueEvent::BotLowMana:
            return _config.lowManaChance;
        case BotProactiveDialogueEvent::LongInactivity:
            return _config.inactivityChance;
        case BotProactiveDialogueEvent::SustainedTeamwork:
        case BotProactiveDialogueEvent::RelationshipImproved:
        case BotProactiveDialogueEvent::RelationshipWorsened:
            return _config.teamworkChance;
        case BotProactiveDialogueEvent::BotReplyToBot:
            return 100;
    }

    return 0;
}

uint32 BotProactiveDialogueMgr::AdjustChance(
    BotProactiveDialogueContext const& context,
    uint32 baseChance) const
{
    int32 chance = static_cast<int32>(baseChance);
    chance += static_cast<int32>(context.personality.talkativeness) / 5;

    if (context.personality.archetype == BotPersonalityArchetype::Stoic)
        chance -= 25;
    else if (context.personality.archetype ==
        BotPersonalityArchetype::Nervous &&
        (context.event == BotProactiveDialogueEvent::BotCriticalHealth ||
            context.event == BotProactiveDialogueEvent::GroupWipe))
        chance += 15;

    if (context.moodIntensity >= 40)
        chance += 10;
    else if (context.moodIntensity < 10)
        chance -= 5;

    if (context.priority == BotDialoguePriority::Critical)
        chance += 10;

    return static_cast<uint32>(std::clamp(chance, 0, 100));
}

bool BotProactiveDialogueMgr::RollChance(
    BotProactiveDialogueContext const& context,
    uint32 chance) const
{
    if (chance >= 100)
        return true;
    if (!chance)
        return false;

    return Mix(context.selectionSeed ^ 0x9e3779b9u) % 100 < chance;
}

uint32 BotProactiveDialogueMgr::GetSelectionSeed(
    BotProactiveDialogueContext const& context) const
{
    uint32 seed = context.botGuid;
    seed ^= Mix(context.relatedPlayerGuid + 0x85ebca6bu);
    seed ^= static_cast<uint32>(context.event) * 0x27d4eb2du;
    seed ^= static_cast<uint32>(context.tone) * 0x165667b1u;
    seed ^= context.groupId * 0x9e3779b9u;
    seed ^= context.eventTime;
    return Mix(seed);
}

uint32 BotProactiveDialogueMgr::GetDelayMs(
    BotProactiveDialogueEvent /*event*/,
    bool isBanterReply) const
{
    uint32 const minDelay = isBanterReply ?
        _config.botBanterDelayMinMs :
        _config.delayMinMs;
    uint32 const maxDelay = isBanterReply ?
        _config.botBanterDelayMaxMs :
        _config.delayMaxMs;

    if (maxDelay <= minDelay)
        return minDelay;

    return urand(minDelay, maxDelay);
}

bool BotProactiveDialogueMgr::IsEventStale(
    PendingEvent const& event,
    uint32 nowMs) const
{
    return getMSTimeDiff(event.queuedAtMs, nowMs) >
        _config.eventExpirySeconds * IN_MILLISECONDS;
}

bool BotProactiveDialogueMgr::IsSameTopic(
    PendingEvent const& left,
    PendingEvent const& right) const
{
    return left.groupId == right.groupId &&
        left.event == right.event &&
        left.sourceEntry == right.sourceEntry &&
        left.preferredBotGuid == right.preferredBotGuid &&
        left.relatedPlayerGuid == right.relatedPlayerGuid;
}

void BotProactiveDialogueMgr::TouchActivity(Player* bot, uint32 nowMs)
{
    if (!bot)
        return;

    BotLiveState& state = _trackedBots[bot->GetGUID().GetCounter()];
    state.lastSeenMs = nowMs;
    state.lastActivityMs = nowMs;
}

void BotProactiveDialogueMgr::RemoveEventsForGuid(ObjectGuid guid)
{
    if (guid.IsEmpty())
        return;

    for (auto itr = _queue.begin(); itr != _queue.end();)
    {
        if (itr->preferredBotGuid == guid || itr->relatedPlayerGuid == guid)
        {
            if (_queuedPerGroup[itr->groupId])
                --_queuedPerGroup[itr->groupId];
            _coordinator.NoteDequeuedEvent(itr->groupId);
            itr = _queue.erase(itr);
        }
        else
            ++itr;
    }
}

void BotProactiveDialogueMgr::RemoveEventsForGroup(uint32 groupId)
{
    if (!groupId)
        return;

    for (auto itr = _queue.begin(); itr != _queue.end();)
    {
        if (itr->groupId == groupId)
        {
            if (_queuedPerGroup[itr->groupId])
                --_queuedPerGroup[itr->groupId];
            _coordinator.NoteDequeuedEvent(itr->groupId);
            itr = _queue.erase(itr);
        }
        else
            ++itr;
    }

    _queuedPerGroup.erase(groupId);
    _coordinator.ClearGroup(groupId);
}

void BotProactiveDialogueMgr::TruncateResponse(std::string& response) const
{
    if (response.size() <= CHAT_LENGTH_LIMIT)
        return;

    response.resize(CHAT_LENGTH_LIMIT);
    while (!response.empty())
    {
        std::wstring wide;
        if (Utf8toWStr(response, wide))
            return;

        response.pop_back();
    }
}

uint32 BotProactiveDialogueMgr::MakeSourceKey(
    BotGameplayEvent event,
    BotGameplayContext const& context,
    Player const* player) const
{
    uint32 seed = static_cast<uint32>(event);
    seed ^= context.mapId * 0x45d9f3bu;
    seed ^= context.instanceId * 0x119de1f3u;
    seed ^= context.sourceEntry * 0x27d4eb2du;
    seed ^= context.value * 0x165667b1u;
    if (player && !IsGroupLevelEvent(
            ProactiveEventForGameplay(event).value_or(
                BotProactiveDialogueEvent::BotHealed)))
        seed ^= player->GetGUID().GetCounter();

    return Mix(seed);
}
