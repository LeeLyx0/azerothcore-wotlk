#include "BotProactiveDialogueEvent.h"

#include <cctype>
#include <string>

namespace
{
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
}

char const* BotProactiveDialogueEventToString(
    BotProactiveDialogueEvent event)
{
    switch (event)
    {
        case BotProactiveDialogueEvent::GroupJoined:
            return "GroupJoined";
        case BotProactiveDialogueEvent::DungeonEntered:
            return "DungeonEntered";
        case BotProactiveDialogueEvent::SharedEliteKilled:
            return "SharedEliteKilled";
        case BotProactiveDialogueEvent::SharedBossKilled:
            return "SharedBossKilled";
        case BotProactiveDialogueEvent::BotHealed:
            return "BotHealed";
        case BotProactiveDialogueEvent::BotSaved:
            return "BotSaved";
        case BotProactiveDialogueEvent::BotResurrected:
            return "BotResurrected";
        case BotProactiveDialogueEvent::PlayerDied:
            return "PlayerDied";
        case BotProactiveDialogueEvent::RepeatedPlayerDeath:
            return "RepeatedPlayerDeath";
        case BotProactiveDialogueEvent::BotDied:
            return "BotDied";
        case BotProactiveDialogueEvent::GroupWipe:
            return "GroupWipe";
        case BotProactiveDialogueEvent::DungeonCompleted:
            return "DungeonCompleted";
        case BotProactiveDialogueEvent::RaidEncounterCompleted:
            return "RaidEncounterCompleted";
        case BotProactiveDialogueEvent::DangerousPull:
            return "DangerousPull";
        case BotProactiveDialogueEvent::PlayerAbandonedCombat:
            return "PlayerAbandonedCombat";
        case BotProactiveDialogueEvent::BotLowHealth:
            return "BotLowHealth";
        case BotProactiveDialogueEvent::BotCriticalHealth:
            return "BotCriticalHealth";
        case BotProactiveDialogueEvent::BotLowMana:
            return "BotLowMana";
        case BotProactiveDialogueEvent::LongInactivity:
            return "LongInactivity";
        case BotProactiveDialogueEvent::SustainedTeamwork:
            return "SustainedTeamwork";
        case BotProactiveDialogueEvent::RelationshipImproved:
            return "RelationshipImproved";
        case BotProactiveDialogueEvent::RelationshipWorsened:
            return "RelationshipWorsened";
        case BotProactiveDialogueEvent::BotReplyToBot:
            return "BotReplyToBot";
    }

    return "GroupJoined";
}

bool BotProactiveDialogueEventFromString(
    std::string_view text,
    BotProactiveDialogueEvent& event)
{
    std::string const normalized = NormalizeToken(text);

    if (normalized == "groupjoined" || normalized == "join")
        event = BotProactiveDialogueEvent::GroupJoined;
    else if (normalized == "dungeonentered" || normalized == "enterdungeon")
        event = BotProactiveDialogueEvent::DungeonEntered;
    else if (normalized == "elite" || normalized == "sharedelitekilled")
        event = BotProactiveDialogueEvent::SharedEliteKilled;
    else if (normalized == "boss" || normalized == "sharedbosskilled")
        event = BotProactiveDialogueEvent::SharedBossKilled;
    else if (normalized == "bothealed" || normalized == "healed")
        event = BotProactiveDialogueEvent::BotHealed;
    else if (normalized == "botsaved" || normalized == "saved")
        event = BotProactiveDialogueEvent::BotSaved;
    else if (normalized == "botresurrected" || normalized == "resurrected")
        event = BotProactiveDialogueEvent::BotResurrected;
    else if (normalized == "playerdied" || normalized == "playerdeath")
        event = BotProactiveDialogueEvent::PlayerDied;
    else if (normalized == "repeateddeath" ||
             normalized == "repeatedplayerdeath")
        event = BotProactiveDialogueEvent::RepeatedPlayerDeath;
    else if (normalized == "botdied" || normalized == "botdeath")
        event = BotProactiveDialogueEvent::BotDied;
    else if (normalized == "wipe" || normalized == "groupwipe")
        event = BotProactiveDialogueEvent::GroupWipe;
    else if (normalized == "dungeon" || normalized == "dungeoncompleted")
        event = BotProactiveDialogueEvent::DungeonCompleted;
    else if (normalized == "raid" || normalized == "raidencountercompleted")
        event = BotProactiveDialogueEvent::RaidEncounterCompleted;
    else if (normalized == "danger" || normalized == "dangerouspull")
        event = BotProactiveDialogueEvent::DangerousPull;
    else if (normalized == "abandon" ||
             normalized == "playerabandonedcombat")
        event = BotProactiveDialogueEvent::PlayerAbandonedCombat;
    else if (normalized == "lowhealth" || normalized == "botlowhealth")
        event = BotProactiveDialogueEvent::BotLowHealth;
    else if (normalized == "criticalhealth" ||
             normalized == "botcriticalhealth")
        event = BotProactiveDialogueEvent::BotCriticalHealth;
    else if (normalized == "lowmana" || normalized == "botlowmana")
        event = BotProactiveDialogueEvent::BotLowMana;
    else if (normalized == "inactivity" || normalized == "longinactivity")
        event = BotProactiveDialogueEvent::LongInactivity;
    else if (normalized == "teamwork" || normalized == "sustainedteamwork")
        event = BotProactiveDialogueEvent::SustainedTeamwork;
    else if (normalized == "relationshipimproved")
        event = BotProactiveDialogueEvent::RelationshipImproved;
    else if (normalized == "relationshipworsened")
        event = BotProactiveDialogueEvent::RelationshipWorsened;
    else if (normalized == "banter" || normalized == "botreplytobot")
        event = BotProactiveDialogueEvent::BotReplyToBot;
    else
        return false;

    return true;
}

char const* BotDialoguePriorityToString(BotDialoguePriority priority)
{
    switch (priority)
    {
        case BotDialoguePriority::Low:
            return "Low";
        case BotDialoguePriority::Normal:
            return "Normal";
        case BotDialoguePriority::High:
            return "High";
        case BotDialoguePriority::Critical:
            return "Critical";
    }

    return "Low";
}

BotDialoguePriority GetDialoguePriority(BotProactiveDialogueEvent event)
{
    switch (event)
    {
        case BotProactiveDialogueEvent::DungeonCompleted:
        case BotProactiveDialogueEvent::RaidEncounterCompleted:
        case BotProactiveDialogueEvent::GroupWipe:
        case BotProactiveDialogueEvent::BotResurrected:
        case BotProactiveDialogueEvent::PlayerAbandonedCombat:
            return BotDialoguePriority::Critical;
        case BotProactiveDialogueEvent::SharedBossKilled:
        case BotProactiveDialogueEvent::BotSaved:
        case BotProactiveDialogueEvent::RepeatedPlayerDeath:
        case BotProactiveDialogueEvent::BotCriticalHealth:
            return BotDialoguePriority::High;
        case BotProactiveDialogueEvent::SharedEliteKilled:
        case BotProactiveDialogueEvent::BotHealed:
        case BotProactiveDialogueEvent::BotDied:
        case BotProactiveDialogueEvent::SustainedTeamwork:
        case BotProactiveDialogueEvent::GroupJoined:
        case BotProactiveDialogueEvent::DungeonEntered:
        case BotProactiveDialogueEvent::RelationshipImproved:
        case BotProactiveDialogueEvent::RelationshipWorsened:
            return BotDialoguePriority::Normal;
        case BotProactiveDialogueEvent::PlayerDied:
        case BotProactiveDialogueEvent::DangerousPull:
        case BotProactiveDialogueEvent::BotLowHealth:
        case BotProactiveDialogueEvent::BotLowMana:
        case BotProactiveDialogueEvent::LongInactivity:
        case BotProactiveDialogueEvent::BotReplyToBot:
            return BotDialoguePriority::Low;
    }

    return BotDialoguePriority::Low;
}
