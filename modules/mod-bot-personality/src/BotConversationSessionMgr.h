#ifndef MOD_BOT_PERSONALITY_BOT_CONVERSATION_SESSION_MGR_H
#define MOD_BOT_PERSONALITY_BOT_CONVERSATION_SESSION_MGR_H

#include "BotLlmTypes.h"
#include "Define.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

struct BotConversationSessionInfo
{
    uint64 sessionId = 0;
    uint32 botGuid = 0;
    uint32 playerGuid = 0;
    uint32 turns = 0;
    uint32 characters = 0;
    uint32 ageMs = 0;
    bool summarizationQueued = false;
};

class BotConversationSessionMgr
{
public:
    static BotConversationSessionMgr& instance()
    {
        static BotConversationSessionMgr instance;
        return instance;
    }

    void AddPlayerTurn(
        Player const* bot,
        Player const* player,
        std::string const& text,
        uint32 nowMs);
    void AddBotTurn(
        uint32 botGuid,
        uint32 playerGuid,
        std::string const& text,
        uint32 nowMs);
    void Update(uint32 nowMs);
    void OnPlayerLogout(uint32 guid);
    void OnPlayerMapChanged(uint32 guid);
    bool ForceSummarize(uint32 botGuid, uint32 playerGuid);
    uint32 FlushEligible();
    void Clear();

    std::size_t GetSessionCount() const { return _sessions.size(); }
    std::vector<BotConversationSessionInfo> GetSessions(uint32 limit) const;

private:
    struct Session
    {
        uint64 sessionId = 0;
        uint32 botGuid = 0;
        uint32 playerGuid = 0;
        std::string botName;
        std::string playerName;
        uint32 startedAtMs = 0;
        uint32 lastActivityAtMs = 0;
        std::vector<BotConversationTurn> turns;
        uint32 totalCharacters = 0;
        bool summarizationQueued = false;
    };

    BotConversationSessionMgr() = default;
    ~BotConversationSessionMgr() = default;

    uint64 MakeKey(uint32 botGuid, uint32 playerGuid) const;
    Session& GetOrCreate(
        Player const* bot,
        Player const* player,
        uint32 nowMs);
    bool CloseSession(uint64 key, bool forceEligible);
    bool IsEligible(Session const& session) const;
    bool IsCommandLooking(std::string const& text) const;
    void ExtractDeterministicPreference(
        Player const* bot,
        Player const* player,
        std::string const& text);
    void EnforceLimit();

    std::unordered_map<uint64, Session> _sessions;
    uint64 _nextSessionId = 1;
};

#define sBotConversationSessionMgr BotConversationSessionMgr::instance()

#endif
