#ifndef MOD_BOT_PERSONALITY_BOT_PERSONALITY_H
#define MOD_BOT_PERSONALITY_BOT_PERSONALITY_H

#include "Define.h"

#include <random>
#include <string>

class Player;

enum class BotPersonalityArchetype : uint8
{
    Friendly = 0,
    Stoic,
    Sarcastic,
    Nervous,
    Competitive,
    Helpful,
    Arrogant,
    Reckless,
    Veteran,
    NewAdventurer,
    Max
};

struct BotPersonality
{
    uint32 botGuid = 0;

    BotPersonalityArchetype archetype =
        BotPersonalityArchetype::Friendly;

    int8 friendliness = 0;
    int8 confidence = 0;
    int8 patience = 0;
    int8 humour = 0;
    int8 competitiveness = 0;
    int8 greed = 0;
    int8 bravery = 0;
    int8 talkativeness = 0;

    uint8 speechStyle = 0;
};

char const* BotPersonalityArchetypeToString(BotPersonalityArchetype archetype);
std::string BotPersonalityToLower(std::string text);
bool BotPersonalityArchetypeFromString(
    std::string const& archetypeName,
    BotPersonalityArchetype& archetype);

BotPersonality GeneratePersonality(Player const* bot);
BotPersonality GeneratePersonality(Player const* bot, int32 variation);
BotPersonality GeneratePersonality(
    Player const* bot,
    BotPersonalityArchetype forcedArchetype);
BotPersonality GeneratePersonality(
    Player const* bot,
    BotPersonalityArchetype forcedArchetype,
    int32 variation);
void ApplyArchetypeDefaults(BotPersonality& personality);
void ApplyClassModifiers(uint8 playerClass, BotPersonality& personality);
void ApplyRaceModifiers(uint8 race, BotPersonality& personality);
void AddIndividualVariation(
    std::mt19937& random,
    BotPersonality& personality,
    int32 variation);
void AddIndividualVariation(BotPersonality& personality, int32 variation);
void ClampPersonality(BotPersonality& personality);

#endif
