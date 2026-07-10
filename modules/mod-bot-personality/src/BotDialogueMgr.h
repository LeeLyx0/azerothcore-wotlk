#ifndef MOD_BOT_PERSONALITY_BOT_DIALOGUE_MGR_H
#define MOD_BOT_PERSONALITY_BOT_DIALOGUE_MGR_H

#include "BotChatIntent.h"
#include "BotDialogueContext.h"
#include "Define.h"
#include "ObjectGuid.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

class IBotDialogueProvider;
class Group;
class Player;

struct BotDialogueCooldownStats
{
    std::size_t pairCooldownEntries = 0;
    std::size_t botCooldownEntries = 0;
    std::size_t duplicateEntries = 0;
    std::size_t rateWindowEntries = 0;
    std::size_t recentResponseEntries = 0;
};

struct BotDialogueDebugResult
{
    bool success = false;
    std::string error;
    BotDialogueContext context;
    std::string response;
};

class BotDialogueMgr
{
public:
    static BotDialogueMgr& instance()
    {
        static BotDialogueMgr instance;
        return instance;
    }

    void LoadConfig(bool reload);

    bool IsChatEnabled() const;
    bool IsDebugLoggingEnabled() const;
    void Update(uint32 diff);

    bool HandleIncomingWhisper(
        Player* sender,
        Player* bot,
        uint32 language,
        std::string const& message);
    bool HandleIncomingGroupChat(
        Player* sender,
        Group* group,
        uint32 type,
        uint32 language,
        std::string const& message);

    BotChatIntent ParseIntent(std::string const& message) const;
    std::string NormalizeMessage(std::string const& message) const;

    BotDialogueDebugResult GenerateDebugResponse(
        Player* bot,
        Player* player,
        BotChatIntent intent);
    BotDialogueDebugResult ForceWhisper(
        Player* bot,
        Player* player,
        BotChatIntent intent);

    BotDialogueCooldownStats GetCooldownStats() const;
    void ClearChatState();

private:
    struct Config
    {
        bool enable = true;
        bool respondToWhispers = true;
        bool respondToGroupChat = true;
        bool respondToUnknown = true;
        bool forceResponseForDebug = false;

        uint32 pairCooldownMs = 5000;
        uint32 botCooldownMs = 2000;
        uint32 duplicateWindowMs = 15000;
        uint32 maxResponsesPerMinute = 8;

        uint32 maxInputLength = 255;
        uint32 maxOutputLength = 255;

        uint32 minimumGreetingChance = 70;
        uint32 minimumHelpChance = 85;
        uint32 minimumIdentityChance = 85;
        uint32 minimumThanksChance = 50;
        uint32 minimumInsultChance = 40;
        uint32 unknownChance = 25;

        bool replyDelayEnable = true;
        uint32 replyDelayBaseMs = 700;
        uint32 replyDelayInputCharMs = 8;
        uint32 replyDelayOutputCharMs = 45;
        uint32 replyDelayRandomMs = 1200;
        uint32 replyDelayMaxMs = 10000;

        bool debugLogging = false;
    };

    struct DuplicateRecord
    {
        uint32 messageHash = 0;
        uint32 lastSeenMs = 0;
        uint32 duplicateCount = 0;
    };

    struct PendingReply
    {
        ObjectGuid botGuid;
        ObjectGuid playerGuid;
        uint32 groupId = 0;
        BotDialogueContext context;
        std::string response;
        uint32 queuedAtMs = 0;
        uint32 delayMs = 0;
    };

    BotDialogueMgr();
    ~BotDialogueMgr();

    BotDialogueMgr(BotDialogueMgr const&) = delete;
    BotDialogueMgr& operator=(BotDialogueMgr const&) = delete;

    std::optional<BotDialogueContext> BuildContext(
        Player* sender,
        Player* bot,
        std::string const& message,
        std::optional<BotChatIntent> forcedIntent,
        bool isWhisper,
        bool isPartyChat,
        bool isRaidChat);
    void ApplyRelationshipContext(
        BotDialogueContext& context,
        Player* bot,
        Player* sender) const;

    bool HandleIncomingDialogue(
        Player* sender,
        Player* bot,
        uint32 language,
        std::string const& message,
        bool isWhisper,
        bool isPartyChat,
        bool isRaidChat);
    bool IsEligibleWhisper(
        Player* sender,
        Player* bot,
        uint32 language,
        std::string const& message) const;
    bool IsEligibleGroupChat(
        Player* sender,
        Group* group,
        uint32 type,
        uint32 language,
        std::string const& message) const;
    bool IsAddonControlMessage(std::string const& message) const;
    bool IsPlayerbotsCommand(Player* bot, std::string const& message) const;

    bool ShouldSuppressDuplicate(
        uint32 botGuid,
        uint32 playerGuid,
        std::string const& normalized,
        uint32 nowMs,
        bool& repeatedSpam);
    bool ShouldSuppressCooldowns(
        uint32 botGuid,
        uint32 playerGuid,
        uint32 nowMs);
    void RecordSuccessfulResponse(
        BotDialogueContext const& context,
        std::string const& response,
        uint32 nowMs);

    uint32 GetResponseChance(BotDialogueContext const& context) const;
    bool RollResponseChance(
        BotDialogueContext const& context,
        uint32 chance) const;
    uint32 GetSelectionSeed(BotDialogueContext const& context);

    std::optional<std::string> GenerateResponse(
        BotDialogueContext& context);
    uint32 CalculateReplyDelay(
        BotDialogueContext const& context,
        std::string const& response) const;
    void QueueWhisper(
        Player* bot,
        Player* receiver,
        BotDialogueContext const& context,
        std::string response,
        uint32 nowMs);
    void QueueGroupReply(
        Player* bot,
        Player* receiver,
        BotDialogueContext const& context,
        std::string response,
        uint32 nowMs);
    void ProcessPendingReplies(uint32 nowMs);
    void DeliverPendingReply(PendingReply& reply);
    bool SendWhisper(Player* bot, Player* receiver, std::string& response);
    bool SendGroupChat(
        Player* bot,
        Player* receiver,
        BotDialogueContext const& context,
        std::string& response);

    void Cleanup(uint32 nowMs);

    Config _config;
    std::unique_ptr<IBotDialogueProvider> _provider;

    std::unordered_map<uint64, uint32> _pairCooldowns;
    std::unordered_map<uint32, uint32> _botCooldowns;
    std::unordered_map<uint64, DuplicateRecord> _duplicates;
    std::unordered_map<uint32, std::deque<uint32>> _rateWindows;
    std::unordered_map<uint64, std::string> _recentResponses;
    std::deque<PendingReply> _pendingReplies;

    uint32 _interactionCounter = 0;
    uint32 _lastCleanupMs = 0;
};

#define sBotDialogueMgr BotDialogueMgr::instance()

#endif
