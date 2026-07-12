#include "BotLlmMgr.h"

#include "BotConversationHistoryMgr.h"
#include "BotConversationSessionMgr.h"
#include "BotGameplayTracker.h"
#include "BotLlmHttpClient.h"
#include "BotLlmPromptBuilder.h"
#include "BotLlmResponseValidator.h"
#include "BotMoodMgr.h"
#include "BotMemoryMgr.h"
#include "BotPersonalityMgr.h"
#include "BotProactiveTemplates.h"
#include "BotRelationshipMgr.h"
#include "BotTemplateDialogueProvider.h"
#include "Chat.h"
#include "Config.h"
#include "Group.h"
#include "GroupReference.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "StringFormat.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iterator>
#include <sstream>

namespace
{
constexpr uint32 RATE_WINDOW_MS = 60000;

uint64 MakePairKey(uint32 botGuid, uint32 playerGuid)
{
    return (static_cast<uint64>(botGuid) << 32) |
        static_cast<uint64>(playerGuid);
}

bool IsLocalHost(std::string host)
{
    host = BotPersonalityToLower(host);
    return host == "localhost" ||
        host == "127.0.0.1" ||
        host == "::1" ||
        host == "[::1]";
}

std::string EndpointHost(std::string const& endpoint)
{
    std::size_t const scheme = endpoint.find("://");
    if (scheme == std::string::npos)
        return {};

    std::string rest = endpoint.substr(scheme + 3);
    std::size_t const slash = rest.find('/');
    std::string hostPort = slash == std::string::npos ?
        rest :
        rest.substr(0, slash);
    std::size_t const at = hostPort.rfind('@');
    if (at != std::string::npos)
        hostPort = hostPort.substr(at + 1);
    std::size_t const colon = hostPort.rfind(':');
    if (colon != std::string::npos)
        hostPort = hostPort.substr(0, colon);
    return hostPort;
}

bool IsValidOnlinePlayer(Player const* player)
{
    return player &&
        player->GetSession() &&
        player->IsInWorld() &&
        !player->IsDuringRemoveFromWorld() &&
        !player->IsBeingTeleported();
}

template <typename T>
void TrimDeque(std::deque<T>& values, uint32 nowMs)
{
    while (!values.empty() &&
        getMSTimeDiff(values.front(), nowMs) >= RATE_WINDOW_MS)
        values.pop_front();
}

bool DecodeJsonStringAt(
    std::string const& text,
    std::size_t quote,
    std::string& value)
{
    if (quote >= text.size() || text[quote] != '"')
        return false;

    value.clear();
    for (std::size_t i = quote + 1; i < text.size(); ++i)
    {
        char const c = text[i];
        if (c == '"')
            return true;

        if (c != '\\')
        {
            value.push_back(c);
            continue;
        }

        if (++i >= text.size())
            return false;

        switch (text[i])
        {
            case '"':
            case '\\':
            case '/':
                value.push_back(text[i]);
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                return false;
        }
    }

    return false;
}

bool FindJsonValue(
    std::string const& text,
    std::string_view field,
    std::size_t& valueOffset)
{
    std::string const key = Acore::StringFormat("\"{}\"", field);
    std::size_t const offset = text.find(key);
    if (offset == std::string::npos)
        return false;
    std::size_t const colon = text.find(':', offset + key.size());
    if (colon == std::string::npos)
        return false;
    valueOffset = text.find_first_not_of(" \t\r\n", colon + 1);
    return valueOffset != std::string::npos;
}

bool ExtractJsonStringField(
    std::string const& text,
    std::string_view field,
    std::string& value)
{
    std::size_t offset = 0;
    return FindJsonValue(text, field, offset) &&
        offset < text.size() && text[offset] == '"' &&
        DecodeJsonStringAt(text, offset, value);
}

bool ExtractJsonBoolField(
    std::string const& text,
    std::string_view field,
    bool& value)
{
    std::size_t offset = 0;
    if (!FindJsonValue(text, field, offset))
        return false;
    if (text.compare(offset, 4, "true") == 0)
    {
        value = true;
        return true;
    }
    if (text.compare(offset, 5, "false") == 0)
    {
        value = false;
        return true;
    }
    return false;
}

bool ExtractJsonIntField(
    std::string const& text,
    std::string_view field,
    int32& value)
{
    std::size_t offset = 0;
    if (!FindJsonValue(text, field, offset))
        return false;
    char const* begin = text.data() + offset;
    char const* end = text.data() + text.size();
    auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr != begin;
}

bool IsConversationMemoryType(BotMemoryType type)
{
    switch (type)
    {
        case BotMemoryType::ConversationSummary:
        case BotMemoryType::PlayerPreference:
        case BotMemoryType::PlayerStatement:
        case BotMemoryType::PositiveInteraction:
        case BotMemoryType::NegativeInteraction:
        case BotMemoryType::PersonalTopic:
        case BotMemoryType::PromiseOrPlan:
        case BotMemoryType::Conflict:
        case BotMemoryType::Reconciliation:
            return true;
        default:
            return false;
    }
}

std::string CanonicalSummarySubject(
    BotMemoryType type,
    std::string const& suggested,
    uint64 sessionId)
{
    std::string const lower = BotPersonalityToLower(suggested);
    if (type == BotMemoryType::PlayerPreference)
    {
        if (lower.find("role") != std::string::npos)
            return "player_preference:role";
        return "player_preference:general";
    }
    if (type == BotMemoryType::Conflict)
        return "conflict:recent";
    if (type == BotMemoryType::Reconciliation)
        return "reconciliation:recent";
    if (type == BotMemoryType::PromiseOrPlan)
        return "promise_or_plan:recent";
    return Acore::StringFormat(
        "conversation:{}:{}",
        static_cast<uint32>(type),
        sessionId);
}
}

BotLlmMgr::~BotLlmMgr()
{
    Shutdown();
}

void BotLlmMgr::LoadConfig(bool reload)
{
    StopWorkers();

    _config.enable = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.Enable",
        false);
    _config.debugLogging = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.DebugLogging",
        false);

    BotLlmProviderMode mode = BotLlmProviderMode::Hybrid;
    BotLlmProviderModeFromString(
        sConfigMgr->GetOption<std::string>(
            "BotPersonality.LLM.Mode",
            "Hybrid"),
        mode);
    _config.mode = mode;
    _config.enableForWhispers = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.EnableForWhispers",
        true);
    _config.enableForGroupChat = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.EnableForGroupChat",
        true);
    _config.enableForProactiveChat = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.EnableForProactiveChat",
        true);
    _config.enableForBotBanter = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.EnableForBotBanter",
        false);
    _config.suppressPlayerbotsCommands = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.SuppressPlayerbotsCommands",
        true);
    _config.fallbackToTemplates = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.FallbackToTemplates",
        true);
    _config.endpoint = sConfigMgr->GetOption<std::string>(
        "BotPersonality.LLM.Endpoint",
        "http://127.0.0.1:11434/v1/chat/completions");
    _config.allowRemoteEndpoint = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.AllowRemoteEndpoint",
        false);
    _config.allowHttpWithoutTls = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.AllowHttpWithoutTls",
        true);
    _config.model = sConfigMgr->GetOption<std::string>(
        "BotPersonality.LLM.Model",
        "local-model");
    _config.apiKeyEnvironmentVariable = sConfigMgr->GetOption<std::string>(
        "BotPersonality.LLM.ApiKeyEnvironmentVariable",
        "WOW_BOT_LLM_API_KEY");
    _config.apiKey.clear();
    if (!_config.apiKeyEnvironmentVariable.empty())
    {
        if (char const* value = std::getenv(
                _config.apiKeyEnvironmentVariable.c_str()))
            _config.apiKey = value;
    }

    _config.connectTimeoutMs = ReadUIntConfig(
        "BotPersonality.LLM.ConnectTimeoutMs",
        2000,
        100,
        60000);
    _config.requestTimeoutMs = ReadUIntConfig(
        "BotPersonality.LLM.RequestTimeoutMs",
        7000,
        100,
        120000);
    _config.reactiveDeadlineMs = ReadUIntConfig(
        "BotPersonality.LLM.ReactiveResponseDeadlineMs",
        8000,
        100,
        120000);
    _config.proactiveDeadlineMs = ReadUIntConfig(
        "BotPersonality.LLM.ProactiveResponseDeadlineMs",
        5000,
        100,
        120000);
    _config.workers = ReadUIntConfig(
        "BotPersonality.LLM.Workers",
        2,
        1,
        8);
    _config.maxInFlight = ReadUIntConfig(
        "BotPersonality.LLM.MaxInFlightRequests",
        4,
        1,
        64);
    _config.maxPendingRequests = ReadUIntConfig(
        "BotPersonality.LLM.Queue.MaxPendingRequests",
        100,
        1,
        10000);
    _config.maxPendingPerBot = ReadUIntConfig(
        "BotPersonality.LLM.Queue.MaxPendingPerBot",
        1,
        1,
        100);
    _config.maxPendingPerPlayer = ReadUIntConfig(
        "BotPersonality.LLM.Queue.MaxPendingPerPlayer",
        3,
        1,
        100);
    _config.maxCompletedResults = ReadUIntConfig(
        "BotPersonality.LLM.Queue.MaxCompletedResults",
        200,
        1,
        10000);
    _config.maxRequestsPerMinute = ReadUIntConfig(
        "BotPersonality.LLM.RateLimit.MaxRequestsPerMinute",
        30,
        1,
        10000);
    _config.maxRequestsPerPlayerPerMinute = ReadUIntConfig(
        "BotPersonality.LLM.RateLimit.MaxRequestsPerPlayerPerMinute",
        10,
        1,
        10000);
    _config.maxRequestsPerBotPerMinute = ReadUIntConfig(
        "BotPersonality.LLM.RateLimit.MaxRequestsPerBotPerMinute",
        10,
        1,
        10000);
    _config.minIntervalPerConversationMs = ReadUIntConfig(
        "BotPersonality.LLM.RateLimit.MinIntervalPerConversationMs",
        3000,
        0,
        600000);
    _config.maxPromptCharacters = ReadUIntConfig(
        "BotPersonality.LLM.MaxPromptCharacters",
        6000,
        1000,
        50000);
    _config.maxOutputCharacters = ReadUIntConfig(
        "BotPersonality.LLM.MaxOutputCharacters",
        180,
        1,
        255);
    _config.maxOutputWords = ReadUIntConfig(
        "BotPersonality.LLM.MaxOutputWords",
        30,
        1,
        100);
    _config.allowMultiline = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.AllowMultiline",
        false);
    _config.truncateLongOutput = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.TruncateLongOutput",
        false);
    _config.modelSettings.temperature = sConfigMgr->GetOption<float>(
        "BotPersonality.LLM.Temperature",
        0.8f);
    _config.modelSettings.topP = sConfigMgr->GetOption<float>(
        "BotPersonality.LLM.TopP",
        0.9f);
    _config.modelSettings.maxTokens = ReadUIntConfig(
        "BotPersonality.LLM.MaxTokens",
        80,
        1,
        512);
    _config.modelSettings.frequencyPenalty = sConfigMgr->GetOption<float>(
        "BotPersonality.LLM.FrequencyPenalty",
        0.2f);
    _config.modelSettings.presencePenalty = sConfigMgr->GetOption<float>(
        "BotPersonality.LLM.PresencePenalty",
        0.0f);
    _config.circuitFailureThreshold = ReadUIntConfig(
        "BotPersonality.LLM.CircuitBreaker.FailureThreshold",
        5,
        1,
        1000);
    _config.circuitOpenSeconds = ReadUIntConfig(
        "BotPersonality.LLM.CircuitBreaker.OpenSeconds",
        60,
        1,
        3600);
    _config.retryCount = ReadUIntConfig(
        "BotPersonality.LLM.RetryCount",
        0,
        0,
        5);
    _config.retryDelayMs = ReadUIntConfig(
        "BotPersonality.LLM.RetryDelayMs",
        250,
        0,
        10000);
    _config.logPrompts = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.LogPrompts",
        false);
    _config.logResponses = sConfigMgr->GetOption<bool>(
        "BotPersonality.LLM.LogResponses",
        false);

    _config.routeWhisper[static_cast<uint8>(BotChatIntent::Greeting)] =
        sConfigMgr->GetOption<bool>("BotPersonality.LLM.Route.Greeting", true);
    _config.routeWhisper[static_cast<uint8>(BotChatIntent::Farewell)] =
        sConfigMgr->GetOption<bool>("BotPersonality.LLM.Route.Farewell", true);
    _config.routeWhisper[static_cast<uint8>(BotChatIntent::Thanks)] =
        sConfigMgr->GetOption<bool>("BotPersonality.LLM.Route.Thanks", true);
    _config.routeWhisper[static_cast<uint8>(BotChatIntent::Praise)] =
        sConfigMgr->GetOption<bool>("BotPersonality.LLM.Route.Praise", true);
    _config.routeWhisper[static_cast<uint8>(BotChatIntent::Apology)] =
        sConfigMgr->GetOption<bool>("BotPersonality.LLM.Route.Apology", true);
    _config.routeWhisper[static_cast<uint8>(BotChatIntent::Insult)] =
        sConfigMgr->GetOption<bool>("BotPersonality.LLM.Route.Insult", true);
    _config.routeWhisper[static_cast<uint8>(BotChatIntent::HelpRequest)] =
        sConfigMgr->GetOption<bool>("BotPersonality.LLM.Route.HelpRequest", true);
    _config.routeWhisper[static_cast<uint8>(BotChatIntent::IdentityQuestion)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.IdentityQuestion",
            true);
    _config.routeWhisper[static_cast<uint8>(BotChatIntent::WellbeingQuestion)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.WellbeingQuestion",
            true);
    _config.routeWhisper[static_cast<uint8>(BotChatIntent::Agreement)] =
        sConfigMgr->GetOption<bool>("BotPersonality.LLM.Route.Agreement", true);
    _config.routeWhisper[static_cast<uint8>(BotChatIntent::Disagreement)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.Disagreement",
            true);
    _config.routeWhisper[static_cast<uint8>(BotChatIntent::Unknown)] =
        sConfigMgr->GetOption<bool>("BotPersonality.LLM.Route.Unknown", true);

    for (bool& route : _config.routeProactive)
        route = false;
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::DungeonEntered)] = true;
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::SharedBossKilled)] =
        sConfigMgr->GetOption<bool>("BotPersonality.LLM.Route.BossKill", true);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::BotResurrected)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.Resurrection",
            true);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::RepeatedPlayerDeath)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.RepeatedPlayerDeath",
            true);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::GroupWipe)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.GroupWipe",
            true);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::DungeonCompleted)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.DungeonComplete",
            true);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::RaidEncounterCompleted)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.RaidEncounterComplete",
            true);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::GroupJoined)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.GroupJoined",
            false);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::SharedEliteKilled)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.EliteKill",
            false);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::PlayerDied)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.PlayerDeath",
            false);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::BotLowHealth)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.LowHealth",
            false);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::BotCriticalHealth)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.CriticalHealth",
            false);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::BotLowMana)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.LowMana",
            false);
    _config.routeProactive[
        static_cast<uint8>(BotProactiveDialogueEvent::LongInactivity)] =
        sConfigMgr->GetOption<bool>(
            "BotPersonality.LLM.Route.Inactivity",
            false);

    sBotConversationHistoryMgr.LoadConfig(_config.debugLogging);

    if (_config.enable && !IsEndpointAllowed(_config.endpoint))
    {
        LOG_WARN(
            "module.botpersonality.llm",
            "LLM endpoint rejected by safety settings: {}",
            RedactEndpoint(_config.endpoint));
        _config.enable = false;
    }

    if (!_config.enable || _config.mode == BotLlmProviderMode::Template)
        ClearQueue();
    else
        StartWorkers();

    LOG_INFO(
        "server.loading",
        "Bot Personality Phase 6 LLM {}{}",
        _config.enable ? "enabled" : "disabled",
        reload ? " after config reload" : "");
}

bool BotLlmMgr::ShouldSuppressPlayerbotsCommands() const
{
    return _config.enable &&
        _config.suppressPlayerbotsCommands &&
        _config.mode != BotLlmProviderMode::Template;
}

void BotLlmMgr::Update(uint32 /*diff*/)
{
    if (!_config.enable)
        return;

    ProcessResults(getMSTime());
}

void BotLlmMgr::Shutdown()
{
    StopWorkers();
    ProcessResults(getMSTime());
    ClearQueue();
}

void BotLlmMgr::StartWorkers()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _stopping = false;
    if (!_workers.empty())
        return;

    for (uint32 i = 0; i < _config.workers; ++i)
        _workers.emplace_back(&BotLlmMgr::WorkerLoop, this);
}

void BotLlmMgr::StopWorkers()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
    }

    _condition.notify_all();
    for (std::thread& worker : _workers)
    {
        if (worker.joinable())
            worker.join();
    }

    _workers.clear();

    std::lock_guard<std::mutex> lock(_mutex);
    _inFlight = 0;
    _inFlightMemorySummaries = 0;
    _inFlightByBot.clear();
    _inFlightByPlayer.clear();
}

uint32 BotLlmMgr::ReadUIntConfig(
    char const* name,
    int32 defaultValue,
    int32 minValue,
    int32 maxValue) const
{
    int32 const configured = sConfigMgr->GetOption<int32>(
        name,
        defaultValue);
    int32 const clamped = std::clamp(configured, minValue, maxValue);
    if (_config.debugLogging && configured != clamped)
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

bool BotLlmMgr::IsEndpointAllowed(std::string const& endpoint) const
{
    std::string lowered = BotPersonalityToLower(endpoint);
    bool const isHttp = lowered.rfind("http://", 0) == 0;
    bool const isHttps = lowered.rfind("https://", 0) == 0;
    if (!isHttp && !isHttps)
        return false;

    if (isHttp && !_config.allowHttpWithoutTls)
        return false;

    std::string const host = EndpointHost(endpoint);
    return _config.allowRemoteEndpoint || IsLocalHost(host);
}

std::string BotLlmMgr::RedactEndpoint(std::string endpoint) const
{
    std::size_t const scheme = endpoint.find("://");
    if (scheme == std::string::npos)
        return endpoint;

    std::size_t const at = endpoint.find('@', scheme + 3);
    if (at == std::string::npos)
        return endpoint;

    return endpoint.substr(0, scheme + 3) + "<redacted>@" +
        endpoint.substr(at + 1);
}

bool BotLlmMgr::ShouldUseForDialogue(
    BotDialogueContext const& context) const
{
    if (!_config.enable ||
        _config.mode == BotLlmProviderMode::Template)
        return false;

    if (context.isWhisper && !_config.enableForWhispers)
        return false;

    if ((context.isPartyChat || context.isRaidChat) &&
        !_config.enableForGroupChat)
        return false;

    uint8 const index = static_cast<uint8>(context.intent);
    if (index >= std::size(_config.routeWhisper))
        return false;

    return _config.mode == BotLlmProviderMode::Llm ||
        _config.routeWhisper[index];
}

bool BotLlmMgr::ShouldUseForProactive(
    BotProactiveDialogueContext const& context) const
{
    if (!_config.enable ||
        !_config.enableForProactiveChat ||
        _config.mode == BotLlmProviderMode::Template)
        return false;

    if (context.isBanterReply && !_config.enableForBotBanter)
        return false;

    uint8 const index = static_cast<uint8>(context.event);
    if (index >= std::size(_config.routeProactive))
        return false;

    return _config.mode == BotLlmProviderMode::Llm ||
        _config.routeProactive[index];
}

bool BotLlmMgr::TryQueueWhisper(
    Player* bot,
    Player* player,
    BotDialogueContext const& context,
    std::string const& fallbackResponse)
{
    return TryQueueDialogue(bot, player, context, fallbackResponse);
}

bool BotLlmMgr::TryQueueDialogue(
    Player* bot,
    Player* player,
    BotDialogueContext const& context,
    std::string const& fallbackResponse)
{
    if (!ShouldUseForDialogue(context))
        return false;

    BotLlmRequest request = BuildDialogueRequest(
        bot,
        player,
        context,
        fallbackResponse,
        false,
        true);
    return Enqueue(std::move(request));
}

bool BotLlmMgr::TryQueueProactive(
    Player* bot,
    Group* group,
    BotProactiveDialogueContext const& context,
    std::string const& fallbackResponse)
{
    if (!ShouldUseForProactive(context))
        return false;

    BotLlmRequest request = BuildProactiveRequest(
        bot,
        group,
        context,
        fallbackResponse);
    return Enqueue(std::move(request));
}

bool BotLlmMgr::TryQueueMemorySummary(
    uint64 sessionId,
    uint32 botGuid,
    uint32 playerGuid,
    std::string botName,
    std::string playerName,
    std::vector<BotConversationTurn> turns)
{
    if (!_config.enable || _config.mode == BotLlmProviderMode::Template ||
        !sBotMemoryMgr.IsLlmSummarizationEnabled() || turns.empty())
        return false;

    if (GetMemorySummaryQueueDepth() >=
        sBotMemoryMgr.GetSummaryMaxPendingRequests())
        return false;

    BotLlmRequest request = BuildMemorySummaryRequest(
        sessionId,
        botGuid,
        playerGuid,
        std::move(botName),
        std::move(playerName),
        std::move(turns));
    return Enqueue(std::move(request));
}

uint32 BotLlmMgr::GetMemorySummaryQueueDepth() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    uint32 count = _inFlightMemorySummaries;
    for (BotLlmRequest const& request : _pending)
    {
        if (request.type == BotLlmRequestType::MemorySummary)
            ++count;
    }
    for (BotLlmResult const& result : _completed)
    {
        if (result.request.type == BotLlmRequestType::MemorySummary)
            ++count;
    }
    return count;
}

bool BotLlmMgr::QueueDebugWhisper(
    Player* bot,
    Player* player,
    std::string const& message,
    bool send,
    uint64& requestId)
{
    if (!_config.enable || _config.mode == BotLlmProviderMode::Template)
        return false;

    BotDialogueContext context;
    context.botGuid = bot ? bot->GetGUID().GetCounter() : 0;
    context.playerGuid = player ? player->GetGUID().GetCounter() : 0;
    context.botName = bot ? bot->GetName() : "";
    context.playerName = player ? player->GetName() : "GM";
    context.originalMessage = message;
    context.intent = ParseBotChatIntent(message);
    context.isWhisper = true;

    if (BotPersonality const* personality =
            sBotPersonalityMgr.GetOrCreatePersonality(bot))
        context.personality = *personality;
    else
        return false;

    BotRelationship relationship;
    context.hasExistingRelationship =
        player &&
        sBotRelationshipMgr.GetExistingRelationship(
            bot,
            player,
            relationship);
    if (context.hasExistingRelationship)
    {
        context.relationship = relationship;
        context.relationshipLevel = GetRelationshipLevel(
            relationship.affinity);
    }

    context.tone = DetermineBotResponseTone(
        context.personality,
        context.intent,
        context.relationship,
        context.relationshipLevel);
    context.mapId = bot ? bot->GetMapId() : 0;
    context.zoneId = bot ? bot->GetZoneId() : 0;

    BotTemplateDialogueProvider provider;
    std::string fallback = provider.GenerateResponse(context).value_or("");
    BotLlmRequest request = BuildDialogueRequest(
        bot,
        player,
        context,
        fallback,
        !send,
        send);
    request.type = BotLlmRequestType::DebugTest;
    requestId = request.requestId;
    return Enqueue(std::move(request));
}

std::optional<BotLlmPrompt> BotLlmMgr::BuildPromptPreview(
    Player* bot,
    Player* player,
    std::string const& message)
{
    if (!bot || !sBotPersonalityMgr.IsPlayerbot(bot))
        return std::nullopt;

    uint64 ignored = 0;
    BotDialogueContext context;
    context.botGuid = bot->GetGUID().GetCounter();
    context.playerGuid = player ? player->GetGUID().GetCounter() : 0;
    context.botName = bot->GetName();
    context.playerName = player ? player->GetName() : "GM";
    context.originalMessage = message;
    context.intent = ParseBotChatIntent(message);
    context.isWhisper = true;

    BotPersonality const* personality =
        sBotPersonalityMgr.GetOrCreatePersonality(bot);
    if (!personality)
        return std::nullopt;

    context.personality = *personality;
    BotRelationship relationship;
    context.hasExistingRelationship =
        player &&
        sBotRelationshipMgr.GetExistingRelationship(
            bot,
            player,
            relationship);
    if (context.hasExistingRelationship)
    {
        context.relationship = relationship;
        context.relationshipLevel = GetRelationshipLevel(
            relationship.affinity);
    }
    context.tone = DetermineBotResponseTone(
        context.personality,
        context.intent,
        context.relationship,
        context.relationshipLevel);
    BotLlmRequest request = BuildDialogueRequest(
        bot,
        player,
        context,
        "",
        true,
        false);
    request.requestId = ignored;

    return BotLlmPromptBuilder().Build(request);
}

BotLlmRequest BotLlmMgr::BuildDialogueRequest(
    Player* bot,
    Player* player,
    BotDialogueContext const& context,
    std::string const& fallbackResponse,
    bool debugOnly,
    bool send)
{
    uint32 const nowMs = getMSTime();
    BotLlmRequest request;
    request.requestId = _nextRequestId++;
    if (debugOnly)
        request.type = BotLlmRequestType::DebugTest;
    else if (context.isRaidChat)
        request.type = BotLlmRequestType::ReactiveRaid;
    else if (context.isPartyChat)
        request.type = BotLlmRequestType::ReactiveParty;
    else
        request.type = BotLlmRequestType::ReactiveWhisper;
    request.botGuid = context.botGuid;
    request.playerGuid = context.playerGuid;
    request.groupId = bot && bot->GetGroup() ?
        bot->GetGroup()->GetGUID().GetCounter() :
        0;
    request.mapId = context.mapId;
    request.zoneId = context.zoneId;
    request.instanceId = bot ? bot->GetInstanceId() : 0;
    request.botName = context.botName;
    request.playerName = context.playerName;
    request.playerMessage = context.originalMessage;
    request.fallbackResponse = fallbackResponse;
    request.dialogueContext = context;
    request.hasDialogueContext = true;
    request.debugOnly = debugOnly;
    request.sendToPlayer = send;
    request.createdAtMs = nowMs;
    request.expiresAtMs = nowMs + _config.reactiveDeadlineMs;

    if (BotMood const* mood = sBotMoodMgr.GetMood(bot))
    {
        request.mood = *mood;
        request.baselineMood = GetBaselineMood(context.personality);
        request.dominantMood = GetDominantMood(
            request.mood,
            request.baselineMood);
        request.moodIntensity = GetMoodIntensity(
            request.mood,
            request.baselineMood);
    }

    request.recentHistory = sBotConversationHistoryMgr.GetHistory(
        request.botGuid,
        request.playerGuid,
        nowMs);
    request.recentGameplay = sBotGameplayTracker.GetRecentEvents(
        request.botGuid,
        request.playerGuid,
        3);
    BotMemoryQuery memoryQuery;
    memoryQuery.botGuid = request.botGuid;
    memoryQuery.playerGuid = request.playerGuid;
    memoryQuery.intent = context.intent;
    memoryQuery.isWhisper = context.isWhisper;
    memoryQuery.mapId = request.mapId;
    memoryQuery.instanceId = request.instanceId;
    for (BotMemorySelection const& selection :
        sBotMemoryMgr.Retrieve(memoryQuery))
    {
        request.relevantMemories.push_back(
            {
                selection.memory.memoryId,
                selection.memory.summary,
                static_cast<uint8>(selection.memory.confidence)
            });
    }
    request.endpoint = _config.endpoint;
    request.model = _config.model;
    request.apiKey = _config.apiKey;
    request.modelSettings = _config.modelSettings;
    request.connectTimeoutMs = _config.connectTimeoutMs;
    request.requestTimeoutMs = _config.requestTimeoutMs;
    request.maxPromptCharacters = _config.maxPromptCharacters;
    request.maxOutputCharacters = _config.maxOutputCharacters;
    request.maxOutputWords = _config.maxOutputWords;
    request.allowMultiline = _config.allowMultiline;
    request.truncateLongOutput = _config.truncateLongOutput;
    return request;
}

BotLlmRequest BotLlmMgr::BuildProactiveRequest(
    Player* bot,
    Group* group,
    BotProactiveDialogueContext const& context,
    std::string const& fallbackResponse)
{
    uint32 const nowMs = getMSTime();
    BotLlmRequest request;
    request.requestId = _nextRequestId++;
    request.type = group && group->isRaidGroup() ?
        BotLlmRequestType::ProactiveRaid :
        BotLlmRequestType::ProactiveParty;
    if (!group)
        request.type = BotLlmRequestType::ProactiveSay;
    request.botGuid = context.botGuid;
    request.playerGuid = context.relatedPlayerGuid;
    request.groupId = context.groupId;
    request.mapId = context.mapId;
    request.zoneId = context.zoneId;
    request.instanceId = context.instanceId;
    request.botName = context.botName;
    request.playerName = context.playerName.empty() ?
        "the group" :
        context.playerName;
    request.fallbackResponse = fallbackResponse;
    request.proactiveContext = context;
    request.hasProactiveContext = true;
    request.mood = context.mood;
    request.baselineMood = context.baselineMood;
    request.dominantMood = context.dominantMood;
    request.moodIntensity = context.moodIntensity;
    request.createdAtMs = nowMs;
    request.expiresAtMs = nowMs + _config.proactiveDeadlineMs;
    request.endpoint = _config.endpoint;
    request.model = _config.model;
    request.apiKey = _config.apiKey;
    request.modelSettings = _config.modelSettings;
    request.connectTimeoutMs = _config.connectTimeoutMs;
    request.requestTimeoutMs = _config.requestTimeoutMs;
    request.maxPromptCharacters = _config.maxPromptCharacters;
    request.maxOutputCharacters = _config.maxOutputCharacters;
    request.maxOutputWords = _config.maxOutputWords;
    request.allowMultiline = _config.allowMultiline;
    request.truncateLongOutput = _config.truncateLongOutput;
    if (request.playerGuid)
    {
        request.recentHistory = sBotConversationHistoryMgr.GetHistory(
            request.botGuid,
            request.playerGuid,
            nowMs);
        BotMemoryQuery memoryQuery;
        memoryQuery.botGuid = request.botGuid;
        memoryQuery.playerGuid = request.playerGuid;
        memoryQuery.isProactive = true;
        memoryQuery.hasProactiveEvent = true;
        memoryQuery.proactiveEvent = context.event;
        memoryQuery.mapId = request.mapId;
        memoryQuery.instanceId = request.instanceId;
        for (BotMemorySelection const& selection :
            sBotMemoryMgr.Retrieve(memoryQuery))
        {
            request.relevantMemories.push_back(
                {
                    selection.memory.memoryId,
                    selection.memory.summary,
                    static_cast<uint8>(selection.memory.confidence)
                });
        }
    }

    return request;
}

BotLlmRequest BotLlmMgr::BuildMemorySummaryRequest(
    uint64 sessionId,
    uint32 botGuid,
    uint32 playerGuid,
    std::string botName,
    std::string playerName,
    std::vector<BotConversationTurn> turns)
{
    uint32 const maximum =
        sBotMemoryMgr.GetSummaryMaxTranscriptCharacters();
    auto characters = [&turns]()
    {
        uint32 total = 0;
        for (BotConversationTurn const& turn : turns)
            total += static_cast<uint32>(turn.text.size());
        return total;
    };
    while (!turns.empty() && characters() > maximum)
        turns.erase(turns.begin());

    uint32 const nowMs = getMSTime();
    BotLlmRequest request;
    request.requestId = _nextRequestId++;
    request.type = BotLlmRequestType::MemorySummary;
    request.memorySessionId = sessionId;
    request.botGuid = botGuid;
    request.playerGuid = playerGuid;
    request.botName = std::move(botName);
    request.playerName = std::move(playerName);
    request.recentHistory = std::move(turns);
    request.endpoint = _config.endpoint;
    request.model = _config.model;
    request.apiKey = _config.apiKey;
    request.modelSettings = _config.modelSettings;
    request.modelSettings.temperature = std::min(
        request.modelSettings.temperature,
        0.3f);
    request.modelSettings.maxTokens = std::max<uint32>(
        request.modelSettings.maxTokens,
        160);
    request.connectTimeoutMs = _config.connectTimeoutMs;
    request.requestTimeoutMs = sBotMemoryMgr.GetSummaryRequestTimeoutMs();
    request.maxPromptCharacters = std::max<uint32>(
        _config.maxPromptCharacters,
        maximum + 2000);
    request.maxOutputCharacters = 1024;
    request.maxOutputWords = 150;
    request.createdAtMs = nowMs;
    request.expiresAtMs = nowMs +
        sBotMemoryMgr.GetSummaryMaxQueueAgeMs();
    request.sendToPlayer = false;
    return request;
}

bool BotLlmMgr::Enqueue(BotLlmRequest request)
{
    uint32 const nowMs = getMSTime();
    ++_metrics.requestsAttempted;

    if (IsCircuitOpen(nowMs))
    {
        ++_metrics.connectionFailures;
        return false;
    }

    if (request.type != BotLlmRequestType::MemorySummary &&
        IsRateLimited(request, nowMs))
    {
        ++_metrics.rejectedByRateLimit;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        uint32 pendingForBot = _inFlightByBot[request.botGuid];
        uint32 pendingForPlayer = request.playerGuid ?
            _inFlightByPlayer[request.playerGuid] :
            0;
        for (BotLlmRequest const& pending : _pending)
        {
            if (request.type != BotLlmRequestType::MemorySummary &&
                pending.type == BotLlmRequestType::MemorySummary)
                continue;
            if (pending.botGuid == request.botGuid)
                ++pendingForBot;
            if (request.playerGuid && pending.playerGuid == request.playerGuid)
                ++pendingForPlayer;
        }

        if (_pending.size() >= _config.maxPendingRequests ||
            _inFlight >= _config.maxInFlight ||
            pendingForBot >= _config.maxPendingPerBot ||
            (request.playerGuid &&
                pendingForPlayer >= _config.maxPendingPerPlayer))
        {
            ++_metrics.rejectedByQueue;
            return false;
        }

        if (request.type == BotLlmRequestType::MemorySummary)
            _pending.push_back(request);
        else
        {
            auto firstSummary = std::find_if(
                _pending.begin(),
                _pending.end(),
                [](BotLlmRequest const& pending)
                {
                    return pending.type == BotLlmRequestType::MemorySummary;
                });
            _pending.insert(firstSummary, request);
        }
        ++_metrics.requestsQueued;
    }

    if (request.type != BotLlmRequestType::MemorySummary)
        RecordRate(request, nowMs);
    if (request.hasDialogueContext && !request.playerMessage.empty())
    {
        sBotConversationHistoryMgr.AddTurn(
            request.botGuid,
            request.playerGuid,
            BotConversationSpeaker::Player,
            request.playerMessage,
            nowMs);
    }

    _condition.notify_one();
    return true;
}

void BotLlmMgr::WorkerLoop()
{
    while (true)
    {
        BotLlmRequest request;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _condition.wait(lock, [this]
            {
                return _stopping || !_pending.empty();
            });

            if (_stopping && _pending.empty())
                return;

            request = _pending.front();
            _pending.pop_front();
            ++_inFlight;
            if (request.type == BotLlmRequestType::MemorySummary)
                ++_inFlightMemorySummaries;
            if (request.type != BotLlmRequestType::MemorySummary)
            {
                ++_inFlightByBot[request.botGuid];
                if (request.playerGuid)
                    ++_inFlightByPlayer[request.playerGuid];
            }
        }

        BotLlmResult result = ExecuteRequest(request);

        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_inFlight)
                --_inFlight;
            if (request.type == BotLlmRequestType::MemorySummary &&
                _inFlightMemorySummaries)
                --_inFlightMemorySummaries;
            if (request.type != BotLlmRequestType::MemorySummary)
            {
                if (_inFlightByBot[request.botGuid])
                    --_inFlightByBot[request.botGuid];
                if (request.playerGuid &&
                    _inFlightByPlayer[request.playerGuid])
                    --_inFlightByPlayer[request.playerGuid];
            }

            _completed.push_back(std::move(result));
            while (_completed.size() > _config.maxCompletedResults)
                _completed.pop_front();
        }
    }
}

BotLlmResult BotLlmMgr::ExecuteRequest(BotLlmRequest const& request)
{
    BotLlmResult result;
    result.request = request;
    BotLlmPrompt prompt = BotLlmPromptBuilder().Build(request);

    if (_config.logPrompts && _config.debugLogging)
    {
        LOG_DEBUG(
            "module.botpersonality.llm",
            "LLM prompt request {} has {} chars and {} history turns",
            request.requestId,
            prompt.promptCharacters,
            prompt.historyTurnsIncluded);
    }

    BotLlmHttpClient client;
    BotLlmHttpResult httpResult =
        client.PostChatCompletion(request, prompt.requestBody);
    result.statusCode = httpResult.statusCode;
    result.latencyMs = httpResult.latencyMs;
    result.completedAtMs = getMSTime();

    if (!httpResult.success)
    {
        result.error = httpResult.error.empty() ?
            "http request failed" :
            httpResult.error;
        result.timedOut = result.latencyMs >= request.requestTimeoutMs;
        return result;
    }

    std::string content;
    if (!ExtractContent(httpResult.body, content))
    {
        result.error = "invalid json response";
        return result;
    }

    if (request.type == BotLlmRequestType::MemorySummary)
    {
        if (content.empty() || content.size() > 2048)
        {
            result.error = "invalid memory summary json size";
            return result;
        }
        result.success = true;
        result.response = std::move(content);
        return result;
    }

    BotLlmValidationResult validation =
        BotLlmResponseValidator().Validate(
            content,
            request.botName,
            request.maxOutputCharacters,
            request.maxOutputWords,
            request.allowMultiline,
            request.truncateLongOutput);
    if (!validation.success)
    {
        result.error = validation.error;
        return result;
    }

    result.success = true;
    result.response = std::move(validation.text);
    return result;
}

bool BotLlmMgr::ExtractContent(
    std::string const& body,
    std::string& content) const
{
    std::size_t offset = 0;
    while (true)
    {
        std::size_t const key = body.find("\"content\"", offset);
        if (key == std::string::npos)
            return false;

        std::size_t colon = body.find(':', key + 9);
        if (colon == std::string::npos)
            return false;

        std::size_t quote = body.find('"', colon + 1);
        if (quote == std::string::npos)
            return false;

        std::string value;
        if (DecodeJsonStringAt(body, quote, value) && !value.empty())
        {
            content = std::move(value);
            return true;
        }

        offset = quote + 1;
    }
}

void BotLlmMgr::ProcessResults(uint32 nowMs)
{
    std::deque<BotLlmResult> results;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        results.swap(_completed);
    }

    for (BotLlmResult& result : results)
        ProcessResult(result, nowMs);
}

void BotLlmMgr::ProcessResult(BotLlmResult& result, uint32 nowMs)
{
    if (getMSTimeDiff(result.request.createdAtMs, nowMs) >
        getMSTimeDiff(result.request.createdAtMs, result.request.expiresAtMs))
    {
        result.stale = true;
        ++_metrics.rejectedAsStale;
        NoteFailure(nowMs);
        return;
    }

    if (result.request.type == BotLlmRequestType::MemorySummary)
    {
        ProcessMemorySummaryResult(result, nowMs);
        return;
    }

    if (result.success)
    {
        bool sent = false;
        if (result.request.hasDialogueContext)
            sent = SendDialogueResult(result, result.response);
        else if (result.request.hasProactiveContext)
            sent = SendProactiveResult(result, result.response);

        if (sent || result.request.debugOnly)
        {
            ++_metrics.completedSuccessfully;
            NoteSuccess(result.latencyMs, nowMs);
            if (result.request.playerGuid &&
                result.request.sendToPlayer &&
                !result.request.debugOnly)
            {
                sBotConversationHistoryMgr.AddTurn(
                    result.request.botGuid,
                    result.request.playerGuid,
                    BotConversationSpeaker::Bot,
                    result.response,
                    nowMs);
                sBotConversationSessionMgr.AddBotTurn(
                    result.request.botGuid,
                    result.request.playerGuid,
                    result.response,
                    nowMs);
                std::vector<uint64> recalled;
                for (BotMemoryPromptEntry const& memory :
                    result.request.relevantMemories)
                    recalled.push_back(memory.memoryId);
                sBotMemoryMgr.MarkRecalled(recalled);
            }
            return;
        }

        ++_metrics.silentFailures;
        NoteFailure(nowMs);
        return;
    }

    if (result.timedOut)
        ++_metrics.timedOut;
    else if (result.statusCode >= 400)
        ++_metrics.httpFailures;
    else if (result.statusCode == 0)
        ++_metrics.connectionFailures;
    else if (result.error.find("json") != std::string::npos)
        ++_metrics.jsonFailures;
    else if (!result.error.empty())
        ++_metrics.rejectedByValidator;
    else
        ++_metrics.connectionFailures;

    if (SendFallback(result))
    {
        ++_metrics.templateFallbacks;
        if (result.request.playerGuid)
        {
            sBotConversationSessionMgr.AddBotTurn(
                result.request.botGuid,
                result.request.playerGuid,
                result.request.fallbackResponse,
                nowMs);
        }
        NoteFailure(nowMs);
        return;
    }

    ++_metrics.silentFailures;
    NoteFailure(nowMs);
}

void BotLlmMgr::ProcessMemorySummaryResult(
    BotLlmResult& result,
    uint32 nowMs)
{
    if (!result.success)
    {
        sBotMemoryMgr.NoteSummaryRejected();
        NoteFailure(nowMs);
        return;
    }

    bool shouldStore = false;
    if (!ExtractJsonBoolField(
            result.response, "should_store", shouldStore))
    {
        sBotMemoryMgr.NoteSummaryRejected();
        NoteFailure(nowMs);
        return;
    }
    if (!shouldStore)
    {
        sBotMemoryMgr.NoteSummaryCompleted();
        NoteSuccess(result.latencyMs, nowMs);
        return;
    }

    std::string typeText;
    std::string summary;
    std::string subjectKey;
    int32 importance = 0;
    int32 confidence = 0;
    bool negative = false;
    if (!ExtractJsonStringField(result.response, "memory_type", typeText) ||
        !ExtractJsonStringField(result.response, "summary", summary) ||
        !ExtractJsonStringField(
            result.response, "subject_key", subjectKey) ||
        !ExtractJsonIntField(result.response, "importance", importance) ||
        !ExtractJsonIntField(result.response, "confidence", confidence) ||
        !ExtractJsonBoolField(result.response, "negative", negative))
    {
        sBotMemoryMgr.NoteSummaryRejected();
        NoteFailure(nowMs);
        return;
    }

    BotMemoryType type;
    if (!BotMemoryTypeFromString(typeText, type) ||
        !IsConversationMemoryType(type))
    {
        sBotMemoryMgr.NoteSummaryRejected();
        NoteFailure(nowMs);
        return;
    }

    BotMemoryCandidate candidate;
    candidate.botGuid = result.request.botGuid;
    candidate.playerGuid = result.request.playerGuid;
    candidate.suggestedType = type;
    candidate.source = BotMemorySource::LlmSummarizedConversation;
    candidate.deterministicSummary = std::move(summary);
    candidate.subjectKey = CanonicalSummarySubject(
        type,
        subjectKey,
        result.request.memorySessionId);
    candidate.importance = std::clamp(importance, 0, 70);
    candidate.confidence = std::clamp(confidence, 0, 80);
    candidate.sourceReference = result.request.memorySessionId;
    candidate.negative = negative;
    if (!sBotMemoryMgr.StoreCandidate(std::move(candidate)))
    {
        sBotMemoryMgr.NoteSummaryRejected();
        NoteFailure(nowMs);
        return;
    }

    sBotMemoryMgr.NoteSummaryCompleted();
    NoteSuccess(result.latencyMs, nowMs);
}

bool BotLlmMgr::SendDialogueResult(
    BotLlmResult& result,
    std::string response)
{
    if (result.request.debugOnly && !result.request.sendToPlayer)
        return true;

    Player* bot = ObjectAccessor::FindConnectedPlayer(
        ObjectGuid::Create<HighGuid::Player>(result.request.botGuid));
    Player* player = ObjectAccessor::FindConnectedPlayer(
        ObjectGuid::Create<HighGuid::Player>(result.request.playerGuid));
    if (!IsValidOnlinePlayer(bot) ||
        !IsValidOnlinePlayer(player) ||
        !sBotPersonalityMgr.IsPlayerbot(bot) ||
        sBotPersonalityMgr.IsPlayerbot(player))
        return false;

    if (result.request.dialogueContext.isWhisper)
    {
        bot->Whisper(response, LANG_UNIVERSAL, player);
        return true;
    }

    Group* group = bot->GetGroup();
    if (!group || group != player->GetGroup())
        return false;

    if (result.request.groupId &&
        group->GetGUID().GetCounter() != result.request.groupId)
        return false;

    ChatMsg const chatType = result.request.dialogueContext.isRaidChat ?
        CHAT_MSG_RAID :
        CHAT_MSG_PARTY;
    WorldPacket data;
    ChatHandler::BuildChatPacket(
        data,
        chatType,
        response,
        LANG_UNIVERSAL,
        CHAT_TAG_NONE,
        bot->GetGUID(),
        bot->GetName());

    bool sent = false;
    uint8 const targetSubgroup = group->GetMemberGroup(player->GetGUID());
    for (GroupReference* itr = group->GetFirstMember();
        itr != nullptr;
        itr = itr->next())
    {
        Player* receiver = itr->GetSource();
        if (!IsValidOnlinePlayer(receiver) ||
            sBotPersonalityMgr.IsPlayerbot(receiver))
            continue;

        if (result.request.dialogueContext.isPartyChat &&
            group->isRaidGroup() &&
            group->GetMemberGroup(receiver->GetGUID()) != targetSubgroup)
            continue;

        receiver->GetSession()->SendPacket(&data);
        sent = true;
    }

    return sent;
}

bool BotLlmMgr::SendProactiveResult(
    BotLlmResult& result,
    std::string response)
{
    Player* bot = ObjectAccessor::FindConnectedPlayer(
        ObjectGuid::Create<HighGuid::Player>(result.request.botGuid));
    if (!IsValidOnlinePlayer(bot) || !sBotPersonalityMgr.IsPlayerbot(bot))
        return false;

    Group* group = bot->GetGroup();
    if (result.request.groupId)
    {
        if (!group || group->GetGUID().GetCounter() != result.request.groupId)
            return false;
    }

    if (group)
    {
        ChatMsg const chatType = group->isRaidGroup() ?
            CHAT_MSG_RAID :
            CHAT_MSG_PARTY;
        WorldPacket data;
        ChatHandler::BuildChatPacket(
            data,
            chatType,
            response,
            LANG_UNIVERSAL,
            CHAT_TAG_NONE,
            bot->GetGUID(),
            bot->GetName());

        bool sent = false;
        for (GroupReference* itr = group->GetFirstMember();
            itr != nullptr;
            itr = itr->next())
        {
            Player* receiver = itr->GetSource();
            if (!IsValidOnlinePlayer(receiver) ||
                sBotPersonalityMgr.IsPlayerbot(receiver))
                continue;

            receiver->GetSession()->SendPacket(&data);
            sent = true;
        }

        return sent;
    }

    bot->Say(response, LANG_UNIVERSAL);
    return true;
}

bool BotLlmMgr::SendFallback(BotLlmResult& result)
{
    if (!_config.fallbackToTemplates || result.request.fallbackResponse.empty())
        return false;

    result.fallbackUsed = true;
    if (result.request.hasDialogueContext)
        return SendDialogueResult(result, result.request.fallbackResponse);

    if (result.request.hasProactiveContext)
        return SendProactiveResult(result, result.request.fallbackResponse);

    return false;
}

bool BotLlmMgr::IsCircuitOpen(uint32 nowMs) const
{
    return _circuitState == BotLlmCircuitState::Open &&
        nowMs < _circuitOpenedUntilMs;
}

void BotLlmMgr::NoteSuccess(uint32 latencyMs, uint32 nowMs)
{
    _lastSuccessMs = nowMs;
    _consecutiveFailures = 0;
    _circuitState = BotLlmCircuitState::Closed;
    _metrics.totalLatencyMs += latencyMs;
    _metrics.maxLatencyMs = std::max(_metrics.maxLatencyMs, latencyMs);
}

void BotLlmMgr::NoteFailure(uint32 nowMs)
{
    _lastFailureMs = nowMs;
    ++_consecutiveFailures;
    if (_consecutiveFailures >= _config.circuitFailureThreshold)
    {
        _circuitState = BotLlmCircuitState::Open;
        _circuitOpenedUntilMs = nowMs +
            _config.circuitOpenSeconds * IN_MILLISECONDS;
    }
}

bool BotLlmMgr::IsRateLimited(
    BotLlmRequest const& request,
    uint32 nowMs)
{
    CleanupRateWindows(nowMs);
    if (_globalRate.size() >= _config.maxRequestsPerMinute)
        return true;

    if (request.playerGuid &&
        _playerRate[request.playerGuid].size() >=
            _config.maxRequestsPerPlayerPerMinute)
        return true;

    if (_botRate[request.botGuid].size() >=
        _config.maxRequestsPerBotPerMinute)
        return true;

    uint64 const pairKey = MakePairKey(request.botGuid, request.playerGuid);
    auto recent = _conversationLastRequest.find(pairKey);
    if (_config.minIntervalPerConversationMs > 0 &&
        recent != _conversationLastRequest.end() &&
        getMSTimeDiff(recent->second, nowMs) <
            _config.minIntervalPerConversationMs)
        return true;

    return false;
}

void BotLlmMgr::RecordRate(BotLlmRequest const& request, uint32 nowMs)
{
    _globalRate.push_back(nowMs);
    if (request.playerGuid)
        _playerRate[request.playerGuid].push_back(nowMs);
    _botRate[request.botGuid].push_back(nowMs);
    _conversationLastRequest[MakePairKey(
        request.botGuid,
        request.playerGuid)] = nowMs;
}

void BotLlmMgr::CleanupRateWindows(uint32 nowMs)
{
    TrimDeque(_globalRate, nowMs);
    for (auto itr = _playerRate.begin(); itr != _playerRate.end();)
    {
        TrimDeque(itr->second, nowMs);
        if (itr->second.empty())
            itr = _playerRate.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _botRate.begin(); itr != _botRate.end();)
    {
        TrimDeque(itr->second, nowMs);
        if (itr->second.empty())
            itr = _botRate.erase(itr);
        else
            ++itr;
    }

    for (auto itr = _conversationLastRequest.begin();
        itr != _conversationLastRequest.end();)
    {
        if (getMSTimeDiff(itr->second, nowMs) > RATE_WINDOW_MS)
            itr = _conversationLastRequest.erase(itr);
        else
            ++itr;
    }
}

BotLlmRuntimeStats BotLlmMgr::GetStats() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    BotLlmRuntimeStats stats;
    stats.enabled = _config.enable;
    stats.mode = _config.mode;
    stats.circuitState = _circuitState;
    stats.apiKeyConfigured = !_config.apiKey.empty();
    stats.fallbackToTemplates = _config.fallbackToTemplates;
    stats.enableForWhispers = _config.enableForWhispers;
    stats.enableForGroupChat = _config.enableForGroupChat;
    stats.enableForProactiveChat = _config.enableForProactiveChat;
    stats.suppressPlayerbotsCommands = ShouldSuppressPlayerbotsCommands();
    stats.endpoint = RedactEndpoint(_config.endpoint);
    stats.model = _config.model;
    stats.workers = static_cast<uint32>(_workers.size());
    stats.maxInFlight = _config.maxInFlight;
    stats.pendingRequests = static_cast<uint32>(_pending.size());
    stats.inFlightRequests = _inFlight;
    stats.completedResults = static_cast<uint32>(_completed.size());
    stats.historyConversations = static_cast<uint32>(
        sBotConversationHistoryMgr.GetConversationCount());
    stats.consecutiveFailures = _consecutiveFailures;
    stats.lastSuccessMs = _lastSuccessMs;
    stats.lastFailureMs = _lastFailureMs;
    stats.circuitOpenedUntilMs = _circuitOpenedUntilMs;
    stats.metrics = _metrics;
    return stats;
}

std::vector<BotLlmQueueEntry> BotLlmMgr::GetQueue(uint32 limit) const
{
    std::vector<BotLlmQueueEntry> entries;
    uint32 const nowMs = getMSTime();
    std::lock_guard<std::mutex> lock(_mutex);
    for (BotLlmRequest const& request : _pending)
    {
        if (entries.size() >= limit)
            break;

        BotLlmQueueEntry entry;
        entry.requestId = request.requestId;
        entry.type = request.type;
        entry.botGuid = request.botGuid;
        entry.playerGuid = request.playerGuid;
        entry.groupId = request.groupId;
        entry.ageMs = getMSTimeDiff(request.createdAtMs, nowMs);
        entry.deadlineRemainingMs = nowMs >= request.expiresAtMs ?
            0 :
            getMSTimeDiff(nowMs, request.expiresAtMs);
        entries.push_back(entry);
    }

    return entries;
}

void BotLlmMgr::ClearQueue()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _pending.clear();
    _completed.clear();
}

void BotLlmMgr::ResetMetrics()
{
    _metrics = {};
}

void BotLlmMgr::ResetCircuit()
{
    _circuitState = BotLlmCircuitState::Closed;
    _consecutiveFailures = 0;
    _circuitOpenedUntilMs = 0;
}

void BotLlmMgr::ClearHistory()
{
    sBotConversationHistoryMgr.Clear();
}

void BotLlmMgr::ClearHistory(uint32 botGuid)
{
    sBotConversationHistoryMgr.ClearBot(botGuid);
}

void BotLlmMgr::ClearHistory(uint32 botGuid, uint32 playerGuid)
{
    sBotConversationHistoryMgr.ClearConversation(botGuid, playerGuid);
}
