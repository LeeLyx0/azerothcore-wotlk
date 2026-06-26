#include "BotChatIntent.h"

#include "Util.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <string_view>

namespace
{
template <std::size_t Size>
bool ContainsPhrase(
    std::string const& normalized,
    std::array<std::string_view, Size> const& phrases)
{
    std::string const padded = " " + normalized + " ";

    for (std::string_view phrase : phrases)
    {
        std::string const needle = " " + std::string(phrase) + " ";
        if (padded.find(needle) != std::string::npos)
            return true;
    }

    return false;
}

template <std::size_t Size>
bool StartsWithPhrase(
    std::string const& normalized,
    std::array<std::string_view, Size> const& phrases)
{
    for (std::string_view phrase : phrases)
    {
        if (normalized == phrase)
            return true;

        if (normalized.rfind(std::string(phrase) + " ", 0) == 0)
            return true;
    }

    return false;
}

std::string ToLowerUtf8Safe(std::string const& text)
{
    std::wstring wide;
    if (Utf8toWStr(text, wide))
    {
        for (wchar_t& ch : wide)
            ch = static_cast<wchar_t>(std::towlower(ch));

        std::string lowered;
        if (WStrToUtf8(wide, lowered))
            return lowered;
    }

    std::string lowered = text;
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

bool IsAsciiPunctuation(unsigned char c)
{
    return c < 128 && std::ispunct(c) != 0;
}

bool IsAsciiSpace(unsigned char c)
{
    return c < 128 && std::isspace(c) != 0;
}

bool IsDirectInsult(std::string const& normalized)
{
    static constexpr std::array ExactInsults =
    {
        std::string_view("idiot"),
        std::string_view("stupid"),
        std::string_view("useless"),
        std::string_view("terrible"),
        std::string_view("noob")
    };

    static constexpr std::array DirectedInsults =
    {
        std::string_view("bad bot"),
        std::string_view("you suck"),
        std::string_view("you are an idiot"),
        std::string_view("you are idiot"),
        std::string_view("you are stupid"),
        std::string_view("you are useless"),
        std::string_view("you are terrible"),
        std::string_view("you re stupid"),
        std::string_view("you re useless"),
        std::string_view("you re terrible"),
        std::string_view("stupid bot"),
        std::string_view("idiot bot"),
        std::string_view("useless bot")
    };

    for (std::string_view phrase : ExactInsults)
    {
        if (normalized == phrase)
            return true;
    }

    return ContainsPhrase(normalized, DirectedInsults);
}

bool IsGreeting(std::string const& normalized)
{
    static constexpr std::array Phrases =
    {
        std::string_view("hello"),
        std::string_view("hi"),
        std::string_view("hey"),
        std::string_view("hiya"),
        std::string_view("greetings"),
        std::string_view("good morning"),
        std::string_view("good afternoon"),
        std::string_view("good evening")
    };

    return StartsWithPhrase(normalized, Phrases);
}

bool IsFarewell(std::string const& normalized)
{
    static constexpr std::array Phrases =
    {
        std::string_view("bye"),
        std::string_view("goodbye"),
        std::string_view("see you"),
        std::string_view("later"),
        std::string_view("farewell")
    };

    return StartsWithPhrase(normalized, Phrases);
}

bool IsThanks(std::string const& normalized)
{
    static constexpr std::array Phrases =
    {
        std::string_view("thanks"),
        std::string_view("thank you"),
        std::string_view("cheers"),
        std::string_view("much appreciated")
    };

    return StartsWithPhrase(normalized, Phrases) ||
        ContainsPhrase(normalized, Phrases);
}

bool IsPraise(std::string const& normalized)
{
    static constexpr std::array Phrases =
    {
        std::string_view("good job"),
        std::string_view("well done"),
        std::string_view("nice work"),
        std::string_view("great healing"),
        std::string_view("great tanking"),
        std::string_view("you did well")
    };

    return ContainsPhrase(normalized, Phrases);
}

bool IsApology(std::string const& normalized)
{
    static constexpr std::array Phrases =
    {
        std::string_view("sorry"),
        std::string_view("my fault"),
        std::string_view("my bad"),
        std::string_view("apologies")
    };

    return StartsWithPhrase(normalized, Phrases) ||
        ContainsPhrase(normalized, Phrases);
}

bool IsHelpRequest(std::string const& normalized)
{
    static constexpr std::array Phrases =
    {
        std::string_view("help"),
        std::string_view("can you help"),
        std::string_view("what should i do"),
        std::string_view("i need help")
    };

    return StartsWithPhrase(normalized, Phrases) ||
        ContainsPhrase(normalized, Phrases);
}

bool IsIdentityQuestion(std::string const& normalized)
{
    static constexpr std::array Phrases =
    {
        std::string_view("who are you"),
        std::string_view("what are you"),
        std::string_view("tell me about yourself"),
        std::string_view("what is your name")
    };

    return StartsWithPhrase(normalized, Phrases) ||
        ContainsPhrase(normalized, Phrases);
}

bool IsWellbeingQuestion(std::string const& normalized)
{
    static constexpr std::array Phrases =
    {
        std::string_view("how are you"),
        std::string_view("are you okay"),
        std::string_view("how do you feel"),
        std::string_view("you alright")
    };

    return StartsWithPhrase(normalized, Phrases) ||
        ContainsPhrase(normalized, Phrases);
}

bool IsAgreement(std::string const& normalized)
{
    static constexpr std::array Phrases =
    {
        std::string_view("yes"),
        std::string_view("agreed"),
        std::string_view("sounds good"),
        std::string_view("that works")
    };

    return StartsWithPhrase(normalized, Phrases);
}

bool IsDisagreement(std::string const& normalized)
{
    static constexpr std::array Phrases =
    {
        std::string_view("no"),
        std::string_view("not really"),
        std::string_view("i disagree"),
        std::string_view("that will not work")
    };

    return StartsWithPhrase(normalized, Phrases);
}

int32 Trait(BotPersonality const& personality, int8 BotPersonality::*trait)
{
    return static_cast<int32>(personality.*trait);
}

bool IsStoicPersonality(BotPersonality const& personality)
{
    return personality.archetype == BotPersonalityArchetype::Stoic ||
        personality.archetype == BotPersonalityArchetype::Veteran ||
        (Trait(personality, &BotPersonality::talkativeness) <= -45 &&
            Trait(personality, &BotPersonality::patience) >= 30);
}
}

char const* BotChatIntentToString(BotChatIntent intent)
{
    switch (intent)
    {
        case BotChatIntent::Greeting:
            return "Greeting";
        case BotChatIntent::Farewell:
            return "Farewell";
        case BotChatIntent::Thanks:
            return "Thanks";
        case BotChatIntent::Praise:
            return "Praise";
        case BotChatIntent::Apology:
            return "Apology";
        case BotChatIntent::Insult:
            return "Insult";
        case BotChatIntent::HelpRequest:
            return "HelpRequest";
        case BotChatIntent::IdentityQuestion:
            return "IdentityQuestion";
        case BotChatIntent::WellbeingQuestion:
            return "WellbeingQuestion";
        case BotChatIntent::Agreement:
            return "Agreement";
        case BotChatIntent::Disagreement:
            return "Disagreement";
        case BotChatIntent::Unknown:
            return "Unknown";
    }

    return "Unknown";
}

char const* BotResponseToneToString(BotResponseTone tone)
{
    switch (tone)
    {
        case BotResponseTone::Warm:
            return "Warm";
        case BotResponseTone::Neutral:
            return "Neutral";
        case BotResponseTone::Cold:
            return "Cold";
        case BotResponseTone::Sarcastic:
            return "Sarcastic";
        case BotResponseTone::Nervous:
            return "Nervous";
        case BotResponseTone::Enthusiastic:
            return "Enthusiastic";
        case BotResponseTone::Arrogant:
            return "Arrogant";
        case BotResponseTone::Stoic:
            return "Stoic";
    }

    return "Neutral";
}

bool BotChatIntentFromString(std::string const& text, BotChatIntent& intent)
{
    std::string const normalized = NormalizeBotChatMessage(text);

    if (normalized == "greeting" || normalized == "hello")
        intent = BotChatIntent::Greeting;
    else if (normalized == "farewell" || normalized == "bye")
        intent = BotChatIntent::Farewell;
    else if (normalized == "thanks" || normalized == "thank you")
        intent = BotChatIntent::Thanks;
    else if (normalized == "praise")
        intent = BotChatIntent::Praise;
    else if (normalized == "apology" || normalized == "sorry")
        intent = BotChatIntent::Apology;
    else if (normalized == "insult")
        intent = BotChatIntent::Insult;
    else if (normalized == "help" || normalized == "helprequest")
        intent = BotChatIntent::HelpRequest;
    else if (normalized == "help request")
        intent = BotChatIntent::HelpRequest;
    else if (normalized == "identity" || normalized == "identityquestion")
        intent = BotChatIntent::IdentityQuestion;
    else if (normalized == "identity question")
        intent = BotChatIntent::IdentityQuestion;
    else if (normalized == "wellbeing" || normalized == "wellbeingquestion")
        intent = BotChatIntent::WellbeingQuestion;
    else if (normalized == "wellbeing question")
        intent = BotChatIntent::WellbeingQuestion;
    else if (normalized == "agreement" || normalized == "agree")
        intent = BotChatIntent::Agreement;
    else if (normalized == "disagreement" || normalized == "disagree")
        intent = BotChatIntent::Disagreement;
    else if (normalized == "unknown")
        intent = BotChatIntent::Unknown;
    else
        return false;

    return true;
}

std::string NormalizeBotChatMessage(std::string const& text)
{
    std::string const lowered = ToLowerUtf8Safe(text);

    std::string normalized;
    normalized.reserve(lowered.size());

    bool lastWasSpace = true;
    for (unsigned char c : lowered)
    {
        if (IsAsciiSpace(c) || IsAsciiPunctuation(c))
        {
            if (!lastWasSpace)
            {
                normalized.push_back(' ');
                lastWasSpace = true;
            }

            continue;
        }

        normalized.push_back(static_cast<char>(c));
        lastWasSpace = false;
    }

    if (!normalized.empty() && normalized.back() == ' ')
        normalized.pop_back();

    return normalized;
}

BotChatIntent ParseBotChatIntent(std::string const& text)
{
    std::string const normalized = NormalizeBotChatMessage(text);
    if (normalized.empty())
        return BotChatIntent::Unknown;

    if (IsDirectInsult(normalized))
        return BotChatIntent::Insult;

    if (IsApology(normalized))
        return BotChatIntent::Apology;

    if (IsThanks(normalized))
        return BotChatIntent::Thanks;

    if (IsPraise(normalized))
        return BotChatIntent::Praise;

    if (IsGreeting(normalized))
        return BotChatIntent::Greeting;

    if (IsFarewell(normalized))
        return BotChatIntent::Farewell;

    if (IsHelpRequest(normalized))
        return BotChatIntent::HelpRequest;

    if (IsIdentityQuestion(normalized))
        return BotChatIntent::IdentityQuestion;

    if (IsWellbeingQuestion(normalized))
        return BotChatIntent::WellbeingQuestion;

    if (IsAgreement(normalized))
        return BotChatIntent::Agreement;

    if (IsDisagreement(normalized))
        return BotChatIntent::Disagreement;

    return BotChatIntent::Unknown;
}

BotResponseTone DetermineBotResponseTone(
    BotPersonality const& personality,
    BotChatIntent intent)
{
    int32 const friendliness = Trait(
        personality,
        &BotPersonality::friendliness);
    int32 const confidence = Trait(
        personality,
        &BotPersonality::confidence);
    int32 const humour = Trait(personality, &BotPersonality::humour);
    int32 const talkativeness = Trait(
        personality,
        &BotPersonality::talkativeness);

    bool const nervous =
        personality.archetype == BotPersonalityArchetype::Nervous ||
        confidence <= -35;
    bool const arrogant =
        personality.archetype == BotPersonalityArchetype::Arrogant ||
        (confidence >= 55 && friendliness <= -15);

    if (intent == BotChatIntent::Apology)
    {
        if (nervous)
            return BotResponseTone::Nervous;

        if (friendliness >= 20)
            return BotResponseTone::Warm;

        if (IsStoicPersonality(personality))
            return BotResponseTone::Stoic;

        return BotResponseTone::Neutral;
    }

    if (intent == BotChatIntent::Insult)
    {
        if (nervous)
            return BotResponseTone::Nervous;

        if (arrogant)
            return BotResponseTone::Arrogant;

        if (IsStoicPersonality(personality))
            return BotResponseTone::Stoic;

        return friendliness <= 35 ?
            BotResponseTone::Cold :
            BotResponseTone::Neutral;
    }

    if (intent == BotChatIntent::Praise && arrogant)
        return BotResponseTone::Arrogant;

    if (friendliness >= 50 && talkativeness >= 35)
        return BotResponseTone::Enthusiastic;

    if (friendliness >= 45)
        return BotResponseTone::Warm;

    if (humour >= 45 && confidence >= 30 &&
        intent != BotChatIntent::Thanks)
        return BotResponseTone::Sarcastic;

    if (nervous)
        return BotResponseTone::Nervous;

    if (arrogant)
        return BotResponseTone::Arrogant;

    if (IsStoicPersonality(personality))
        return BotResponseTone::Stoic;

    if (friendliness <= -35)
        return BotResponseTone::Cold;

    return BotResponseTone::Neutral;
}

BotResponseTone DetermineBotResponseTone(
    BotPersonality const& personality,
    BotChatIntent intent,
    BotRelationship const& relationship,
    BotRelationshipLevel relationshipLevel)
{
    BotResponseTone tone = DetermineBotResponseTone(personality, intent);

    bool const highAffinity =
        relationshipLevel == BotRelationshipLevel::Friendly ||
        relationshipLevel == BotRelationshipLevel::Trusted ||
        relationshipLevel == BotRelationshipLevel::Loyal;
    bool const lowAffinity =
        relationshipLevel == BotRelationshipLevel::Hostile ||
        relationshipLevel == BotRelationshipLevel::Disliked ||
        relationshipLevel == BotRelationshipLevel::Wary;

    if (highAffinity)
    {
        if (tone == BotResponseTone::Cold)
            tone = BotResponseTone::Neutral;
        else if (tone == BotResponseTone::Arrogant &&
                 relationship.affinity >= 400)
            tone = BotResponseTone::Neutral;
        else if (tone == BotResponseTone::Sarcastic &&
                 relationship.affinity >= 400)
            tone = BotResponseTone::Warm;
        else if (tone == BotResponseTone::Neutral &&
                 relationship.affinity >= 700)
            tone = BotResponseTone::Warm;
    }

    if (lowAffinity)
    {
        if (tone == BotResponseTone::Enthusiastic)
            tone = BotResponseTone::Neutral;
        else if (tone == BotResponseTone::Warm)
            tone = BotResponseTone::Neutral;

        if (relationship.affinity <= -300 &&
            tone == BotResponseTone::Neutral)
            tone = BotResponseTone::Cold;

        if (personality.archetype == BotPersonalityArchetype::Nervous &&
            relationship.affinity <= -300)
            tone = BotResponseTone::Nervous;
    }

    if (relationship.trust >= 300 &&
        (intent == BotChatIntent::Apology ||
            intent == BotChatIntent::HelpRequest ||
            intent == BotChatIntent::WellbeingQuestion) &&
        tone == BotResponseTone::Neutral)
        tone = BotResponseTone::Warm;

    if (relationship.trust <= -300 &&
        (intent == BotChatIntent::Apology ||
            intent == BotChatIntent::HelpRequest) &&
        tone == BotResponseTone::Warm)
        tone = BotResponseTone::Neutral;

    if (relationship.respect <= -300 &&
        (personality.archetype == BotPersonalityArchetype::Arrogant ||
            personality.archetype == BotPersonalityArchetype::Competitive) &&
        tone == BotResponseTone::Neutral)
        tone = BotResponseTone::Arrogant;

    if (relationship.respect >= 300 &&
        personality.archetype == BotPersonalityArchetype::Competitive &&
        tone == BotResponseTone::Cold)
        tone = BotResponseTone::Neutral;

    return tone;
}
