#include "BotGameplayTracker.h"

#include "Creature.h"
#include "GlobalScript.h"
#include "GroupScript.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "UnitScript.h"

#include <list>

class BotGameplayPlayerScript : public PlayerScript
{
public:
    BotGameplayPlayerScript()
        : PlayerScript(
              "BotGameplayPlayerScript",
              {
                  PLAYERHOOK_ON_CREATURE_KILL,
                  PLAYERHOOK_ON_CREATURE_KILLED_BY_PET,
                  PLAYERHOOK_ON_SPELL_CAST
              })
    {
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        sBotGameplayTracker.RecordCreatureKill(killer, killed);
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        sBotGameplayTracker.RecordCreatureKill(owner, killed);
    }

    void OnPlayerSpellCast(
        Player* player,
        Spell* spell,
        bool /*skipCheck*/) override
    {
        if (!player || !spell || !spell->GetSpellInfo())
            return;

        SpellInfo const* spellInfo = spell->GetSpellInfo();
        if (!spellInfo->HasEffect(SPELL_EFFECT_RESURRECT) &&
            !spellInfo->HasEffect(SPELL_EFFECT_RESURRECT_NEW))
            return;

        Unit* target = spell->GetOriginalTarget();
        if (!target)
            return;

        Player* targetPlayer = target->ToPlayer();
        if (!targetPlayer)
            return;

        sBotGameplayTracker.RecordResurrectionCast(
            player,
            targetPlayer,
            spellInfo->Id);
    }
};

class BotGameplayUnitScript : public UnitScript
{
public:
    BotGameplayUnitScript()
        : UnitScript(
              "BotGameplayUnitScript",
              true,
              {
                  UNITHOOK_ON_HEAL,
                  UNITHOOK_ON_UNIT_DEATH
              })
    {
    }

    void OnHeal(Unit* healer, Unit* receiver, uint32& gain) override
    {
        sBotGameplayTracker.RecordHeal(healer, receiver, gain);
    }

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        if (Player* player = unit ? unit->ToPlayer() : nullptr)
            sBotGameplayTracker.RecordPlayerDeath(player, killer);
    }
};

class BotGameplayGroupScript : public GroupScript
{
public:
    BotGameplayGroupScript()
        : GroupScript(
              "BotGameplayGroupScript",
              {
                  GROUPHOOK_ON_ADD_MEMBER,
                  GROUPHOOK_ON_REMOVE_MEMBER
              })
    {
    }

    void OnAddMember(Group* group, ObjectGuid guid) override
    {
        sBotGameplayTracker.RecordGroupMemberAdded(group, guid);
    }

    void OnRemoveMember(
        Group* group,
        ObjectGuid guid,
        RemoveMethod method,
        ObjectGuid /*kicker*/,
        char const* /*reason*/) override
    {
        sBotGameplayTracker.RecordGroupMemberRemoved(group, guid, method);
    }
};

class BotGameplayGlobalScript : public GlobalScript
{
public:
    BotGameplayGlobalScript()
        : GlobalScript(
              "BotGameplayGlobalScript",
              {
                  GLOBALHOOK_ON_AFTER_UPDATE_ENCOUNTER_STATE
              })
    {
    }

    void OnAfterUpdateEncounterState(
        Map* map,
        EncounterCreditType /*type*/,
        uint32 creditEntry,
        Unit* source,
        Difficulty /*difficulty_fixed*/,
        std::list<DungeonEncounter const*> const* /*encounters*/,
        uint32 dungeonCompleted,
        bool updated) override
    {
        sBotGameplayTracker.RecordEncounterCompleted(
            map,
            source,
            creditEntry,
            dungeonCompleted,
            updated);
    }
};

void AddBotPersonalityGameplayScripts()
{
    new BotGameplayPlayerScript();
    new BotGameplayUnitScript();
    new BotGameplayGroupScript();
    new BotGameplayGlobalScript();
}
