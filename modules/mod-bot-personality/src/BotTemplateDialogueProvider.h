#ifndef MOD_BOT_PERSONALITY_BOT_TEMPLATE_DIALOGUE_PROVIDER_H
#define MOD_BOT_PERSONALITY_BOT_TEMPLATE_DIALOGUE_PROVIDER_H

#include "IBotDialogueProvider.h"

class BotTemplateDialogueProvider final : public IBotDialogueProvider
{
public:
    std::optional<std::string> GenerateResponse(
        BotDialogueContext const& context) override;
};

#endif
