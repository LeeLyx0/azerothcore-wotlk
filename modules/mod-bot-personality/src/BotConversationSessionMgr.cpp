#include "BotConversationSessionMgr.h"

#include "BotLlmMgr.h"
#include "BotMemoryMgr.h"
#include "Player.h"
#include "StringFormat.h"
#include "Timer.h"

#include <algorithm>
#include <cctype>

namespace
{
std::string Lower(std::string text)
{
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
    return text;
}

bool ContainsRolePreference(
    std::string const& text,
    std::string_view role,
    std::string_view verb)
{
    return text.find(Acore::StringFormat("i prefer {}", verb)) !=
            std::string::npos ||
        text.find(Acore::StringFormat("i prefer to {}", verb)) !=
            std::string::npos ||
        text.find(Acore::StringFormat("i usually {}", verb)) !=
            std::string::npos ||
        text.find(Acore::StringFormat("i usually play {}", role)) !=
            std::string::npos;
}
}

void BotConversationSessionMgr::AddPlayerTurn(
    Player const* bot,
    Player const* player,
    std::string const& text,
    uint32 nowMs)
{
    if (!sBotMemoryMgr.IsConversationSummaryEnabled() ||
        !bot || !player || text.empty() || IsCommandLooking(text) ||
        !sBotMemoryMgr.IsConversationTextSafe(text))
        return;

    ExtractDeterministicPreference(bot, player, text);
    Session& session = GetOrCreate(bot, player, nowMs);
    uint32 const maximumCharacters = sBotMemoryMgr.GetSessionMaxCharacters();
    uint32 const maximumTurns = sBotMemoryMgr.GetSessionMaxTurns();
    if (session.turns.size() >= maximumTurns ||
        session.totalCharacters + text.size() > maximumCharacters)
    {
        uint64 const key = MakeKey(session.botGuid, session.playerGuid);
        CloseSession(key, false);
        Session& replacement = GetOrCreate(bot, player, nowMs);
        replacement.turns.push_back(
            { BotConversationSpeaker::Player, text, nowMs });
        replacement.totalCharacters += text.size();
        replacement.lastActivityAtMs = nowMs;
        return;
    }

    session.turns.push_back(
        { BotConversationSpeaker::Player, text, nowMs });
    session.totalCharacters += text.size();
    session.lastActivityAtMs = nowMs;
}

void BotConversationSessionMgr::AddBotTurn(
    uint32 botGuid,
    uint32 playerGuid,
    std::string const& text,
    uint32 nowMs)
{
    if (!sBotMemoryMgr.IsConversationSummaryEnabled() || text.empty())
        return;

    auto itr = _sessions.find(MakeKey(botGuid, playerGuid));
    if (itr == _sessions.end())
        return;
    if (itr->second.turns.size() >= sBotMemoryMgr.GetSessionMaxTurns() ||
        itr->second.totalCharacters + text.size() >
            sBotMemoryMgr.GetSessionMaxCharacters())
    {
        CloseSession(itr->first, false);
        return;
    }

    itr->second.turns.push_back(
        { BotConversationSpeaker::Bot, text, nowMs });
    itr->second.totalCharacters += text.size();
    itr->second.lastActivityAtMs = nowMs;
}

void BotConversationSessionMgr::Update(uint32 nowMs)
{
    std::vector<uint64> expired;
    for (auto const& pair : _sessions)
    {
        if (getMSTimeDiff(pair.second.lastActivityAtMs, nowMs) >=
            sBotMemoryMgr.GetSessionTimeoutMs())
            expired.push_back(pair.first);
    }
    for (uint64 key : expired)
        CloseSession(key, false);
}

void BotConversationSessionMgr::OnPlayerLogout(uint32 guid)
{
    std::vector<uint64> matching;
    for (auto const& pair : _sessions)
    {
        if (pair.second.botGuid == guid || pair.second.playerGuid == guid)
            matching.push_back(pair.first);
    }
    for (uint64 key : matching)
        CloseSession(key, false);
}

void BotConversationSessionMgr::OnPlayerMapChanged(uint32 guid)
{
    OnPlayerLogout(guid);
}

bool BotConversationSessionMgr::ForceSummarize(
    uint32 botGuid,
    uint32 playerGuid)
{
    uint64 const key = MakeKey(botGuid, playerGuid);
    if (_sessions.find(key) == _sessions.end())
        return false;
    return CloseSession(key, true);
}

uint32 BotConversationSessionMgr::FlushEligible()
{
    std::vector<uint64> keys;
    for (auto const& pair : _sessions)
    {
        if (IsEligible(pair.second))
            keys.push_back(pair.first);
    }
    uint32 queued = 0;
    for (uint64 key : keys)
    {
        if (CloseSession(key, false))
            ++queued;
    }
    return queued;
}

void BotConversationSessionMgr::Clear()
{
    _sessions.clear();
}

std::vector<BotConversationSessionInfo>
BotConversationSessionMgr::GetSessions(uint32 limit) const
{
    std::vector<BotConversationSessionInfo> entries;
    uint32 const nowMs = getMSTime();
    for (auto const& pair : _sessions)
    {
        BotConversationSessionInfo info;
        info.sessionId = pair.second.sessionId;
        info.botGuid = pair.second.botGuid;
        info.playerGuid = pair.second.playerGuid;
        info.turns = static_cast<uint32>(pair.second.turns.size());
        info.characters = pair.second.totalCharacters;
        info.ageMs = getMSTimeDiff(pair.second.startedAtMs, nowMs);
        info.summarizationQueued = pair.second.summarizationQueued;
        entries.push_back(info);
    }
    std::sort(
        entries.begin(),
        entries.end(),
        [](BotConversationSessionInfo const& left,
            BotConversationSessionInfo const& right)
        {
            return left.ageMs > right.ageMs;
        });
    if (entries.size() > limit)
        entries.resize(limit);
    return entries;
}

uint64 BotConversationSessionMgr::MakeKey(
    uint32 botGuid,
    uint32 playerGuid) const
{
    return (static_cast<uint64>(botGuid) << 32) | playerGuid;
}

BotConversationSessionMgr::Session&
BotConversationSessionMgr::GetOrCreate(
    Player const* bot,
    Player const* player,
    uint32 nowMs)
{
    uint32 const botGuid = bot->GetGUID().GetCounter();
    uint32 const playerGuid = player->GetGUID().GetCounter();
    uint64 const key = MakeKey(botGuid, playerGuid);
    auto itr = _sessions.find(key);
    if (itr != _sessions.end())
        return itr->second;

    Session session;
    session.sessionId = _nextSessionId++;
    session.botGuid = botGuid;
    session.playerGuid = playerGuid;
    session.botName = bot->GetName();
    session.playerName = player->GetName();
    session.startedAtMs = nowMs;
    session.lastActivityAtMs = nowMs;
    auto inserted = _sessions.emplace(key, std::move(session));
    EnforceLimit();
    return inserted.first->second;
}

bool BotConversationSessionMgr::CloseSession(
    uint64 key,
    bool forceEligible)
{
    auto itr = _sessions.find(key);
    if (itr == _sessions.end())
        return false;

    Session session = std::move(itr->second);
    _sessions.erase(itr);
    if ((!forceEligible && !IsEligible(session)) ||
        !sBotMemoryMgr.IsLlmSummarizationEnabled())
        return false;

    bool const queued = sBotLlmMgr.TryQueueMemorySummary(
        session.sessionId,
        session.botGuid,
        session.playerGuid,
        session.botName,
        session.playerName,
        std::move(session.turns));
    if (queued)
        sBotMemoryMgr.NoteSummaryQueued();
    return queued;
}

bool BotConversationSessionMgr::IsEligible(Session const& session) const
{
    return session.turns.size() >=
            sBotMemoryMgr.GetMinimumTurnsToSummarize() &&
        session.totalCharacters >=
            sBotMemoryMgr.GetMinimumCharactersToSummarize();
}

bool BotConversationSessionMgr::IsCommandLooking(
    std::string const& text) const
{
    std::string const lower = Lower(text);
    if (lower.empty())
        return true;
    if (lower.front() == '.' || lower.front() == '/' || lower.front() == '#')
        return true;
    return lower.find("bot\t") != std::string::npos ||
        lower.find('\t') != std::string::npos;
}

void BotConversationSessionMgr::ExtractDeterministicPreference(
    Player const* bot,
    Player const* player,
    std::string const& text)
{
    std::string const lower = Lower(text);
    std::string role;
    if (ContainsRolePreference(lower, "tank", "tank") ||
        ContainsRolePreference(lower, "tanking", "tanking"))
        role = "tanking";
    else if (ContainsRolePreference(lower, "healer", "heal") ||
        ContainsRolePreference(lower, "healing", "healing"))
        role = "healing";
    else if (ContainsRolePreference(lower, "dps", "dps"))
        role = "playing damage roles";
    if (role.empty())
        return;

    BotMemoryCandidate candidate;
    candidate.botGuid = bot->GetGUID().GetCounter();
    candidate.playerGuid = player->GetGUID().GetCounter();
    candidate.suggestedType = BotMemoryType::PlayerPreference;
    candidate.source = BotMemorySource::DeterministicConversationRule;
    candidate.deterministicSummary = Acore::StringFormat(
        "{} prefers {} in groups.", player->GetName(), role);
    candidate.subjectKey = "player_preference:role";
    candidate.importance = 55;
    candidate.confidence = 92;
    sBotMemoryMgr.StoreCandidate(std::move(candidate));
}

void BotConversationSessionMgr::EnforceLimit()
{
    while (_sessions.size() > sBotMemoryMgr.GetMaxActiveSessions())
    {
        auto oldest = _sessions.begin();
        for (auto itr = _sessions.begin(); itr != _sessions.end(); ++itr)
        {
            if (itr->second.lastActivityAtMs <
                oldest->second.lastActivityAtMs)
                oldest = itr;
        }
        CloseSession(oldest->first, false);
    }
}
