#include "BotMood.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string>

namespace
{
int32 Trait(BotPersonality const& personality, int8 BotPersonality::*trait)
{
    return static_cast<int32>(personality.*trait);
}

std::string NormalizeToken(std::string_view text)
{
    std::string normalized;
    normalized.reserve(text.size());

    for (unsigned char c : text)
    {
        if (c == '_' || c == '-' || std::isspace(c))
            continue;

        normalized.push_back(static_cast<char>(std::tolower(c)));
    }

    return normalized;
}

int16 ClampBaseline(int32 value)
{
    return static_cast<int16>(std::clamp(value, 0, 25));
}

int32 ScaleValue(int32 value, int32 percent)
{
    return value * percent / 100;
}

void ScalePositive(int32& value, int32 percent)
{
    if (value > 0)
        value = ScaleValue(value, percent);
}

void ScaleNegative(int32& value, int32 percent)
{
    if (value < 0)
        value = ScaleValue(value, percent);
}

void ClampDeltaMagnitude(BotMoodDelta const& base, BotMoodDelta& delta)
{
    auto clampField = [](int32 baseValue, int32 value)
    {
        int32 const limit = std::max(2, std::abs(baseValue) * 2);
        return std::clamp(value, -limit, limit);
    };

    delta.happiness = clampField(base.happiness, delta.happiness);
    delta.frustration = clampField(base.frustration, delta.frustration);
    delta.moodConfidence =
        clampField(base.moodConfidence, delta.moodConfidence);
    delta.fear = clampField(base.fear, delta.fear);
    delta.excitement = clampField(base.excitement, delta.excitement);
    delta.boredom = clampField(base.boredom, delta.boredom);
}

bool IsSuccessEvent(BotMoodEvent event)
{
    switch (event)
    {
        case BotMoodEvent::SharedEliteKill:
        case BotMoodEvent::SharedBossKill:
        case BotMoodEvent::DungeonCompleted:
        case BotMoodEvent::RaidEncounterCompleted:
        case BotMoodEvent::SustainedTeamwork:
            return true;
        default:
            return false;
    }
}

bool IsFailureEvent(BotMoodEvent event)
{
    switch (event)
    {
        case BotMoodEvent::PlayerDied:
        case BotMoodEvent::BotDied:
        case BotMoodEvent::RepeatedPlayerDeath:
        case BotMoodEvent::GroupWipe:
        case BotMoodEvent::PlayerAbandonedCombat:
        case BotMoodEvent::RepetitiveCombat:
            return true;
        default:
            return false;
    }
}

bool IsDangerEvent(BotMoodEvent event)
{
    switch (event)
    {
        case BotMoodEvent::DangerousPull:
        case BotMoodEvent::LowHealth:
        case BotMoodEvent::CriticalHealth:
        case BotMoodEvent::GroupWipe:
            return true;
        default:
            return false;
    }
}
}

char const* BotMoodEventToString(BotMoodEvent event)
{
    switch (event)
    {
        case BotMoodEvent::PlayerGreeting:
            return "PlayerGreeting";
        case BotMoodEvent::PlayerThanksBot:
            return "PlayerThanksBot";
        case BotMoodEvent::PlayerPraisesBot:
            return "PlayerPraisesBot";
        case BotMoodEvent::PlayerInsultsBot:
            return "PlayerInsultsBot";
        case BotMoodEvent::PlayerApologises:
            return "PlayerApologises";
        case BotMoodEvent::SharedNormalKill:
            return "SharedNormalKill";
        case BotMoodEvent::SharedEliteKill:
            return "SharedEliteKill";
        case BotMoodEvent::SharedBossKill:
            return "SharedBossKill";
        case BotMoodEvent::BotWasHealed:
            return "BotWasHealed";
        case BotMoodEvent::BotWasSaved:
            return "BotWasSaved";
        case BotMoodEvent::BotWasResurrected:
            return "BotWasResurrected";
        case BotMoodEvent::PlayerDied:
            return "PlayerDied";
        case BotMoodEvent::BotDied:
            return "BotDied";
        case BotMoodEvent::RepeatedPlayerDeath:
            return "RepeatedPlayerDeath";
        case BotMoodEvent::GroupWipe:
            return "GroupWipe";
        case BotMoodEvent::DungeonCompleted:
            return "DungeonCompleted";
        case BotMoodEvent::RaidEncounterCompleted:
            return "RaidEncounterCompleted";
        case BotMoodEvent::DangerousPull:
            return "DangerousPull";
        case BotMoodEvent::PlayerAbandonedCombat:
            return "PlayerAbandonedCombat";
        case BotMoodEvent::EnteredDungeon:
            return "EnteredDungeon";
        case BotMoodEvent::JoinedGroup:
            return "JoinedGroup";
        case BotMoodEvent::LeftGroup:
            return "LeftGroup";
        case BotMoodEvent::LowHealth:
            return "LowHealth";
        case BotMoodEvent::CriticalHealth:
            return "CriticalHealth";
        case BotMoodEvent::LowMana:
            return "LowMana";
        case BotMoodEvent::LongInactivity:
            return "LongInactivity";
        case BotMoodEvent::RepetitiveCombat:
            return "RepetitiveCombat";
        case BotMoodEvent::SustainedTeamwork:
            return "SustainedTeamwork";
    }

    return "PlayerGreeting";
}

bool BotMoodEventFromString(std::string_view text, BotMoodEvent& event)
{
    std::string const normalized = NormalizeToken(text);

    if (normalized == "greeting" || normalized == "playergreeting")
        event = BotMoodEvent::PlayerGreeting;
    else if (normalized == "thanks" || normalized == "playerthanksbot")
        event = BotMoodEvent::PlayerThanksBot;
    else if (normalized == "praise" || normalized == "playerpraisesbot")
        event = BotMoodEvent::PlayerPraisesBot;
    else if (normalized == "insult" || normalized == "playerinsultsbot")
        event = BotMoodEvent::PlayerInsultsBot;
    else if (normalized == "apology" || normalized == "playerapologises")
        event = BotMoodEvent::PlayerApologises;
    else if (normalized == "normal" || normalized == "sharednormalkill")
        event = BotMoodEvent::SharedNormalKill;
    else if (normalized == "elite" || normalized == "sharedelitekill")
        event = BotMoodEvent::SharedEliteKill;
    else if (normalized == "boss" || normalized == "sharedbosskill")
        event = BotMoodEvent::SharedBossKill;
    else if (normalized == "healed" || normalized == "botwashealed")
        event = BotMoodEvent::BotWasHealed;
    else if (normalized == "saved" || normalized == "botwassaved")
        event = BotMoodEvent::BotWasSaved;
    else if (normalized == "resurrected" || normalized == "botwasresurrected")
        event = BotMoodEvent::BotWasResurrected;
    else if (normalized == "playerdied" || normalized == "playerdeath")
        event = BotMoodEvent::PlayerDied;
    else if (normalized == "botdied" || normalized == "botdeath")
        event = BotMoodEvent::BotDied;
    else if (normalized == "repeateddeath" ||
             normalized == "repeatedplayerdeath")
        event = BotMoodEvent::RepeatedPlayerDeath;
    else if (normalized == "wipe" || normalized == "groupwipe")
        event = BotMoodEvent::GroupWipe;
    else if (normalized == "dungeon" || normalized == "dungeoncompleted")
        event = BotMoodEvent::DungeonCompleted;
    else if (normalized == "raid" || normalized == "raidencountercompleted")
        event = BotMoodEvent::RaidEncounterCompleted;
    else if (normalized == "danger" || normalized == "dangerouspull")
        event = BotMoodEvent::DangerousPull;
    else if (normalized == "abandon" ||
             normalized == "playerabandonedcombat")
        event = BotMoodEvent::PlayerAbandonedCombat;
    else if (normalized == "entereddungeon")
        event = BotMoodEvent::EnteredDungeon;
    else if (normalized == "joinedgroup")
        event = BotMoodEvent::JoinedGroup;
    else if (normalized == "leftgroup")
        event = BotMoodEvent::LeftGroup;
    else if (normalized == "lowhealth")
        event = BotMoodEvent::LowHealth;
    else if (normalized == "criticalhealth")
        event = BotMoodEvent::CriticalHealth;
    else if (normalized == "lowmana")
        event = BotMoodEvent::LowMana;
    else if (normalized == "inactivity" || normalized == "longinactivity")
        event = BotMoodEvent::LongInactivity;
    else if (normalized == "repetitivecombat")
        event = BotMoodEvent::RepetitiveCombat;
    else if (normalized == "teamwork" || normalized == "sustainedteamwork")
        event = BotMoodEvent::SustainedTeamwork;
    else
        return false;

    return true;
}

char const* BotDominantMoodToString(BotDominantMood mood)
{
    switch (mood)
    {
        case BotDominantMood::Calm:
            return "Calm";
        case BotDominantMood::Happy:
            return "Happy";
        case BotDominantMood::Frustrated:
            return "Frustrated";
        case BotDominantMood::Confident:
            return "Confident";
        case BotDominantMood::Afraid:
            return "Afraid";
        case BotDominantMood::Excited:
            return "Excited";
        case BotDominantMood::Bored:
            return "Bored";
    }

    return "Calm";
}

BotMood GetBaselineMood(BotPersonality const& personality)
{
    BotMood baseline;

    int32 const friendliness = Trait(personality, &BotPersonality::friendliness);
    int32 const confidence = Trait(personality, &BotPersonality::confidence);
    int32 const patience = Trait(personality, &BotPersonality::patience);
    int32 const bravery = Trait(personality, &BotPersonality::bravery);
    int32 const talkativeness =
        Trait(personality, &BotPersonality::talkativeness);

    baseline.happiness = ClampBaseline(8 + std::max(friendliness, 0) / 12);
    baseline.frustration = ClampBaseline(5 + std::max(-patience, 0) / 12);
    baseline.moodConfidence =
        ClampBaseline(8 + std::max(confidence, 0) / 10);
    baseline.fear =
        ClampBaseline(4 + std::max(-bravery, 0) / 12);
    baseline.excitement =
        ClampBaseline(4 + std::max(talkativeness, 0) / 18);
    baseline.boredom =
        ClampBaseline(4 + std::max(talkativeness, 0) / 16);

    switch (personality.archetype)
    {
        case BotPersonalityArchetype::Friendly:
            baseline.happiness += 3;
            break;
        case BotPersonalityArchetype::Stoic:
            baseline.excitement = std::max<int16>(0, baseline.excitement - 2);
            break;
        case BotPersonalityArchetype::Nervous:
            baseline.fear += 5;
            break;
        case BotPersonalityArchetype::Competitive:
            baseline.moodConfidence += 4;
            break;
        case BotPersonalityArchetype::Helpful:
            baseline.happiness += 2;
            break;
        case BotPersonalityArchetype::Arrogant:
            baseline.moodConfidence += 5;
            break;
        case BotPersonalityArchetype::Reckless:
            baseline.fear = std::max<int16>(0, baseline.fear - 2);
            baseline.excitement += 3;
            break;
        case BotPersonalityArchetype::Veteran:
            baseline.moodConfidence += 3;
            baseline.excitement = std::max<int16>(0, baseline.excitement - 1);
            break;
        case BotPersonalityArchetype::Sarcastic:
        case BotPersonalityArchetype::NewAdventurer:
        case BotPersonalityArchetype::Max:
            break;
    }

    baseline.happiness = ClampBaseline(baseline.happiness);
    baseline.frustration = ClampBaseline(baseline.frustration);
    baseline.moodConfidence = ClampBaseline(baseline.moodConfidence);
    baseline.fear = ClampBaseline(baseline.fear);
    baseline.excitement = ClampBaseline(baseline.excitement);
    baseline.boredom = ClampBaseline(baseline.boredom);
    return baseline;
}

BotMoodDelta GetBaseMoodDelta(BotMoodEvent event)
{
    switch (event)
    {
        case BotMoodEvent::PlayerGreeting:
            return { 2, 0, 0, 0, 0, -2 };
        case BotMoodEvent::PlayerThanksBot:
            return { 4, -1, 2, 0, 0, 0 };
        case BotMoodEvent::PlayerPraisesBot:
            return { 5, 0, 5, 0, 2, 0 };
        case BotMoodEvent::PlayerInsultsBot:
            return { -4, 8, -2, 0, 0, 0 };
        case BotMoodEvent::PlayerApologises:
            return { 2, -4, 0, 0, 0, 0 };
        case BotMoodEvent::SharedNormalKill:
            return { 0, 0, 1, 0, 0, -1 };
        case BotMoodEvent::SharedEliteKill:
            return { 2, 0, 3, 0, 3, -4 };
        case BotMoodEvent::SharedBossKill:
            return { 8, -5, 8, -5, 10, -10 };
        case BotMoodEvent::BotWasHealed:
            return { 2, 0, 0, -2, 0, 0 };
        case BotMoodEvent::BotWasSaved:
            return { 4, 0, 2, -8, 0, 0 };
        case BotMoodEvent::BotWasResurrected:
            return { 2, -2, 0, -3, 0, 0 };
        case BotMoodEvent::PlayerDied:
            return { -1, 0, 0, 1, 0, 0 };
        case BotMoodEvent::BotDied:
            return { -4, 7, -5, 3, 0, 0 };
        case BotMoodEvent::RepeatedPlayerDeath:
            return { 0, 6, -3, 2, 0, 0 };
        case BotMoodEvent::GroupWipe:
            return { -8, 12, -8, 6, -5, 0 };
        case BotMoodEvent::DungeonCompleted:
            return { 10, -8, 8, -5, 8, -10 };
        case BotMoodEvent::RaidEncounterCompleted:
            return { 8, -5, 10, 0, 10, 0 };
        case BotMoodEvent::DangerousPull:
            return { 0, 5, 0, 5, 2, 0 };
        case BotMoodEvent::PlayerAbandonedCombat:
            return { -5, 10, -4, 4, 0, 0 };
        case BotMoodEvent::EnteredDungeon:
            return { 1, 0, 1, 1, 2, -4 };
        case BotMoodEvent::JoinedGroup:
            return { 2, 0, 0, 0, 1, -4 };
        case BotMoodEvent::LeftGroup:
            return { -2, 2, 0, 0, 0, 0 };
        case BotMoodEvent::LowHealth:
            return { 0, 1, -1, 4, 0, 0 };
        case BotMoodEvent::CriticalHealth:
            return { -2, 3, -3, 9, 0, 0 };
        case BotMoodEvent::LowMana:
            return { 0, 2, -1, 2, 0, 0 };
        case BotMoodEvent::LongInactivity:
            return { 0, 0, 0, 0, -3, 8 };
        case BotMoodEvent::RepetitiveCombat:
            return { 0, 3, 0, 0, -2, 4 };
        case BotMoodEvent::SustainedTeamwork:
            return { 3, -2, 3, 0, 0, -3 };
    }

    return {};
}

BotMoodDelta CalculateMoodDelta(
    BotPersonality const& personality,
    BotMoodEvent event,
    BotMoodDelta const& baseDelta)
{
    BotMoodDelta delta = baseDelta;

    int32 const friendliness = Trait(personality, &BotPersonality::friendliness);
    int32 const patience = Trait(personality, &BotPersonality::patience);
    int32 const confidence = Trait(personality, &BotPersonality::confidence);
    int32 const humour = Trait(personality, &BotPersonality::humour);
    int32 const competitiveness =
        Trait(personality, &BotPersonality::competitiveness);
    int32 const bravery = Trait(personality, &BotPersonality::bravery);
    int32 const talkativeness =
        Trait(personality, &BotPersonality::talkativeness);

    if (friendliness > 0 &&
        (event == BotMoodEvent::PlayerThanksBot ||
            event == BotMoodEvent::PlayerPraisesBot ||
            event == BotMoodEvent::BotWasHealed ||
            event == BotMoodEvent::BotWasResurrected ||
            event == BotMoodEvent::SustainedTeamwork))
        ScalePositive(delta.happiness, 100 + friendliness / 4);

    if (delta.frustration > 0)
    {
        int32 percent = 100;
        if (patience > 0)
            percent -= std::min(45, patience / 2);
        else if (patience < 0)
            percent += std::min(50, -patience / 2);

        if (humour > 35 && IsFailureEvent(event))
            percent -= 10;

        delta.frustration = ScaleValue(delta.frustration, percent);
    }

    if (delta.moodConfidence < 0 && confidence > 0)
        delta.moodConfidence =
            ScaleValue(delta.moodConfidence, 100 - confidence / 3);
    else if (delta.moodConfidence > 0 && confidence > 0)
        ScalePositive(delta.moodConfidence, 100 + confidence / 4);

    if (competitiveness > 0 && IsSuccessEvent(event))
    {
        ScalePositive(delta.moodConfidence, 100 + competitiveness / 3);
        ScalePositive(delta.excitement, 100 + competitiveness / 4);
    }

    if (competitiveness > 0 && IsFailureEvent(event))
    {
        ScaleNegative(delta.moodConfidence, 100 + competitiveness / 4);
        ScalePositive(delta.frustration, 100 + competitiveness / 4);
    }

    if (bravery > 0 && IsDangerEvent(event))
        delta.fear = ScaleValue(delta.fear, 100 - std::min(50, bravery / 2));

    if (bravery > 0 && IsSuccessEvent(event))
        ScalePositive(delta.excitement, 100 + bravery / 5);

    if (talkativeness > 0 && event == BotMoodEvent::LongInactivity)
        ScalePositive(delta.boredom, 100 + talkativeness / 3);

    switch (personality.archetype)
    {
        case BotPersonalityArchetype::Nervous:
            ScalePositive(delta.fear, 135);
            if (event == BotMoodEvent::BotWasHealed ||
                event == BotMoodEvent::BotWasSaved ||
                event == BotMoodEvent::BotWasResurrected ||
                event == BotMoodEvent::DungeonCompleted)
                ScaleNegative(delta.fear, 140);
            break;
        case BotPersonalityArchetype::Stoic:
            break;
        case BotPersonalityArchetype::Reckless:
            if (event == BotMoodEvent::DangerousPull ||
                event == BotMoodEvent::LowHealth ||
                event == BotMoodEvent::CriticalHealth)
            {
                ScalePositive(delta.excitement, 150);
                delta.fear = ScaleValue(delta.fear, 65);
            }
            break;
        case BotPersonalityArchetype::Veteran:
            if (event == BotMoodEvent::SharedNormalKill)
                delta.excitement = 0;
            if (event == BotMoodEvent::SharedBossKill ||
                event == BotMoodEvent::DungeonCompleted ||
                event == BotMoodEvent::RaidEncounterCompleted)
                ScalePositive(delta.moodConfidence, 125);
            if (event == BotMoodEvent::RepeatedPlayerDeath)
                ScalePositive(delta.frustration, 125);
            break;
        case BotPersonalityArchetype::Helpful:
            if (event == BotMoodEvent::BotWasHealed ||
                event == BotMoodEvent::BotWasResurrected ||
                event == BotMoodEvent::SustainedTeamwork)
                ScalePositive(delta.happiness, 125);
            break;
        case BotPersonalityArchetype::Friendly:
        case BotPersonalityArchetype::Sarcastic:
        case BotPersonalityArchetype::Competitive:
        case BotPersonalityArchetype::Arrogant:
        case BotPersonalityArchetype::NewAdventurer:
        case BotPersonalityArchetype::Max:
            break;
    }

    ClampDeltaMagnitude(baseDelta, delta);
    return delta;
}

BotDominantMood GetDominantMood(BotMood const& mood, BotMood const& baseline)
{
    struct Candidate
    {
        BotDominantMood mood;
        int32 value;
    };

    std::array<Candidate, 6> const candidates =
    {
        Candidate{ BotDominantMood::Happy,
            mood.happiness - baseline.happiness },
        Candidate{ BotDominantMood::Frustrated,
            mood.frustration - baseline.frustration },
        Candidate{ BotDominantMood::Confident,
            mood.moodConfidence - baseline.moodConfidence },
        Candidate{ BotDominantMood::Afraid,
            mood.fear - baseline.fear },
        Candidate{ BotDominantMood::Excited,
            mood.excitement - baseline.excitement },
        Candidate{ BotDominantMood::Bored,
            mood.boredom - baseline.boredom }
    };

    Candidate best{ BotDominantMood::Calm, 0 };
    for (Candidate const& candidate : candidates)
    {
        if (candidate.value > best.value)
            best = candidate;
    }

    return best.value >= 15 ? best.mood : BotDominantMood::Calm;
}

uint8 GetMoodIntensity(BotMood const& mood, BotMood const& baseline)
{
    int32 intensity = 0;
    intensity = std::max<int32>(intensity, mood.happiness - baseline.happiness);
    intensity = std::max<int32>(
        intensity,
        mood.frustration - baseline.frustration);
    intensity = std::max<int32>(
        intensity,
        mood.moodConfidence - baseline.moodConfidence);
    intensity = std::max<int32>(intensity, mood.fear - baseline.fear);
    intensity = std::max<int32>(
        intensity,
        mood.excitement - baseline.excitement);
    intensity = std::max<int32>(intensity, mood.boredom - baseline.boredom);

    return static_cast<uint8>(std::clamp(intensity, 0, 100));
}
