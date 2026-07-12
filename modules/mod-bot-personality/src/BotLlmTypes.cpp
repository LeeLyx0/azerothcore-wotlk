#include "BotLlmTypes.h"

#include "BotPersonality.h"

char const* BotLlmRequestTypeToString(BotLlmRequestType type)
{
    switch (type)
    {
        case BotLlmRequestType::ReactiveWhisper:
            return "ReactiveWhisper";
        case BotLlmRequestType::ReactiveParty:
            return "ReactiveParty";
        case BotLlmRequestType::ReactiveRaid:
            return "ReactiveRaid";
        case BotLlmRequestType::ProactiveParty:
            return "ProactiveParty";
        case BotLlmRequestType::ProactiveRaid:
            return "ProactiveRaid";
        case BotLlmRequestType::ProactiveSay:
            return "ProactiveSay";
        case BotLlmRequestType::DebugTest:
            return "DebugTest";
        case BotLlmRequestType::MemorySummary:
            return "MemorySummary";
    }

    return "ReactiveWhisper";
}

char const* BotLlmProviderModeToString(BotLlmProviderMode mode)
{
    switch (mode)
    {
        case BotLlmProviderMode::Template:
            return "Template";
        case BotLlmProviderMode::Llm:
            return "Llm";
        case BotLlmProviderMode::Hybrid:
            return "Hybrid";
    }

    return "Hybrid";
}

char const* BotLlmCircuitStateToString(BotLlmCircuitState state)
{
    switch (state)
    {
        case BotLlmCircuitState::Closed:
            return "Closed";
        case BotLlmCircuitState::Open:
            return "Open";
    }

    return "Closed";
}

bool BotLlmProviderModeFromString(
    std::string const& text,
    BotLlmProviderMode& mode)
{
    std::string lowered = BotPersonalityToLower(text);
    if (lowered == "template" || lowered == "templates")
    {
        mode = BotLlmProviderMode::Template;
        return true;
    }

    if (lowered == "llm" || lowered == "openai")
    {
        mode = BotLlmProviderMode::Llm;
        return true;
    }

    if (lowered == "hybrid")
    {
        mode = BotLlmProviderMode::Hybrid;
        return true;
    }

    return false;
}
