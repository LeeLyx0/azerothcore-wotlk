#include "BotPersonalityMgr.h"

#include "CharacterCache.h"
#include "Chat.h"
#include "CommandScript.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "Util.h"

#include <charconv>
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
            {
                "clearcache",
                HandleClearCacheCommand,
                SEC_ADMINISTRATOR,
                Console::Yes
            },
            { "", HandleHelpCommand, SEC_GAMEMASTER, Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "botpersonality", botPersonalityCommandTable }
        };

        return commandTable;
    }

    static bool HandleHelpCommand(ChatHandler* handler, char const* /*args*/)
    {
        SendRootUsage(handler);
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
};

void AddBotPersonalityCommands()
{
    new bot_personality_commandscript();
}
