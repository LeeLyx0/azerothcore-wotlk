#include "BotPersonalityMgr.h"

#include "Config.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "Player.h"
#include "QueryResult.h"
#include "Util.h"
#include "WorldSession.h"

#ifdef MOD_PLAYERBOTS
#include "Playerbots.h"
#endif

#include <algorithm>

namespace
{
constexpr int32 BOT_PERSONALITY_MIN_TRAIT = -100;
constexpr int32 BOT_PERSONALITY_MAX_TRAIT = 100;
constexpr int32 BOT_PERSONALITY_MIN_VARIATION = 0;
constexpr int32 BOT_PERSONALITY_MAX_VARIATION = 40;
constexpr int32 BOT_PERSONALITY_MAX_SPEECH_STYLE = 10;

int8 ClampTraitForStorage(int32 value)
{
    int32 const clamped = std::clamp(
        value,
        BOT_PERSONALITY_MIN_TRAIT,
        BOT_PERSONALITY_MAX_TRAIT);
    return static_cast<int8>(clamped);
}

uint8 ClampSpeechStyleForStorage(int32 value)
{
    return static_cast<uint8>(
        std::clamp(value, 0, BOT_PERSONALITY_MAX_SPEECH_STYLE));
}

int32 TraitToInt(int8 value)
{
    return static_cast<int32>(value);
}

int8 LoadTrait(
    Field const& field,
    char const* name,
    uint32 botGuid,
    bool& corrected)
{
    int32 const rawValue = field.Get<int32>();
    int8 const clampedValue = ClampTraitForStorage(rawValue);

    if (rawValue != static_cast<int32>(clampedValue))
    {
        corrected = true;
        LOG_ERROR(
            "module.botpersonality",
            "Invalid {} value {} loaded for bot {}, clamped to {}",
            name,
            rawValue,
            botGuid,
            static_cast<int32>(clampedValue));
    }

    return clampedValue;
}

uint8 LoadSpeechStyle(Field const& field, uint32 botGuid, bool& corrected)
{
    int32 const rawValue = field.Get<int32>();
    uint8 const clampedValue = ClampSpeechStyleForStorage(rawValue);

    if (rawValue != static_cast<int32>(clampedValue))
    {
        corrected = true;
        LOG_ERROR(
            "module.botpersonality",
            "Invalid speech style {} loaded for bot {}, clamped to {}",
            rawValue,
            botGuid,
            static_cast<uint32>(clampedValue));
    }

    return clampedValue;
}

}

void BotPersonalityMgr::LoadConfig(bool reload)
{
    _enabled = sConfigMgr->GetOption<bool>("BotPersonality.Enable", true);
    _generateOnLogin = sConfigMgr->GetOption<bool>(
        "BotPersonality.GenerateOnLogin", true);
    _saveGenerated = sConfigMgr->GetOption<bool>(
        "BotPersonality.SaveGenerated", true);
    _debugLogging = sConfigMgr->GetOption<bool>(
        "BotPersonality.DebugLogging", false);

    int32 configuredVariation = sConfigMgr->GetOption<int32>(
        "BotPersonality.Variation", 15);
    _variation = std::clamp(
        configuredVariation,
        BOT_PERSONALITY_MIN_VARIATION,
        BOT_PERSONALITY_MAX_VARIATION);

    if (configuredVariation != _variation)
    {
        LOG_WARN(
            "module.botpersonality",
            "BotPersonality.Variation value {} is invalid, clamped to {}",
            configuredVariation,
            _variation);
    }

    if (!_enabled)
        ClearCache();

    LOG_INFO(
        "server.loading",
        "Bot Personality module {}{}",
        _enabled ? "enabled" : "disabled",
        reload ? " after config reload" : "");
}

bool BotPersonalityMgr::IsPlayerbot(Player const* player) const
{
    if (!player)
        return false;

#ifdef MOD_PLAYERBOTS
    PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(
        const_cast<Player*>(player));
    return botAI && !botAI->IsRealPlayer();
#else
    return false;
#endif
}

BotPersonality const* BotPersonalityMgr::GetOrCreatePersonality(Player* bot)
{
    if (!_enabled)
        return nullptr;

    if (!IsPlayerbot(bot))
        return nullptr;

    uint32 const botGuid = bot->GetGUID().GetCounter();
    if (BotPersonality const* cached = GetCachedPersonality(botGuid))
        return cached;

    BotPersonality personality;
    bool corrected = false;
    LoadResult const result = LoadPersonality(botGuid, personality, corrected);
    if (result == LoadResult::Loaded)
    {
        bool const changed = ValidateLoadedPersonality(personality);
        _cache[botGuid] = personality;

        if (changed || corrected)
            SavePersonality(personality);

        if (_debugLogging)
        {
            LOG_DEBUG(
                "module.botpersonality",
                "Loaded personality for bot {}",
                botGuid);
        }

        return &_cache[botGuid];
    }

    personality = GenerateForBot(bot);
    _cache[botGuid] = personality;

    if (_saveGenerated)
        SavePersonality(personality);

    LOG_INFO(
        "module.botpersonality",
        "Generated {} personality for bot {}",
        BotPersonalityArchetypeToString(personality.archetype),
        botGuid);

    return &_cache[botGuid];
}

BotPersonality const* BotPersonalityMgr::GetCachedPersonality(
    uint32 botGuid) const
{
    auto const itr = _cache.find(botGuid);
    if (itr == _cache.end())
        return nullptr;

    return &itr->second;
}

bool BotPersonalityMgr::ReloadPersonality(Player* bot)
{
    if (!_enabled || !IsPlayerbot(bot))
        return false;

    uint32 const botGuid = bot->GetGUID().GetCounter();
    _cache.erase(botGuid);

    BotPersonality personality;
    bool corrected = false;
    if (LoadPersonality(botGuid, personality, corrected) != LoadResult::Loaded)
        return false;

    bool const changed = ValidateLoadedPersonality(personality);
    _cache[botGuid] = personality;

    if (changed || corrected)
        SavePersonality(personality);

    LOG_INFO(
        "module.botpersonality",
        "Reloaded personality for bot {}",
        botGuid);
    return true;
}

BotPersonality const* BotPersonalityMgr::RegeneratePersonality(Player* bot)
{
    if (!_enabled || !IsPlayerbot(bot))
        return nullptr;

    uint32 const botGuid = bot->GetGUID().GetCounter();
    DeletePersonality(botGuid);
    _cache.erase(botGuid);

    BotPersonality personality = GenerateForBot(bot);
    SavePersonality(personality);
    _cache[botGuid] = personality;

    LOG_INFO(
        "module.botpersonality",
        "Regenerated {} personality for bot {}",
        BotPersonalityArchetypeToString(personality.archetype),
        botGuid);

    return &_cache[botGuid];
}

bool BotPersonalityMgr::SetTrait(
    Player* bot,
    std::string const& traitName,
    int32 value)
{
    BotPersonality const* current = GetOrCreatePersonality(bot);
    if (!current)
        return false;

    uint32 const botGuid = current->botGuid;
    BotPersonality personality = *current;
    std::string normalizedTrait = BotPersonalityToLower(traitName);

    if (normalizedTrait == "friendliness")
        personality.friendliness = ClampTraitForStorage(value);
    else if (normalizedTrait == "confidence")
        personality.confidence = ClampTraitForStorage(value);
    else if (normalizedTrait == "patience")
        personality.patience = ClampTraitForStorage(value);
    else if (normalizedTrait == "humour")
        personality.humour = ClampTraitForStorage(value);
    else if (normalizedTrait == "competitiveness")
        personality.competitiveness = ClampTraitForStorage(value);
    else if (normalizedTrait == "greed")
        personality.greed = ClampTraitForStorage(value);
    else if (normalizedTrait == "bravery")
        personality.bravery = ClampTraitForStorage(value);
    else if (normalizedTrait == "talkativeness")
        personality.talkativeness = ClampTraitForStorage(value);
    else if (normalizedTrait == "speechstyle" ||
             normalizedTrait == "speech_style")
        personality.speechStyle = ClampSpeechStyleForStorage(value);
    else
        return false;

    ClampPersonality(personality);
    SavePersonality(personality);
    _cache[botGuid] = personality;

    return true;
}

bool BotPersonalityMgr::SetArchetype(
    Player* bot,
    BotPersonalityArchetype archetype,
    bool resetTraits)
{
    BotPersonality const* current = GetOrCreatePersonality(bot);
    if (!current)
        return false;

    uint32 const botGuid = current->botGuid;
    BotPersonality personality = *current;
    personality.archetype = archetype;

    if (resetTraits)
    {
        personality = GenerateForBot(bot, archetype);
        personality.archetype = archetype;
    }

    ClampPersonality(personality);
    SavePersonality(personality);
    _cache[botGuid] = personality;

    LOG_INFO(
        "module.botpersonality",
        "Set personality archetype for bot {} to {}{}",
        botGuid,
        BotPersonalityArchetypeToString(archetype),
        resetTraits ? " with trait reset" : "");

    return true;
}

void BotPersonalityMgr::QueueLoginGeneration(Player* player)
{
    if (!_enabled || !_generateOnLogin || !player || !player->GetSession() ||
        !player->GetSession()->IsBot())
        return;

    if (IsPlayerbot(player))
    {
        GetOrCreatePersonality(player);
        return;
    }

    _pendingLoginGenerations.insert(player->GetGUID());
}

void BotPersonalityMgr::ProcessQueuedLoginGeneration(Player* player)
{
    if (!_enabled || !_generateOnLogin || !player)
        return;

    auto const itr = _pendingLoginGenerations.find(player->GetGUID());
    if (itr == _pendingLoginGenerations.end())
        return;

    if (!IsPlayerbot(player))
        return;

    _pendingLoginGenerations.erase(itr);
    GetOrCreatePersonality(player);
}

void BotPersonalityMgr::RemoveQueuedLoginGeneration(ObjectGuid const& guid)
{
    _pendingLoginGenerations.erase(guid);
}

void BotPersonalityMgr::ClearCache()
{
    _cache.clear();
    _pendingLoginGenerations.clear();
}

BotPersonalityMgr::LoadResult BotPersonalityMgr::LoadPersonality(
    uint32 botGuid,
    BotPersonality& personality,
    bool& corrected)
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT `bot_guid`, `archetype`, `friendliness`, `confidence`, "
        "`patience`, `humour`, `competitiveness`, `greed`, `bravery`, "
        "`talkativeness`, `speech_style` FROM `bot_personality` "
        "WHERE `bot_guid` = {} LIMIT 2",
        botGuid);

    if (!result)
        return LoadResult::NotFound;

    if (result->GetRowCount() > 1)
    {
        LOG_ERROR(
            "module.botpersonality",
            "Duplicate personality rows found for bot {}",
            botGuid);
    }

    Field* fields = result->Fetch();
    personality.botGuid = fields[0].Get<uint32>();
    if (personality.botGuid != botGuid)
    {
        LOG_ERROR(
            "module.botpersonality",
            "Loaded personality row for bot {} while requesting bot {}",
            personality.botGuid,
            botGuid);
        personality.botGuid = botGuid;
    }

    uint32 const archetypeValue = fields[1].Get<uint32>();
    if (archetypeValue >= static_cast<uint32>(BotPersonalityArchetype::Max))
    {
        corrected = true;
        LOG_ERROR(
            "module.botpersonality",
            "Invalid archetype {} loaded for bot {}",
            archetypeValue,
            botGuid);
        personality.archetype = BotPersonalityArchetype::Friendly;
    }
    else
        personality.archetype = static_cast<BotPersonalityArchetype>(
            archetypeValue);

    personality.friendliness = LoadTrait(
        fields[2],
        "friendliness",
        botGuid,
        corrected);
    personality.confidence = LoadTrait(
        fields[3],
        "confidence",
        botGuid,
        corrected);
    personality.patience = LoadTrait(
        fields[4],
        "patience",
        botGuid,
        corrected);
    personality.humour = LoadTrait(fields[5], "humour", botGuid, corrected);
    personality.competitiveness = LoadTrait(
        fields[6],
        "competitiveness",
        botGuid,
        corrected);
    personality.greed = LoadTrait(fields[7], "greed", botGuid, corrected);
    personality.bravery = LoadTrait(fields[8], "bravery", botGuid, corrected);
    personality.talkativeness = LoadTrait(
        fields[9],
        "talkativeness",
        botGuid,
        corrected);
    personality.speechStyle = LoadSpeechStyle(fields[10], botGuid, corrected);

    return LoadResult::Loaded;
}

void BotPersonalityMgr::SavePersonality(BotPersonality const& personality)
{
    CharacterDatabase.Execute(
        "REPLACE INTO `bot_personality` "
        "(`bot_guid`, `archetype`, `friendliness`, `confidence`, `patience`, "
        "`humour`, `competitiveness`, `greed`, `bravery`, `talkativeness`, "
        "`speech_style`) VALUES "
        "({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
        personality.botGuid,
        static_cast<uint32>(personality.archetype),
        TraitToInt(personality.friendliness),
        TraitToInt(personality.confidence),
        TraitToInt(personality.patience),
        TraitToInt(personality.humour),
        TraitToInt(personality.competitiveness),
        TraitToInt(personality.greed),
        TraitToInt(personality.bravery),
        TraitToInt(personality.talkativeness),
        static_cast<uint32>(personality.speechStyle));
}

void BotPersonalityMgr::DeletePersonality(uint32 botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM `bot_personality` WHERE `bot_guid` = {}",
        botGuid);
}

BotPersonality BotPersonalityMgr::GenerateForBot(Player* bot)
{
    return GeneratePersonality(bot, _variation);
}

BotPersonality BotPersonalityMgr::GenerateForBot(
    Player* bot,
    BotPersonalityArchetype archetype)
{
    return GeneratePersonality(bot, archetype, _variation);
}

bool BotPersonalityMgr::ValidateLoadedPersonality(BotPersonality& personality)
{
    bool changed = false;

    auto validateTrait =
        [&changed](int8& trait, char const* name, uint32 botGuid)
    {
        int32 const oldValue = static_cast<int32>(trait);
        trait = ClampTraitForStorage(oldValue);
        if (static_cast<int32>(trait) != oldValue)
        {
            changed = true;
            LOG_ERROR(
                "module.botpersonality",
                "Invalid {} value {} loaded for bot {}, clamped to {}",
                name,
                oldValue,
                botGuid,
                static_cast<int32>(trait));
        }
    };

    if (personality.archetype >= BotPersonalityArchetype::Max)
    {
        personality.archetype = BotPersonalityArchetype::Friendly;
        changed = true;
    }

    validateTrait(
        personality.friendliness,
        "friendliness",
        personality.botGuid);
    validateTrait(personality.confidence, "confidence", personality.botGuid);
    validateTrait(personality.patience, "patience", personality.botGuid);
    validateTrait(personality.humour, "humour", personality.botGuid);
    validateTrait(
        personality.competitiveness,
        "competitiveness",
        personality.botGuid);
    validateTrait(personality.greed, "greed", personality.botGuid);
    validateTrait(personality.bravery, "bravery", personality.botGuid);
    validateTrait(
        personality.talkativeness,
        "talkativeness",
        personality.botGuid);

    uint8 const oldSpeechStyle = personality.speechStyle;
    personality.speechStyle = ClampSpeechStyleForStorage(
        personality.speechStyle);
    if (personality.speechStyle != oldSpeechStyle)
    {
        changed = true;
        LOG_ERROR(
            "module.botpersonality",
            "Invalid speech style {} loaded for bot {}, clamped to {}",
            static_cast<uint32>(oldSpeechStyle),
            personality.botGuid,
            static_cast<uint32>(personality.speechStyle));
    }

    return changed;
}
