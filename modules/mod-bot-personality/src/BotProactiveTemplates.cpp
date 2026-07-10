#include "BotProactiveTemplates.h"

#include <array>
#include <string_view>

namespace
{
using TemplateList = std::array<std::string_view, 3>;

int32 Trait(BotPersonality const& personality, int8 BotPersonality::*trait)
{
    return static_cast<int32>(personality.*trait);
}

void ReplaceAll(
    std::string& text,
    std::string_view placeholder,
    std::string const& value)
{
    std::size_t offset = 0;
    while ((offset = text.find(placeholder, offset)) != std::string::npos)
    {
        text.replace(offset, placeholder.size(), value);
        offset += value.size();
    }
}

std::string Render(
    std::string_view text,
    BotProactiveDialogueContext const& context)
{
    std::string rendered(text);
    ReplaceAll(rendered, "{bot}", context.botName);
    ReplaceAll(
        rendered,
        "{player}",
        context.playerName.empty() ? std::string("everyone") :
            context.playerName);
    ReplaceAll(
        rendered,
        "{speaker}",
        context.previousSpeakerName.empty() ? std::string("that") :
            context.previousSpeakerName);
    return rendered;
}

TemplateList const& GroupJoinTemplates(BotResponseTone tone)
{
    static constexpr TemplateList warm =
    {
        "Good to be with you.",
        "Hello, everyone. I am ready.",
        "Good company. Let us begin."
    };
    static constexpr TemplateList stoic =
    {
        "Ready.",
        "I am here.",
        "Proceed."
    };
    static constexpr TemplateList sarcastic =
    {
        "Try not to make me regret joining.",
        "Here we are. Bold choice.",
        "This should be memorable."
    };
    static constexpr TemplateList neutral =
    {
        "Greetings.",
        "Ready when you are.",
        "I will follow."
    };

    if (tone == BotResponseTone::Warm ||
        tone == BotResponseTone::Enthusiastic)
        return warm;
    if (tone == BotResponseTone::Stoic)
        return stoic;
    if (tone == BotResponseTone::Sarcastic)
        return sarcastic;
    return neutral;
}

TemplateList const& BossKillTemplates(
    BotProactiveDialogueContext const& context)
{
    static constexpr TemplateList friendly =
    {
        "Well fought, everyone.",
        "That was a good fight.",
        "We handled that together."
    };
    static constexpr TemplateList stoic =
    {
        "Target down.",
        "Well fought.",
        "It is finished."
    };
    static constexpr TemplateList competitive =
    {
        "That is how it is done.",
        "Finally, a worthy fight.",
        "Good. Find us something harder."
    };
    static constexpr TemplateList nervous =
    {
        "It is dead, yes? Truly dead?",
        "I cannot believe that worked.",
        "Let us keep moving before it gets back up."
    };
    static constexpr TemplateList sarcastic =
    {
        "A clean victory. Suspicious.",
        "Look at that. Competence.",
        "Almost like we planned it."
    };

    if (context.tone == BotResponseTone::Stoic)
        return stoic;
    if (context.tone == BotResponseTone::Nervous)
        return nervous;
    if (context.tone == BotResponseTone::Sarcastic)
        return sarcastic;
    if (context.personality.archetype ==
        BotPersonalityArchetype::Competitive)
        return competitive;
    return friendly;
}

TemplateList const& CompletionTemplates(BotResponseTone tone)
{
    static constexpr TemplateList warm =
    {
        "We did it. Well fought, everyone.",
        "That was a good run.",
        "I would travel with this group again."
    };
    static constexpr TemplateList stoic =
    {
        "Finished.",
        "Well fought.",
        "The task is done."
    };
    static constexpr TemplateList competitive =
    {
        "Good work. Efficient enough.",
        "Done. Next challenge.",
        "A proper finish."
    };
    static constexpr TemplateList sarcastic =
    {
        "We survived our own plan. Impressive.",
        "Done, and with only minor theatrics.",
        "A surprisingly acceptable result."
    };

    if (tone == BotResponseTone::Stoic)
        return stoic;
    if (tone == BotResponseTone::Sarcastic)
        return sarcastic;
    if (tone == BotResponseTone::Arrogant ||
        tone == BotResponseTone::Cold)
        return competitive;
    return warm;
}

TemplateList const& WipeTemplates(BotResponseTone tone)
{
    static constexpr TemplateList warm =
    {
        "No matter. We try again.",
        "Everyone ready for another attempt?",
        "We will get it next time."
    };
    static constexpr TemplateList stoic =
    {
        "Regroup.",
        "Recover, then retry.",
        "We correct this."
    };
    static constexpr TemplateList sarcastic =
    {
        "That could have gone marginally better.",
        "An excellent demonstration of what not to do.",
        "At least we were consistent."
    };
    static constexpr TemplateList nervous =
    {
        "That was bad. We are trying again?",
        "I did not like that at all.",
        "Please tell me we have a better plan."
    };

    if (tone == BotResponseTone::Stoic)
        return stoic;
    if (tone == BotResponseTone::Sarcastic ||
        tone == BotResponseTone::Arrogant)
        return sarcastic;
    if (tone == BotResponseTone::Nervous)
        return nervous;
    return warm;
}

TemplateList const& ResurrectionTemplates(
    BotProactiveDialogueContext const& context)
{
    static constexpr TemplateList positive =
    {
        "I knew I could rely on you, {player}.",
        "Thank you. I owe you one.",
        "Good timing, {player}."
    };
    static constexpr TemplateList negative =
    {
        "Useful, for once.",
        "I suppose I should thank you.",
        "Do not expect me to make a habit of dying."
    };
    static constexpr TemplateList neutral =
    {
        "Thank you, {player}.",
        "I am back.",
        "Good timing."
    };

    if (context.relationshipLevel == BotRelationshipLevel::Hostile ||
        context.relationshipLevel == BotRelationshipLevel::Disliked ||
        context.relationshipLevel == BotRelationshipLevel::Wary)
        return negative;
    if (context.relationshipLevel == BotRelationshipLevel::Friendly ||
        context.relationshipLevel == BotRelationshipLevel::Trusted ||
        context.relationshipLevel == BotRelationshipLevel::Loyal)
        return positive;
    return neutral;
}

TemplateList const& HealthTemplates(BotProactiveDialogueEvent event,
    BotResponseTone tone)
{
    static constexpr TemplateList criticalNervous =
    {
        "I could use some help!",
        "This is going badly!",
        "Please tell me someone has a heal ready."
    };
    static constexpr TemplateList criticalStoic =
    {
        "Need healing.",
        "I am nearly down.",
        "Assist."
    };
    static constexpr TemplateList low =
    {
        "I am taking heavy hits.",
        "Health is getting low.",
        "A heal soon would help."
    };
    static constexpr TemplateList mana =
    {
        "Mana is low.",
        "I need a moment for mana.",
        "Running low on mana."
    };

    if (event == BotProactiveDialogueEvent::BotLowMana)
        return mana;
    if (event == BotProactiveDialogueEvent::BotCriticalHealth &&
        tone == BotResponseTone::Stoic)
        return criticalStoic;
    if (event == BotProactiveDialogueEvent::BotCriticalHealth)
        return criticalNervous;
    return low;
}

TemplateList const& MiscTemplates(BotProactiveDialogueEvent event)
{
    static constexpr TemplateList elite =
    {
        "A stronger foe. Good.",
        "That one was worth the effort.",
        "Clean enough."
    };
    static constexpr TemplateList healed =
    {
        "Good heal, {player}.",
        "Thank you for the heal.",
        "That helped."
    };
    static constexpr TemplateList playerDeath =
    {
        "Careful. That was close.",
        "We need to keep you standing.",
        "Recover and stay sharp."
    };
    static constexpr TemplateList repeatedDeath =
    {
        "We need a different approach.",
        "This is becoming a pattern.",
        "Slow down before this gets worse."
    };
    static constexpr TemplateList botDeath =
    {
        "I misjudged that.",
        "That hurt.",
        "I will recover."
    };
    static constexpr TemplateList abandon =
    {
        "Leaving mid-fight helps no one.",
        "We were still engaged.",
        "Do not abandon the line."
    };
    static constexpr TemplateList danger =
    {
        "That pull is dangerous.",
        "Careful. This may turn ugly.",
        "Brace yourselves."
    };
    static constexpr TemplateList idle =
    {
        "Quiet stretch.",
        "Are we waiting for something?",
        "Still here."
    };
    static constexpr TemplateList teamwork =
    {
        "We are moving well together.",
        "Good rhythm.",
        "This group is finding its stride."
    };
    static constexpr TemplateList banter =
    {
        "Agreed.",
        "That sounds right.",
        "Noted, {speaker}."
    };

    switch (event)
    {
        case BotProactiveDialogueEvent::SharedEliteKilled:
            return elite;
        case BotProactiveDialogueEvent::BotHealed:
        case BotProactiveDialogueEvent::BotSaved:
            return healed;
        case BotProactiveDialogueEvent::PlayerDied:
            return playerDeath;
        case BotProactiveDialogueEvent::RepeatedPlayerDeath:
            return repeatedDeath;
        case BotProactiveDialogueEvent::BotDied:
            return botDeath;
        case BotProactiveDialogueEvent::PlayerAbandonedCombat:
            return abandon;
        case BotProactiveDialogueEvent::DangerousPull:
        case BotProactiveDialogueEvent::DungeonEntered:
            return danger;
        case BotProactiveDialogueEvent::LongInactivity:
            return idle;
        case BotProactiveDialogueEvent::SustainedTeamwork:
        case BotProactiveDialogueEvent::RelationshipImproved:
            return teamwork;
        case BotProactiveDialogueEvent::BotReplyToBot:
            return banter;
        case BotProactiveDialogueEvent::RelationshipWorsened:
            return repeatedDeath;
        default:
            return teamwork;
    }
}

TemplateList const& TemplatesFor(BotProactiveDialogueContext const& context)
{
    switch (context.event)
    {
        case BotProactiveDialogueEvent::GroupJoined:
            return GroupJoinTemplates(context.tone);
        case BotProactiveDialogueEvent::SharedBossKilled:
            return BossKillTemplates(context);
        case BotProactiveDialogueEvent::DungeonCompleted:
        case BotProactiveDialogueEvent::RaidEncounterCompleted:
            return CompletionTemplates(context.tone);
        case BotProactiveDialogueEvent::GroupWipe:
            return WipeTemplates(context.tone);
        case BotProactiveDialogueEvent::BotResurrected:
            return ResurrectionTemplates(context);
        case BotProactiveDialogueEvent::BotLowHealth:
        case BotProactiveDialogueEvent::BotCriticalHealth:
        case BotProactiveDialogueEvent::BotLowMana:
            return HealthTemplates(context.event, context.tone);
        default:
            return MiscTemplates(context.event);
    }
}
}

BotResponseTone DetermineProactiveTone(
    BotProactiveDialogueContext const& context)
{
    BotChatIntent intent = BotChatIntent::Unknown;
    switch (context.event)
    {
        case BotProactiveDialogueEvent::GroupJoined:
        case BotProactiveDialogueEvent::DungeonEntered:
            intent = BotChatIntent::Greeting;
            break;
        case BotProactiveDialogueEvent::BotResurrected:
        case BotProactiveDialogueEvent::BotHealed:
        case BotProactiveDialogueEvent::BotSaved:
            intent = BotChatIntent::Thanks;
            break;
        case BotProactiveDialogueEvent::SharedBossKilled:
        case BotProactiveDialogueEvent::DungeonCompleted:
        case BotProactiveDialogueEvent::RaidEncounterCompleted:
        case BotProactiveDialogueEvent::SustainedTeamwork:
            intent = BotChatIntent::Praise;
            break;
        case BotProactiveDialogueEvent::GroupWipe:
        case BotProactiveDialogueEvent::PlayerDied:
        case BotProactiveDialogueEvent::RepeatedPlayerDeath:
        case BotProactiveDialogueEvent::BotDied:
        case BotProactiveDialogueEvent::PlayerAbandonedCombat:
            intent = BotChatIntent::Disagreement;
            break;
        default:
            break;
    }

    BotResponseTone tone = context.hasExistingRelationship ?
        DetermineBotResponseTone(
            context.personality,
            intent,
            context.relationship,
            context.relationshipLevel) :
        DetermineBotResponseTone(context.personality, intent);

    if (context.dominantMood == BotDominantMood::Afraid)
        tone = BotResponseTone::Nervous;
    else if (context.dominantMood == BotDominantMood::Frustrated &&
        tone == BotResponseTone::Warm)
        tone = BotResponseTone::Neutral;
    else if (context.dominantMood == BotDominantMood::Frustrated &&
        Trait(context.personality, &BotPersonality::humour) >= 25)
        tone = BotResponseTone::Sarcastic;
    else if ((context.dominantMood == BotDominantMood::Happy ||
        context.dominantMood == BotDominantMood::Excited) &&
        tone == BotResponseTone::Neutral)
        tone = BotResponseTone::Warm;
    else if (context.dominantMood == BotDominantMood::Bored &&
        Trait(context.personality, &BotPersonality::humour) >= 35)
        tone = BotResponseTone::Sarcastic;

    if (context.personality.archetype == BotPersonalityArchetype::Stoic ||
        context.personality.archetype == BotPersonalityArchetype::Veteran)
    {
        if (context.event == BotProactiveDialogueEvent::BotCriticalHealth ||
            context.event == BotProactiveDialogueEvent::GroupWipe)
            return BotResponseTone::Stoic;
    }

    return tone;
}

std::optional<std::string> GenerateProactiveResponse(
    BotProactiveDialogueContext const& context)
{
    TemplateList const& templates = TemplatesFor(context);
    std::size_t index = context.selectionSeed % templates.size();

    for (std::size_t offset = 0; offset < templates.size(); ++offset)
    {
        std::size_t const candidate = (index + offset) % templates.size();
        std::string response = Render(templates[candidate], context);
        if (response.empty())
            continue;

        if (templates.size() > 1 &&
            !context.previousResponse.empty() &&
            response == context.previousResponse)
            continue;

        return response;
    }

    return Render(templates[index], context);
}
