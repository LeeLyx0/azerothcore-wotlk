#ifndef MOD_BOT_PERSONALITY_BOT_PERSONALITY_MGR_H
#define MOD_BOT_PERSONALITY_BOT_PERSONALITY_MGR_H

#include "BotPersonality.h"
#include "ObjectGuid.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

class Player;

class BotPersonalityMgr
{
public:
    static BotPersonalityMgr& instance()
    {
        static BotPersonalityMgr instance;
        return instance;
    }

    void LoadConfig(bool reload);

    bool IsEnabled() const { return _enabled; }
    bool IsGenerateOnLoginEnabled() const { return _generateOnLogin; }
    bool IsPlayerbot(Player const* player) const;

    BotPersonality const* GetOrCreatePersonality(Player* bot);
    BotPersonality const* GetCachedPersonality(uint32 botGuid) const;

    bool ReloadPersonality(Player* bot);
    BotPersonality const* RegeneratePersonality(Player* bot);

    bool SetTrait(Player* bot, std::string const& traitName, int32 value);
    bool SetArchetype(
        Player* bot,
        BotPersonalityArchetype archetype,
        bool resetTraits);

    void QueueLoginGeneration(Player* player);
    void ProcessQueuedLoginGeneration(Player* player);
    void RemoveQueuedLoginGeneration(ObjectGuid const& guid);

    void ClearCache();

private:
    enum class LoadResult
    {
        Error,
        NotFound,
        Loaded
    };

    BotPersonalityMgr() = default;
    ~BotPersonalityMgr() = default;

    BotPersonalityMgr(BotPersonalityMgr const&) = delete;
    BotPersonalityMgr& operator=(BotPersonalityMgr const&) = delete;

    LoadResult LoadPersonality(
        uint32 botGuid,
        BotPersonality& personality,
        bool& corrected);
    void SavePersonality(BotPersonality const& personality);
    void DeletePersonality(uint32 botGuid);

    BotPersonality GenerateForBot(Player* bot);
    BotPersonality GenerateForBot(
        Player* bot,
        BotPersonalityArchetype archetype);

    bool ValidateLoadedPersonality(BotPersonality& personality);

    bool _enabled = true;
    bool _generateOnLogin = true;
    bool _saveGenerated = true;
    bool _debugLogging = false;
    int32 _variation = 15;

    std::unordered_map<uint32, BotPersonality> _cache;
    std::unordered_set<ObjectGuid> _pendingLoginGenerations;
};

#define sBotPersonalityMgr BotPersonalityMgr::instance()

#endif
