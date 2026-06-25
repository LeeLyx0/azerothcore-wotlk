#ifndef MOD_BOT_PERSONALITY_BOT_DIALOGUE_CONTEXT_H
#define MOD_BOT_PERSONALITY_BOT_DIALOGUE_CONTEXT_H

#include "BotChatIntent.h"
#include "BotPersonality.h"
#include "Define.h"

#include <string>

struct BotDialogueContext
{
    uint32 botGuid = 0;
    uint32 playerGuid = 0;

    std::string botName;
    std::string playerName;
    std::string originalMessage;

    BotChatIntent intent = BotChatIntent::Unknown;
    BotResponseTone tone = BotResponseTone::Neutral;

    BotPersonality personality;

    bool isWhisper = false;
    bool isPartyChat = false;
    bool isRaidChat = false;
    bool isSay = false;

    bool botInCombat = false;
    bool playerInCombat = false;
    bool inGroup = false;
    bool inDungeon = false;
    bool inRaid = false;

    uint32 mapId = 0;
    uint32 zoneId = 0;

    uint32 selectionSeed = 0;
    std::string previousResponse;
};

#endif
