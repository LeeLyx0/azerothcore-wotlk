#ifndef MOD_BOT_PERSONALITY_BOT_PROACTIVE_DIALOGUE_EVENT_H
#define MOD_BOT_PERSONALITY_BOT_PROACTIVE_DIALOGUE_EVENT_H

#include "Define.h"

#include <string_view>

enum class BotProactiveDialogueEvent : uint8
{
    GroupJoined = 0,
    DungeonEntered,

    SharedEliteKilled,
    SharedBossKilled,

    BotHealed,
    BotSaved,
    BotResurrected,

    PlayerDied,
    RepeatedPlayerDeath,
    BotDied,
    GroupWipe,

    DungeonCompleted,
    RaidEncounterCompleted,

    DangerousPull,
    PlayerAbandonedCombat,

    BotLowHealth,
    BotCriticalHealth,
    BotLowMana,

    LongInactivity,
    SustainedTeamwork,

    RelationshipImproved,
    RelationshipWorsened,

    BotReplyToBot
};

enum class BotDialoguePriority : uint8
{
    Low = 0,
    Normal,
    High,
    Critical
};

char const* BotProactiveDialogueEventToString(
    BotProactiveDialogueEvent event);
bool BotProactiveDialogueEventFromString(
    std::string_view text,
    BotProactiveDialogueEvent& event);
char const* BotDialoguePriorityToString(BotDialoguePriority priority);
BotDialoguePriority GetDialoguePriority(BotProactiveDialogueEvent event);

#endif
