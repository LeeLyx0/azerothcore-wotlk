#include "BotMemoryCommands.h"

#include "BotConversationSessionMgr.h"
#include "BotMemoryMgr.h"
#include "BotPersonalityMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "StringFormat.h"
#include "Util.h"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
template <typename... Args>
void Send(ChatHandler* handler, char const* format, Args&&... args)
{
    handler->SendSysMessage(Acore::StringFormat(
        format,
        std::forward<Args>(args)...));
}

std::vector<std::string> Tokens(char const* args)
{
    std::vector<std::string> tokens;
    std::istringstream input(args ? args : "");
    std::string token;
    while (input >> token)
        tokens.push_back(token);
    return tokens;
}

template <typename T>
bool ParseNumber(std::string const& text, T& value)
{
    char const* begin = text.data();
    char const* end = begin + text.size();
    auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

Player* FindOnline(
    ChatHandler* handler,
    std::string name,
    bool requireBot)
{
    if (!normalizePlayerName(name))
    {
        Send(handler, "Invalid character name '{}'.", name);
        return nullptr;
    }
    ObjectGuid const guid = sCharacterCache->GetCharacterGuidByName(name);
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (!player)
    {
        Send(handler, "Character '{}' is not online.", name);
        return nullptr;
    }
    bool const isBot = sBotPersonalityMgr.IsPlayerbot(player);
    if (requireBot != isBot)
    {
        Send(
            handler,
            "Character '{}' is {}a Playerbot.",
            name,
            requireBot ? "not " : "");
        return nullptr;
    }
    return player;
}

std::string CharacterName(uint32 guid)
{
    std::string name;
    if (sCharacterCache->GetCharacterNameByGuid(
            ObjectGuid::Create<HighGuid::Player>(guid), name))
        return name;
    return Acore::StringFormat("#{}", guid);
}

std::string FormatTime(uint32 timestamp)
{
    if (!timestamp)
        return "never";
    return Acore::Time::TimeToTimestampStr(Seconds(timestamp));
}

void Usage(ChatHandler* handler)
{
    Send(handler, "Usage: .botpersonality memory status");
    Send(handler, "Usage: .botpersonality memory show <bot> <player> [limit]");
    Send(handler, "Usage: .botpersonality memory get <memoryId>");
    Send(
        handler,
        "Usage: .botpersonality memory add <bot> <player> <type> "
        "<importance> <summary>");
    Send(handler, "Usage: .botpersonality memory delete <memoryId>");
    Send(handler, "Usage: .botpersonality memory clear <bot> <player>");
    Send(handler, "Usage: .botpersonality memory pin <memoryId>");
    Send(handler, "Usage: .botpersonality memory unpin <memoryId>");
    Send(
        handler,
        "Usage: .botpersonality memory setimportance <memoryId> <value>");
    Send(
        handler,
        "Usage: .botpersonality memory retrieve <bot> <player> <intent>");
    Send(
        handler,
        "Usage: .botpersonality memory summarize <bot> <player>");
    Send(handler, "Usage: .botpersonality memory sessions");
    Send(handler, "Usage: .botpersonality memory flushsessions");
    Send(handler, "Usage: .botpersonality memory expire");
    Send(handler, "Usage: .botpersonality memory clearcache");
    Send(handler, "Usage: .botpersonality memory metrics");
    Send(handler, "Usage: .botpersonality memory resetmetrics");
}

bool HandleStatus(ChatHandler* handler, char const* /*args*/)
{
    BotMemoryRuntimeStats const stats = sBotMemoryMgr.GetStats();
    Send(handler, "Memory enabled: {}", stats.enabled ? "Yes" : "No");
    Send(
        handler,
        "Gameplay memories: {}",
        stats.gameplayMemories ? "Enabled" : "Disabled");
    Send(
        handler,
        "Relationship milestones: {}",
        stats.relationshipMilestones ? "Enabled" : "Disabled");
    Send(
        handler,
        "Conversation summaries: {}",
        stats.conversationSummaries ? "Enabled" : "Disabled");
    Send(
        handler,
        "LLM summarization: {}",
        stats.llmSummarization ? "Enabled" : "Disabled");
    Send(handler, "Cached relationships: {}", stats.cachedRelationships);
    Send(handler, "Cached memories: {}", stats.cachedMemories);
    Send(handler, "Active conversation sessions: {}", stats.activeSessions);
    Send(
        handler,
        "Pending summary requests: {}",
        stats.pendingSummaryRequests);
    Send(handler, "Persistent memories: {}", stats.persistentMemories);
    return true;
}

bool ResolvePair(
    ChatHandler* handler,
    std::vector<std::string> const& tokens,
    Player*& bot,
    Player*& player)
{
    if (tokens.size() < 2)
    {
        Usage(handler);
        return false;
    }
    bot = FindOnline(handler, tokens[0], true);
    player = FindOnline(handler, tokens[1], false);
    return bot && player;
}

void SendMemory(ChatHandler* handler, BotMemory const& memory)
{
    Send(handler, "ID: {}", memory.memoryId);
    Send(handler, "Bot: {}", CharacterName(memory.botGuid));
    Send(handler, "Player: {}", CharacterName(memory.playerGuid));
    Send(handler, "Type: {}", BotMemoryTypeToString(memory.type));
    Send(handler, "Source: {}", BotMemorySourceToString(memory.source));
    Send(handler, "Importance: {}", memory.importance);
    Send(handler, "Confidence: {}", memory.confidence);
    Send(handler, "Created: {}", FormatTime(memory.createdAt));
    Send(handler, "Updated: {}", FormatTime(memory.updatedAt));
    Send(handler, "Last recalled: {}", FormatTime(memory.lastRecalledAt));
    Send(handler, "Expires: {}", FormatTime(memory.expiresAt));
    Send(handler, "Reinforced: {}", memory.reinforcementCount);
    Send(handler, "Pinned: {}", memory.pinned ? "Yes" : "No");
    Send(handler, "Negative: {}", memory.negative ? "Yes" : "No");
    Send(handler, "Subject: {}", memory.subjectKey);
    Send(handler, "Source event: {}", memory.sourceEventId);
    Send(handler, "Source reference: {}", memory.sourceReference);
    Send(handler, "Summary: {}", memory.summary);
}

bool HandleShow(ChatHandler* handler, char const* args)
{
    std::vector<std::string> const tokens = Tokens(args);
    Player* bot = nullptr;
    Player* player = nullptr;
    if (!ResolvePair(handler, tokens, bot, player))
        return false;
    uint32 limit = 20;
    if (tokens.size() >= 3 && !ParseNumber(tokens[2], limit))
    {
        Send(handler, "Invalid limit '{}'.", tokens[2]);
        return false;
    }
    limit = std::clamp(limit, 1u, 100u);
    std::vector<BotMemory> memories = sBotMemoryMgr.ListMemories(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter(),
        limit);
    Send(handler, "Bot: {}", bot->GetName());
    Send(handler, "Player: {}", player->GetName());
    Send(handler, "Memories: {}", memories.size());
    uint32 const now = static_cast<uint32>(
        GameTime::GetGameTime().count());
    for (BotMemory const& memory : memories)
    {
        Send(
            handler,
            "#{} {} importance {} confidence {} reinforced {} pinned {}{}",
            memory.memoryId,
            BotMemoryTypeToString(memory.type),
            memory.importance,
            memory.confidence,
            memory.reinforcementCount,
            memory.pinned ? "yes" : "no",
            !memory.pinned && memory.expiresAt && memory.expiresAt <= now ?
                " EXPIRED" : "");
        Send(handler, "  {}", memory.summary);
    }
    return true;
}

bool HandleGet(ChatHandler* handler, char const* args)
{
    uint64 id = 0;
    std::vector<std::string> const tokens = Tokens(args);
    if (tokens.empty() || !ParseNumber(tokens[0], id))
        return false;
    std::optional<BotMemory> memory = sBotMemoryMgr.GetMemory(id);
    if (!memory)
    {
        Send(handler, "Memory {} was not found.", id);
        return false;
    }
    SendMemory(handler, *memory);
    return true;
}

bool HandleAdd(ChatHandler* handler, char const* args)
{
    std::vector<std::string> const tokens = Tokens(args);
    if (tokens.size() < 5)
    {
        Usage(handler);
        return false;
    }
    Player* bot = nullptr;
    Player* player = nullptr;
    if (!ResolvePair(handler, tokens, bot, player))
        return false;
    BotMemoryType type;
    int32 importance = 0;
    if (!BotMemoryTypeFromString(tokens[2], type) ||
        !ParseNumber(tokens[3], importance))
    {
        Send(handler, "Invalid memory type or importance.");
        return false;
    }
    std::string summary;
    for (std::size_t i = 4; i < tokens.size(); ++i)
    {
        if (!summary.empty())
            summary += ' ';
        summary += tokens[i];
    }
    bool const stored = sBotMemoryMgr.AddGmMemory(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter(),
        type,
        importance,
        std::move(summary),
        false);
    Send(handler, stored ? "Memory added." : "Memory was rejected.");
    return stored;
}

bool HandleDelete(ChatHandler* handler, char const* args)
{
    uint64 id = 0;
    std::vector<std::string> const tokens = Tokens(args);
    if (tokens.empty() || !ParseNumber(tokens[0], id))
        return false;
    if (!sBotMemoryMgr.GetMemory(id))
    {
        Send(handler, "Memory {} was not found.", id);
        return false;
    }
    sBotMemoryMgr.DeleteMemory(id);
    Send(handler, "Memory {} deleted.", id);
    return true;
}

bool HandleClear(ChatHandler* handler, char const* args)
{
    std::vector<std::string> const tokens = Tokens(args);
    Player* bot = nullptr;
    Player* player = nullptr;
    if (!ResolvePair(handler, tokens, bot, player))
        return false;
    uint32 const deleted = sBotMemoryMgr.ClearMemories(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter());
    Send(handler, "Deleted {} memories.", deleted);
    return true;
}

bool SetPinned(ChatHandler* handler, char const* args, bool pinned)
{
    uint64 id = 0;
    std::vector<std::string> const tokens = Tokens(args);
    if (tokens.empty() || !ParseNumber(tokens[0], id))
        return false;
    bool const changed = sBotMemoryMgr.SetPinned(id, pinned);
    Send(
        handler,
        changed ? "Memory updated." : "Memory was not found or limit hit.");
    return changed;
}

bool HandlePin(ChatHandler* handler, char const* args)
{
    return SetPinned(handler, args, true);
}

bool HandleUnpin(ChatHandler* handler, char const* args)
{
    return SetPinned(handler, args, false);
}

bool HandleSetImportance(ChatHandler* handler, char const* args)
{
    std::vector<std::string> const tokens = Tokens(args);
    uint64 id = 0;
    int32 value = 0;
    if (tokens.size() < 2 || !ParseNumber(tokens[0], id) ||
        !ParseNumber(tokens[1], value))
        return false;
    bool const changed = sBotMemoryMgr.SetImportance(id, value);
    Send(handler, changed ? "Importance updated." : "Memory not found.");
    return changed;
}

bool HandleRetrieve(ChatHandler* handler, char const* args)
{
    std::vector<std::string> const tokens = Tokens(args);
    Player* bot = nullptr;
    Player* player = nullptr;
    if (!ResolvePair(handler, tokens, bot, player) || tokens.size() < 3)
        return false;
    BotChatIntent intent;
    if (!BotChatIntentFromString(tokens[2], intent))
    {
        Send(handler, "Invalid intent '{}'.", tokens[2]);
        return false;
    }
    BotMemoryQuery query;
    query.botGuid = bot->GetGUID().GetCounter();
    query.playerGuid = player->GetGUID().GetCounter();
    query.intent = intent;
    std::vector<BotMemorySelection> selected = sBotMemoryMgr.Retrieve(query);
    Send(handler, "Query intent: {}", BotChatIntentToString(intent));
    Send(handler, "Selected memories: {}", selected.size());
    for (BotMemorySelection const& selection : selected)
    {
        Send(handler, "Score {}: {}", selection.score,
            selection.memory.summary);
    }
    return true;
}

bool HandleSummarize(ChatHandler* handler, char const* args)
{
    std::vector<std::string> const tokens = Tokens(args);
    Player* bot = nullptr;
    Player* player = nullptr;
    if (!ResolvePair(handler, tokens, bot, player))
        return false;
    bool const queued = sBotConversationSessionMgr.ForceSummarize(
        bot->GetGUID().GetCounter(),
        player->GetGUID().GetCounter());
    Send(
        handler,
        queued ? "Summary queued." :
            "No active session or summarization unavailable.");
    return queued;
}

bool HandleSessions(ChatHandler* handler, char const* /*args*/)
{
    std::vector<BotConversationSessionInfo> sessions =
        sBotConversationSessionMgr.GetSessions(50);
    Send(handler, "Active sessions: {}", sessions.size());
    for (BotConversationSessionInfo const& session : sessions)
    {
        Send(
            handler,
            "#{} {} / {} turns {} chars {} age {} ms queued {}",
            session.sessionId,
            CharacterName(session.botGuid),
            CharacterName(session.playerGuid),
            session.turns,
            session.characters,
            session.ageMs,
            session.summarizationQueued ? "yes" : "no");
    }
    return true;
}

bool HandleFlushSessions(ChatHandler* handler, char const* /*args*/)
{
    uint32 const queued = sBotConversationSessionMgr.FlushEligible();
    Send(handler, "Eligible sessions queued: {}", queued);
    return true;
}

bool HandleExpire(ChatHandler* handler, char const* /*args*/)
{
    BotMemoryExpiryStats const stats = sBotMemoryMgr.ExpireMemories();
    Send(handler, "Expired deleted: {}", stats.deleted);
    Send(handler, "Expired skipped because pinned: {}", stats.pinnedSkipped);
    Send(handler, "Failures: {}", stats.failures);
    return true;
}

bool HandleClearCache(ChatHandler* handler, char const* /*args*/)
{
    sBotMemoryMgr.ClearCache();
    sBotConversationSessionMgr.Clear();
    Send(handler, "Memory cache and temporary sessions cleared.");
    return true;
}

bool HandleMetrics(ChatHandler* handler, char const* /*args*/)
{
    BotMemoryMetrics const metrics = sBotMemoryMgr.GetStats().metrics;
    Send(handler, "Candidates created: {}", metrics.candidatesCreated);
    Send(handler, "Candidates rejected: {}", metrics.candidatesRejected);
    Send(handler, "Deterministic memories stored: {}",
        metrics.deterministicStored);
    Send(handler, "LLM summary requests queued: {}",
        metrics.summaryRequestsQueued);
    Send(handler, "LLM summaries completed: {}",
        metrics.summariesCompleted);
    Send(handler, "LLM summaries rejected: {}", metrics.summariesRejected);
    Send(handler, "Privacy rejections: {}", metrics.privacyRejected);
    Send(handler, "Duplicates ignored: {}", metrics.duplicatesIgnored);
    Send(handler, "Memories reinforced: {}", metrics.memoriesReinforced);
    Send(handler, "Memories merged: {}", metrics.memoriesMerged);
    Send(handler, "Memories expired: {}", metrics.memoriesExpired);
    Send(handler, "Memories evicted: {}", metrics.memoriesEvicted);
    Send(handler, "Memories retrieved: {}", metrics.memoriesRetrieved);
    return true;
}

bool HandleResetMetrics(ChatHandler* handler, char const* /*args*/)
{
    sBotMemoryMgr.ResetMetrics();
    Send(handler, "Memory metrics reset.");
    return true;
}

bool HandleHelp(ChatHandler* handler, char const* /*args*/)
{
    Usage(handler);
    return true;
}
}

ChatCommandTable const& GetBotMemoryCommandTable()
{
    static ChatCommandTable commands =
    {
        { "status", HandleStatus, SEC_GAMEMASTER, Console::Yes },
        { "show", HandleShow, SEC_ADMINISTRATOR, Console::Yes },
        { "get", HandleGet, SEC_ADMINISTRATOR, Console::Yes },
        { "add", HandleAdd, SEC_ADMINISTRATOR, Console::Yes },
        { "delete", HandleDelete, SEC_ADMINISTRATOR, Console::Yes },
        { "clear", HandleClear, SEC_ADMINISTRATOR, Console::Yes },
        { "pin", HandlePin, SEC_ADMINISTRATOR, Console::Yes },
        { "unpin", HandleUnpin, SEC_ADMINISTRATOR, Console::Yes },
        {
            "setimportance",
            HandleSetImportance,
            SEC_ADMINISTRATOR,
            Console::Yes
        },
        { "retrieve", HandleRetrieve, SEC_ADMINISTRATOR, Console::Yes },
        { "summarize", HandleSummarize, SEC_ADMINISTRATOR, Console::Yes },
        { "sessions", HandleSessions, SEC_ADMINISTRATOR, Console::Yes },
        {
            "flushsessions",
            HandleFlushSessions,
            SEC_ADMINISTRATOR,
            Console::Yes
        },
        { "expire", HandleExpire, SEC_ADMINISTRATOR, Console::Yes },
        { "clearcache", HandleClearCache, SEC_ADMINISTRATOR, Console::Yes },
        { "metrics", HandleMetrics, SEC_GAMEMASTER, Console::Yes },
        {
            "resetmetrics",
            HandleResetMetrics,
            SEC_ADMINISTRATOR,
            Console::Yes
        },
        { "", HandleHelp, SEC_GAMEMASTER, Console::Yes }
    };
    return commands;
}
