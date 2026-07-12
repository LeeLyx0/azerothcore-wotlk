#include "BotMemory.h"

#include <cctype>

namespace
{
std::string Normalize(std::string_view text)
{
    std::string normalized;
    for (unsigned char c : text)
    {
        if (c == '_' || c == '-' || std::isspace(c))
            continue;

        normalized.push_back(static_cast<char>(std::tolower(c)));
    }

    return normalized;
}
}

char const* BotMemoryTypeToString(BotMemoryType type)
{
    switch (type)
    {
        case BotMemoryType::ConversationSummary:
            return "ConversationSummary";
        case BotMemoryType::PlayerPreference:
            return "PlayerPreference";
        case BotMemoryType::PlayerStatement:
            return "PlayerStatement";
        case BotMemoryType::SharedGameplay:
            return "SharedGameplay";
        case BotMemoryType::RelationshipMilestone:
            return "RelationshipMilestone";
        case BotMemoryType::PositiveInteraction:
            return "PositiveInteraction";
        case BotMemoryType::NegativeInteraction:
            return "NegativeInteraction";
        case BotMemoryType::DungeonCompletion:
            return "DungeonCompletion";
        case BotMemoryType::RaidCompletion:
            return "RaidCompletion";
        case BotMemoryType::Resurrection:
            return "Resurrection";
        case BotMemoryType::Wipe:
            return "Wipe";
        case BotMemoryType::RepeatedFailure:
            return "RepeatedFailure";
        case BotMemoryType::GroupHistory:
            return "GroupHistory";
        case BotMemoryType::PersonalTopic:
            return "PersonalTopic";
        case BotMemoryType::PromiseOrPlan:
            return "PromiseOrPlan";
        case BotMemoryType::Conflict:
            return "Conflict";
        case BotMemoryType::Reconciliation:
            return "Reconciliation";
        case BotMemoryType::CustomGmMemory:
            return "CustomGmMemory";
    }

    return "ConversationSummary";
}

bool BotMemoryTypeFromString(std::string_view text, BotMemoryType& type)
{
    std::string const value = Normalize(text);
    for (uint8 i = 0;
        i <= static_cast<uint8>(BotMemoryType::CustomGmMemory);
        ++i)
    {
        BotMemoryType const candidate = static_cast<BotMemoryType>(i);
        if (Normalize(BotMemoryTypeToString(candidate)) == value)
        {
            type = candidate;
            return true;
        }
    }

    return false;
}

char const* BotMemorySourceToString(BotMemorySource source)
{
    switch (source)
    {
        case BotMemorySource::ServerGenerated:
            return "ServerGenerated";
        case BotMemorySource::VerifiedGameplayEvent:
            return "VerifiedGameplayEvent";
        case BotMemorySource::DeterministicConversationRule:
            return "DeterministicConversationRule";
        case BotMemorySource::LlmSummarizedConversation:
            return "LlmSummarizedConversation";
        case BotMemorySource::RelationshipMilestone:
            return "RelationshipMilestone";
        case BotMemorySource::GmCreated:
            return "GmCreated";
    }

    return "ServerGenerated";
}

bool IsValidBotMemoryType(BotMemoryType type)
{
    return static_cast<uint8>(type) <=
        static_cast<uint8>(BotMemoryType::CustomGmMemory);
}

bool IsValidBotMemorySource(BotMemorySource source)
{
    return static_cast<uint8>(source) <=
        static_cast<uint8>(BotMemorySource::GmCreated);
}
