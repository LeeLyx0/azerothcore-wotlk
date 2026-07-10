#include "BotDialogueMgr.h"

#include "IBotDialogueProvider.h"
#include "BotLlmMgr.h"
#include "BotMoodMgr.h"
#include "BotPersonalityMgr.h"
#include "BotRelationshipMgr.h"
#include "BotTemplateDialogueProvider.h"
#include "Chat.h"
#include "Config.h"
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

#ifdef MOD_PLAYERBOTS
#include "Playerbots.h"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr uint32 CHAT_LENGTH_LIMIT = 255;
constexpr std::size_t MAX_CHAT_CACHE_ENTRIES = 4096;
constexpr uint32 RATE_WINDOW_MS = 60000;
constexpr uint32 CLEANUP_INTERVAL_MS = 60000;

uint64 MakePairKey(uint32 botGuid, uint32 playerGuid)
{
    return (static_cast<uint64>(botGuid) << 32) |
        static_cast<uint64>(playerGuid);
}

uint64 MakeRecentResponseKey(uint32 botGuid, BotChatIntent intent)
{
    return (static_cast<uint64>(botGuid) << 32) |
        static_cast<uint64>(static_cast<uint32>(intent));
}

uint32 StableHash(std::string const& text)
{
    uint32 hash = 2166136261u;
    for (unsigned char c : text)
    {
        hash ^= c;
        hash *= 16777619u;
    }

    return hash;
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

std::string TrimAscii(std::string text)
{
    auto isSpace = [](unsigned char c)
    {
        return std::isspace(c) != 0;
    };

    auto begin = std::find_if_not(text.begin(), text.end(), isSpace);
    auto end = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();

    if (begin >= end)
        return {};

    return std::string(begin, end);
}

bool StartsWith(std::string const& text, std::string const& prefix)
{
    return text.rfind(prefix, 0) == 0;
}

template <typename Map>
void TrimMapToLimit(Map& map)
{
    while (map.size() > MAX_CHAT_CACHE_ENTRIES)
        map.erase(map.begin());
}

void TruncateUtf8Bytes(std::string& text, std::size_t maxBytes)
{
    if (text.size() <= maxBytes)
        return;

    text.resize(maxBytes);

    while (!text.empty())
    {
        std::wstring wide;
        if (Utf8toWStr(text, wide))
            return;

        text.pop_back();
    }
}

uint32 ReadClampedConfig(
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
            "module.botpersonality.chat",
            "{} value {} is invalid, clamped to {}",
            name,
            configured,
            clamped);
    }

    return static_cast<uint32>(clamped);
}

bool IsValidOnlinePlayer(Player const* player)
{
    return player &&
        player->GetSession() &&
        player->IsInWorld() &&
        !player->IsDuringRemoveFromWorld() &&
        !player->IsBeingTeleported();
}

uint32 TalkativenessChance(int8 talkativeness)
{
    int32 const value = std::clamp<int32>(talkativeness, -100, 100);

    if (value <= -50)
        return static_cast<uint32>(20 + ((value + 100) * 20 / 50));

    if (value <= 0)
        return static_cast<uint32>(40 + ((value + 50) * 25 / 50));

    if (value <= 50)
        return static_cast<uint32>(65 + (value * 20 / 50));

    return static_cast<uint32>(85 + ((value - 50) * 15 / 50));
}

#ifdef MOD_PLAYERBOTS
bool HasPlayerbotTrigger(PlayerbotAI* botAI, std::string const& command)
{
    if (!botAI || command.empty() || !botAI->GetAiObjectContext())
        return false;

    return botAI->GetAiObjectContext()->GetTrigger(command) != nullptr;
}

bool MatchesPlayerbotTrigger(PlayerbotAI* botAI, std::string const& command)
{
    if (HasPlayerbotTrigger(botAI, command))
        return true;

    std::size_t offset = std::string::npos;
    while (true)
    {
        std::size_t const found = command.rfind(' ', offset);
        if (found == std::string::npos || found == 0)
            break;

        std::string const name = command.substr(0, found);
        if (HasPlayerbotTrigger(botAI, name))
            return true;

        offset = found - 1;
    }

    return false;
}
#endif

BotRelationshipEvent RelationshipEventForIntent(BotChatIntent intent)
{
    switch (intent)
    {
        case BotChatIntent::Greeting:
            return BotRelationshipEvent::Greeting;
        case BotChatIntent::Thanks:
            return BotRelationshipEvent::Thanks;
        case BotChatIntent::Praise:
            return BotRelationshipEvent::Praise;
        case BotChatIntent::Apology:
            return BotRelationshipEvent::Apology;
        case BotChatIntent::Insult:
            return BotRelationshipEvent::Insult;
        case BotChatIntent::HelpRequest:
            return BotRelationshipEvent::HelpRequest;
        case BotChatIntent::Farewell:
        case BotChatIntent::IdentityQuestion:
        case BotChatIntent::WellbeingQuestion:
        case BotChatIntent::Agreement:
        case BotChatIntent::Disagreement:
        case BotChatIntent::Unknown:
            return BotRelationshipEvent::Conversation;
    }

    return BotRelationshipEvent::Conversation;
}

std::optional<BotMoodEvent> MoodEventForIntent(BotChatIntent intent)
{
    switch (intent)
    {
        case BotChatIntent::Greeting:
            return BotMoodEvent::PlayerGreeting;
        case BotChatIntent::Thanks:
            return BotMoodEvent::PlayerThanksBot;
        case BotChatIntent::Praise:
            return BotMoodEvent::PlayerPraisesBot;
        case BotChatIntent::Apology:
            return BotMoodEvent::PlayerApologises;
        case BotChatIntent::Insult:
            return BotMoodEvent::PlayerInsultsBot;
        case BotChatIntent::Farewell:
        case BotChatIntent::HelpRequest:
        case BotChatIntent::IdentityQuestion:
        case BotChatIntent::WellbeingQuestion:
        case BotChatIntent::Agreement:
        case BotChatIntent::Disagreement:
        case BotChatIntent::Unknown:
            return std::nullopt;
    }

    return std::nullopt;
}
}

BotDialogueMgr::BotDialogueMgr()
    : _provider(std::make_unique<BotTemplateDialogueProvider>())
{
}

BotDialogueMgr::~BotDialogueMgr() = default;

void BotDialogueMgr::LoadConfig(bool reload)
{
    _config.enable = sConfigMgr->GetOption<bool>(
        "BotPersonality.Chat.Enable",
        true);
    _config.respondToWhispers = sConfigMgr->GetOption<bool>(
        "BotPersonality.Chat.RespondToWhispers",
        true);
    _config.respondToGroupChat = sConfigMgr->GetOption<bool>(
        "BotPersonality.Chat.RespondToGroupChat",
        true);
    _config.respondToUnknown = sConfigMgr->GetOption<bool>(
        "BotPersonality.Chat.RespondToUnknown",
        true);
    _config.forceResponseForDebug = sConfigMgr->GetOption<bool>(
        "BotPersonality.Chat.ForceResponseForDebug",
        false);
    _config.debugLogging = sConfigMgr->GetOption<bool>(
        "BotPersonality.Chat.DebugLogging",
        false);

    _config.pairCooldownMs = ReadClampedConfig(
        "BotPersonality.Chat.PairCooldownMs",
        5000,
        0,
        600000,
        _config.debugLogging);
    _config.botCooldownMs = ReadClampedConfig(
        "BotPersonality.Chat.BotCooldownMs",
        2000,
        0,
        600000,
        _config.debugLogging);
    _config.duplicateWindowMs = ReadClampedConfig(
        "BotPersonality.Chat.DuplicateWindowMs",
        15000,
        0,
        600000,
        _config.debugLogging);
    _config.maxResponsesPerMinute = ReadClampedConfig(
        "BotPersonality.Chat.MaxResponsesPerMinute",
        8,
        1,
        60,
        _config.debugLogging);
    _config.maxInputLength = ReadClampedConfig(
        "BotPersonality.Chat.MaxInputLength",
        255,
        1,
        CHAT_LENGTH_LIMIT,
        _config.debugLogging);
    _config.maxOutputLength = ReadClampedConfig(
        "BotPersonality.Chat.MaxOutputLength",
        255,
        1,
        CHAT_LENGTH_LIMIT,
        _config.debugLogging);
    _config.minimumGreetingChance = ReadClampedConfig(
        "BotPersonality.Chat.MinimumGreetingChance",
        70,
        0,
        100,
        _config.debugLogging);
    _config.minimumHelpChance = ReadClampedConfig(
        "BotPersonality.Chat.MinimumHelpChance",
        85,
        0,
        100,
        _config.debugLogging);
    _config.minimumIdentityChance = ReadClampedConfig(
        "BotPersonality.Chat.MinimumIdentityChance",
        85,
        0,
        100,
        _config.debugLogging);
    _config.minimumThanksChance = ReadClampedConfig(
        "BotPersonality.Chat.MinimumThanksChance",
        50,
        0,
        100,
        _config.debugLogging);
    _config.minimumInsultChance = ReadClampedConfig(
        "BotPersonality.Chat.MinimumInsultChance",
        40,
        0,
        100,
        _config.debugLogging);
    _config.unknownChance = ReadClampedConfig(
        "BotPersonality.Chat.UnknownChance",
        25,
        0,
        100,
        _config.debugLogging);
    _config.replyDelayEnable = sConfigMgr->GetOption<bool>(
        "BotPersonality.Chat.ReplyDelay.Enable",
        true);
    _config.replyDelayBaseMs = ReadClampedConfig(
        "BotPersonality.Chat.ReplyDelay.BaseMs",
        700,
        0,
        60000,
        _config.debugLogging);
    _config.replyDelayInputCharMs = ReadClampedConfig(
        "BotPersonality.Chat.ReplyDelay.InputCharMs",
        8,
        0,
        250,
        _config.debugLogging);
    _config.replyDelayOutputCharMs = ReadClampedConfig(
        "BotPersonality.Chat.ReplyDelay.OutputCharMs",
        45,
        0,
        250,
        _config.debugLogging);
    _config.replyDelayRandomMs = ReadClampedConfig(
        "BotPersonality.Chat.ReplyDelay.RandomMs",
        1200,
        0,
        60000,
        _config.debugLogging);
    _config.replyDelayMaxMs = ReadClampedConfig(
        "BotPersonality.Chat.ReplyDelay.MaxMs",
        10000,
        0,
        60000,
        _config.debugLogging);

    if (!IsChatEnabled())
        ClearChatState();

    LOG_INFO(
        "server.loading",
        "Bot Personality Phase 2 chat {}{}",
        IsChatEnabled() ? "enabled" : "disabled",
        reload ? " after config reload" : "");
}

bool BotDialogueMgr::IsChatEnabled() const
{
    return sBotPersonalityMgr.IsEnabled() && _config.enable;
}

bool BotDialogueMgr::IsDebugLoggingEnabled() const
{
    return _config.debugLogging;
}

void BotDialogueMgr::Update(uint32 /*diff*/)
{
    if (_pendingReplies.empty())
        return;

    ProcessPendingReplies(getMSTime());
}

bool BotDialogueMgr::HandleIncomingWhisper(
    Player* sender,
    Player* bot,
    uint32 language,
    std::string const& message)
{
    if (_config.debugLogging && sender && bot)
    {
        LOG_DEBUG(
            "module.botpersonality.chat",
            "Whisper observed from {} to {} ({} bytes, contents redacted)",
            sender->GetGUID().GetCounter(),
            bot->GetGUID().GetCounter(),
            message.size());
    }

    return HandleIncomingDialogue(
        sender,
        bot,
        language,
        message,
        true,
        false,
        false);
}

bool BotDialogueMgr::HandleIncomingGroupChat(
    Player* sender,
    Group* group,
    uint32 type,
    uint32 language,
    std::string const& message)
{
    if (!IsEligibleGroupChat(sender, group, type, language, message))
        return false;

    bool const isPartyChat =
        type == CHAT_MSG_PARTY ||
        type == CHAT_MSG_PARTY_LEADER;
    bool const isRaidChat =
        type == CHAT_MSG_RAID ||
        type == CHAT_MSG_RAID_LEADER;

    std::vector<Player*> candidates;
    uint8 const senderSubgroup = group->GetMemberGroup(sender->GetGUID());
    for (GroupReference* itr = group->GetFirstMember();
        itr != nullptr;
        itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!IsValidOnlinePlayer(member) ||
            member->GetGUID() == sender->GetGUID() ||
            !sBotPersonalityMgr.IsPlayerbot(member))
            continue;

        if (isPartyChat &&
            group->isRaidGroup() &&
            group->GetMemberGroup(member->GetGUID()) != senderSubgroup)
            continue;

        if (IsPlayerbotsCommand(member, message))
        {
            if (_config.debugLogging)
            {
                LOG_DEBUG(
                    "module.botpersonality.chat",
                    "Group chat recognized as Playerbots command");
            }

            return false;
        }

        candidates.push_back(member);
    }

    if (candidates.empty())
        return false;

    if (candidates.size() > 1)
    {
        std::size_t const offset = urand(
            0,
            static_cast<uint32>(candidates.size() - 1));
        std::rotate(
            candidates.begin(),
            candidates.begin() + offset,
            candidates.end());
    }

    for (Player* bot : candidates)
    {
        if (HandleIncomingDialogue(
                sender,
                bot,
                language,
                message,
                false,
                isPartyChat,
                isRaidChat))
            return true;
    }

    return false;
}

bool BotDialogueMgr::HandleIncomingDialogue(
    Player* sender,
    Player* bot,
    uint32 language,
    std::string const& message,
    bool isWhisper,
    bool isPartyChat,
    bool isRaidChat)
{
    if (isWhisper && !IsEligibleWhisper(sender, bot, language, message))
        return false;

    if (IsPlayerbotsCommand(bot, message))
    {
        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.chat",
                "Whisper to bot {} recognized as Playerbots command",
                bot->GetGUID().GetCounter());
        }

        return false;
    }

    std::string const normalized = NormalizeMessage(message);
    uint32 const nowMs = getMSTime();
    Cleanup(nowMs);

    uint32 const botGuid = bot->GetGUID().GetCounter();
    uint32 const playerGuid = sender->GetGUID().GetCounter();
    bool repeatedSpam = false;
    if (ShouldSuppressDuplicate(
            botGuid,
            playerGuid,
            normalized,
            nowMs,
            repeatedSpam))
    {
        if (repeatedSpam)
        {
            sBotRelationshipMgr.ApplyEvent(
                bot,
                sender,
                BotRelationshipEvent::RepeatedSpam);
        }

        return false;
    }

    std::optional<BotDialogueContext> context =
        BuildContext(
            sender,
            bot,
            message,
            std::nullopt,
            isWhisper,
            isPartyChat,
            isRaidChat);
    if (!context)
        return false;

    if (context->intent == BotChatIntent::Unknown &&
        !_config.respondToUnknown)
    {
        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.chat",
                "Unknown response disabled for bot {}",
                botGuid);
        }

        return false;
    }

    bool relationshipApplied = false;
    if (context->intent != BotChatIntent::Unknown)
    {
        relationshipApplied = sBotRelationshipMgr.ApplyEvent(
            bot,
            sender,
            RelationshipEventForIntent(context->intent));
        if (relationshipApplied)
            ApplyRelationshipContext(*context, bot, sender);

        if (std::optional<BotMoodEvent> moodEvent =
                MoodEventForIntent(context->intent))
            sBotMoodMgr.ApplyMoodEvent(
                bot,
                *moodEvent,
                sender->GetGUID().GetCounter(),
                _config.duplicateWindowMs);
    }

    if (_config.debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.chat",
            "Detected intent {} and tone {} for bot {}",
            BotChatIntentToString(context->intent),
            BotResponseToneToString(context->tone),
            botGuid);
    }

    if (ShouldSuppressCooldowns(botGuid, playerGuid, nowMs))
        return false;

    context->selectionSeed = GetSelectionSeed(*context);

    uint32 const chance = GetResponseChance(*context);
    if (_config.debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.chat",
            "Response chance for bot {} is {}",
            botGuid,
            chance);
    }

    if (!RollResponseChance(*context, chance))
    {
        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.chat",
                "Response suppressed by random chance for bot {}",
                botGuid);
        }

        return false;
    }

    std::optional<std::string> response = GenerateResponse(*context);
    if (!response || response->empty())
        return false;

    if (context->intent == BotChatIntent::Unknown && !relationshipApplied)
    {
        relationshipApplied = sBotRelationshipMgr.ApplyEvent(
            bot,
            sender,
            BotRelationshipEvent::Conversation);
        if (relationshipApplied)
            ApplyRelationshipContext(*context, bot, sender);
    }

    std::string responseText = *response;
    TruncateUtf8Bytes(responseText, _config.maxOutputLength);
    if (responseText.empty())
        return false;

    if (sBotLlmMgr.TryQueueDialogue(bot, sender, *context, responseText))
    {
        RecordSuccessfulResponse(*context, "", nowMs);
        return true;
    }

    if (context->isWhisper)
        QueueWhisper(bot, sender, *context, responseText, nowMs);
    else
        QueueGroupReply(bot, sender, *context, responseText, nowMs);
    RecordSuccessfulResponse(*context, responseText, nowMs);

    if (_config.debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.chat",
            "{} response queued for bot {} to player {}",
            context->isWhisper ? "Whisper" : "Group chat",
            botGuid,
            playerGuid);
    }

    return true;
}

BotChatIntent BotDialogueMgr::ParseIntent(
    std::string const& message) const
{
    return ParseBotChatIntent(message);
}

std::string BotDialogueMgr::NormalizeMessage(
    std::string const& message) const
{
    return NormalizeBotChatMessage(message);
}

BotDialogueDebugResult BotDialogueMgr::GenerateDebugResponse(
    Player* bot,
    Player* player,
    BotChatIntent intent)
{
    BotDialogueDebugResult result;

    std::optional<BotDialogueContext> context =
        BuildContext(
            player,
            bot,
            "",
            intent,
            true,
            false,
            false);
    if (!context)
    {
        result.error = "Unable to build dialogue context.";
        return result;
    }

    std::optional<std::string> response = GenerateResponse(*context);
    if (!response || response->empty())
    {
        result.error = "No response template was available.";
        return result;
    }

    TruncateUtf8Bytes(*response, _config.maxOutputLength);

    result.success = true;
    result.context = *context;
    result.response = *response;
    return result;
}

BotDialogueDebugResult BotDialogueMgr::ForceWhisper(
    Player* bot,
    Player* player,
    BotChatIntent intent)
{
    BotDialogueDebugResult result =
        GenerateDebugResponse(bot, player, intent);
    if (!result.success)
        return result;

    if (!player || sBotPersonalityMgr.IsPlayerbot(player))
    {
        result.success = false;
        result.error = "Target player must be an online real player.";
        return result;
    }

    if (!SendWhisper(bot, player, result.response))
    {
        result.success = false;
        result.error = "Whisper could not be sent.";
        return result;
    }

    return result;
}

BotDialogueCooldownStats BotDialogueMgr::GetCooldownStats() const
{
    BotDialogueCooldownStats stats;
    stats.pairCooldownEntries = _pairCooldowns.size();
    stats.botCooldownEntries = _botCooldowns.size();
    stats.duplicateEntries = _duplicates.size();
    stats.rateWindowEntries = _rateWindows.size();
    stats.recentResponseEntries = _recentResponses.size();
    return stats;
}

void BotDialogueMgr::ClearChatState()
{
    _pairCooldowns.clear();
    _botCooldowns.clear();
    _duplicates.clear();
    _rateWindows.clear();
    _recentResponses.clear();
    _pendingReplies.clear();
}

std::optional<BotDialogueContext> BotDialogueMgr::BuildContext(
    Player* sender,
    Player* bot,
    std::string const& message,
    std::optional<BotChatIntent> forcedIntent,
    bool isWhisper,
    bool isPartyChat,
    bool isRaidChat)
{
    if (!bot || !sBotPersonalityMgr.IsPlayerbot(bot))
        return std::nullopt;

    BotPersonality const* personality =
        sBotPersonalityMgr.GetOrCreatePersonality(bot);
    if (!personality)
        return std::nullopt;

    BotDialogueContext context;
    context.botGuid = bot->GetGUID().GetCounter();
    context.playerGuid = sender ? sender->GetGUID().GetCounter() : 0;
    context.botName = bot->GetName();
    context.playerName = sender ? sender->GetName() : "GM";
    context.originalMessage = message;
    context.intent = forcedIntent ? *forcedIntent : ParseIntent(message);
    context.personality = *personality;
    ApplyRelationshipContext(context, bot, sender);
    context.isWhisper = isWhisper;
    context.isPartyChat = isPartyChat;
    context.isRaidChat = isRaidChat;

    context.botInCombat = bot->IsInCombat();
    context.playerInCombat = sender && sender->IsInCombat();
    context.inGroup = bot->GetGroup() != nullptr;

    if (Map const* map = bot->GetMap())
    {
        context.inDungeon = map->IsDungeon();
        context.inRaid = map->IsRaid();
    }

    context.mapId = bot->GetMapId();
    context.zoneId = bot->GetZoneId();

    return context;
}

void BotDialogueMgr::ApplyRelationshipContext(
    BotDialogueContext& context,
    Player* bot,
    Player* sender) const
{
    BotRelationship relationship;
    context.hasExistingRelationship =
        sender &&
        sBotRelationshipMgr.GetExistingRelationship(bot, sender, relationship);

    if (context.hasExistingRelationship)
    {
        context.relationship = relationship;
        context.relationshipLevel = GetRelationshipLevel(
            relationship.affinity);
        context.tone = DetermineBotResponseTone(
            context.personality,
            context.intent,
            context.relationship,
            context.relationshipLevel);
        return;
    }

    context.relationship = {};
    context.relationshipLevel = BotRelationshipLevel::Neutral;
    context.tone = DetermineBotResponseTone(
        context.personality,
        context.intent);
}

bool BotDialogueMgr::IsEligibleWhisper(
    Player* sender,
    Player* bot,
    uint32 language,
    std::string const& message) const
{
    if (!IsChatEnabled() || !_config.respondToWhispers)
        return false;

    if (language == LANG_ADDON)
    {
        if (_config.debugLogging)
            LOG_DEBUG("module.botpersonality.chat", "Addon whisper rejected");

        return false;
    }

    if (!IsValidOnlinePlayer(sender))
    {
        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.chat",
                "Sender rejected for personality chat");
        }

        return false;
    }

    if (!IsValidOnlinePlayer(bot))
    {
        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.chat",
                "Recipient rejected for personality chat");
        }

        return false;
    }

    if (sender->GetGUID() == bot->GetGUID())
        return false;

    if (sender->GetSession()->IsBot() ||
        sBotPersonalityMgr.IsPlayerbot(sender))
        return false;

    if (!sBotPersonalityMgr.IsPlayerbot(bot))
        return false;

    if (!sender->CanSpeak())
        return false;

    if (message.empty() || message.size() > _config.maxInputLength)
        return false;

    if (IsAddonControlMessage(message))
        return false;

    if (NormalizeMessage(message).empty())
        return false;

    return true;
}

bool BotDialogueMgr::IsEligibleGroupChat(
    Player* sender,
    Group* group,
    uint32 type,
    uint32 language,
    std::string const& message) const
{
    if (!IsChatEnabled() || !_config.respondToGroupChat)
        return false;

    if (language == LANG_ADDON)
        return false;

    if (type != CHAT_MSG_PARTY &&
        type != CHAT_MSG_PARTY_LEADER &&
        type != CHAT_MSG_RAID &&
        type != CHAT_MSG_RAID_LEADER)
        return false;

    if (!IsValidOnlinePlayer(sender) || !group)
        return false;

    if (sender->GetSession()->IsBot() ||
        sBotPersonalityMgr.IsPlayerbot(sender))
        return false;

    if (sender->GetGroup() != group)
        return false;

    if (!sender->CanSpeak())
        return false;

    if (message.empty() || message.size() > _config.maxInputLength)
        return false;

    if (IsAddonControlMessage(message))
        return false;

    if (NormalizeMessage(message).empty())
        return false;

    return true;
}

bool BotDialogueMgr::IsAddonControlMessage(
    std::string const& message) const
{
    if (StartsWith(message, "BOT\t"))
        return true;

    if (message.find('\t') != std::string::npos)
        return true;

    return !message.empty() &&
        static_cast<unsigned char>(message.front()) < 0x20;
}

bool BotDialogueMgr::IsPlayerbotsCommand(
    Player* bot,
    std::string const& message) const
{
#ifdef MOD_PLAYERBOTS
    PlayerbotAI* botAI = sPlayerbotsMgr.GetPlayerbotAI(bot);
    if (!botAI || botAI->IsRealPlayer())
        return false;

    if (!sPlayerbotAIConfig.commandSeparator.empty() &&
        message.find(sPlayerbotAIConfig.commandSeparator) !=
            std::string::npos)
    {
        std::vector<std::string> commands;
        split(
            commands,
            message,
            sPlayerbotAIConfig.commandSeparator.c_str());

        for (std::string const& command : commands)
        {
            if (IsPlayerbotsCommand(bot, command))
                return true;
        }

        return false;
    }

    std::string filtered = TrimAscii(BotPersonalityToLower(message));
    if (StartsWith(filtered, "bot\t"))
        filtered = TrimAscii(filtered.substr(4));

    if (!sPlayerbotAIConfig.commandPrefix.empty())
    {
        std::string prefix = BotPersonalityToLower(
            sPlayerbotAIConfig.commandPrefix);
        if (!StartsWith(filtered, prefix))
            return false;

        filtered = TrimAscii(filtered.substr(prefix.size()));
    }

    static constexpr std::array ChatPrefixes =
    {
        std::string_view("#w "),
        std::string_view("#p "),
        std::string_view("#r "),
        std::string_view("#a "),
        std::string_view("#g ")
    };

    for (std::string_view prefix : ChatPrefixes)
    {
        if (filtered.rfind(prefix, 0) == 0)
        {
            filtered = TrimAscii(filtered.substr(prefix.size()));
            break;
        }
    }

    if (filtered.empty())
        return false;

    std::string const normalized = NormalizeMessage(filtered);
    if (normalized == "reset" ||
        normalized == "logout" ||
        normalized == "logout cancel" ||
        StartsWith(normalized, "debug ") ||
        StartsWith(normalized, "do ") ||
        StartsWith(normalized, "d "))
        return true;

    if (MatchesPlayerbotTrigger(botAI, normalized))
        return true;

    if (sPlayerbotAIConfig.enableAutoTradeOnItemMention &&
        ChatHelper::parseableItem(filtered))
        return true;

    return false;
#else
    (void)bot;
    (void)message;
    return false;
#endif
}

bool BotDialogueMgr::ShouldSuppressDuplicate(
    uint32 botGuid,
    uint32 playerGuid,
    std::string const& normalized,
    uint32 nowMs,
    bool& repeatedSpam)
{
    repeatedSpam = false;

    if (_config.duplicateWindowMs == 0)
        return false;

    uint64 const key = MakePairKey(botGuid, playerGuid);
    uint32 const messageHash = StableHash(normalized);

    auto itr = _duplicates.find(key);
    if (itr != _duplicates.end() &&
        itr->second.messageHash == messageHash &&
        getMSTimeDiff(itr->second.lastSeenMs, nowMs) <
            _config.duplicateWindowMs)
    {
        itr->second.lastSeenMs = nowMs;
        ++itr->second.duplicateCount;
        repeatedSpam = itr->second.duplicateCount >= 2;

        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.chat",
                "Duplicate message suppressed for bot {} player {}",
                botGuid,
                playerGuid);
        }

        return true;
    }

    _duplicates[key] = { messageHash, nowMs, 0 };
    TrimMapToLimit(_duplicates);
    return false;
}

bool BotDialogueMgr::ShouldSuppressCooldowns(
    uint32 botGuid,
    uint32 playerGuid,
    uint32 nowMs)
{
    uint64 const pairKey = MakePairKey(botGuid, playerGuid);
    auto pairItr = _pairCooldowns.find(pairKey);
    if (_config.pairCooldownMs > 0 &&
        pairItr != _pairCooldowns.end() &&
        getMSTimeDiff(pairItr->second, nowMs) < _config.pairCooldownMs)
    {
        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.chat",
                "Pair cooldown suppressed bot {} player {}",
                botGuid,
                playerGuid);
        }

        return true;
    }

    auto botItr = _botCooldowns.find(botGuid);
    if (_config.botCooldownMs > 0 &&
        botItr != _botCooldowns.end() &&
        getMSTimeDiff(botItr->second, nowMs) < _config.botCooldownMs)
    {
        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.chat",
                "Bot cooldown suppressed bot {}",
                botGuid);
        }

        return true;
    }

    std::deque<uint32>& window = _rateWindows[botGuid];
    while (!window.empty() &&
        getMSTimeDiff(window.front(), nowMs) >= RATE_WINDOW_MS)
        window.pop_front();

    if (window.size() >= _config.maxResponsesPerMinute)
    {
        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.chat",
                "Rate limit suppressed bot {}",
                botGuid);
        }

        return true;
    }

    return false;
}

void BotDialogueMgr::RecordSuccessfulResponse(
    BotDialogueContext const& context,
    std::string const& response,
    uint32 nowMs)
{
    _pairCooldowns[MakePairKey(context.botGuid, context.playerGuid)] = nowMs;
    _botCooldowns[context.botGuid] = nowMs;
    _rateWindows[context.botGuid].push_back(nowMs);
    if (!response.empty())
    {
        _recentResponses[
            MakeRecentResponseKey(context.botGuid, context.intent)] =
            response;
    }

    TrimMapToLimit(_pairCooldowns);
    TrimMapToLimit(_botCooldowns);
    TrimMapToLimit(_rateWindows);
    TrimMapToLimit(_recentResponses);
}

uint32 BotDialogueMgr::GetResponseChance(
    BotDialogueContext const& context) const
{
    if (_config.forceResponseForDebug)
        return 100;

    if (context.intent == BotChatIntent::Unknown)
        return _config.respondToUnknown ? _config.unknownChance : 0;

    uint32 chance = TalkativenessChance(context.personality.talkativeness);

    switch (context.intent)
    {
        case BotChatIntent::Greeting:
            chance = std::max(chance, _config.minimumGreetingChance);
            break;
        case BotChatIntent::HelpRequest:
            chance = std::max(chance, _config.minimumHelpChance);
            break;
        case BotChatIntent::IdentityQuestion:
            chance = std::max(chance, _config.minimumIdentityChance);
            break;
        case BotChatIntent::Thanks:
            chance = std::max(chance, _config.minimumThanksChance);
            break;
        case BotChatIntent::Insult:
            chance = std::max(chance, _config.minimumInsultChance);
            break;
        default:
            break;
    }

    return std::clamp<uint32>(chance, 0, 100);
}

bool BotDialogueMgr::RollResponseChance(
    BotDialogueContext const& context,
    uint32 chance) const
{
    if (chance >= 100)
        return true;

    if (chance == 0)
        return false;

    uint32 const rollSeed =
        context.selectionSeed ^ StableHash(context.originalMessage) ^
        0x9e3779b9u;
    uint32 const roll = Mix(rollSeed) % 100;
    return roll < chance;
}

uint32 BotDialogueMgr::GetSelectionSeed(BotDialogueContext const& context)
{
    ++_interactionCounter;

    uint32 seed = context.botGuid;
    seed ^= Mix(context.playerGuid + 0x85ebca6bu);
    seed ^= static_cast<uint32>(context.intent) * 0x27d4eb2du;
    seed ^= static_cast<uint32>(context.tone) * 0x165667b1u;
    seed ^= static_cast<uint32>(context.personality.speechStyle) << 24;
    seed ^= _interactionCounter * 0x9e3779b9u;

    return Mix(seed);
}

std::optional<std::string> BotDialogueMgr::GenerateResponse(
    BotDialogueContext& context)
{
    if (!_provider)
        return std::nullopt;

    uint64 const recentKey = MakeRecentResponseKey(
        context.botGuid,
        context.intent);
    auto recent = _recentResponses.find(recentKey);
    if (recent != _recentResponses.end())
        context.previousResponse = recent->second;

    if (context.selectionSeed == 0)
        context.selectionSeed = GetSelectionSeed(context);

    std::optional<std::string> response =
        _provider->GenerateResponse(context);

    if (response && _config.debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.chat",
            "Selected response template for bot {} intent {}",
            context.botGuid,
            BotChatIntentToString(context.intent));
    }

    return response;
}

uint32 BotDialogueMgr::CalculateReplyDelay(
    BotDialogueContext const& context,
    std::string const& response) const
{
    if (!_config.replyDelayEnable)
        return 0;

    uint64 delay = _config.replyDelayBaseMs;
    delay += static_cast<uint64>(context.originalMessage.size()) *
        _config.replyDelayInputCharMs;
    delay += static_cast<uint64>(response.size()) *
        _config.replyDelayOutputCharMs;

    if (_config.replyDelayRandomMs > 0)
        delay += urand(0, _config.replyDelayRandomMs);

    if (_config.replyDelayMaxMs > 0)
        delay = std::min<uint64>(delay, _config.replyDelayMaxMs);

    delay = std::min<uint64>(
        delay,
        std::numeric_limits<uint32>::max());

    return static_cast<uint32>(delay);
}

void BotDialogueMgr::QueueWhisper(
    Player* bot,
    Player* receiver,
    BotDialogueContext const& context,
    std::string response,
    uint32 nowMs)
{
    TruncateUtf8Bytes(response, _config.maxOutputLength);
    if (response.empty())
        return;

    PendingReply& reply = _pendingReplies.emplace_back();
    reply.botGuid = bot->GetGUID();
    reply.playerGuid = receiver->GetGUID();
    reply.context = context;
    reply.response = std::move(response);
    reply.queuedAtMs = nowMs;
    reply.delayMs = CalculateReplyDelay(reply.context, reply.response);

    while (_pendingReplies.size() > MAX_CHAT_CACHE_ENTRIES)
        _pendingReplies.pop_front();

    if (_config.debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.chat",
            "Queued delayed whisper bot {} player {} delay {} ms",
            context.botGuid,
            context.playerGuid,
            reply.delayMs);
    }
}

void BotDialogueMgr::QueueGroupReply(
    Player* bot,
    Player* receiver,
    BotDialogueContext const& context,
    std::string response,
    uint32 nowMs)
{
    TruncateUtf8Bytes(response, _config.maxOutputLength);
    if (response.empty())
        return;

    PendingReply& reply = _pendingReplies.emplace_back();
    reply.botGuid = bot->GetGUID();
    reply.playerGuid = receiver->GetGUID();
    reply.groupId = bot->GetGroup() ?
        bot->GetGroup()->GetGUID().GetCounter() :
        0;
    reply.context = context;
    reply.response = std::move(response);
    reply.queuedAtMs = nowMs;
    reply.delayMs = CalculateReplyDelay(reply.context, reply.response);

    while (_pendingReplies.size() > MAX_CHAT_CACHE_ENTRIES)
        _pendingReplies.pop_front();

    if (_config.debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.chat",
            "Queued delayed group reply bot {} player {} delay {} ms",
            context.botGuid,
            context.playerGuid,
            reply.delayMs);
    }
}

void BotDialogueMgr::ProcessPendingReplies(uint32 nowMs)
{
    for (auto itr = _pendingReplies.begin(); itr != _pendingReplies.end();)
    {
        if (getMSTimeDiff(itr->queuedAtMs, nowMs) < itr->delayMs)
        {
            ++itr;
            continue;
        }

        DeliverPendingReply(*itr);
        itr = _pendingReplies.erase(itr);
    }
}

void BotDialogueMgr::DeliverPendingReply(PendingReply& reply)
{
    Player* bot = ObjectAccessor::FindPlayer(reply.botGuid);
    Player* receiver = ObjectAccessor::FindPlayer(reply.playerGuid);

    if (!IsChatEnabled() ||
        !IsValidOnlinePlayer(bot) ||
        !IsValidOnlinePlayer(receiver) ||
        !sBotPersonalityMgr.IsPlayerbot(bot) ||
        sBotPersonalityMgr.IsPlayerbot(receiver) ||
        receiver->GetSession()->IsBot())
    {
        if (_config.debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality.chat",
                "Dropped queued whisper bot {} player {}",
                reply.context.botGuid,
                reply.context.playerGuid);
        }

        return;
    }

    bool sent = false;
    if (reply.context.isWhisper)
        sent = SendWhisper(bot, receiver, reply.response);
    else
    {
        if (reply.groupId &&
            (!bot->GetGroup() ||
                bot->GetGroup()->GetGUID().GetCounter() != reply.groupId))
            return;

        sent = SendGroupChat(bot, receiver, reply.context, reply.response);
    }

    if (!sent)
        return;

    if (_config.debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.chat",
            "Delayed {} sent for bot {} to player {}",
            reply.context.isWhisper ? "whisper" : "group chat",
            reply.context.botGuid,
            reply.context.playerGuid);
    }
}

bool BotDialogueMgr::SendWhisper(
    Player* bot,
    Player* receiver,
    std::string& response)
{
    if (!IsValidOnlinePlayer(bot) || !IsValidOnlinePlayer(receiver))
        return false;

    TruncateUtf8Bytes(response, _config.maxOutputLength);
    if (response.empty())
        return false;

    bot->Whisper(response, LANG_UNIVERSAL, receiver);
    return true;
}

bool BotDialogueMgr::SendGroupChat(
    Player* bot,
    Player* receiver,
    BotDialogueContext const& context,
    std::string& response)
{
    if (!IsValidOnlinePlayer(bot) || !IsValidOnlinePlayer(receiver))
        return false;

    Group* group = bot->GetGroup();
    if (!group || group != receiver->GetGroup())
        return false;

    TruncateUtf8Bytes(response, _config.maxOutputLength);
    if (response.empty())
        return false;

    ChatMsg const chatType = context.isRaidChat ?
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
    uint8 const targetSubgroup = group->GetMemberGroup(receiver->GetGUID());
    for (GroupReference* itr = group->GetFirstMember();
        itr != nullptr;
        itr = itr->next())
    {
        Player* player = itr->GetSource();
        if (!IsValidOnlinePlayer(player) ||
            sBotPersonalityMgr.IsPlayerbot(player))
            continue;

        if (context.isPartyChat &&
            group->isRaidGroup() &&
            group->GetMemberGroup(player->GetGUID()) != targetSubgroup)
            continue;

        player->GetSession()->SendPacket(&data);
        sent = true;
    }

    return sent;
}

void BotDialogueMgr::Cleanup(uint32 nowMs)
{
    if (_lastCleanupMs &&
        getMSTimeDiff(_lastCleanupMs, nowMs) < CLEANUP_INTERVAL_MS)
        return;

    _lastCleanupMs = nowMs;

    uint32 const staleCooldownMs = std::max(
        {
            _config.pairCooldownMs,
            _config.botCooldownMs,
            _config.duplicateWindowMs,
            RATE_WINDOW_MS
        }) + RATE_WINDOW_MS;

    for (auto itr = _pairCooldowns.begin(); itr != _pairCooldowns.end();)
    {
        if (getMSTimeDiff(itr->second, nowMs) > staleCooldownMs)
            itr = _pairCooldowns.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _botCooldowns.begin(); itr != _botCooldowns.end();)
    {
        if (getMSTimeDiff(itr->second, nowMs) > staleCooldownMs)
            itr = _botCooldowns.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _duplicates.begin(); itr != _duplicates.end();)
    {
        if (getMSTimeDiff(itr->second.lastSeenMs, nowMs) >
            staleCooldownMs)
            itr = _duplicates.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _rateWindows.begin(); itr != _rateWindows.end();)
    {
        std::deque<uint32>& window = itr->second;
        while (!window.empty() &&
            getMSTimeDiff(window.front(), nowMs) >= RATE_WINDOW_MS)
            window.pop_front();

        if (window.empty())
            itr = _rateWindows.erase(itr);
        else
            ++itr;
    }

    TrimMapToLimit(_pairCooldowns);
    TrimMapToLimit(_botCooldowns);
    TrimMapToLimit(_duplicates);
    TrimMapToLimit(_rateWindows);
    TrimMapToLimit(_recentResponses);
}
