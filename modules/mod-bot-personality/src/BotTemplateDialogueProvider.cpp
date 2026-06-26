#include "BotTemplateDialogueProvider.h"

#include <array>
#include <string_view>

namespace
{
using TemplateList = std::array<std::string_view, 3>;

enum class RelationshipTemplateBand : uint8
{
    Negative = 0,
    Neutral,
    Positive,
    Loyal
};

struct TemplateGroup
{
    BotChatIntent intent;
    BotResponseTone tone;
    TemplateList templates;
};

struct RelationshipTemplateGroup
{
    BotChatIntent intent;
    RelationshipTemplateBand band;
    BotResponseTone tone;
    bool toneSpecific;
    TemplateList templates;
};

RelationshipTemplateBand RelationshipBandForLevel(
    BotRelationshipLevel level)
{
    switch (level)
    {
        case BotRelationshipLevel::Hostile:
        case BotRelationshipLevel::Disliked:
        case BotRelationshipLevel::Wary:
            return RelationshipTemplateBand::Negative;
        case BotRelationshipLevel::Friendly:
        case BotRelationshipLevel::Trusted:
            return RelationshipTemplateBand::Positive;
        case BotRelationshipLevel::Loyal:
            return RelationshipTemplateBand::Loyal;
        case BotRelationshipLevel::Neutral:
            return RelationshipTemplateBand::Neutral;
    }

    return RelationshipTemplateBand::Neutral;
}

RelationshipTemplateGroup const RelationshipTemplateGroups[] =
{
    {
        BotChatIntent::Greeting,
        RelationshipTemplateBand::Negative,
        BotResponseTone::Neutral,
        false,
        {
            "What do you need?",
            "I remember you. Speak plainly.",
            "Make it quick."
        }
    },
    {
        BotChatIntent::Greeting,
        RelationshipTemplateBand::Positive,
        BotResponseTone::Neutral,
        false,
        {
            "Good to see you again, {player}.",
            "There you are. Ready to continue?",
            "I was hoping you would return."
        }
    },
    {
        BotChatIntent::Greeting,
        RelationshipTemplateBand::Loyal,
        BotResponseTone::Neutral,
        false,
        {
            "I knew you would return, {player}.",
            "Ready when you are, old friend.",
            "It is good to have you here."
        }
    },
    {
        BotChatIntent::Greeting,
        RelationshipTemplateBand::Positive,
        BotResponseTone::Sarcastic,
        true,
        {
            "Back again? Good. Things were getting dull.",
            "There you are. Try not to make me admit I missed you.",
            "Good, {player}. I was almost forced to be productive alone."
        }
    },
    {
        BotChatIntent::Greeting,
        RelationshipTemplateBand::Negative,
        BotResponseTone::Sarcastic,
        true,
        {
            "Back again? How fortunate for me.",
            "You again. My day is complete.",
            "Say it before I start enjoying the silence."
        }
    },
    {
        BotChatIntent::Farewell,
        RelationshipTemplateBand::Negative,
        BotResponseTone::Neutral,
        false,
        {
            "Fine. Go.",
            "We are done here.",
            "Until you need something else."
        }
    },
    {
        BotChatIntent::Farewell,
        RelationshipTemplateBand::Positive,
        BotResponseTone::Neutral,
        false,
        {
            "Safe travels, {player}.",
            "Come back in one piece.",
            "Until next time. I will be ready."
        }
    },
    {
        BotChatIntent::Farewell,
        RelationshipTemplateBand::Loyal,
        BotResponseTone::Neutral,
        false,
        {
            "Go safely, old friend.",
            "I will be here when you return.",
            "Until we travel together again."
        }
    },
    {
        BotChatIntent::Thanks,
        RelationshipTemplateBand::Negative,
        BotResponseTone::Neutral,
        false,
        {
            "Good. Remember it.",
            "Do not waste it.",
            "Fine."
        }
    },
    {
        BotChatIntent::Thanks,
        RelationshipTemplateBand::Positive,
        BotResponseTone::Neutral,
        false,
        {
            "Any time, {player}.",
            "That is what companions do.",
            "You would do the same."
        }
    },
    {
        BotChatIntent::Thanks,
        RelationshipTemplateBand::Loyal,
        BotResponseTone::Neutral,
        false,
        {
            "Always, {player}.",
            "You never need to ask twice.",
            "At your side, as ever."
        }
    },
    {
        BotChatIntent::Praise,
        RelationshipTemplateBand::Negative,
        BotResponseTone::Neutral,
        false,
        {
            "Praise from you is unexpected.",
            "At least you noticed.",
            "Keep that judgment sharp."
        }
    },
    {
        BotChatIntent::Praise,
        RelationshipTemplateBand::Positive,
        BotResponseTone::Neutral,
        false,
        {
            "That means something coming from you.",
            "We did well together.",
            "Your eye for the work is improving."
        }
    },
    {
        BotChatIntent::Praise,
        RelationshipTemplateBand::Loyal,
        BotResponseTone::Neutral,
        false,
        {
            "From you, that matters.",
            "We have earned that together.",
            "Your confidence is well placed."
        }
    },
    {
        BotChatIntent::Apology,
        RelationshipTemplateBand::Negative,
        BotResponseTone::Neutral,
        false,
        {
            "I heard you. Prove it next time.",
            "Words are easy. Do better.",
            "Accepted for now."
        }
    },
    {
        BotChatIntent::Apology,
        RelationshipTemplateBand::Positive,
        BotResponseTone::Neutral,
        false,
        {
            "It is all right, {player}.",
            "I trust you to make it right.",
            "We recover and continue."
        }
    },
    {
        BotChatIntent::Apology,
        RelationshipTemplateBand::Loyal,
        BotResponseTone::Neutral,
        false,
        {
            "No harm between us.",
            "We have survived worse.",
            "I know your intent. We move on."
        }
    },
    {
        BotChatIntent::Insult,
        RelationshipTemplateBand::Negative,
        BotResponseTone::Neutral,
        false,
        {
            "That is enough.",
            "Speak with purpose or not at all.",
            "You are not helping your case."
        }
    },
    {
        BotChatIntent::Insult,
        RelationshipTemplateBand::Positive,
        BotResponseTone::Neutral,
        false,
        {
            "Careful, {player}. We are better than that.",
            "I will let that pass once.",
            "Say what you mean without the edge."
        }
    },
    {
        BotChatIntent::Insult,
        RelationshipTemplateBand::Loyal,
        BotResponseTone::Neutral,
        false,
        {
            "I know you. That is not like you.",
            "Take a breath. We can speak plainly.",
            "I will not answer anger with anger."
        }
    },
    {
        BotChatIntent::HelpRequest,
        RelationshipTemplateBand::Negative,
        BotResponseTone::Neutral,
        false,
        {
            "Be specific.",
            "Explain exactly what you need.",
            "I will hear the request."
        }
    },
    {
        BotChatIntent::HelpRequest,
        RelationshipTemplateBand::Positive,
        BotResponseTone::Neutral,
        false,
        {
            "Tell me what you need, {player}.",
            "I will help if I can.",
            "Say the task and we will handle it."
        }
    },
    {
        BotChatIntent::HelpRequest,
        RelationshipTemplateBand::Loyal,
        BotResponseTone::Neutral,
        false,
        {
            "Always. What do you need?",
            "Name it, and I will do what I can.",
            "You have my help."
        }
    },
    {
        BotChatIntent::IdentityQuestion,
        RelationshipTemplateBand::Negative,
        BotResponseTone::Neutral,
        false,
        {
            "I am {bot}. You know enough.",
            "{bot}. Remember it this time.",
            "You are speaking to {bot}."
        }
    },
    {
        BotChatIntent::IdentityQuestion,
        RelationshipTemplateBand::Positive,
        BotResponseTone::Neutral,
        false,
        {
            "I am {bot}. You know me well enough by now.",
            "{bot}, still here with you.",
            "You know me as {bot}, {player}."
        }
    },
    {
        BotChatIntent::IdentityQuestion,
        RelationshipTemplateBand::Loyal,
        BotResponseTone::Neutral,
        false,
        {
            "I am {bot}, your companion.",
            "{bot}. You know my name, old friend.",
            "At your side, I am {bot}."
        }
    },
    {
        BotChatIntent::WellbeingQuestion,
        RelationshipTemplateBand::Negative,
        BotResponseTone::Neutral,
        false,
        {
            "Well enough.",
            "I am functional.",
            "Do not concern yourself."
        }
    },
    {
        BotChatIntent::WellbeingQuestion,
        RelationshipTemplateBand::Positive,
        BotResponseTone::Neutral,
        false,
        {
            "Better for a familiar voice.",
            "Steady, {player}. Thank you.",
            "I am well enough to continue."
        }
    },
    {
        BotChatIntent::WellbeingQuestion,
        RelationshipTemplateBand::Loyal,
        BotResponseTone::Neutral,
        false,
        {
            "I am steady with you here.",
            "Well, old friend. Ready for more.",
            "Better knowing you are near."
        }
    }
};

TemplateGroup const TemplateGroups[] =
{
    {
        BotChatIntent::Greeting,
        BotResponseTone::Warm,
        {
            "Good to see you, {player}.",
            "Hello, {player}. Ready for another adventure?",
            "There you are. I was hoping you would stop by."
        }
    },
    {
        BotChatIntent::Greeting,
        BotResponseTone::Neutral,
        {
            "Hello.",
            "Greetings, {player}.",
            "What do you need?"
        }
    },
    {
        BotChatIntent::Greeting,
        BotResponseTone::Cold,
        {
            "You again.",
            "Make it quick.",
            "What is it?"
        }
    },
    {
        BotChatIntent::Greeting,
        BotResponseTone::Sarcastic,
        {
            "Back for another flawless plan, are we?",
            "Well, look who finally showed up.",
            "I was beginning to enjoy the silence."
        }
    },
    {
        BotChatIntent::Greeting,
        BotResponseTone::Nervous,
        {
            "Oh, hello. Is something wrong?",
            "Hello. We are not going anywhere dangerous, are we?",
            "You startled me, {player}."
        }
    },
    {
        BotChatIntent::Greeting,
        BotResponseTone::Enthusiastic,
        {
            "Hello, {player}! I am ready.",
            "There you are! This should be good.",
            "Good timing. I was eager to get moving."
        }
    },
    {
        BotChatIntent::Greeting,
        BotResponseTone::Arrogant,
        {
            "You found the right person.",
            "At last, someone sensible speaks to me.",
            "Try to keep up, {player}."
        }
    },
    {
        BotChatIntent::Greeting,
        BotResponseTone::Stoic,
        {
            "Greetings.",
            "I am ready.",
            "Speak."
        }
    },
    {
        BotChatIntent::Farewell,
        BotResponseTone::Warm,
        {
            "Safe travels, {player}.",
            "Until next time. Take care.",
            "May your road be kind."
        }
    },
    {
        BotChatIntent::Farewell,
        BotResponseTone::Neutral,
        {
            "Farewell.",
            "Until later.",
            "Very well. Goodbye."
        }
    },
    {
        BotChatIntent::Farewell,
        BotResponseTone::Cold,
        {
            "Go, then.",
            "Fine.",
            "We are done here."
        }
    },
    {
        BotChatIntent::Farewell,
        BotResponseTone::Sarcastic,
        {
            "Try not to vanish at the worst possible moment.",
            "Leaving already? How dramatic.",
            "I will treasure the quiet."
        }
    },
    {
        BotChatIntent::Farewell,
        BotResponseTone::Nervous,
        {
            "Right. Be careful out there.",
            "You are coming back, yes?",
            "I will stay alert. Mostly."
        }
    },
    {
        BotChatIntent::Farewell,
        BotResponseTone::Enthusiastic,
        {
            "Safe travels! We will do more soon.",
            "Until next time, {player}!",
            "Come back with a good story."
        }
    },
    {
        BotChatIntent::Farewell,
        BotResponseTone::Arrogant,
        {
            "Try not to need me too badly.",
            "I will manage without you.",
            "Go on. I have this handled."
        }
    },
    {
        BotChatIntent::Farewell,
        BotResponseTone::Stoic,
        {
            "Farewell.",
            "Go safely.",
            "We part."
        }
    },
    {
        BotChatIntent::Thanks,
        BotResponseTone::Warm,
        {
            "Any time, {player}.",
            "Glad to help.",
            "You are welcome."
        }
    },
    {
        BotChatIntent::Thanks,
        BotResponseTone::Neutral,
        {
            "You are welcome.",
            "Of course.",
            "Noted."
        }
    },
    {
        BotChatIntent::Thanks,
        BotResponseTone::Cold,
        {
            "Do not make a habit of needing it.",
            "Fine.",
            "It was necessary."
        }
    },
    {
        BotChatIntent::Thanks,
        BotResponseTone::Sarcastic,
        {
            "Mark the day. Gratitude.",
            "Careful, praise like that may spoil me.",
            "I suppose miracles do happen."
        }
    },
    {
        BotChatIntent::Thanks,
        BotResponseTone::Nervous,
        {
            "Oh. You are welcome.",
            "I did all right, then?",
            "Glad that helped."
        }
    },
    {
        BotChatIntent::Thanks,
        BotResponseTone::Enthusiastic,
        {
            "Always happy to help!",
            "That is what companions are for.",
            "Good. Let us keep going."
        }
    },
    {
        BotChatIntent::Thanks,
        BotResponseTone::Arrogant,
        {
            "Naturally.",
            "I do have my uses.",
            "Credit where it is due."
        }
    },
    {
        BotChatIntent::Thanks,
        BotResponseTone::Stoic,
        {
            "Accepted.",
            "You are welcome.",
            "It is done."
        }
    },
    {
        BotChatIntent::Praise,
        BotResponseTone::Warm,
        {
            "That means something coming from you.",
            "Thank you, {player}.",
            "We did well together."
        }
    },
    {
        BotChatIntent::Praise,
        BotResponseTone::Neutral,
        {
            "Thank you.",
            "A clean result.",
            "The work was done."
        }
    },
    {
        BotChatIntent::Praise,
        BotResponseTone::Cold,
        {
            "I know.",
            "Do not sound surprised.",
            "Expected."
        }
    },
    {
        BotChatIntent::Praise,
        BotResponseTone::Sarcastic,
        {
            "Careful, I may start expecting applause.",
            "High praise. I will try to survive it.",
            "Yes, that was almost competent of us."
        }
    },
    {
        BotChatIntent::Praise,
        BotResponseTone::Nervous,
        {
            "Really? Good.",
            "I was worried I had missed something.",
            "Thank you. I will try to keep that up."
        }
    },
    {
        BotChatIntent::Praise,
        BotResponseTone::Enthusiastic,
        {
            "We were excellent!",
            "Thank you, {player}! That felt good.",
            "Then let us do it again."
        }
    },
    {
        BotChatIntent::Praise,
        BotResponseTone::Arrogant,
        {
            "A fair assessment.",
            "At least someone noticed.",
            "I make it look easy."
        }
    },
    {
        BotChatIntent::Praise,
        BotResponseTone::Stoic,
        {
            "Acknowledged.",
            "The result was acceptable.",
            "We proceed."
        }
    },
    {
        BotChatIntent::Apology,
        BotResponseTone::Warm,
        {
            "It is all right, {player}.",
            "No harm done.",
            "We recover and move on."
        }
    },
    {
        BotChatIntent::Apology,
        BotResponseTone::Neutral,
        {
            "Accepted.",
            "Very well.",
            "Let us move on."
        }
    },
    {
        BotChatIntent::Apology,
        BotResponseTone::Cold,
        {
            "Do better next time.",
            "Accepted. Barely.",
            "See that it improves."
        }
    },
    {
        BotChatIntent::Apology,
        BotResponseTone::Sarcastic,
        {
            "An apology. Rare treasure.",
            "Good. We can pretend that was planned.",
            "Fine, I will withhold the speech."
        }
    },
    {
        BotChatIntent::Apology,
        BotResponseTone::Nervous,
        {
            "It is fine. I think.",
            "All right. Let us just be careful.",
            "No need to worry me further."
        }
    },
    {
        BotChatIntent::Apology,
        BotResponseTone::Enthusiastic,
        {
            "No trouble. We are still standing!",
            "All forgiven. Forward!",
            "It happens. We will make it right."
        }
    },
    {
        BotChatIntent::Apology,
        BotResponseTone::Arrogant,
        {
            "Accepted. Try to learn from it.",
            "At least you noticed.",
            "Good. I prefer competence."
        }
    },
    {
        BotChatIntent::Apology,
        BotResponseTone::Stoic,
        {
            "Accepted.",
            "Correct the error.",
            "We continue."
        }
    },
    {
        BotChatIntent::Insult,
        BotResponseTone::Warm,
        {
            "That is uncalled for.",
            "I will listen when you speak plainly.",
            "We can do better than that."
        }
    },
    {
        BotChatIntent::Insult,
        BotResponseTone::Neutral,
        {
            "That does not help.",
            "Say what you need.",
            "Keep it useful."
        }
    },
    {
        BotChatIntent::Insult,
        BotResponseTone::Cold,
        {
            "Mind your tongue.",
            "Get to the point.",
            "Enough."
        }
    },
    {
        BotChatIntent::Insult,
        BotResponseTone::Sarcastic,
        {
            "A cutting analysis. Truly.",
            "Save that fire for our enemies.",
            "Brilliant. Now try being useful."
        }
    },
    {
        BotChatIntent::Insult,
        BotResponseTone::Nervous,
        {
            "That was not necessary.",
            "I heard you. Calm down.",
            "Please keep your voice steady."
        }
    },
    {
        BotChatIntent::Insult,
        BotResponseTone::Enthusiastic,
        {
            "Let us turn that energy toward the fight.",
            "I would rather prove you wrong.",
            "Sharp words. Sharper focus, please."
        }
    },
    {
        BotChatIntent::Insult,
        BotResponseTone::Arrogant,
        {
            "Bold words from your side of the plan.",
            "When you are done, I will continue succeeding.",
            "Try criticism after results."
        }
    },
    {
        BotChatIntent::Insult,
        BotResponseTone::Stoic,
        {
            "Noted.",
            "Discipline yourself.",
            "The insult changes nothing."
        }
    },
    {
        BotChatIntent::HelpRequest,
        BotResponseTone::Warm,
        {
            "Tell me what you need.",
            "I will help if I can.",
            "What is the problem?"
        }
    },
    {
        BotChatIntent::HelpRequest,
        BotResponseTone::Neutral,
        {
            "Give me a clear instruction.",
            "What do you need?",
            "State the problem."
        }
    },
    {
        BotChatIntent::HelpRequest,
        BotResponseTone::Cold,
        {
            "Be specific.",
            "Ask clearly.",
            "What exactly do you want?"
        }
    },
    {
        BotChatIntent::HelpRequest,
        BotResponseTone::Sarcastic,
        {
            "A clear request would be a fine start.",
            "I can help. I cannot read minds.",
            "Tell me the problem before it finds us."
        }
    },
    {
        BotChatIntent::HelpRequest,
        BotResponseTone::Nervous,
        {
            "I can try. What is wrong?",
            "Tell me carefully, please.",
            "What do you need me to understand?"
        }
    },
    {
        BotChatIntent::HelpRequest,
        BotResponseTone::Enthusiastic,
        {
            "Of course. Tell me what you need!",
            "I am ready. What is the task?",
            "Say it clearly and I will do my best."
        }
    },
    {
        BotChatIntent::HelpRequest,
        BotResponseTone::Arrogant,
        {
            "Good choice. Explain the problem.",
            "I can help if the request is clear.",
            "State it plainly."
        }
    },
    {
        BotChatIntent::HelpRequest,
        BotResponseTone::Stoic,
        {
            "State your need.",
            "Give the instruction.",
            "I will assess it."
        }
    },
    {
        BotChatIntent::IdentityQuestion,
        BotResponseTone::Warm,
        {
            "I am {bot}. I travel with good company.",
            "I am {bot}, and I am glad you asked.",
            "You know me as {bot}. That is enough for now."
        }
    },
    {
        BotChatIntent::IdentityQuestion,
        BotResponseTone::Neutral,
        {
            "I am {bot}.",
            "My name is {bot}.",
            "I am the one you called, {player}."
        }
    },
    {
        BotChatIntent::IdentityQuestion,
        BotResponseTone::Cold,
        {
            "I am {bot}. Remember it.",
            "Names matter. Mine is {bot}.",
            "You are speaking to {bot}."
        }
    },
    {
        BotChatIntent::IdentityQuestion,
        BotResponseTone::Sarcastic,
        {
            "I am {bot}. The mystery deepens.",
            "Still {bot}, last I checked.",
            "I answer to {bot}, usually with patience."
        }
    },
    {
        BotChatIntent::IdentityQuestion,
        BotResponseTone::Nervous,
        {
            "I am {bot}. Did I forget to say?",
            "My name is {bot}. Is that all right?",
            "I am {bot}. I hope that helps."
        }
    },
    {
        BotChatIntent::IdentityQuestion,
        BotResponseTone::Enthusiastic,
        {
            "I am {bot}, ready for whatever comes!",
            "Name is {bot}. Good to be asked!",
            "{bot}, at your side."
        }
    },
    {
        BotChatIntent::IdentityQuestion,
        BotResponseTone::Arrogant,
        {
            "I am {bot}. A useful name to remember.",
            "You are speaking with {bot}.",
            "{bot}. Try not to forget."
        }
    },
    {
        BotChatIntent::IdentityQuestion,
        BotResponseTone::Stoic,
        {
            "I am {bot}.",
            "Name: {bot}.",
            "{bot}."
        }
    },
    {
        BotChatIntent::WellbeingQuestion,
        BotResponseTone::Warm,
        {
            "I am well enough. Thank you.",
            "Better for hearing a friendly voice.",
            "I am steady, {player}."
        }
    },
    {
        BotChatIntent::WellbeingQuestion,
        BotResponseTone::Neutral,
        {
            "I am fine.",
            "Ready enough.",
            "No complaints worth naming."
        }
    },
    {
        BotChatIntent::WellbeingQuestion,
        BotResponseTone::Cold,
        {
            "I am functional.",
            "Well enough.",
            "Do not worry about me."
        }
    },
    {
        BotChatIntent::WellbeingQuestion,
        BotResponseTone::Sarcastic,
        {
            "Still breathing. A promising sign.",
            "Fine, despite our best efforts.",
            "I have had worse days. Recently."
        }
    },
    {
        BotChatIntent::WellbeingQuestion,
        BotResponseTone::Nervous,
        {
            "I think I am all right.",
            "A little tense, but ready.",
            "Fine. Mostly fine."
        }
    },
    {
        BotChatIntent::WellbeingQuestion,
        BotResponseTone::Enthusiastic,
        {
            "Doing well and ready to go!",
            "I feel sharp today.",
            "Good enough to face trouble."
        }
    },
    {
        BotChatIntent::WellbeingQuestion,
        BotResponseTone::Arrogant,
        {
            "Excellent, naturally.",
            "In better form than most.",
            "Ready, as expected."
        }
    },
    {
        BotChatIntent::WellbeingQuestion,
        BotResponseTone::Stoic,
        {
            "Stable.",
            "I endure.",
            "Ready."
        }
    },
    {
        BotChatIntent::Agreement,
        BotResponseTone::Warm,
        {
            "Good. We agree.",
            "Then we are of one mind.",
            "I am glad we see it the same way."
        }
    },
    {
        BotChatIntent::Agreement,
        BotResponseTone::Neutral,
        {
            "Agreed.",
            "Very well.",
            "So be it."
        }
    },
    {
        BotChatIntent::Agreement,
        BotResponseTone::Cold,
        {
            "Fine.",
            "At least that is settled.",
            "Good enough."
        }
    },
    {
        BotChatIntent::Agreement,
        BotResponseTone::Sarcastic,
        {
            "Agreement. How refreshing.",
            "A rare moment of clarity.",
            "Then the stars have aligned."
        }
    },
    {
        BotChatIntent::Agreement,
        BotResponseTone::Nervous,
        {
            "Good. That helps.",
            "All right, then.",
            "I hoped you would agree."
        }
    },
    {
        BotChatIntent::Agreement,
        BotResponseTone::Enthusiastic,
        {
            "Good! Let us act on it.",
            "Agreed. Forward!",
            "Then we move together."
        }
    },
    {
        BotChatIntent::Agreement,
        BotResponseTone::Arrogant,
        {
            "Wise of you.",
            "Correct.",
            "You are learning."
        }
    },
    {
        BotChatIntent::Agreement,
        BotResponseTone::Stoic,
        {
            "Agreed.",
            "Confirmed.",
            "Proceed."
        }
    },
    {
        BotChatIntent::Disagreement,
        BotResponseTone::Warm,
        {
            "Then tell me your concern.",
            "All right. What troubles you?",
            "We can think it through."
        }
    },
    {
        BotChatIntent::Disagreement,
        BotResponseTone::Neutral,
        {
            "Explain.",
            "What would you change?",
            "Then give me another plan."
        }
    },
    {
        BotChatIntent::Disagreement,
        BotResponseTone::Cold,
        {
            "Then be useful and explain.",
            "Objection noted.",
            "Make your point."
        }
    },
    {
        BotChatIntent::Disagreement,
        BotResponseTone::Sarcastic,
        {
            "A disagreement. We are rich with surprises.",
            "Then dazzle me with the better plan.",
            "By all means, improve the situation."
        }
    },
    {
        BotChatIntent::Disagreement,
        BotResponseTone::Nervous,
        {
            "Oh. What did I miss?",
            "Then maybe we should slow down.",
            "Tell me before this gets worse."
        }
    },
    {
        BotChatIntent::Disagreement,
        BotResponseTone::Enthusiastic,
        {
            "Good. A better idea may help.",
            "Then let us fix the plan.",
            "Say the better route."
        }
    },
    {
        BotChatIntent::Disagreement,
        BotResponseTone::Arrogant,
        {
            "Convince me.",
            "You may try to improve it.",
            "This should be interesting."
        }
    },
    {
        BotChatIntent::Disagreement,
        BotResponseTone::Stoic,
        {
            "Explain.",
            "State the objection.",
            "I will hear it."
        }
    },
    {
        BotChatIntent::Unknown,
        BotResponseTone::Warm,
        {
            "I am listening.",
            "Go on.",
            "What is on your mind?"
        }
    },
    {
        BotChatIntent::Unknown,
        BotResponseTone::Neutral,
        {
            "I am not sure what you mean.",
            "Could you be more specific?",
            "What are you asking?"
        }
    },
    {
        BotChatIntent::Unknown,
        BotResponseTone::Cold,
        {
            "Be specific.",
            "That means nothing to me.",
            "Get to the point."
        }
    },
    {
        BotChatIntent::Unknown,
        BotResponseTone::Sarcastic,
        {
            "That was almost a sentence.",
            "Try that again with a point.",
            "I await the useful part."
        }
    },
    {
        BotChatIntent::Unknown,
        BotResponseTone::Nervous,
        {
            "I am not sure I follow.",
            "Could you say that another way?",
            "I do not understand yet."
        }
    },
    {
        BotChatIntent::Unknown,
        BotResponseTone::Enthusiastic,
        {
            "I am listening. Give me the details!",
            "Say more and I will try to follow.",
            "Tell me what you mean."
        }
    },
    {
        BotChatIntent::Unknown,
        BotResponseTone::Arrogant,
        {
            "Make sense, then try again.",
            "That requires more clarity.",
            "Use plain words."
        }
    },
    {
        BotChatIntent::Unknown,
        BotResponseTone::Stoic,
        {
            "Clarify.",
            "I do not understand.",
            "Be precise."
        }
    }
};

TemplateGroup const* FindGroup(BotChatIntent intent, BotResponseTone tone)
{
    for (TemplateGroup const& group : TemplateGroups)
    {
        if (group.intent == intent && group.tone == tone)
            return &group;
    }

    return nullptr;
}

RelationshipTemplateGroup const* FindRelationshipGroup(
    BotChatIntent intent,
    BotRelationshipLevel level,
    BotResponseTone tone,
    bool toneSpecific)
{
    RelationshipTemplateBand const band = RelationshipBandForLevel(level);

    for (RelationshipTemplateGroup const& group : RelationshipTemplateGroups)
    {
        if (group.intent != intent || group.band != band ||
            group.toneSpecific != toneSpecific)
            continue;

        if (!toneSpecific || group.tone == tone)
            return &group;
    }

    return nullptr;
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

std::string RenderTemplate(
    std::string_view text,
    BotDialogueContext const& context)
{
    std::string rendered(text);
    ReplaceAll(rendered, "{player}", context.playerName);
    ReplaceAll(rendered, "{bot}", context.botName);
    return rendered;
}
}

std::optional<std::string> BotTemplateDialogueProvider::GenerateResponse(
    BotDialogueContext const& context)
{
    TemplateList const* templates = nullptr;

    if (context.hasExistingRelationship)
    {
        if (RelationshipTemplateGroup const* relationshipGroup =
                FindRelationshipGroup(
                    context.intent,
                    context.relationshipLevel,
                    context.tone,
                    true))
            templates = &relationshipGroup->templates;

        if (!templates)
        {
            if (RelationshipTemplateGroup const* relationshipGroup =
                    FindRelationshipGroup(
                        context.intent,
                        context.relationshipLevel,
                        context.tone,
                        false))
                templates = &relationshipGroup->templates;
        }
    }

    TemplateGroup const* group = nullptr;
    if (!templates)
    {
        group = FindGroup(context.intent, context.tone);
        if (group)
            templates = &group->templates;
    }

    if (!templates)
    {
        group = FindGroup(context.intent, BotResponseTone::Neutral);
        if (group)
            templates = &group->templates;
    }

    if (!templates)
    {
        group = FindGroup(BotChatIntent::Unknown, BotResponseTone::Neutral);
        if (group)
            templates = &group->templates;
    }

    if (!templates)
        return std::nullopt;

    std::size_t index = context.selectionSeed % templates->size();
    for (std::size_t offset = 0; offset < templates->size(); ++offset)
    {
        std::size_t const candidate =
            (index + offset) % templates->size();
        std::string response = RenderTemplate(
            (*templates)[candidate],
            context);

        if (response.empty())
            continue;

        if (templates->size() > 1 &&
            !context.previousResponse.empty() &&
            response == context.previousResponse)
            continue;

        return response;
    }

    return RenderTemplate((*templates)[index], context);
}
