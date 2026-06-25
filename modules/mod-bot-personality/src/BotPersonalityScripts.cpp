#include "BotPersonalityMgr.h"

#include "BotDialogueMgr.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "WorldScript.h"

class BotPersonalityWorldScript : public WorldScript
{
public:
    BotPersonalityWorldScript()
        : WorldScript(
              "BotPersonalityWorldScript",
              {
                  WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED,
                  WORLDHOOK_ON_AFTER_CONFIG_LOAD,
                  WORLDHOOK_ON_UPDATE
              })
    {
    }

    void OnBeforeWorldInitialized() override
    {
        sBotPersonalityMgr.LoadConfig(false);
        sBotDialogueMgr.LoadConfig(false);
    }

    void OnAfterConfigLoad(bool reload) override
    {
        sBotPersonalityMgr.LoadConfig(reload);
        sBotDialogueMgr.LoadConfig(reload);
    }

    void OnUpdate(uint32 diff) override
    {
        sBotDialogueMgr.Update(diff);
    }
};

class BotPersonalityPlayerScript : public PlayerScript
{
public:
    BotPersonalityPlayerScript()
        : PlayerScript(
              "BotPersonalityPlayerScript",
              {
                  PLAYERHOOK_ON_LOGIN
              })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        sBotPersonalityMgr.QueueLoginGeneration(player);
    }
};

class BotPersonalityPlayerbotScript : public PlayerbotScript
{
public:
    BotPersonalityPlayerbotScript()
        : PlayerbotScript("BotPersonalityPlayerbotScript")
    {
    }

    void OnPlayerbotUpdateSessions(Player* player) override
    {
        sBotPersonalityMgr.ProcessQueuedLoginGeneration(player);
    }

    void OnPlayerbotLogout(Player* player) override
    {
        if (player)
            sBotPersonalityMgr.RemoveQueuedLoginGeneration(player->GetGUID());
    }
};

void AddBotPersonalityChatScripts();
void AddBotPersonalityCommands();

void AddBotPersonalityScripts()
{
    new BotPersonalityWorldScript();
    new BotPersonalityPlayerScript();
    new BotPersonalityPlayerbotScript();
    AddBotPersonalityChatScripts();
    AddBotPersonalityCommands();
}
