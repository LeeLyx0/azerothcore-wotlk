#include "BotPersonality.h"

#include "Player.h"
#include "SharedDefines.h"
#include "Util.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <optional>
#include <random>

namespace
{
constexpr int32 BOT_PERSONALITY_MIN_TRAIT = -100;
constexpr int32 BOT_PERSONALITY_MAX_TRAIT = 100;
constexpr uint8 BOT_PERSONALITY_MAX_SPEECH_STYLE = 10;

struct BotPersonalityDefaults
{
    BotPersonalityArchetype archetype;
    int32 friendliness;
    int32 confidence;
    int32 patience;
    int32 humour;
    int32 competitiveness;
    int32 greed;
    int32 bravery;
    int32 talkativeness;
};

constexpr std::array<BotPersonalityDefaults, 10> ArchetypeDefaults =
{{
    { BotPersonalityArchetype::Friendly, 65, 20, 50, 25, 0, -20, 10, 40 },
    { BotPersonalityArchetype::Stoic, 5, 45, 55, -30, 15, 0, 45, -55 },
    { BotPersonalityArchetype::Sarcastic, 5, 55, -15, 75, 25, 5, 20, 35 },
    { BotPersonalityArchetype::Nervous, 25, -55, 20, -5, -30, -5, -60, 5 },
    { BotPersonalityArchetype::Competitive, 0, 55, -20, 10, 80, 20, 40, 20 },
    { BotPersonalityArchetype::Helpful, 55, 10, 70, 15, -20, -55, 15, 30 },
    { BotPersonalityArchetype::Arrogant, -25, 85, -35, 10, 55, 20, 35, 25 },
    { BotPersonalityArchetype::Reckless, 10, 65, -50, 30, 45, 5, 90, 25 },
    { BotPersonalityArchetype::Veteran, 5, 70, 45, -5, 25, -5, 60, -20 },
    { BotPersonalityArchetype::NewAdventurer, 35, -15, 25, 20, 10, 5, -5, 35 }
}};

int8 ClampTrait(int32 value)
{
    int32 const clamped = std::clamp(
        value,
        BOT_PERSONALITY_MIN_TRAIT,
        BOT_PERSONALITY_MAX_TRAIT);
    return static_cast<int8>(clamped);
}

uint8 ClampSpeechStyle(int32 value)
{
    return static_cast<uint8>(
        std::clamp<int32>(value, 0, BOT_PERSONALITY_MAX_SPEECH_STYLE));
}

void SetTraits(
    BotPersonality& personality,
    BotPersonalityDefaults const& defaults)
{
    personality.friendliness = ClampTrait(defaults.friendliness);
    personality.confidence = ClampTrait(defaults.confidence);
    personality.patience = ClampTrait(defaults.patience);
    personality.humour = ClampTrait(defaults.humour);
    personality.competitiveness = ClampTrait(defaults.competitiveness);
    personality.greed = ClampTrait(defaults.greed);
    personality.bravery = ClampTrait(defaults.bravery);
    personality.talkativeness = ClampTrait(defaults.talkativeness);
}

void AddTrait(int8& trait, int32 delta)
{
    trait = ClampTrait(static_cast<int32>(trait) + delta);
}

void AddTraitVariation(int8& trait, std::mt19937& random, int32 variation)
{
    if (variation <= 0)
        return;

    std::uniform_int_distribution<int32> distribution(-variation, variation);
    AddTrait(trait, distribution(random));
}

bool TryParseArchetypeNumber(
    std::string const& archetypeName,
    BotPersonalityArchetype& archetype)
{
    uint32 value = 0;
    auto const* begin = archetypeName.data();
    auto const* end = begin + archetypeName.size();
    auto result = std::from_chars(begin, end, value);

    if (result.ec != std::errc() || result.ptr != end)
        return false;

    if (value >= static_cast<uint32>(BotPersonalityArchetype::Max))
        return false;

    archetype = static_cast<BotPersonalityArchetype>(value);
    return true;
}

BotPersonality GeneratePersonality(
    Player const* bot,
    std::optional<BotPersonalityArchetype> forcedArchetype,
    int32 variation)
{
    BotPersonality personality;
    if (!bot)
        return personality;

    personality.botGuid = bot->GetGUID().GetCounter();

    std::mt19937 random(personality.botGuid);
    if (forcedArchetype)
        personality.archetype = *forcedArchetype;
    else
    {
        std::uniform_int_distribution<uint32> archetypeDistribution(
            0,
            static_cast<uint32>(BotPersonalityArchetype::Max) - 1);
        personality.archetype = static_cast<BotPersonalityArchetype>(
            archetypeDistribution(random));
    }

    ApplyArchetypeDefaults(personality);
    ApplyClassModifiers(bot->getClass(), personality);
    ApplyRaceModifiers(bot->getRace(), personality);

    std::uniform_int_distribution<int32> speechStyleDistribution(0, 3);
    personality.speechStyle = ClampSpeechStyle(speechStyleDistribution(random));

    AddIndividualVariation(random, personality, variation);
    ClampPersonality(personality);

    return personality;
}
}

char const* BotPersonalityArchetypeToString(BotPersonalityArchetype archetype)
{
    switch (archetype)
    {
        case BotPersonalityArchetype::Friendly:
            return "Friendly";
        case BotPersonalityArchetype::Stoic:
            return "Stoic";
        case BotPersonalityArchetype::Sarcastic:
            return "Sarcastic";
        case BotPersonalityArchetype::Nervous:
            return "Nervous";
        case BotPersonalityArchetype::Competitive:
            return "Competitive";
        case BotPersonalityArchetype::Helpful:
            return "Helpful";
        case BotPersonalityArchetype::Arrogant:
            return "Arrogant";
        case BotPersonalityArchetype::Reckless:
            return "Reckless";
        case BotPersonalityArchetype::Veteran:
            return "Veteran";
        case BotPersonalityArchetype::NewAdventurer:
            return "NewAdventurer";
        case BotPersonalityArchetype::Max:
            break;
    }

    return "Unknown";
}

std::string BotPersonalityToLower(std::string text)
{
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    return text;
}

bool BotPersonalityArchetypeFromString(
    std::string const& archetypeName,
    BotPersonalityArchetype& archetype)
{
    if (archetypeName.empty())
        return false;

    if (TryParseArchetypeNumber(archetypeName, archetype))
        return true;

    std::string lower = BotPersonalityToLower(archetypeName);

    if (lower == "friendly")
        archetype = BotPersonalityArchetype::Friendly;
    else if (lower == "stoic")
        archetype = BotPersonalityArchetype::Stoic;
    else if (lower == "sarcastic")
        archetype = BotPersonalityArchetype::Sarcastic;
    else if (lower == "nervous")
        archetype = BotPersonalityArchetype::Nervous;
    else if (lower == "competitive")
        archetype = BotPersonalityArchetype::Competitive;
    else if (lower == "helpful")
        archetype = BotPersonalityArchetype::Helpful;
    else if (lower == "arrogant")
        archetype = BotPersonalityArchetype::Arrogant;
    else if (lower == "reckless")
        archetype = BotPersonalityArchetype::Reckless;
    else if (lower == "veteran")
        archetype = BotPersonalityArchetype::Veteran;
    else if (lower == "newadventurer" || lower == "new_adventurer" ||
             lower == "new-adventurer")
        archetype = BotPersonalityArchetype::NewAdventurer;
    else
        return false;

    return true;
}

BotPersonality GeneratePersonality(Player const* bot)
{
    return GeneratePersonality(bot, 15);
}

BotPersonality GeneratePersonality(Player const* bot, int32 variation)
{
    return GeneratePersonality(bot, std::nullopt, variation);
}

BotPersonality GeneratePersonality(
    Player const* bot,
    BotPersonalityArchetype forcedArchetype)
{
    return GeneratePersonality(bot, forcedArchetype, 15);
}

BotPersonality GeneratePersonality(
    Player const* bot,
    BotPersonalityArchetype forcedArchetype,
    int32 variation)
{
    return GeneratePersonality(
        bot,
        std::optional<BotPersonalityArchetype>(forcedArchetype),
        variation);
}

void ApplyArchetypeDefaults(BotPersonality& personality)
{
    for (BotPersonalityDefaults const& defaults : ArchetypeDefaults)
    {
        if (defaults.archetype == personality.archetype)
        {
            SetTraits(personality, defaults);
            return;
        }
    }

    personality.archetype = BotPersonalityArchetype::Friendly;
    SetTraits(personality, ArchetypeDefaults.front());
}

void ApplyClassModifiers(uint8 playerClass, BotPersonality& personality)
{
    switch (playerClass)
    {
        case CLASS_WARRIOR:
            AddTrait(personality.confidence, 5);
            AddTrait(personality.bravery, 8);
            AddTrait(personality.patience, -3);
            AddTrait(personality.competitiveness, 5);
            break;
        case CLASS_PALADIN:
            AddTrait(personality.friendliness, 8);
            AddTrait(personality.patience, 5);
            AddTrait(personality.greed, -5);
            AddTrait(personality.bravery, 5);
            break;
        case CLASS_HUNTER:
            AddTrait(personality.confidence, 3);
            AddTrait(personality.patience, 5);
            AddTrait(personality.talkativeness, -5);
            AddTrait(personality.bravery, 3);
            break;
        case CLASS_ROGUE:
            AddTrait(personality.greed, 8);
            AddTrait(personality.humour, 5);
            AddTrait(personality.patience, -5);
            AddTrait(personality.friendliness, -3);
            break;
        case CLASS_PRIEST:
            AddTrait(personality.friendliness, 6);
            AddTrait(personality.patience, 8);
            AddTrait(personality.bravery, -3);
            AddTrait(personality.greed, -5);
            break;
        case CLASS_DEATH_KNIGHT:
            AddTrait(personality.confidence, 8);
            AddTrait(personality.humour, -5);
            AddTrait(personality.bravery, 8);
            AddTrait(personality.friendliness, -5);
            break;
        case CLASS_SHAMAN:
            AddTrait(personality.patience, 5);
            AddTrait(personality.bravery, 4);
            AddTrait(personality.talkativeness, 3);
            break;
        case CLASS_MAGE:
            AddTrait(personality.confidence, 6);
            AddTrait(personality.patience, -3);
            AddTrait(personality.humour, 4);
            AddTrait(personality.bravery, -4);
            break;
        case CLASS_WARLOCK:
            AddTrait(personality.confidence, 7);
            AddTrait(personality.friendliness, -6);
            AddTrait(personality.humour, 5);
            AddTrait(personality.greed, 4);
            break;
        case CLASS_DRUID:
            AddTrait(personality.friendliness, 5);
            AddTrait(personality.patience, 6);
            AddTrait(personality.greed, -6);
            break;
        default:
            break;
    }
}

void ApplyRaceModifiers(uint8 race, BotPersonality& personality)
{
    switch (race)
    {
        case RACE_HUMAN:
            AddTrait(personality.friendliness, 5);
            AddTrait(personality.confidence, 3);
            break;
        case RACE_ORC:
            AddTrait(personality.bravery, 6);
            AddTrait(personality.patience, -4);
            AddTrait(personality.competitiveness, 4);
            break;
        case RACE_DWARF:
            AddTrait(personality.patience, 5);
            AddTrait(personality.bravery, 5);
            AddTrait(personality.humour, 3);
            break;
        case RACE_NIGHTELF:
            AddTrait(personality.patience, 6);
            AddTrait(personality.talkativeness, -5);
            break;
        case RACE_UNDEAD_PLAYER:
            AddTrait(personality.humour, 6);
            AddTrait(personality.friendliness, -8);
            AddTrait(personality.bravery, 4);
            break;
        case RACE_TAUREN:
            AddTrait(personality.patience, 8);
            AddTrait(personality.friendliness, 4);
            AddTrait(personality.bravery, 3);
            break;
        case RACE_GNOME:
            AddTrait(personality.humour, 8);
            AddTrait(personality.confidence, 2);
            AddTrait(personality.talkativeness, 4);
            break;
        case RACE_TROLL:
            AddTrait(personality.humour, 5);
            AddTrait(personality.patience, -3);
            AddTrait(personality.bravery, 3);
            break;
        case RACE_BLOODELF:
            AddTrait(personality.confidence, 5);
            AddTrait(personality.friendliness, -3);
            AddTrait(personality.talkativeness, 3);
            break;
        case RACE_DRAENEI:
            AddTrait(personality.friendliness, 5);
            AddTrait(personality.patience, 5);
            AddTrait(personality.bravery, 2);
            break;
        default:
            break;
    }
}

void AddIndividualVariation(
    std::mt19937& random,
    BotPersonality& personality,
    int32 variation)
{
    int32 const safeVariation = std::clamp<int32>(variation, 0, 40);

    AddTraitVariation(personality.friendliness, random, safeVariation);
    AddTraitVariation(personality.confidence, random, safeVariation);
    AddTraitVariation(personality.patience, random, safeVariation);
    AddTraitVariation(personality.humour, random, safeVariation);
    AddTraitVariation(personality.competitiveness, random, safeVariation);
    AddTraitVariation(personality.greed, random, safeVariation);
    AddTraitVariation(personality.bravery, random, safeVariation);
    AddTraitVariation(personality.talkativeness, random, safeVariation);
}

void AddIndividualVariation(BotPersonality& personality, int32 variation)
{
    std::mt19937 random(personality.botGuid);
    AddIndividualVariation(random, personality, variation);
}

void ClampPersonality(BotPersonality& personality)
{
    personality.friendliness = ClampTrait(personality.friendliness);
    personality.confidence = ClampTrait(personality.confidence);
    personality.patience = ClampTrait(personality.patience);
    personality.humour = ClampTrait(personality.humour);
    personality.competitiveness = ClampTrait(personality.competitiveness);
    personality.greed = ClampTrait(personality.greed);
    personality.bravery = ClampTrait(personality.bravery);
    personality.talkativeness = ClampTrait(personality.talkativeness);
    personality.speechStyle = ClampSpeechStyle(personality.speechStyle);

    if (personality.archetype >= BotPersonalityArchetype::Max)
        personality.archetype = BotPersonalityArchetype::Friendly;
}
