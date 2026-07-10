#ifndef MOD_BOT_PERSONALITY_BOT_LLM_RESPONSE_VALIDATOR_H
#define MOD_BOT_PERSONALITY_BOT_LLM_RESPONSE_VALIDATOR_H

#include "Define.h"

#include <string>

struct BotLlmValidationResult
{
    bool success = false;
    std::string text;
    std::string error;
};

class BotLlmResponseValidator
{
public:
    BotLlmValidationResult Validate(
        std::string text,
        std::string const& botName,
        uint32 maxCharacters,
        uint32 maxWords,
        bool allowMultiline,
        bool truncateLongOutput) const;
};

#endif
