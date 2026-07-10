#include "BotLlmResponseValidator.h"

#include "BotPersonality.h"
#include "Util.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace
{
std::string TrimAscii(std::string text)
{
    auto isSpace = [](unsigned char c)
    {
        return std::isspace(c) != 0;
    };

    auto begin = std::find_if_not(text.begin(), text.end(), isSpace);
    auto end = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
    if (begin >= end)
        return {};

    return std::string(begin, end);
}

void TrimQuotes(std::string& text)
{
    text = TrimAscii(text);
    while (text.size() >= 2 &&
        ((text.front() == '"' && text.back() == '"') ||
            (text.front() == '\'' && text.back() == '\'')))
    {
        text = TrimAscii(text.substr(1, text.size() - 2));
    }
}

bool Contains(std::string const& haystack, char const* needle)
{
    return haystack.find(needle) != std::string::npos;
}

bool LooksLikeForbiddenDisclosure(std::string const& lowered)
{
    return Contains(lowered, "as an ai") ||
        Contains(lowered, "language model") ||
        Contains(lowered, "system prompt") ||
        Contains(lowered, "these instructions") ||
        Contains(lowered, "api key") ||
        Contains(lowered, "openai") ||
        Contains(lowered, "server script");
}

bool LooksLikeCommand(std::string const& text)
{
    if (text.empty())
        return false;

    char const first = text.front();
    if (first == '.' || first == '/' || first == '#' || first == '!')
        return true;

    std::string lowered = BotPersonalityToLower(text);
    return lowered == "follow" ||
        lowered == "stay" ||
        lowered == "attack" ||
        lowered == "reset" ||
        lowered.rfind("do ", 0) == 0 ||
        lowered.rfind("debug ", 0) == 0;
}

void RemoveSpeakerPrefix(std::string& text, std::string const& botName)
{
    std::size_t const colon = text.find(':');
    if (colon == std::string::npos || colon > 32)
        return;

    std::string prefix = BotPersonalityToLower(
        TrimAscii(text.substr(0, colon)));
    std::string bot = BotPersonalityToLower(botName);
    if (prefix == bot || prefix == "bot" || prefix == "assistant")
        text = TrimAscii(text.substr(colon + 1));
}

std::vector<std::string> SplitWords(std::string const& text)
{
    std::istringstream stream(text);
    std::vector<std::string> words;
    std::string word;
    while (stream >> word)
        words.push_back(word);

    return words;
}

void TruncateUtf8Bytes(std::string& text, std::size_t maxBytes)
{
    if (text.size() <= maxBytes)
        return;

    text.resize(maxBytes);
    while (!text.empty())
    {
        std::wstring wide;
        if (Utf8toWStr(text, wide))
            return;

        text.pop_back();
    }
}
}

BotLlmValidationResult BotLlmResponseValidator::Validate(
    std::string text,
    std::string const& botName,
    uint32 maxCharacters,
    uint32 maxWords,
    bool allowMultiline,
    bool truncateLongOutput) const
{
    BotLlmValidationResult result;

    TrimQuotes(text);
    RemoveSpeakerPrefix(text, botName);
    TrimQuotes(text);

    if (text.empty())
    {
        result.error = "empty response";
        return result;
    }

    if (!allowMultiline &&
        (text.find('\n') != std::string::npos ||
            text.find('\r') != std::string::npos))
    {
        result.error = "multiline response";
        return result;
    }

    std::wstring wide;
    if (!Utf8toWStr(text, wide))
    {
        result.error = "invalid utf8";
        return result;
    }

    std::string const lowered = BotPersonalityToLower(text);
    if (LooksLikeForbiddenDisclosure(lowered))
    {
        result.error = "forbidden disclosure";
        return result;
    }

    if (LooksLikeCommand(text))
    {
        result.error = "command-looking response";
        return result;
    }

    std::vector<std::string> words = SplitWords(text);
    if (maxWords > 0 && words.size() > maxWords)
    {
        if (!truncateLongOutput)
        {
            result.error = "too many words";
            return result;
        }

        std::ostringstream out;
        for (uint32 i = 0; i < maxWords && i < words.size(); ++i)
        {
            if (i)
                out << ' ';
            out << words[i];
        }
        text = out.str();
    }

    if (maxCharacters > 0 && text.size() > maxCharacters)
    {
        if (!truncateLongOutput)
        {
            result.error = "too many characters";
            return result;
        }

        TruncateUtf8Bytes(text, maxCharacters);
    }

    text = TrimAscii(text);
    if (text.empty())
    {
        result.error = "empty after sanitization";
        return result;
    }

    result.success = true;
    result.text = std::move(text);
    return result;
}
