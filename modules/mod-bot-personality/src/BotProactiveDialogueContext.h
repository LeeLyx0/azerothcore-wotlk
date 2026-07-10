#ifndef MOD_BOT_PERSONALITY_BOT_PROACTIVE_DIALOGUE_CONTEXT_H
#define MOD_BOT_PERSONALITY_BOT_PROACTIVE_DIALOGUE_CONTEXT_H

#include "BotChatIntent.h"
#include "BotGameplayTracker.h"
#include "BotMood.h"
#include "BotPersonality.h"
#include "BotProactiveDialogueEvent.h"
#include "BotRelationship.h"
#include "Define.h"

#include <string>
#include <vector>

struct BotProactiveDialogueContext
{
    BotProactiveDialogueEvent event =
        BotProactiveDialogueEvent::GroupJoined;
    BotDialoguePriority priority = BotDialoguePriority::Low;

    uint32 botGuid = 0;
    uint32 relatedPlayerGuid = 0;
    uint32 groupId = 0;

    std::string botName;
    std::string playerName;
    std::string previousSpeakerName;

    BotResponseTone tone = BotResponseTone::Neutral;
    BotPersonality personality;
    BotMood mood;
    BotMood baselineMood;
    BotDominantMood dominantMood = BotDominantMood::Calm;
    uint8 moodIntensity = 0;

    BotRelationship relationship;
    BotRelationshipLevel relationshipLevel = BotRelationshipLevel::Neutral;
    bool hasExistingRelationship = false;

    std::vector<BotGameplayRecentEvent> recentGameplay;

    uint32 mapId = 0;
    uint32 zoneId = 0;
    uint32 instanceId = 0;
    uint32 sourceEntry = 0;
    uint32 eventValue = 0;

    bool inDungeon = false;
    bool inRaid = false;
    bool inCombat = false;
    bool isBanterReply = false;

    uint32 eventTime = 0;
    uint32 selectionSeed = 0;
    std::string previousResponse;
};

#endif
