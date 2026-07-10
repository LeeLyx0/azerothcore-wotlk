#ifndef MOD_BOT_PERSONALITY_BOT_LLM_TYPES_H
#define MOD_BOT_PERSONALITY_BOT_LLM_TYPES_H

#include "BotDialogueContext.h"
#include "BotMood.h"
#include "BotProactiveDialogueContext.h"
#include "Define.h"

#include <deque>
#include <string>
#include <vector>

enum class BotLlmRequestType : uint8
{
    ReactiveWhisper = 0,
    ReactiveParty,
    ReactiveRaid,
    ProactiveParty,
    ProactiveRaid,
    ProactiveSay,
    DebugTest
};

enum class BotLlmProviderMode : uint8
{
    Template = 0,
    Llm,
    Hybrid
};

enum class BotConversationSpeaker : uint8
{
    Player = 0,
    Bot
};

enum class BotLlmCircuitState : uint8
{
    Closed = 0,
    Open
};

struct BotConversationTurn
{
    BotConversationSpeaker speaker = BotConversationSpeaker::Player;
    std::string text;
    uint32 timestampMs = 0;
};

struct BotLlmModelSettings
{
    float temperature = 0.8f;
    float topP = 0.9f;
    float frequencyPenalty = 0.2f;
    float presencePenalty = 0.0f;
    uint32 maxTokens = 80;
};

struct BotLlmRequest
{
    uint64 requestId = 0;
    BotLlmRequestType type = BotLlmRequestType::ReactiveWhisper;

    uint32 botGuid = 0;
    uint32 playerGuid = 0;
    uint32 groupId = 0;
    uint32 mapId = 0;
    uint32 zoneId = 0;
    uint32 instanceId = 0;

    std::string botName;
    std::string playerName;
    std::string playerMessage;
    std::string fallbackResponse;

    BotDialogueContext dialogueContext;
    BotProactiveDialogueContext proactiveContext;
    bool hasDialogueContext = false;
    bool hasProactiveContext = false;

    BotMood mood;
    BotMood baselineMood;
    BotDominantMood dominantMood = BotDominantMood::Calm;
    uint8 moodIntensity = 0;

    std::vector<BotConversationTurn> recentHistory;

    std::string endpoint;
    std::string model;
    std::string apiKey;
    BotLlmModelSettings modelSettings;

    uint32 connectTimeoutMs = 2000;
    uint32 requestTimeoutMs = 7000;
    uint32 maxPromptCharacters = 6000;
    uint32 maxOutputCharacters = 180;
    uint32 maxOutputWords = 30;

    bool allowMultiline = false;
    bool truncateLongOutput = false;
    bool sendToPlayer = true;
    bool debugOnly = false;

    uint32 createdAtMs = 0;
    uint32 expiresAtMs = 0;
};

struct BotLlmPrompt
{
    std::string systemMessage;
    std::string userMessage;
    std::string requestBody;
    uint32 promptCharacters = 0;
    uint32 historyTurnsIncluded = 0;
};

struct BotLlmHttpResult
{
    bool success = false;
    int32 statusCode = 0;
    std::string body;
    std::string error;
    uint32 latencyMs = 0;
};

struct BotLlmResult
{
    BotLlmRequest request;
    bool success = false;
    bool fallbackUsed = false;
    bool timedOut = false;
    bool stale = false;
    int32 statusCode = 0;
    std::string response;
    std::string error;
    uint32 latencyMs = 0;
    uint32 completedAtMs = 0;
};

struct BotLlmMetrics
{
    uint64 requestsAttempted = 0;
    uint64 requestsQueued = 0;
    uint64 rejectedByQueue = 0;
    uint64 rejectedByRateLimit = 0;
    uint64 completedSuccessfully = 0;
    uint64 timedOut = 0;
    uint64 connectionFailures = 0;
    uint64 httpFailures = 0;
    uint64 jsonFailures = 0;
    uint64 rejectedByValidator = 0;
    uint64 rejectedAsStale = 0;
    uint64 templateFallbacks = 0;
    uint64 silentFailures = 0;
    uint64 totalLatencyMs = 0;
    uint32 maxLatencyMs = 0;
};

struct BotLlmRuntimeStats
{
    bool enabled = false;
    BotLlmProviderMode mode = BotLlmProviderMode::Hybrid;
    BotLlmCircuitState circuitState = BotLlmCircuitState::Closed;
    bool apiKeyConfigured = false;
    bool fallbackToTemplates = true;
    bool enableForWhispers = true;
    bool enableForGroupChat = true;
    bool enableForProactiveChat = true;
    std::string endpoint;
    std::string model;
    uint32 workers = 0;
    uint32 maxInFlight = 0;
    uint32 pendingRequests = 0;
    uint32 inFlightRequests = 0;
    uint32 completedResults = 0;
    uint32 historyConversations = 0;
    uint32 consecutiveFailures = 0;
    uint32 lastSuccessMs = 0;
    uint32 lastFailureMs = 0;
    uint32 circuitOpenedUntilMs = 0;
    BotLlmMetrics metrics;
};

struct BotLlmQueueEntry
{
    uint64 requestId = 0;
    BotLlmRequestType type = BotLlmRequestType::ReactiveWhisper;
    uint32 botGuid = 0;
    uint32 playerGuid = 0;
    uint32 groupId = 0;
    uint32 ageMs = 0;
    uint32 deadlineRemainingMs = 0;
};

char const* BotLlmRequestTypeToString(BotLlmRequestType type);
char const* BotLlmProviderModeToString(BotLlmProviderMode mode);
char const* BotLlmCircuitStateToString(BotLlmCircuitState state);
bool BotLlmProviderModeFromString(
    std::string const& text,
    BotLlmProviderMode& mode);

#endif
