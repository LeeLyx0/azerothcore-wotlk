#ifndef MOD_BOT_PERSONALITY_BOT_LLM_MGR_H
#define MOD_BOT_PERSONALITY_BOT_LLM_MGR_H

#include "BotDialogueContext.h"
#include "BotLlmTypes.h"
#include "BotProactiveDialogueContext.h"
#include "Define.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class Group;
class Player;

class BotLlmMgr
{
public:
    static BotLlmMgr& instance()
    {
        static BotLlmMgr instance;
        return instance;
    }

    void LoadConfig(bool reload);
    void Update(uint32 diff);
    void Shutdown();

    bool IsEnabled() const { return _config.enable; }
    bool IsDebugLoggingEnabled() const { return _config.debugLogging; }

    bool TryQueueWhisper(
        Player* bot,
        Player* player,
        BotDialogueContext const& context,
        std::string const& fallbackResponse);
    bool TryQueueDialogue(
        Player* bot,
        Player* player,
        BotDialogueContext const& context,
        std::string const& fallbackResponse);
    bool TryQueueProactive(
        Player* bot,
        Group* group,
        BotProactiveDialogueContext const& context,
        std::string const& fallbackResponse);
    bool QueueDebugWhisper(
        Player* bot,
        Player* player,
        std::string const& message,
        bool send,
        uint64& requestId);

    std::optional<BotLlmPrompt> BuildPromptPreview(
        Player* bot,
        Player* player,
        std::string const& message);

    BotLlmRuntimeStats GetStats() const;
    std::vector<BotLlmQueueEntry> GetQueue(uint32 limit) const;
    void ClearQueue();
    void ResetMetrics();
    void ResetCircuit();
    void ClearHistory();
    void ClearHistory(uint32 botGuid);
    void ClearHistory(uint32 botGuid, uint32 playerGuid);

private:
    struct Config
    {
        bool enable = false;
        bool debugLogging = false;
        BotLlmProviderMode mode = BotLlmProviderMode::Hybrid;
        bool enableForWhispers = true;
        bool enableForGroupChat = true;
        bool enableForProactiveChat = true;
        bool enableForBotBanter = false;
        bool fallbackToTemplates = true;

        std::string endpoint =
            "http://127.0.0.1:11434/v1/chat/completions";
        bool allowRemoteEndpoint = false;
        bool allowHttpWithoutTls = true;
        std::string model = "local-model";
        std::string apiKeyEnvironmentVariable = "WOW_BOT_LLM_API_KEY";
        std::string apiKey;

        uint32 connectTimeoutMs = 2000;
        uint32 requestTimeoutMs = 7000;
        uint32 reactiveDeadlineMs = 8000;
        uint32 proactiveDeadlineMs = 5000;
        uint32 workers = 2;
        uint32 maxInFlight = 4;
        uint32 maxPendingRequests = 100;
        uint32 maxPendingPerBot = 1;
        uint32 maxPendingPerPlayer = 3;
        uint32 maxCompletedResults = 200;

        uint32 maxRequestsPerMinute = 30;
        uint32 maxRequestsPerPlayerPerMinute = 10;
        uint32 maxRequestsPerBotPerMinute = 10;
        uint32 minIntervalPerConversationMs = 3000;

        uint32 maxPromptCharacters = 6000;
        uint32 maxOutputCharacters = 180;
        uint32 maxOutputWords = 30;
        bool allowMultiline = false;
        bool truncateLongOutput = false;

        BotLlmModelSettings modelSettings;
        uint32 circuitFailureThreshold = 5;
        uint32 circuitOpenSeconds = 60;
        uint32 retryCount = 0;
        uint32 retryDelayMs = 250;
        bool logPrompts = false;
        bool logResponses = false;

        bool routeWhisper[12] = {};
        bool routeProactive[32] = {};
    };

    BotLlmMgr() = default;
    ~BotLlmMgr();

    BotLlmMgr(BotLlmMgr const&) = delete;
    BotLlmMgr& operator=(BotLlmMgr const&) = delete;

    void StartWorkers();
    void StopWorkers();
    void WorkerLoop();

    bool ShouldUseForDialogue(BotDialogueContext const& context) const;
    bool ShouldUseForProactive(
        BotProactiveDialogueContext const& context) const;
    bool Enqueue(BotLlmRequest request);
    BotLlmRequest BuildDialogueRequest(
        Player* bot,
        Player* player,
        BotDialogueContext const& context,
        std::string const& fallbackResponse,
        bool debugOnly,
        bool send);
    BotLlmRequest BuildProactiveRequest(
        Player* bot,
        Group* group,
        BotProactiveDialogueContext const& context,
        std::string const& fallbackResponse);

    BotLlmResult ExecuteRequest(BotLlmRequest const& request);
    bool ExtractContent(std::string const& body, std::string& content) const;
    void ProcessResults(uint32 nowMs);
    void ProcessResult(BotLlmResult& result, uint32 nowMs);
    bool SendDialogueResult(BotLlmResult& result, std::string response);
    bool SendProactiveResult(BotLlmResult& result, std::string response);
    bool SendFallback(BotLlmResult& result);

    bool IsCircuitOpen(uint32 nowMs) const;
    void NoteSuccess(uint32 latencyMs, uint32 nowMs);
    void NoteFailure(uint32 nowMs);
    bool IsRateLimited(BotLlmRequest const& request, uint32 nowMs);
    void RecordRate(BotLlmRequest const& request, uint32 nowMs);
    void CleanupRateWindows(uint32 nowMs);

    uint32 ReadUIntConfig(
        char const* name,
        int32 defaultValue,
        int32 minValue,
        int32 maxValue) const;
    bool IsEndpointAllowed(std::string const& endpoint) const;
    std::string RedactEndpoint(std::string endpoint) const;

    mutable std::mutex _mutex;
    std::condition_variable _condition;
    std::deque<BotLlmRequest> _pending;
    std::deque<BotLlmResult> _completed;
    std::vector<std::thread> _workers;
    std::unordered_map<uint32, uint32> _inFlightByBot;
    std::unordered_map<uint32, uint32> _inFlightByPlayer;

    Config _config;
    BotLlmMetrics _metrics;
    BotLlmCircuitState _circuitState = BotLlmCircuitState::Closed;
    uint32 _circuitOpenedUntilMs = 0;
    uint32 _consecutiveFailures = 0;
    uint32 _lastSuccessMs = 0;
    uint32 _lastFailureMs = 0;
    uint32 _lastCleanupMs = 0;
    uint64 _nextRequestId = 1;
    uint32 _inFlight = 0;
    bool _stopping = false;

    std::deque<uint32> _globalRate;
    std::unordered_map<uint32, std::deque<uint32>> _playerRate;
    std::unordered_map<uint32, std::deque<uint32>> _botRate;
    std::unordered_map<uint64, uint32> _conversationLastRequest;
};

#define sBotLlmMgr BotLlmMgr::instance()

#endif
