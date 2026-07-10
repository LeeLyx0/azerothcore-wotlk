#ifndef MOD_BOT_PERSONALITY_BOT_CONVERSATION_HISTORY_MGR_H
#define MOD_BOT_PERSONALITY_BOT_CONVERSATION_HISTORY_MGR_H

#include "BotLlmTypes.h"
#include "Define.h"

#include <cstddef>
#include <deque>
#include <unordered_map>
#include <vector>

class BotConversationHistoryMgr
{
public:
    static BotConversationHistoryMgr& instance()
    {
        static BotConversationHistoryMgr instance;
        return instance;
    }

    void LoadConfig(bool debugLogging);
    bool IsEnabled() const { return _enable; }

    void AddTurn(
        uint32 botGuid,
        uint32 playerGuid,
        BotConversationSpeaker speaker,
        std::string text,
        uint32 nowMs);
    std::vector<BotConversationTurn> GetHistory(
        uint32 botGuid,
        uint32 playerGuid,
        uint32 nowMs);
    void Clear();
    void ClearBot(uint32 botGuid);
    void ClearConversation(uint32 botGuid, uint32 playerGuid);
    std::size_t GetConversationCount() const;

private:
    struct Conversation
    {
        std::deque<BotConversationTurn> turns;
        uint32 lastAccessMs = 0;
    };

    BotConversationHistoryMgr() = default;
    ~BotConversationHistoryMgr() = default;

    uint64 MakeKey(uint32 botGuid, uint32 playerGuid) const;
    void TrimConversation(Conversation& conversation);
    void Cleanup(uint32 nowMs);

    bool _enable = true;
    bool _debugLogging = false;
    uint32 _maxTurns = 8;
    uint32 _maxCharacters = 2000;
    uint32 _expiryMs = 30 * 60 * 1000;
    uint32 _maxConversations = 10000;

    std::unordered_map<uint64, Conversation> _conversations;
};

#define sBotConversationHistoryMgr BotConversationHistoryMgr::instance()

#endif
