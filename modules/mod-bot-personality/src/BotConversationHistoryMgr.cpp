#include "BotConversationHistoryMgr.h"

#include "Config.h"
#include "Log.h"
#include "Timer.h"

#include <algorithm>

namespace
{
uint32 ReadUIntConfig(
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
            "module.botpersonality.llm",
            "{} value {} clamped to {}",
            name,
            configured,
            clamped);
    }

    return static_cast<uint32>(clamped);
}
}

void BotConversationHistoryMgr::LoadConfig(bool debugLogging)
{
    _debugLogging = debugLogging;
    _enable = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.History.Enable",
        true);
    _maxTurns = ReadUIntConfig(
        "BotPersonality.LLM.History.MaxTurns",
        8,
        0,
        50,
        debugLogging);
    _maxCharacters = ReadUIntConfig(
        "BotPersonality.LLM.History.MaxCharacters",
        2000,
        0,
        20000,
        debugLogging);
    _expiryMs = ReadUIntConfig(
        "BotPersonality.LLM.History.ExpiryMinutes",
        30,
        1,
        1440,
        debugLogging) * 60 * IN_MILLISECONDS;
    _maxConversations = ReadUIntConfig(
        "BotPersonality.LLM.History.MaxConversations",
        10000,
        1,
        100000,
        debugLogging);

    if (!_enable)
        Clear();
}

void BotConversationHistoryMgr::AddTurn(
    uint32 botGuid,
    uint32 playerGuid,
    BotConversationSpeaker speaker,
    std::string text,
    uint32 nowMs)
{
    if (!_enable || !botGuid || !playerGuid || text.empty())
        return;

    Cleanup(nowMs);
    Conversation& conversation = _conversations[MakeKey(botGuid, playerGuid)];
    conversation.lastAccessMs = nowMs;
    conversation.turns.push_back({ speaker, std::move(text), nowMs });
    TrimConversation(conversation);

    while (_conversations.size() > _maxConversations)
        _conversations.erase(_conversations.begin());
}

std::vector<BotConversationTurn> BotConversationHistoryMgr::GetHistory(
    uint32 botGuid,
    uint32 playerGuid,
    uint32 nowMs)
{
    if (!_enable || !botGuid || !playerGuid)
        return {};

    Cleanup(nowMs);
    auto itr = _conversations.find(MakeKey(botGuid, playerGuid));
    if (itr == _conversations.end())
        return {};

    itr->second.lastAccessMs = nowMs;
    return {
        itr->second.turns.begin(),
        itr->second.turns.end()
    };
}

void BotConversationHistoryMgr::Clear()
{
    _conversations.clear();
}

void BotConversationHistoryMgr::ClearBot(uint32 botGuid)
{
    for (auto itr = _conversations.begin(); itr != _conversations.end();)
    {
        if ((itr->first >> 32) == botGuid)
            itr = _conversations.erase(itr);
        else
            ++itr;
    }
}

void BotConversationHistoryMgr::ClearConversation(
    uint32 botGuid,
    uint32 playerGuid)
{
    _conversations.erase(MakeKey(botGuid, playerGuid));
}

std::size_t BotConversationHistoryMgr::GetConversationCount() const
{
    return _conversations.size();
}

uint64 BotConversationHistoryMgr::MakeKey(
    uint32 botGuid,
    uint32 playerGuid) const
{
    return (static_cast<uint64>(botGuid) << 32) |
        static_cast<uint64>(playerGuid);
}

void BotConversationHistoryMgr::TrimConversation(Conversation& conversation)
{
    while (conversation.turns.size() > _maxTurns)
        conversation.turns.pop_front();

    uint32 total = 0;
    for (BotConversationTurn const& turn : conversation.turns)
        total += static_cast<uint32>(turn.text.size());

    while (!conversation.turns.empty() && total > _maxCharacters)
    {
        total -= static_cast<uint32>(conversation.turns.front().text.size());
        conversation.turns.pop_front();
    }
}

void BotConversationHistoryMgr::Cleanup(uint32 nowMs)
{
    for (auto itr = _conversations.begin(); itr != _conversations.end();)
    {
        if (itr->second.lastAccessMs &&
            getMSTimeDiff(itr->second.lastAccessMs, nowMs) > _expiryMs)
            itr = _conversations.erase(itr);
        else
            ++itr;
    }
}
