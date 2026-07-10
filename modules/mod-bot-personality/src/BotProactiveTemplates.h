#ifndef MOD_BOT_PERSONALITY_BOT_PROACTIVE_TEMPLATES_H
#define MOD_BOT_PERSONALITY_BOT_PROACTIVE_TEMPLATES_H

#include "BotProactiveDialogueContext.h"

#include <optional>
#include <string>

BotResponseTone DetermineProactiveTone(
    BotProactiveDialogueContext const& context);

std::optional<std::string> GenerateProactiveResponse(
    BotProactiveDialogueContext const& context);

#endif
