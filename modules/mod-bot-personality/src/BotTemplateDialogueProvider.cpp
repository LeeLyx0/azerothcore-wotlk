#include "BotTemplateDialogueProvider.h"

#include <array>
#include <string_view>

namespace
{
using TemplateList = std::array<std::string_view, 3>;

struct TemplateGroup
{
    BotChatIntent intent;
    BotResponseTone tone;
    TemplateList templates;
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
    TemplateGroup const* group = FindGroup(context.intent, context.tone);
    if (!group)
        group = FindGroup(context.intent, BotResponseTone::Neutral);

    if (!group)
        group = FindGroup(BotChatIntent::Unknown, BotResponseTone::Neutral);

    if (!group)
        return std::nullopt;

    std::size_t index = context.selectionSeed % group->templates.size();
    for (std::size_t offset = 0; offset < group->templates.size(); ++offset)
    {
        std::size_t const candidate =
            (index + offset) % group->templates.size();
        std::string response = RenderTemplate(
            group->templates[candidate],
            context);

        if (response.empty())
            continue;

        if (group->templates.size() > 1 &&
            !context.previousResponse.empty() &&
            response == context.previousResponse)
            continue;

        return response;
    }

    return RenderTemplate(group->templates[index], context);
}
