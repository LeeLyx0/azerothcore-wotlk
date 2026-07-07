#include "BotGameplayEvent.h"

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

int32 ScaleValue(int32 value, int32 percent)
{
    return value * percent / 100;
}

bool HasPositiveCore(BotRelationshipDelta const& delta)
{
    return delta.affinity > 0 || delta.trust > 0 || delta.respect > 0;
}

bool HasNegativeCore(BotRelationshipDelta const& delta)
{
    return delta.affinity < 0 || delta.trust < 0 || delta.respect < 0;
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

void ImprovePositiveGameplay(
    BotPersonality const& personality,
    BotGameplayEvent event,
    BotRelationshipDelta& delta)
{
    int32 const friendliness = Trait(
        personality,
        &BotPersonality::friendliness);
    int32 const bravery = Trait(personality, &BotPersonality::bravery);
    int32 const competitiveness = Trait(
        personality,
        &BotPersonality::competitiveness);

    if (friendliness > 0 && delta.affinity > 0)
        delta.affinity += ScaleValue(delta.affinity, friendliness / 4);

    if (bravery > 0 &&
        (event == BotGameplayEvent::SharedBossKill ||
            event == BotGameplayEvent::RaidEncounterCompleted ||
            event == BotGameplayEvent::GroupWipe))
        delta.trust += std::max<int32>(1, bravery / 40);

    if (competitiveness > 0 &&
        (event == BotGameplayEvent::SharedEliteKill ||
            event == BotGameplayEvent::SharedBossKill ||
            event == BotGameplayEvent::DungeonCompleted ||
            event == BotGameplayEvent::RaidEncounterCompleted))
        delta.respect += std::max<int32>(1, competitiveness / 40);

    if (personality.archetype == BotPersonalityArchetype::Helpful &&
        (event == BotGameplayEvent::PlayerHealedBot ||
            event == BotGameplayEvent::PlayerResurrectedBot))
        ++delta.trust;

    if (personality.archetype == BotPersonalityArchetype::Veteran &&
        (event == BotGameplayEvent::DungeonCompleted ||
            event == BotGameplayEvent::RaidEncounterCompleted))
        ++delta.respect;
}

void AdjustNegativeGameplay(
    BotPersonality const& personality,
    BotGameplayEvent event,
    BotRelationshipDelta& delta)
{
    int32 const patience = Trait(personality, &BotPersonality::patience);
    int32 const confidence = Trait(personality, &BotPersonality::confidence);
    int32 const bravery = Trait(personality, &BotPersonality::bravery);

    auto adjustNegative = [patience](int32& value)
    {
        if (value >= 0)
            return;

        if (patience > 0)
            value += ScaleValue(-value, std::min(patience, 100) / 3);
        else if (patience < 0)
            value -= ScaleValue(-value, std::min(-patience, 100) / 3);
    };

    adjustNegative(delta.affinity);
    adjustNegative(delta.trust);
    adjustNegative(delta.respect);

    if (event == BotGameplayEvent::PlayerLeftGroupDuringCombat &&
        confidence > 40)
        --delta.trust;

    if (event == BotGameplayEvent::RepeatedPlayerDeath &&
        personality.archetype == BotPersonalityArchetype::Veteran)
        --delta.respect;

    if (event == BotGameplayEvent::GroupWipe && bravery > 35)
        ++delta.trust;

    if (personality.archetype == BotPersonalityArchetype::Nervous &&
        (event == BotGameplayEvent::GroupWipe ||
            event == BotGameplayEvent::PlayerDied))
        --delta.trust;
}
}

char const* BotGameplayEventToString(BotGameplayEvent event)
{
    switch (event)
    {
        case BotGameplayEvent::SharedNormalKill:
            return "SharedNormalKill";
        case BotGameplayEvent::SharedEliteKill:
            return "SharedEliteKill";
        case BotGameplayEvent::SharedBossKill:
            return "SharedBossKill";
        case BotGameplayEvent::PlayerHealedBot:
            return "PlayerHealedBot";
        case BotGameplayEvent::PlayerResurrectedBot:
            return "PlayerResurrectedBot";
        case BotGameplayEvent::BotHealedPlayer:
            return "BotHealedPlayer";
        case BotGameplayEvent::BotResurrectedPlayer:
            return "BotResurrectedPlayer";
        case BotGameplayEvent::PlayerDied:
            return "PlayerDied";
        case BotGameplayEvent::BotDied:
            return "BotDied";
        case BotGameplayEvent::SharedDeath:
            return "SharedDeath";
        case BotGameplayEvent::GroupWipe:
            return "GroupWipe";
        case BotGameplayEvent::PlayerLeftGroupDuringCombat:
            return "PlayerLeftGroupDuringCombat";
        case BotGameplayEvent::DungeonCompleted:
            return "DungeonCompleted";
        case BotGameplayEvent::RaidEncounterCompleted:
            return "RaidEncounterCompleted";
        case BotGameplayEvent::SustainedTeamwork:
            return "SustainedTeamwork";
        case BotGameplayEvent::RepeatedPlayerDeath:
            return "RepeatedPlayerDeath";
    }

    return "SharedNormalKill";
}

bool BotGameplayEventFromString(
    std::string_view text,
    BotGameplayEvent& event)
{
    std::string const normalized = NormalizeToken(text);

    if (normalized == "sharednormalkill" ||
        normalized == "normalkill" ||
        normalized == "normal")
        event = BotGameplayEvent::SharedNormalKill;
    else if (normalized == "sharedelitekill" ||
        normalized == "elitekill" ||
        normalized == "elite")
        event = BotGameplayEvent::SharedEliteKill;
    else if (normalized == "sharedbosskill" ||
        normalized == "bosskill" ||
        normalized == "boss")
        event = BotGameplayEvent::SharedBossKill;
    else if (normalized == "playerhealedbot" ||
        normalized == "healedbot")
        event = BotGameplayEvent::PlayerHealedBot;
    else if (normalized == "playerresurrectedbot" ||
        normalized == "resurrectedbot" ||
        normalized == "rezbot")
        event = BotGameplayEvent::PlayerResurrectedBot;
    else if (normalized == "bothealedplayer" ||
        normalized == "botheal")
        event = BotGameplayEvent::BotHealedPlayer;
    else if (normalized == "botresurrectedplayer" ||
        normalized == "botrez")
        event = BotGameplayEvent::BotResurrectedPlayer;
    else if (normalized == "playerdied" ||
        normalized == "playerdeath")
        event = BotGameplayEvent::PlayerDied;
    else if (normalized == "botdied" ||
        normalized == "botdeath")
        event = BotGameplayEvent::BotDied;
    else if (normalized == "shareddeath")
        event = BotGameplayEvent::SharedDeath;
    else if (normalized == "groupwipe" || normalized == "wipe")
        event = BotGameplayEvent::GroupWipe;
    else if (normalized == "playerleftgroupduringcombat" ||
        normalized == "leftcombat" ||
        normalized == "leftgroup")
        event = BotGameplayEvent::PlayerLeftGroupDuringCombat;
    else if (normalized == "dungeoncompleted" ||
        normalized == "dungeoncomplete" ||
        normalized == "dungeon")
        event = BotGameplayEvent::DungeonCompleted;
    else if (normalized == "raidencountercompleted" ||
        normalized == "raidencounter" ||
        normalized == "raid")
        event = BotGameplayEvent::RaidEncounterCompleted;
    else if (normalized == "sustainedteamwork" ||
        normalized == "teamwork")
        event = BotGameplayEvent::SustainedTeamwork;
    else if (normalized == "repeatedplayerdeath" ||
        normalized == "repeateddeath")
        event = BotGameplayEvent::RepeatedPlayerDeath;
    else
        return false;

    return true;
}

bool IsPositiveGameplayEvent(BotGameplayEvent event)
{
    switch (event)
    {
        case BotGameplayEvent::SharedNormalKill:
        case BotGameplayEvent::SharedEliteKill:
        case BotGameplayEvent::SharedBossKill:
        case BotGameplayEvent::PlayerHealedBot:
        case BotGameplayEvent::PlayerResurrectedBot:
        case BotGameplayEvent::BotHealedPlayer:
        case BotGameplayEvent::BotResurrectedPlayer:
        case BotGameplayEvent::DungeonCompleted:
        case BotGameplayEvent::RaidEncounterCompleted:
        case BotGameplayEvent::SustainedTeamwork:
            return true;
        case BotGameplayEvent::PlayerDied:
        case BotGameplayEvent::BotDied:
        case BotGameplayEvent::SharedDeath:
        case BotGameplayEvent::GroupWipe:
        case BotGameplayEvent::PlayerLeftGroupDuringCombat:
        case BotGameplayEvent::RepeatedPlayerDeath:
            return false;
    }

    return false;
}

BotRelationshipDelta CalculateGameplayRelationshipDelta(
    BotPersonality const& personality,
    BotGameplayEvent event,
    BotRelationshipDelta baseDelta)
{
    BotRelationshipDelta delta = baseDelta;

    if (HasPositiveCore(delta))
        ImprovePositiveGameplay(personality, event, delta);

    if (HasNegativeCore(delta))
        AdjustNegativeGameplay(personality, event, delta);

    if (personality.archetype == BotPersonalityArchetype::Arrogant &&
        event == BotGameplayEvent::PlayerHealedBot &&
        delta.affinity > 0)
        --delta.affinity;

    if (personality.archetype == BotPersonalityArchetype::Reckless &&
        event == BotGameplayEvent::GroupWipe &&
        delta.affinity < 0)
        ++delta.affinity;

    ClampToDoubleMagnitude(baseDelta, delta);
    return delta;
}
