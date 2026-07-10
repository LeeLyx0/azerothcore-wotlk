#ifndef MOD_BOT_PERSONALITY_BOT_LLM_PROMPT_BUILDER_H
#define MOD_BOT_PERSONALITY_BOT_LLM_PROMPT_BUILDER_H

#include "BotLlmTypes.h"

class BotLlmPromptBuilder
{
public:
    BotLlmPrompt Build(BotLlmRequest const& request) const;

private:
    std::string BuildSystemMessage(BotLlmRequest const& request) const;
    std::string BuildUserMessage(BotLlmRequest const& request) const;
    std::string BuildRequestBody(
        BotLlmRequest const& request,
        std::string const& systemMessage,
        std::string const& userMessage,
        std::vector<BotConversationTurn> const& history) const;
    std::vector<BotConversationTurn> TrimHistoryToBudget(
        BotLlmRequest const& request,
        std::string const& systemMessage,
        std::string const& userMessage) const;
};

std::string BotLlmJsonEscape(std::string const& text);
std::string BotLlmDescribeTrait(char const* name, int32 value);
std::string BotLlmDescribeRelationship(
    BotRelationship const& relationship,
    BotRelationshipLevel level,
    bool hasRelationship);
std::string BotLlmDescribeMood(
    BotMood const& mood,
    BotMood const& baseline,
    BotDominantMood dominantMood,
    uint8 intensity);

#endif
