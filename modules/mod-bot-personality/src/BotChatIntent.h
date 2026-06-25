#ifndef MOD_BOT_PERSONALITY_BOT_CHAT_INTENT_H
#define MOD_BOT_PERSONALITY_BOT_CHAT_INTENT_H

#include "BotPersonality.h"
#include "Define.h"

#include <string>

enum class BotChatIntent : uint8
{
    Greeting = 0,
    Farewell,
    Thanks,
    Praise,
    Apology,
    Insult,
    HelpRequest,
    IdentityQuestion,
    WellbeingQuestion,
    Agreement,
    Disagreement,
    Unknown
};

enum class BotResponseTone : uint8
{
    Warm = 0,
    Neutral,
    Cold,
    Sarcastic,
    Nervous,
    Enthusiastic,
    Arrogant,
    Stoic
};

char const* BotChatIntentToString(BotChatIntent intent);
char const* BotResponseToneToString(BotResponseTone tone);

bool BotChatIntentFromString(std::string const& text, BotChatIntent& intent);

std::string NormalizeBotChatMessage(std::string const& text);
BotChatIntent ParseBotChatIntent(std::string const& text);
BotResponseTone DetermineBotResponseTone(
    BotPersonality const& personality,
    BotChatIntent intent);

#endif
