#ifndef MOD_BOT_PERSONALITY_BOT_LLM_HTTP_CLIENT_H
#define MOD_BOT_PERSONALITY_BOT_LLM_HTTP_CLIENT_H

#include "BotLlmTypes.h"

#include <string>

class BotLlmHttpClient
{
public:
    BotLlmHttpResult PostChatCompletion(
        BotLlmRequest const& request,
        std::string const& body) const;

private:
    struct ParsedUrl
    {
        std::string scheme;
        std::string host;
        std::string port;
        std::string target;
    };

    bool ParseUrl(std::string const& endpoint, ParsedUrl& parsed) const;
};

#endif
