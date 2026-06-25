#include "BotDialogueMgr.h"

#include "PlayerScript.h"
#include "SharedDefines.h"

#include <string>

class BotPersonalityChatScript : public PlayerScript
{
public:
    BotPersonalityChatScript()
        : PlayerScript(
              "BotPersonalityChatScript",
              {
                  PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT
              })
    {
    }

    bool OnPlayerCanUseChat(
        Player* player,
        uint32 type,
        uint32 language,
        std::string& message,
        Player* receiver) override
    {
        if (type == CHAT_MSG_WHISPER)
        {
            sBotDialogueMgr.HandleIncomingWhisper(
                player,
                receiver,
                language,
                message);
        }

        return true;
    }
};

void AddBotPersonalityChatScripts()
{
    new BotPersonalityChatScript();
}
