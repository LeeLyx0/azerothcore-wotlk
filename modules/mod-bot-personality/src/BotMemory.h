#ifndef MOD_BOT_PERSONALITY_BOT_MEMORY_H
#define MOD_BOT_PERSONALITY_BOT_MEMORY_H

#include "BotChatIntent.h"
#include "BotGameplayEvent.h"
#include "BotProactiveDialogueEvent.h"
#include "BotRelationship.h"
#include "Define.h"

#include <string>
#include <string_view>
#include <vector>

enum class BotMemoryType : uint8
{
    ConversationSummary = 0,
    PlayerPreference,
    PlayerStatement,
    SharedGameplay,
    RelationshipMilestone,
    PositiveInteraction,
    NegativeInteraction,
    DungeonCompletion,
    RaidCompletion,
    Resurrection,
    Wipe,
    RepeatedFailure,
    GroupHistory,
    PersonalTopic,
    PromiseOrPlan,
    Conflict,
    Reconciliation,
    CustomGmMemory
};

enum class BotMemorySource : uint8
{
    ServerGenerated = 0,
    VerifiedGameplayEvent,
    DeterministicConversationRule,
    LlmSummarizedConversation,
    RelationshipMilestone,
    GmCreated
};

struct BotMemory
{
    uint64 memoryId = 0;
    uint32 botGuid = 0;
    uint32 playerGuid = 0;
    BotMemoryType type = BotMemoryType::ConversationSummary;
    BotMemorySource source = BotMemorySource::ServerGenerated;
    std::string summary;
    std::string subjectKey;
    int16 importance = 0;
    int16 confidence = 0;
    uint16 reinforcementCount = 0;
    uint32 createdAt = 0;
    uint32 updatedAt = 0;
    uint32 lastRecalledAt = 0;
    uint32 expiresAt = 0;
    uint32 sourceEventId = 0;
    uint64 sourceReference = 0;
    bool pinned = false;
    bool negative = false;
};

struct BotMemoryCandidate
{
    uint32 botGuid = 0;
    uint32 playerGuid = 0;
    BotMemoryType suggestedType = BotMemoryType::ConversationSummary;
    BotMemorySource source = BotMemorySource::ServerGenerated;
    std::string deterministicSummary;
    std::string subjectKey;
    int32 importance = 0;
    int32 confidence = 0;
    uint32 sourceEventId = 0;
    uint64 sourceReference = 0;
    bool negative = false;
    bool pinned = false;
};

struct BotMemoryQuery
{
    uint32 botGuid = 0;
    uint32 playerGuid = 0;
    BotChatIntent intent = BotChatIntent::Unknown;
    BotProactiveDialogueEvent proactiveEvent =
        BotProactiveDialogueEvent::GroupJoined;
    bool hasProactiveEvent = false;
    bool isWhisper = false;
    bool isProactive = false;
    uint32 mapId = 0;
    uint32 instanceId = 0;
    std::string topicHint;
    uint32 maximumResults = 0;
    uint32 maximumCharacters = 0;
};

struct BotMemorySelection
{
    BotMemory memory;
    int32 score = 0;
};

struct BotMemoryTemplateContext
{
    bool hasSharedDungeonHistory = false;
    bool hasRecentResurrectionMemory = false;
    bool hasRecentConflictMemory = false;
    bool hasTrustedMilestone = false;
    bool hasLoyalMilestone = false;
};

struct BotMemoryMetrics
{
    uint64 candidatesCreated = 0;
    uint64 candidatesRejected = 0;
    uint64 deterministicStored = 0;
    uint64 summaryRequestsQueued = 0;
    uint64 summariesCompleted = 0;
    uint64 summariesRejected = 0;
    uint64 privacyRejected = 0;
    uint64 duplicatesIgnored = 0;
    uint64 memoriesReinforced = 0;
    uint64 memoriesMerged = 0;
    uint64 memoriesExpired = 0;
    uint64 memoriesEvicted = 0;
    uint64 memoriesRetrieved = 0;
};

struct BotMemoryRuntimeStats
{
    bool enabled = false;
    bool gameplayMemories = false;
    bool relationshipMilestones = false;
    bool conversationSummaries = false;
    bool llmSummarization = false;
    uint32 cachedRelationships = 0;
    uint32 cachedMemories = 0;
    uint32 activeSessions = 0;
    uint32 pendingSummaryRequests = 0;
    uint64 persistentMemories = 0;
    BotMemoryMetrics metrics;
};

struct BotMemoryExpiryStats
{
    uint32 deleted = 0;
    uint32 pinnedSkipped = 0;
    uint32 failures = 0;
};

char const* BotMemoryTypeToString(BotMemoryType type);
bool BotMemoryTypeFromString(std::string_view text, BotMemoryType& type);
char const* BotMemorySourceToString(BotMemorySource source);
bool IsValidBotMemoryType(BotMemoryType type);
bool IsValidBotMemorySource(BotMemorySource source);

#endif
