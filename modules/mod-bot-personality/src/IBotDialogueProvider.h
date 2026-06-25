#ifndef MOD_BOT_PERSONALITY_I_BOT_DIALOGUE_PROVIDER_H
#define MOD_BOT_PERSONALITY_I_BOT_DIALOGUE_PROVIDER_H

#include "BotDialogueContext.h"

#include <optional>
#include <string>

class IBotDialogueProvider
{
public:
    virtual ~IBotDialogueProvider() = default;

    virtual std::optional<std::string> GenerateResponse(
        BotDialogueContext const& context) = 0;
};

#endif
