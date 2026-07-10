#ifndef MOD_BOT_PERSONALITY_BOT_MOOD_H
#define MOD_BOT_PERSONALITY_BOT_MOOD_H

#include "BotPersonality.h"
#include "Define.h"

#include <string_view>

struct BotMood
{
    int16 happiness = 0;
    int16 frustration = 0;
    int16 moodConfidence = 0;
    int16 fear = 0;
    int16 excitement = 0;
    int16 boredom = 0;

    uint32 lastUpdatedTime = 0;
    uint32 lastDecayTime = 0;
};

struct BotMoodDelta
{
    int32 happiness = 0;
    int32 frustration = 0;
    int32 moodConfidence = 0;
    int32 fear = 0;
    int32 excitement = 0;
    int32 boredom = 0;
};

enum class BotMoodEvent : uint8
{
    PlayerGreeting = 0,
    PlayerThanksBot,
    PlayerPraisesBot,
    PlayerInsultsBot,
    PlayerApologises,

    SharedNormalKill,
    SharedEliteKill,
    SharedBossKill,

    BotWasHealed,
    BotWasSaved,
    BotWasResurrected,

    PlayerDied,
    BotDied,
    RepeatedPlayerDeath,
    GroupWipe,

    DungeonCompleted,
    RaidEncounterCompleted,

    DangerousPull,
    PlayerAbandonedCombat,

    EnteredDungeon,
    JoinedGroup,
    LeftGroup,

    LowHealth,
    CriticalHealth,
    LowMana,

    LongInactivity,
    RepetitiveCombat,
    SustainedTeamwork
};

enum class BotDominantMood : uint8
{
    Calm = 0,
    Happy,
    Frustrated,
    Confident,
    Afraid,
    Excited,
    Bored
};

char const* BotMoodEventToString(BotMoodEvent event);
bool BotMoodEventFromString(std::string_view text, BotMoodEvent& event);

char const* BotDominantMoodToString(BotDominantMood mood);

BotMood GetBaselineMood(BotPersonality const& personality);
BotMoodDelta GetBaseMoodDelta(BotMoodEvent event);
BotMoodDelta CalculateMoodDelta(
    BotPersonality const& personality,
    BotMoodEvent event,
    BotMoodDelta const& baseDelta);
BotDominantMood GetDominantMood(BotMood const& mood, BotMood const& baseline);
uint8 GetMoodIntensity(BotMood const& mood, BotMood const& baseline);

#endif
