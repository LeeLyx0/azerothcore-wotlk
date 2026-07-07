#include "BotPersonalityMgr.h"

#include "BotDialogueMgr.h"
#include "BotGameplayTracker.h"
#include "BotRelationshipMgr.h"
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
                  WORLDHOOK_ON_UPDATE,
                  WORLDHOOK_ON_SHUTDOWN
              })
    {
    }

    void OnBeforeWorldInitialized() override
    {
        sBotPersonalityMgr.LoadConfig(false);
        sBotDialogueMgr.LoadConfig(false);
        sBotRelationshipMgr.LoadConfig(false);
        sBotGameplayTracker.LoadConfig(false);
    }

    void OnAfterConfigLoad(bool reload) override
    {
        sBotPersonalityMgr.LoadConfig(reload);
        sBotDialogueMgr.LoadConfig(reload);
        sBotRelationshipMgr.LoadConfig(reload);
        sBotGameplayTracker.LoadConfig(reload);
    }

    void OnUpdate(uint32 diff) override
    {
        sBotDialogueMgr.Update(diff);
        sBotRelationshipMgr.Update(diff);
        sBotGameplayTracker.Update(diff);
    }

    void OnShutdown() override
    {
        sBotRelationshipMgr.OnShutdown();
    }
};

class BotPersonalityPlayerScript : public PlayerScript
{
public:
    BotPersonalityPlayerScript()
        : PlayerScript(
              "BotPersonalityPlayerScript",
              {
                  PLAYERHOOK_ON_LOGIN,
                  PLAYERHOOK_ON_LOGOUT
              })
    {
    }

    void OnPlayerLogin(Player* player) override
    {
        sBotPersonalityMgr.QueueLoginGeneration(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        if (player)
            sBotRelationshipMgr.SaveRelationshipsForPlayer(player->GetGUID());
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
        if (!player)
            return;

        sBotPersonalityMgr.RemoveQueuedLoginGeneration(player->GetGUID());
        sBotRelationshipMgr.SaveRelationshipsForPlayer(player->GetGUID());
    }
};

void AddBotPersonalityChatScripts();
void AddBotPersonalityGameplayScripts();
void AddBotPersonalityCommands();

void AddBotPersonalityScripts()
{
    new BotPersonalityWorldScript();
    new BotPersonalityPlayerScript();
    new BotPersonalityPlayerbotScript();
    AddBotPersonalityChatScripts();
    AddBotPersonalityGameplayScripts();
    AddBotPersonalityCommands();
}
