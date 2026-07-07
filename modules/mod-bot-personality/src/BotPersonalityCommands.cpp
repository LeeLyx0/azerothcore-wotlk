#include "BotPersonalityMgr.h"

#include "BotDialogueMgr.h"
#include "BotGameplayTracker.h"
#include "BotRelationshipMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "CommandScript.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "Util.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
void Send(ChatHandler* handler, std::string_view message)
{
    handler->SendSysMessage(message);
}

template <typename... Args>
void Send(ChatHandler* handler, char const* fmt, Args&&... args)
{
    handler->SendSysMessage(
        Acore::StringFormat(fmt, std::forward<Args>(args)...));
}

std::vector<std::string> TokenizeArgs(char const* args)
{
    std::vector<std::string> tokens;
    std::istringstream stream(args ? args : "");
    std::string token;

    while (stream >> token)
        tokens.push_back(token);

    return tokens;
}

bool TryParseInt32(std::string const& text, int32& value)
{
    auto const* begin = text.data();
    auto const* end = begin + text.size();
    auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

Player* FindOnlineCharacter(ChatHandler* handler, std::string botName)
{
    if (botName.empty())
    {
        Send(handler, "Bot name is required.");
        return nullptr;
    }

    if (!normalizePlayerName(botName))
    {
        Send(handler, "Invalid character name '{}'.", botName);
        return nullptr;
    }

    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(botName);
    if (guid.IsEmpty())
    {
        Send(handler, "Character '{}' was not found.", botName);
        return nullptr;
    }

    Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
    if (!bot)
    {
        Send(
            handler,
            "Character '{}' is not online. Phase 1 commands support "
            "online bots.",
            botName);
        return nullptr;
    }

    if (!sBotPersonalityMgr.IsPlayerbot(bot))
    {
        Send(
            handler,
            "Character '{}' is online but is not a Playerbot.",
            botName);
        return nullptr;
    }

    return bot;
}

Player* FindOnlineRealPlayer(ChatHandler* handler, std::string playerName)
{
    if (playerName.empty())
    {
        Send(handler, "Player name is required.");
        return nullptr;
    }

    if (!normalizePlayerName(playerName))
    {
        Send(handler, "Invalid character name '{}'.", playerName);
        return nullptr;
    }

    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(
        playerName);
    if (guid.IsEmpty())
    {
        Send(handler, "Character '{}' was not found.", playerName);
        return nullptr;
    }

    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (!player)
    {
        Send(handler, "Character '{}' is not online.", playerName);
        return nullptr;
    }

    if (sBotPersonalityMgr.IsPlayerbot(player))
    {
        Send(handler, "Character '{}' is a Playerbot.", playerName);
        return nullptr;
    }

    return player;
}

int32 TraitValue(BotPersonality const& personality, std::string const& trait)
{
    if (trait == "friendliness")
        return static_cast<int32>(personality.friendliness);
    if (trait == "confidence")
        return static_cast<int32>(personality.confidence);
    if (trait == "patience")
        return static_cast<int32>(personality.patience);
    if (trait == "humour")
        return static_cast<int32>(personality.humour);
    if (trait == "competitiveness")
        return static_cast<int32>(personality.competitiveness);
    if (trait == "greed")
        return static_cast<int32>(personality.greed);
    if (trait == "bravery")
        return static_cast<int32>(personality.bravery);
    if (trait == "talkativeness")
        return static_cast<int32>(personality.talkativeness);
    if (trait == "speechstyle" || trait == "speech_style")
        return static_cast<int32>(personality.speechStyle);

    return 0;
}

void SendPersonality(
    ChatHandler* handler,
    Player const* bot,
    BotPersonality const& personality)
{
    Send(handler, "Bot: {}", bot->GetName());
    Send(handler, "GUID: {}", personality.botGuid);
    Send(
        handler,
        "Archetype: {}",
        BotPersonalityArchetypeToString(personality.archetype));
    Send(
        handler,
        "Friendliness: {}",
        static_cast<int32>(personality.friendliness));
    Send(handler, "Confidence: {}", static_cast<int32>(personality.confidence));
    Send(handler, "Patience: {}", static_cast<int32>(personality.patience));
    Send(handler, "Humour: {}", static_cast<int32>(personality.humour));
    Send(
        handler,
        "Competitiveness: {}",
        static_cast<int32>(personality.competitiveness));
    Send(handler, "Greed: {}", static_cast<int32>(personality.greed));
    Send(handler, "Bravery: {}", static_cast<int32>(personality.bravery));
    Send(
        handler,
        "Talkativeness: {}",
        static_cast<int32>(personality.talkativeness));
    Send(
        handler,
        "Speech style: {}",
        static_cast<uint32>(personality.speechStyle));
}

bool IsResetToken(std::string token)
{
    token = BotPersonalityToLower(token);
    return token == "reset" || token == "true" || token == "1";
}

void SendRootUsage(ChatHandler* handler)
{
    Send(handler, "Usage: .botpersonality show <botName>");
    Send(handler, "Usage: .botpersonality regenerate <botName>");
    Send(handler, "Usage: .botpersonality set <botName> <trait> <value>");
    Send(
        handler,
        "Usage: .botpersonality archetype <botName> <archetype> [reset]");
    Send(handler, "Usage: .botpersonality reload <botName>");
    Send(handler, "Usage: .botpersonality clearcache");
    Send(handler, "Usage: .botpersonality chat");
    Send(handler, "Usage: .botpersonality relationship");
    Send(handler, "Usage: .botpersonality gameplay");
}

void SendChatUsage(ChatHandler* handler)
{
    Send(handler, "Usage: .botpersonality chat test <botName> <intent> [send]");
    Send(handler, "Usage: .botpersonality chat parse <message>");
    Send(handler, "Usage: .botpersonality chat cooldowns");
    Send(handler, "Usage: .botpersonality chat clearcooldowns");
    Send(
        handler,
        "Usage: .botpersonality chat force <botName> <playerName> <intent>");
}

void SendRelationshipUsage(ChatHandler* handler)
{
    Send(
        handler,
        "Usage: .botpersonality relationship show <botName> <playerName>");
    Send(
        handler,
        "Usage: .botpersonality relationship set <botName> <playerName> "
        "<field> <value>");
    Send(
        handler,
        "Usage: .botpersonality relationship adjust <botName> <playerName> "
        "<field> <amount>");
    Send(
        handler,
        "Usage: .botpersonality relationship reset <botName> <playerName>");
    Send(
        handler,
        "Usage: .botpersonality relationship reload <botName> <playerName>");
    Send(handler, "Usage: .botpersonality relationship list <botName> [limit]");
    Send(handler, "Usage: .botpersonality relationship save");
    Send(handler, "Usage: .botpersonality relationship clearcache");
}

void SendGameplayUsage(ChatHandler* handler)
{
    Send(handler, "Usage: .botpersonality gameplay status");
    Send(handler, "Usage: .botpersonality gameplay show <botName> <playerName>");
    Send(
        handler,
        "Usage: .botpersonality gameplay simulate <botName> <playerName> "
        "<event> [apply]");
    Send(
        handler,
        "Usage: .botpersonality gameplay recent <botName> <playerName>");
    Send(handler, "Usage: .botpersonality gameplay trackers");
    Send(handler, "Usage: .botpersonality gameplay cleartrackers");
}

std::string TrimArgs(char const* args)
{
    std::string text(args ? args : "");

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

bool ParseIntentArg(
    ChatHandler* handler,
    std::string const& text,
    BotChatIntent& intent)
{
    if (BotChatIntentFromString(text, intent))
        return true;

    Send(handler, "Invalid intent '{}'.", text);
    Send(
        handler,
        "Supported intents: greeting, farewell, thanks, praise, apology, "
        "insult, help, identity, wellbeing, agreement, disagreement, "
        "unknown.");
    return false;
}

bool ParseGameplayEventArg(
    ChatHandler* handler,
    std::string const& text,
    BotGameplayEvent& event)
{
    if (BotGameplayEventFromString(text, event))
        return true;

    Send(handler, "Invalid gameplay event '{}'.", text);
    Send(
        handler,
        "Supported events: normal, elite, boss, playerhealedbot, "
        "playerresurrectedbot, bothealedplayer, botresurrectedplayer, "
        "playerdied, botdied, shareddeath, wipe, leftcombat, dungeon, "
        "raid, teamwork, repeateddeath.");
    return false;
}

void SendDelta(
    ChatHandler* handler,
    std::string_view label,
    BotRelationshipDelta const& delta)
{
    Send(
        handler,
        "{} delta: affinity {} trust {} respect {} familiarity {}",
        label,
        delta.affinity,
        delta.trust,
        delta.respect,
        delta.familiarity);
}

void SendDialogueDebugResult(
    ChatHandler* handler,
    BotDialogueDebugResult const& result)
{
    Send(handler, "Bot: {}", result.context.botName);
    Send(handler, "Intent: {}", BotChatIntentToString(result.context.intent));
    Send(handler, "Tone: {}", BotResponseToneToString(result.context.tone));
    if (result.context.hasExistingRelationship)
    {
        Send(
            handler,
            "Relationship: {}",
            RelationshipLevelToString(result.context.relationshipLevel));
    }

    Send(handler, "Response: {}", result.response);
}

std::string FormatRelationshipTime(uint32 timestamp)
{
    if (!timestamp)
        return "never";

    return Acore::Time::TimeToTimestampStr(Seconds(timestamp));
}

void SendRelationship(
    ChatHandler* handler,
    Player const* bot,
    Player const* player,
    BotRelationship const& relationship)
{
    Send(handler, "Bot: {}", bot->GetName());
    Send(handler, "Player: {}", player->GetName());
    Send(handler, "Affinity: {}", static_cast<int32>(relationship.affinity));
    Send(
        handler,
        "Relationship level: {}",
        RelationshipLevelToString(
            GetRelationshipLevel(relationship.affinity)));
    Send(handler, "Trust: {}", static_cast<int32>(relationship.trust));
    Send(handler, "Respect: {}", static_cast<int32>(relationship.respect));
    Send(
        handler,
        "Familiarity: {}",
        static_cast<int32>(relationship.familiarity));
    Send(
        handler,
        "Positive interactions: {}",
        relationship.positiveInteractions);
    Send(
        handler,
        "Negative interactions: {}",
        relationship.negativeInteractions);
    Send(
        handler,
        "Gameplay kills: normal {} elite {} boss {}",
        relationship.sharedNormalKills,
        relationship.sharedEliteKills,
        relationship.sharedBossKills);
    Send(
        handler,
        "Gameplay heals/resurrections: player->bot {}/{} bot->player {}/{}",
        relationship.playerHealedBotEvents,
        relationship.playerResurrectedBotEvents,
        relationship.botHealedPlayerEvents,
        relationship.botResurrectedPlayerEvents);
    Send(
        handler,
        "Gameplay deaths: player {} bot {} shared {} wipes {}",
        relationship.playerDeaths,
        relationship.botDeaths,
        relationship.sharedDeaths,
        relationship.groupWipes);
    Send(
        handler,
        "Gameplay completions: dungeons {} raids {} teamwork {}",
        relationship.dungeonsCompleted,
        relationship.raidEncountersCompleted,
        relationship.sustainedTeamworkEvents);
    Send(
        handler,
        "First interaction: {}",
        FormatRelationshipTime(relationship.firstInteraction));
    Send(
        handler,
        "Last interaction: {}",
        FormatRelationshipTime(relationship.lastInteraction));
}

std::string RelationshipPlayerName(uint32 playerGuid)
{
    std::string name;
    ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(playerGuid);
    if (sCharacterCache->GetCharacterNameByGuid(guid, name))
        return name;

    return Acore::StringFormat("#{}", playerGuid);
}
}

class bot_personality_commandscript : public CommandScript
{
public:
    bot_personality_commandscript()
        : CommandScript("bot_personality_commandscript")
    {
    }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable botPersonalityCommandTable =
        {
            { "show", HandleShowCommand, SEC_GAMEMASTER, Console::Yes },
            {
                "regenerate",
                HandleRegenerateCommand,
                SEC_GAMEMASTER,
                Console::Yes
            },
            { "set", HandleSetCommand, SEC_GAMEMASTER, Console::Yes },
            {
                "archetype",
                HandleArchetypeCommand,
                SEC_GAMEMASTER,
                Console::Yes
            },
            { "reload", HandleReloadCommand, SEC_GAMEMASTER, Console::Yes },
            { "relationship", GetRelationshipCommandTable() },
            { "gameplay", GetGameplayCommandTable() },
            {
                "clearcache",
                HandleClearCacheCommand,
                SEC_ADMINISTRATOR,
                Console::Yes
            },
            { "chat", GetChatCommandTable() },
            { "", HandleHelpCommand, SEC_GAMEMASTER, Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "botpersonality", botPersonalityCommandTable }
        };

        return commandTable;
    }

    static ChatCommandTable const& GetChatCommandTable()
    {
        static ChatCommandTable chatCommandTable =
        {
            { "test", HandleChatTestCommand, SEC_GAMEMASTER, Console::Yes },
            { "parse", HandleChatParseCommand, SEC_GAMEMASTER, Console::Yes },
            {
                "cooldowns",
                HandleChatCooldownsCommand,
                SEC_GAMEMASTER,
                Console::Yes
            },
            {
                "clearcooldowns",
                HandleChatClearCooldownsCommand,
                SEC_ADMINISTRATOR,
                Console::Yes
            },
            { "force", HandleChatForceCommand, SEC_GAMEMASTER, Console::No },
            { "", HandleChatHelpCommand, SEC_GAMEMASTER, Console::Yes }
        };

        return chatCommandTable;
    }

    static ChatCommandTable const& GetRelationshipCommandTable()
    {
        static ChatCommandTable relationshipCommandTable =
        {
            {
                "show",
                HandleRelationshipShowCommand,
                SEC_GAMEMASTER,
                Console::Yes
            },
            {
                "set",
                HandleRelationshipSetCommand,
                SEC_GAMEMASTER,
                Console::Yes
            },
            {
                "adjust",
                HandleRelationshipAdjustCommand,
                SEC_GAMEMASTER,
                Console::Yes
            },
            {
                "reset",
                HandleRelationshipResetCommand,
                SEC_ADMINISTRATOR,
                Console::Yes
            },
            {
                "reload",
                HandleRelationshipReloadCommand,
                SEC_GAMEMASTER,
                Console::Yes
            },
            {
                "list",
                HandleRelationshipListCommand,
                SEC_GAMEMASTER,
                Console::Yes
            },
            {
                "save",
                HandleRelationshipSaveCommand,
                SEC_ADMINISTRATOR,
                Console::Yes
            },
            {
                "clearcache",
                HandleRelationshipClearCacheCommand,
                SEC_ADMINISTRATOR,
                Console::Yes
            },
            { "", HandleRelationshipHelpCommand, SEC_GAMEMASTER, Console::Yes }
        };

        return relationshipCommandTable;
    }

    static ChatCommandTable const& GetGameplayCommandTable()
    {
        static ChatCommandTable gameplayCommandTable =
        {
            { "status", HandleGameplayStatusCommand, SEC_GAMEMASTER, Console::Yes },
            { "show", HandleGameplayShowCommand, SEC_GAMEMASTER, Console::Yes },
            {
                "simulate",
                HandleGameplaySimulateCommand,
                SEC_GAMEMASTER,
                Console::Yes
            },
            {
                "recent",
                HandleGameplayRecentCommand,
                SEC_GAMEMASTER,
                Console::Yes
            },
            {
                "trackers",
                HandleGameplayTrackersCommand,
                SEC_GAMEMASTER,
                Console::Yes
            },
            {
                "cleartrackers",
                HandleGameplayClearTrackersCommand,
                SEC_ADMINISTRATOR,
                Console::Yes
            },
            { "", HandleGameplayHelpCommand, SEC_GAMEMASTER, Console::Yes }
        };

        return gameplayCommandTable;
    }

    static bool HandleHelpCommand(ChatHandler* handler, char const* /*args*/)
    {
        SendRootUsage(handler);
        return true;
    }

    static bool HandleChatHelpCommand(
        ChatHandler* handler,
        char const* /*args*/)
    {
        SendChatUsage(handler);
        return true;
    }

    static bool HandleRelationshipHelpCommand(
        ChatHandler* handler,
        char const* /*args*/)
    {
        SendRelationshipUsage(handler);
        return true;
    }

    static bool HandleGameplayHelpCommand(
        ChatHandler* handler,
        char const* /*args*/)
    {
        SendGameplayUsage(handler);
        return true;
    }

    static bool HandleShowCommand(ChatHandler* handler, char const* args)
    {
        if (!sBotPersonalityMgr.IsEnabled())
        {
            Send(handler, "Bot Personality module is disabled.");
            return true;
        }

        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 1)
        {
            Send(handler, "Usage: .botpersonality show <botName>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        BotPersonality const* personality =
            sBotPersonalityMgr.GetOrCreatePersonality(bot);
        if (!personality)
        {
            Send(
                handler,
                "Unable to load or create personality for '{}'.",
                tokens[0]);
            return true;
        }

        SendPersonality(handler, bot, *personality);
        return true;
    }

    static bool HandleRegenerateCommand(ChatHandler* handler, char const* args)
    {
        if (!sBotPersonalityMgr.IsEnabled())
        {
            Send(handler, "Bot Personality module is disabled.");
            return true;
        }

        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 1)
        {
            Send(handler, "Usage: .botpersonality regenerate <botName>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        BotPersonality const* personality =
            sBotPersonalityMgr.RegeneratePersonality(bot);
        if (!personality)
        {
            Send(
                handler,
                "Unable to regenerate personality for '{}'.",
                tokens[0]);
            return true;
        }

        SendPersonality(handler, bot, *personality);
        return true;
    }

    static bool HandleSetCommand(ChatHandler* handler, char const* args)
    {
        if (!sBotPersonalityMgr.IsEnabled())
        {
            Send(handler, "Bot Personality module is disabled.");
            return true;
        }

        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 3)
        {
            Send(
                handler,
                "Usage: .botpersonality set <botName> <trait> <value>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        int32 value = 0;
        if (!TryParseInt32(tokens[2], value))
        {
            Send(handler, "Invalid trait value '{}'.", tokens[2]);
            return false;
        }

        std::string trait = tokens[1];
        trait = BotPersonalityToLower(trait);

        if (!sBotPersonalityMgr.SetTrait(bot, trait, value))
        {
            Send(
                handler,
                "Invalid trait '{}'. Supported traits: friendliness, "
                "confidence, "
                "patience, humour, competitiveness, greed, bravery, "
                "talkativeness, speechstyle.",
                tokens[1]);
            return false;
        }

        BotPersonality const* personality =
            sBotPersonalityMgr.GetCachedPersonality(
                bot->GetGUID().GetCounter());
        if (!personality)
        {
            Send(
                handler,
                "Trait was saved, but the cache entry was not found.");
            return true;
        }

        Send(
            handler,
            "{} {} is now {}.",
            bot->GetName(),
            trait,
            TraitValue(*personality, trait));
        return true;
    }

    static bool HandleArchetypeCommand(ChatHandler* handler, char const* args)
    {
        if (!sBotPersonalityMgr.IsEnabled())
        {
            Send(handler, "Bot Personality module is disabled.");
            return true;
        }

        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() < 2 || tokens.size() > 3)
        {
            Send(
                handler,
                "Usage: .botpersonality archetype <botName> <archetype> "
                "[reset]");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        BotPersonalityArchetype archetype;
        if (!BotPersonalityArchetypeFromString(tokens[1], archetype))
        {
            Send(handler, "Invalid archetype '{}'.", tokens[1]);
            return false;
        }

        bool const resetTraits = tokens.size() == 3 && IsResetToken(tokens[2]);
        if (tokens.size() == 3 && !resetTraits)
        {
            Send(handler, "Optional third argument must be 'reset'.");
            return false;
        }

        if (!sBotPersonalityMgr.SetArchetype(bot, archetype, resetTraits))
        {
            Send(handler, "Unable to set archetype for '{}'.", tokens[0]);
            return true;
        }

        BotPersonality const* personality =
            sBotPersonalityMgr.GetCachedPersonality(
                bot->GetGUID().GetCounter());
        if (!personality)
        {
            Send(
                handler,
                "Archetype was saved, but the cache entry was not found.");
            return true;
        }

        SendPersonality(handler, bot, *personality);
        return true;
    }

    static bool HandleReloadCommand(ChatHandler* handler, char const* args)
    {
        if (!sBotPersonalityMgr.IsEnabled())
        {
            Send(handler, "Bot Personality module is disabled.");
            return true;
        }

        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 1)
        {
            Send(handler, "Usage: .botpersonality reload <botName>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        if (!sBotPersonalityMgr.ReloadPersonality(bot))
        {
            Send(
                handler,
                "No stored personality row found for '{}'. Use show or "
                "regenerate to create one.",
                bot->GetName());
            return true;
        }

        BotPersonality const* personality =
            sBotPersonalityMgr.GetCachedPersonality(
                bot->GetGUID().GetCounter());
        if (personality)
            SendPersonality(handler, bot, *personality);
        else
            Send(
                handler,
                "Personality reloaded, but cache entry was not found.");

        return true;
    }

    static bool HandleClearCacheCommand(ChatHandler* handler, char const* args)
    {
        std::vector<std::string> tokens = TokenizeArgs(args);
        if (!tokens.empty())
        {
            Send(handler, "Usage: .botpersonality clearcache");
            return false;
        }

        sBotPersonalityMgr.ClearCache();
        Send(handler, "Bot Personality cache cleared.");
        return true;
    }

    static bool HandleChatTestCommand(ChatHandler* handler, char const* args)
    {
        if (!sBotPersonalityMgr.IsEnabled())
        {
            Send(handler, "Bot Personality module is disabled.");
            return true;
        }

        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() < 2 || tokens.size() > 3)
        {
            Send(
                handler,
                "Usage: .botpersonality chat test <botName> <intent> [send]");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        BotChatIntent intent;
        if (!ParseIntentArg(handler, tokens[1], intent))
            return false;

        bool sendWhisper = false;
        if (tokens.size() == 3)
        {
            std::string option = BotPersonalityToLower(tokens[2]);
            if (option != "send")
            {
                Send(handler, "Optional third argument must be 'send'.");
                return false;
            }

            sendWhisper = true;
        }

        Player* player = handler->GetPlayer();
        if (sendWhisper && !player)
        {
            Send(handler, "The send option requires an in-game GM.");
            return true;
        }

        BotDialogueDebugResult result = sendWhisper ?
            sBotDialogueMgr.ForceWhisper(bot, player, intent) :
            sBotDialogueMgr.GenerateDebugResponse(bot, player, intent);

        if (!result.success)
        {
            Send(handler, result.error.empty() ?
                "Unable to generate chat response." :
                result.error);
            return true;
        }

        SendDialogueDebugResult(handler, result);
        return true;
    }

    static bool HandleChatParseCommand(ChatHandler* handler, char const* args)
    {
        std::string const message = TrimArgs(args);
        if (message.empty())
        {
            Send(handler, "Usage: .botpersonality chat parse <message>");
            return false;
        }

        std::string const normalized = sBotDialogueMgr.NormalizeMessage(
            message);
        BotChatIntent const intent = sBotDialogueMgr.ParseIntent(message);

        Send(handler, "Normalized: {}", normalized);
        Send(handler, "Intent: {}", BotChatIntentToString(intent));
        return true;
    }

    static bool HandleChatCooldownsCommand(
        ChatHandler* handler,
        char const* args)
    {
        if (!TokenizeArgs(args).empty())
        {
            Send(handler, "Usage: .botpersonality chat cooldowns");
            return false;
        }

        BotDialogueCooldownStats const stats =
            sBotDialogueMgr.GetCooldownStats();

        Send(
            handler,
            "Pair cooldown entries: {}",
            stats.pairCooldownEntries);
        Send(handler, "Bot cooldown entries: {}", stats.botCooldownEntries);
        Send(
            handler,
            "Duplicate-message entries: {}",
            stats.duplicateEntries);
        Send(handler, "Rate-window entries: {}", stats.rateWindowEntries);
        Send(
            handler,
            "Recent-response entries: {}",
            stats.recentResponseEntries);
        return true;
    }

    static bool HandleChatClearCooldownsCommand(
        ChatHandler* handler,
        char const* args)
    {
        if (!TokenizeArgs(args).empty())
        {
            Send(handler, "Usage: .botpersonality chat clearcooldowns");
            return false;
        }

        sBotDialogueMgr.ClearChatState();
        Send(handler, "Bot Personality chat cooldowns cleared.");
        return true;
    }

    static bool HandleChatForceCommand(ChatHandler* handler, char const* args)
    {
        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 3)
        {
            Send(
                handler,
                "Usage: .botpersonality chat force <botName> <playerName> "
                "<intent>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        Player* player = FindOnlineRealPlayer(handler, tokens[1]);
        if (!player)
            return true;

        BotChatIntent intent;
        if (!ParseIntentArg(handler, tokens[2], intent))
            return false;

        BotDialogueDebugResult result =
            sBotDialogueMgr.ForceWhisper(bot, player, intent);
        if (!result.success)
        {
            Send(handler, result.error.empty() ?
                "Unable to send chat response." :
                result.error);
            return true;
        }

        SendDialogueDebugResult(handler, result);
        return true;
    }

    static bool HandleRelationshipShowCommand(
        ChatHandler* handler,
        char const* args)
    {
        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 2)
        {
            Send(
                handler,
                "Usage: .botpersonality relationship show <botName> "
                "<playerName>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        Player* player = FindOnlineRealPlayer(handler, tokens[1]);
        if (!player)
            return true;

        BotRelationship relationship;
        if (!sBotRelationshipMgr.GetExistingRelationship(
                bot,
                player,
                relationship))
        {
            Send(
                handler,
                "No relationship exists for bot '{}' and player '{}'.",
                bot->GetName(),
                player->GetName());
            return true;
        }

        SendRelationship(handler, bot, player, relationship);
        return true;
    }

    static bool HandleRelationshipSetCommand(
        ChatHandler* handler,
        char const* args)
    {
        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 4)
        {
            Send(
                handler,
                "Usage: .botpersonality relationship set <botName> "
                "<playerName> <field> <value>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        Player* player = FindOnlineRealPlayer(handler, tokens[1]);
        if (!player)
            return true;

        int32 value = 0;
        if (!TryParseInt32(tokens[3], value))
        {
            Send(handler, "Invalid relationship value '{}'.", tokens[3]);
            return false;
        }

        if (!sBotRelationshipMgr.SetValue(bot, player, tokens[2], value))
        {
            Send(
                handler,
                "Invalid field '{}'. Use affinity, trust, respect, or "
                "familiarity.",
                tokens[2]);
            return false;
        }

        BotRelationship relationship;
        sBotRelationshipMgr.GetExistingRelationship(bot, player, relationship);
        Send(
            handler,
            "{} / {} relationship saved. Level: {}.",
            bot->GetName(),
            player->GetName(),
            RelationshipLevelToString(
                GetRelationshipLevel(relationship.affinity)));
        return true;
    }

    static bool HandleRelationshipAdjustCommand(
        ChatHandler* handler,
        char const* args)
    {
        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 4)
        {
            Send(
                handler,
                "Usage: .botpersonality relationship adjust <botName> "
                "<playerName> <field> <amount>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        Player* player = FindOnlineRealPlayer(handler, tokens[1]);
        if (!player)
            return true;

        int32 amount = 0;
        if (!TryParseInt32(tokens[3], amount))
        {
            Send(handler, "Invalid relationship amount '{}'.", tokens[3]);
            return false;
        }

        int32 oldValue = 0;
        int32 newValue = 0;
        if (!sBotRelationshipMgr.AdjustValue(
                bot,
                player,
                tokens[2],
                amount,
                oldValue,
                newValue))
        {
            Send(
                handler,
                "Invalid field '{}'. Use affinity, trust, respect, or "
                "familiarity.",
                tokens[2]);
            return false;
        }

        BotRelationship relationship;
        sBotRelationshipMgr.GetExistingRelationship(bot, player, relationship);
        Send(
            handler,
            "{} changed from {} to {}. Level: {}.",
            tokens[2],
            oldValue,
            newValue,
            RelationshipLevelToString(
                GetRelationshipLevel(relationship.affinity)));
        return true;
    }

    static bool HandleRelationshipResetCommand(
        ChatHandler* handler,
        char const* args)
    {
        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 2)
        {
            Send(
                handler,
                "Usage: .botpersonality relationship reset <botName> "
                "<playerName>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        Player* player = FindOnlineRealPlayer(handler, tokens[1]);
        if (!player)
            return true;

        sBotRelationshipMgr.ResetRelationship(bot, player);
        Send(
            handler,
            "Relationship reset for bot '{}' and player '{}'.",
            bot->GetName(),
            player->GetName());
        return true;
    }

    static bool HandleRelationshipReloadCommand(
        ChatHandler* handler,
        char const* args)
    {
        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 2)
        {
            Send(
                handler,
                "Usage: .botpersonality relationship reload <botName> "
                "<playerName>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        Player* player = FindOnlineRealPlayer(handler, tokens[1]);
        if (!player)
            return true;

        if (!sBotRelationshipMgr.ReloadRelationship(bot, player))
        {
            Send(
                handler,
                "No relationship row exists for bot '{}' and player '{}'.",
                bot->GetName(),
                player->GetName());
            return true;
        }

        BotRelationship relationship;
        sBotRelationshipMgr.GetExistingRelationship(bot, player, relationship);
        SendRelationship(handler, bot, player, relationship);
        return true;
    }

    static bool HandleRelationshipListCommand(
        ChatHandler* handler,
        char const* args)
    {
        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.empty() || tokens.size() > 2)
        {
            Send(
                handler,
                "Usage: .botpersonality relationship list <botName> [limit]");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        int32 parsedLimit = 20;
        if (tokens.size() == 2 && !TryParseInt32(tokens[1], parsedLimit))
        {
            Send(handler, "Invalid list limit '{}'.", tokens[1]);
            return false;
        }

        uint32 const limit = static_cast<uint32>(
            std::clamp(parsedLimit, 1, 100));
        std::vector<BotRelationshipListEntry> entries =
            sBotRelationshipMgr.ListRelationships(
                bot->GetGUID().GetCounter(),
                limit);

        if (entries.empty())
        {
            Send(handler, "No relationships found for '{}'.", bot->GetName());
            return true;
        }

        Send(handler, "Relationships for {}:", bot->GetName());
        for (BotRelationshipListEntry const& entry : entries)
        {
            BotRelationship const& relationship = entry.relationship;
            Send(
                handler,
                "{} | affinity {} | {} | trust {} | respect {} | "
                "familiarity {} | last {}",
                RelationshipPlayerName(entry.playerGuid),
                static_cast<int32>(relationship.affinity),
                RelationshipLevelToString(
                    GetRelationshipLevel(relationship.affinity)),
                static_cast<int32>(relationship.trust),
                static_cast<int32>(relationship.respect),
                static_cast<int32>(relationship.familiarity),
                FormatRelationshipTime(relationship.lastInteraction));
        }

        return true;
    }

    static bool HandleRelationshipSaveCommand(
        ChatHandler* handler,
        char const* args)
    {
        if (!TokenizeArgs(args).empty())
        {
            Send(handler, "Usage: .botpersonality relationship save");
            return false;
        }

        BotRelationshipSaveStats const stats =
            sBotRelationshipMgr.SaveDirtyRelationships();
        Send(
            handler,
            "Saved: {} Failed: {} Remaining dirty: {}",
            stats.saved,
            stats.failed,
            stats.remainingDirty);
        return true;
    }

    static bool HandleRelationshipClearCacheCommand(
        ChatHandler* handler,
        char const* args)
    {
        if (!TokenizeArgs(args).empty())
        {
            Send(handler, "Usage: .botpersonality relationship clearcache");
            return false;
        }

        BotRelationshipSaveStats const stats =
            sBotRelationshipMgr.SaveDirtyRelationships();
        sBotRelationshipMgr.ClearCache();
        Send(
            handler,
            "Relationship cache cleared. Saved: {} Remaining dirty: {}",
            stats.saved,
            stats.remainingDirty);
        return true;
    }

    static bool HandleGameplayStatusCommand(
        ChatHandler* handler,
        char const* args)
    {
        if (!TokenizeArgs(args).empty())
        {
            Send(handler, "Usage: .botpersonality gameplay status");
            return false;
        }

        BotGameplayTrackerStats const stats = sBotGameplayTracker.GetStats();
        Send(handler, "Gameplay tracking: {}", stats.enabled ? "on" : "off");
        Send(
            handler,
            "Relationship updates: {}",
            stats.updateRelationships ? "on" : "off");
        Send(
            handler,
            "Tracked pairs: recent {} teamwork {} caps {} deaths {}",
            stats.recentPairs,
            stats.teamworkPairs,
            stats.capPairs,
            stats.deathWindows);
        return true;
    }

    static bool HandleGameplayShowCommand(
        ChatHandler* handler,
        char const* args)
    {
        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 2)
        {
            Send(
                handler,
                "Usage: .botpersonality gameplay show <botName> "
                "<playerName>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        Player* player = FindOnlineRealPlayer(handler, tokens[1]);
        if (!player)
            return true;

        BotRelationship relationship;
        if (!sBotRelationshipMgr.GetExistingRelationship(
                bot,
                player,
                relationship))
        {
            Send(
                handler,
                "No relationship exists for bot '{}' and player '{}'.",
                bot->GetName(),
                player->GetName());
            return true;
        }

        SendRelationship(handler, bot, player, relationship);
        return true;
    }

    static bool HandleGameplaySimulateCommand(
        ChatHandler* handler,
        char const* args)
    {
        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() < 3 || tokens.size() > 4)
        {
            Send(
                handler,
                "Usage: .botpersonality gameplay simulate <botName> "
                "<playerName> <event> [apply]");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        Player* player = FindOnlineRealPlayer(handler, tokens[1]);
        if (!player)
            return true;

        BotGameplayEvent event;
        if (!ParseGameplayEventArg(handler, tokens[2], event))
            return false;

        bool apply = false;
        if (tokens.size() == 4)
        {
            std::string option = BotPersonalityToLower(tokens[3]);
            if (option != "apply")
            {
                Send(handler, "Optional fourth argument must be 'apply'.");
                return false;
            }

            apply = true;
        }

        BotGameplayDebugResult const result =
            sBotGameplayTracker.SimulateEvent(bot, player, event, apply);
        if (!result.success)
        {
            Send(
                handler,
                result.error.empty() ?
                    "Unable to simulate gameplay event." :
                    result.error);
            return true;
        }

        Send(
            handler,
            "{} {} for bot '{}' and player '{}'.",
            apply ? "Applied" : "Previewed",
            BotGameplayEventToString(event),
            bot->GetName(),
            player->GetName());
        SendDelta(handler, "Base", result.baseDelta);
        SendDelta(handler, "Adjusted", result.adjustedDelta);
        Send(handler, "Relationship row updated: {}", result.applied ? "yes" : "no");
        return true;
    }

    static bool HandleGameplayRecentCommand(
        ChatHandler* handler,
        char const* args)
    {
        std::vector<std::string> tokens = TokenizeArgs(args);
        if (tokens.size() != 2)
        {
            Send(
                handler,
                "Usage: .botpersonality gameplay recent <botName> "
                "<playerName>");
            return false;
        }

        Player* bot = FindOnlineCharacter(handler, tokens[0]);
        if (!bot)
            return true;

        Player* player = FindOnlineRealPlayer(handler, tokens[1]);
        if (!player)
            return true;

        std::vector<BotGameplayRecentEvent> events =
            sBotGameplayTracker.GetRecentEvents(
                bot->GetGUID().GetCounter(),
                player->GetGUID().GetCounter(),
                10);

        if (events.empty())
        {
            Send(
                handler,
                "No recent gameplay events for '{}' and '{}'.",
                bot->GetName(),
                player->GetName());
            return true;
        }

        Send(
            handler,
            "Recent gameplay events for {} / {}:",
            bot->GetName(),
            player->GetName());
        for (BotGameplayRecentEvent const& event : events)
        {
            Send(
                handler,
                "{} | {} | map {} instance {} source {} value {} | "
                "delta a:{} t:{} r:{} f:{} | {}",
                FormatRelationshipTime(event.timestamp),
                BotGameplayEventToString(event.event),
                event.context.mapId,
                event.context.instanceId,
                event.context.sourceEntry,
                event.context.value,
                event.adjustedDelta.affinity,
                event.adjustedDelta.trust,
                event.adjustedDelta.respect,
                event.adjustedDelta.familiarity,
                event.applied ? "applied" : event.note);
        }

        return true;
    }

    static bool HandleGameplayTrackersCommand(
        ChatHandler* handler,
        char const* args)
    {
        if (!TokenizeArgs(args).empty())
        {
            Send(handler, "Usage: .botpersonality gameplay trackers");
            return false;
        }

        BotGameplayTrackerStats const stats = sBotGameplayTracker.GetStats();
        Send(handler, "Cooldowns: {}", stats.cooldowns);
        Send(
            handler,
            "Normal kill accumulators: {}",
            stats.normalKillAccumulators);
        Send(handler, "Heal accumulators: {}", stats.healAccumulators);
        Send(handler, "Recent event pairs: {}", stats.recentPairs);
        Send(handler, "Death windows: {}", stats.deathWindows);
        Send(handler, "Hourly cap pairs: {}", stats.capPairs);
        Send(handler, "Teamwork pairs: {}", stats.teamworkPairs);
        return true;
    }

    static bool HandleGameplayClearTrackersCommand(
        ChatHandler* handler,
        char const* args)
    {
        if (!TokenizeArgs(args).empty())
        {
            Send(handler, "Usage: .botpersonality gameplay cleartrackers");
            return false;
        }

        sBotGameplayTracker.ClearTrackers();
        Send(handler, "Bot Personality gameplay trackers cleared.");
        return true;
    }
};

void AddBotPersonalityCommands()
{
    new bot_personality_commandscript();
}
