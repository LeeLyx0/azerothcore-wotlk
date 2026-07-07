#ifndef MOD_BOT_PERSONALITY_BOT_GAMEPLAY_EVENT_H
#define MOD_BOT_PERSONALITY_BOT_GAMEPLAY_EVENT_H

#include "BotPersonality.h"
#include "BotRelationship.h"
#include "Define.h"

#include <string_view>

enum class BotGameplayEvent : uint8
{
    SharedNormalKill = 0,
    SharedEliteKill,
    SharedBossKill,
    PlayerHealedBot,
    PlayerResurrectedBot,
    BotHealedPlayer,
    BotResurrectedPlayer,
    PlayerDied,
    BotDied,
    SharedDeath,
    GroupWipe,
    PlayerLeftGroupDuringCombat,
    DungeonCompleted,
    RaidEncounterCompleted,
    SustainedTeamwork,
    RepeatedPlayerDeath
};

char const* BotGameplayEventToString(BotGameplayEvent event);
bool BotGameplayEventFromString(
    std::string_view text,
    BotGameplayEvent& event);

bool IsPositiveGameplayEvent(BotGameplayEvent event);
BotRelationshipDelta CalculateGameplayRelationshipDelta(
    BotPersonality const& personality,
    BotGameplayEvent event,
    BotRelationshipDelta baseDelta);

#endif
