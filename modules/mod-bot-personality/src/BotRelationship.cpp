#include "BotRelationship.h"

#include "BotPersonality.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace
{
int32 Trait(BotPersonality const& personality, int8 BotPersonality::*trait)
{
    return static_cast<int32>(personality.*trait);
}

std::string ToLowerAscii(std::string_view text)
{
    std::string lowered(text);
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    return lowered;
}

int32 ScaleValue(int32 value, int32 percent)
{
    return value * percent / 100;
}

void ClampToDoubleMagnitude(
    BotRelationshipDelta const& base,
    BotRelationshipDelta& delta)
{
    auto clampField = [](int32 baseValue, int32 value)
    {
        int32 const limit = std::max(2, std::abs(baseValue) * 2);
        return std::clamp(value, -limit, limit);
    };

    delta.affinity = clampField(base.affinity, delta.affinity);
    delta.trust = clampField(base.trust, delta.trust);
    delta.respect = clampField(base.respect, delta.respect);
    delta.familiarity = clampField(base.familiarity, delta.familiarity);
}

bool IsArrogant(BotPersonality const& personality)
{
    return personality.archetype == BotPersonalityArchetype::Arrogant ||
        (Trait(personality, &BotPersonality::confidence) >= 55 &&
            Trait(personality, &BotPersonality::friendliness) <= -15);
}

bool IsNervous(BotPersonality const& personality)
{
    return personality.archetype == BotPersonalityArchetype::Nervous ||
        Trait(personality, &BotPersonality::confidence) <= -35;
}

int32 PositiveTraitBonus(int32 value, int32 trait)
{
    if (value <= 0 || trait <= 0)
        return 0;

    return ScaleValue(value, std::min(trait, 100));
}

int32 NegativeTraitAdjustment(int32 value, int32 trait)
{
    if (value >= 0)
        return value;

    if (trait >= 0)
        return value + ScaleValue(-value, std::min(trait, 100) / 2);

    return value - ScaleValue(-value, std::min(-trait, 100) / 2);
}
}

std::size_t BotPlayerRelationshipKeyHash::operator()(
    BotPlayerRelationshipKey const& key) const
{
    std::size_t hash = static_cast<std::size_t>(key.botGuid);
    hash ^= static_cast<std::size_t>(key.playerGuid) + 0x9e3779b9u +
        (hash << 6) + (hash >> 2);
    return hash;
}

char const* BotRelationshipEventToString(BotRelationshipEvent event)
{
    switch (event)
    {
        case BotRelationshipEvent::Greeting:
            return "Greeting";
        case BotRelationshipEvent::Thanks:
            return "Thanks";
        case BotRelationshipEvent::Praise:
            return "Praise";
        case BotRelationshipEvent::Apology:
            return "Apology";
        case BotRelationshipEvent::Insult:
            return "Insult";
        case BotRelationshipEvent::HelpRequest:
            return "HelpRequest";
        case BotRelationshipEvent::Conversation:
            return "Conversation";
        case BotRelationshipEvent::RepeatedSpam:
            return "RepeatedSpam";
    }

    return "Conversation";
}

bool BotRelationshipEventFromString(
    std::string_view text,
    BotRelationshipEvent& event)
{
    std::string const normalized = ToLowerAscii(text);

    if (normalized == "greeting" || normalized == "hello")
        event = BotRelationshipEvent::Greeting;
    else if (normalized == "thanks" || normalized == "thank")
        event = BotRelationshipEvent::Thanks;
    else if (normalized == "praise")
        event = BotRelationshipEvent::Praise;
    else if (normalized == "apology" || normalized == "sorry")
        event = BotRelationshipEvent::Apology;
    else if (normalized == "insult")
        event = BotRelationshipEvent::Insult;
    else if (normalized == "help" || normalized == "helprequest")
        event = BotRelationshipEvent::HelpRequest;
    else if (normalized == "conversation" || normalized == "talk")
        event = BotRelationshipEvent::Conversation;
    else if (normalized == "spam" || normalized == "repeatedspam")
        event = BotRelationshipEvent::RepeatedSpam;
    else
        return false;

    return true;
}

BotRelationshipLevel GetRelationshipLevel(int32 affinity)
{
    if (affinity <= -601)
        return BotRelationshipLevel::Hostile;
    if (affinity <= -301)
        return BotRelationshipLevel::Disliked;
    if (affinity <= -101)
        return BotRelationshipLevel::Wary;
    if (affinity <= 149)
        return BotRelationshipLevel::Neutral;
    if (affinity <= 399)
        return BotRelationshipLevel::Friendly;
    if (affinity <= 699)
        return BotRelationshipLevel::Trusted;

    return BotRelationshipLevel::Loyal;
}

char const* RelationshipLevelToString(BotRelationshipLevel level)
{
    switch (level)
    {
        case BotRelationshipLevel::Hostile:
            return "Hostile";
        case BotRelationshipLevel::Disliked:
            return "Disliked";
        case BotRelationshipLevel::Wary:
            return "Wary";
        case BotRelationshipLevel::Neutral:
            return "Neutral";
        case BotRelationshipLevel::Friendly:
            return "Friendly";
        case BotRelationshipLevel::Trusted:
            return "Trusted";
        case BotRelationshipLevel::Loyal:
            return "Loyal";
    }

    return "Neutral";
}

bool IsTrusted(BotRelationship const& relationship)
{
    return relationship.trust >= 300;
}

bool IsRespected(BotRelationship const& relationship)
{
    return relationship.respect >= 300;
}

bool IsFamiliar(BotRelationship const& relationship)
{
    return relationship.familiarity >= 150;
}

BotRelationshipDelta CalculateRelationshipDelta(
    BotPersonality const& personality,
    BotRelationshipEvent event,
    BotRelationshipDelta baseDelta)
{
    BotRelationshipDelta delta = baseDelta;

    int32 const friendliness = Trait(
        personality,
        &BotPersonality::friendliness);
    int32 const patience = Trait(personality, &BotPersonality::patience);
    int32 const confidence = Trait(personality, &BotPersonality::confidence);
    int32 const humour = Trait(personality, &BotPersonality::humour);

    delta.affinity += PositiveTraitBonus(delta.affinity, friendliness);

    if (delta.affinity < 0)
    {
        delta.affinity = NegativeTraitAdjustment(delta.affinity, patience);

        if (event == BotRelationshipEvent::Insult && humour > 0)
            delta.affinity += ScaleValue(-delta.affinity, humour / 3);
    }

    if (event == BotRelationshipEvent::Insult ||
        event == BotRelationshipEvent::RepeatedSpam)
    {
        delta.trust = NegativeTraitAdjustment(delta.trust, confidence);
        delta.respect = NegativeTraitAdjustment(delta.respect, patience);

        if (confidence >= 50 && friendliness <= -10 && delta.respect < 0)
            --delta.respect;

        if (IsNervous(personality) && delta.trust < 0)
            --delta.trust;
    }

    if (event == BotRelationshipEvent::Praise && IsArrogant(personality))
    {
        if (delta.affinity > 0)
            --delta.affinity;

        ++delta.respect;
    }

    if (event == BotRelationshipEvent::Apology && IsNervous(personality))
        ++delta.trust;

    if (event == BotRelationshipEvent::Thanks &&
        personality.archetype == BotPersonalityArchetype::Helpful)
        ++delta.affinity;

    ClampToDoubleMagnitude(baseDelta, delta);
    return delta;
}
