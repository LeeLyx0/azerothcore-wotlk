#ifndef MOD_BOT_PERSONALITY_BOT_GAMEPLAY_TRACKER_H
#define MOD_BOT_PERSONALITY_BOT_GAMEPLAY_TRACKER_H

#include "BotGameplayEvent.h"
#include "BotRelationship.h"
#include "ObjectGuid.h"

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

class Creature;
class Group;
class Map;
class Player;
class Unit;
enum RemoveMethod : uint8;

struct BotGameplayContext
{
    uint32 mapId = 0;
    uint32 instanceId = 0;
    uint32 sourceEntry = 0;
    uint32 value = 0;
    bool isDungeon = false;
    bool isRaid = false;
};

struct BotGameplayRecentEvent
{
    BotGameplayEvent event = BotGameplayEvent::SharedNormalKill;
    BotGameplayContext context;
    BotRelationshipDelta baseDelta;
    BotRelationshipDelta adjustedDelta;
    uint32 timestamp = 0;
    bool applied = false;
    std::string note;
};

struct BotGameplayTrackerStats
{
    bool enabled = false;
    bool updateRelationships = false;
    uint32 recentPairs = 0;
    uint32 cooldowns = 0;
    uint32 normalKillAccumulators = 0;
    uint32 healAccumulators = 0;
    uint32 deathWindows = 0;
    uint32 capPairs = 0;
    uint32 teamworkPairs = 0;
};

struct BotGameplayDebugResult
{
    bool success = false;
    bool applied = false;
    std::string error;
    BotGameplayEvent event = BotGameplayEvent::SharedNormalKill;
    BotRelationshipDelta baseDelta;
    BotRelationshipDelta adjustedDelta;
};

class BotGameplayTracker
{
public:
    static BotGameplayTracker& instance()
    {
        static BotGameplayTracker instance;
        return instance;
    }

    void LoadConfig(bool reload);
    void Update(uint32 diff);
    void ClearTrackers();

    bool IsEnabled() const { return _enabled; }
    bool IsDebugLoggingEnabled() const { return _debugLogging; }

    void RecordCreatureKill(Player* killer, Creature* killed);
    void RecordHeal(Unit* healer, Unit* receiver, uint32 gain);
    void RecordPlayerDeath(Player* player, Unit* killer);
    void RecordResurrectionCast(Player* caster, Player* target, uint32 spellId);
    void RecordGroupMemberAdded(Group* group, ObjectGuid guid);
    void RecordGroupMemberRemoved(
        Group* group,
        ObjectGuid guid,
        RemoveMethod method);
    void RecordEncounterCompleted(
        Map* map,
        Unit* source,
        uint32 creditEntry,
        uint32 dungeonCompleted,
        bool updated);

    BotGameplayTrackerStats GetStats() const;
    std::vector<BotGameplayRecentEvent> GetRecentEvents(
        uint32 botGuid,
        uint32 playerGuid,
        uint32 limit) const;
    BotRelationshipDelta GetBaseDelta(BotGameplayEvent event) const;

    BotGameplayDebugResult SimulateEvent(
        Player const* bot,
        Player const* player,
        BotGameplayEvent event,
        bool apply);

private:
    struct TimedAccumulator
    {
        uint32 value = 0;
        uint32 firstMs = 0;
        uint32 lastMs = 0;
        uint32 sourceEntry = 0;
    };

    struct CooldownKey
    {
        BotPlayerRelationshipKey relationship;
        BotGameplayEvent event = BotGameplayEvent::SharedNormalKill;

        bool operator==(CooldownKey const& other) const
        {
            return relationship == other.relationship &&
                event == other.event;
        }
    };

    struct CooldownKeyHash
    {
        std::size_t operator()(CooldownKey const& key) const;
    };

    struct GameplayCapRecord
    {
        uint32 timestamp = 0;
        uint32 positive = 0;
        uint32 negative = 0;
        uint32 familiarity = 0;
    };

    struct DeathWindow
    {
        std::deque<uint32> playerDeaths;
        std::deque<uint32> botDeaths;
        uint32 lastAccessMs = 0;
    };

    struct TeamworkState
    {
        uint32 startedSeconds = 0;
        uint32 lastSeenSeconds = 0;
    };

    BotGameplayTracker() = default;
    ~BotGameplayTracker() = default;

    BotGameplayTracker(BotGameplayTracker const&) = delete;
    BotGameplayTracker& operator=(BotGameplayTracker const&) = delete;

    BotPlayerRelationshipKey MakeKey(
        uint32 botGuid,
        uint32 playerGuid) const;
    bool IsRealPlayer(Player const* player) const;
    bool IsValidPair(Player const* bot, Player const* player) const;
    bool SameGroup(Player const* bot, Player const* player) const;
    bool IsMapAllowed(Map const* map) const;
    bool IsNear(Player const* player, Unit const* reference) const;

    void ApplyGameplayEvent(
        Player* bot,
        Player* player,
        BotGameplayEvent event,
        BotGameplayContext const& context,
        bool applyRelationship,
        bool bypassAntiFarm,
        BotGameplayDebugResult* debugResult);
    void ApplyGameplayCaps(
        BotPlayerRelationshipKey const& key,
        BotRelationshipDelta& delta,
        uint32 nowSeconds);
    void RecordGameplayCaps(
        BotPlayerRelationshipKey const& key,
        BotRelationshipDelta const& delta,
        uint32 nowSeconds);

    bool IsEventOnCooldown(
        BotPlayerRelationshipKey const& key,
        BotGameplayEvent event,
        uint32 nowMs) const;
    void StartEventCooldown(
        BotPlayerRelationshipKey const& key,
        BotGameplayEvent event,
        uint32 nowMs);
    uint32 GetCooldownMs(BotGameplayEvent event) const;

    void RecordRecent(
        BotPlayerRelationshipKey const& key,
        BotGameplayRecentEvent const& recent);
    void TouchTeamwork(Player* bot, Player* player, uint32 nowSeconds);
    void CheckTeamwork(uint32 nowSeconds);
    void Cleanup(uint32 nowMs, uint32 nowSeconds);

    void RecordNormalKill(
        Player* bot,
        Player* player,
        BotGameplayEvent event,
        BotGameplayContext const& context);
    void RecordHealEvent(
        Player* bot,
        Player* player,
        BotGameplayEvent event,
        uint32 amount,
        BotGameplayContext const& context);
    void RecordDeathWindows(
        Player* bot,
        Player* player,
        BotGameplayEvent deathEvent,
        BotGameplayContext const& context);
    void TryRecordGroupWipe(
        Group* group,
        Player* deadPlayer,
        BotGameplayContext const& context);

    bool _enabled = true;
    bool _updateRelationships = true;
    bool _debugLogging = false;

    bool _trackNormalKills = true;
    bool _trackEliteKills = true;
    bool _trackBossKills = true;
    bool _trackHealing = true;
    bool _trackResurrection = true;
    bool _trackDeaths = true;
    bool _trackGroupWipes = true;
    bool _trackGroupLeaves = true;
    bool _trackEncounterCompletion = true;
    bool _trackSustainedTeamwork = true;
    bool _allowPvPEvents = false;

    float _participationDistance = 120.0f;
    uint32 _normalKillBatchSize = 3;
    uint32 _normalKillWindowMs = 300000;
    uint32 _healThreshold = 500;
    uint32 _healWindowMs = 20000;
    uint32 _deathWindowMs = 120000;
    uint32 _repeatedDeathThreshold = 3;
    uint32 _sustainedTeamworkSeconds = 20 * 60;
    uint32 _minimumWipeMembers = 3;

    uint32 _maxPositiveGainPerHour = 40;
    uint32 _maxNegativeLossPerHour = 50;
    uint32 _maxFamiliarityGainPerHour = 60;

    uint32 _normalKillCooldownMs = 60000;
    uint32 _eliteKillCooldownMs = 90000;
    uint32 _bossKillCooldownMs = 300000;
    uint32 _healCooldownMs = 45000;
    uint32 _resurrectionCooldownMs = 300000;
    uint32 _deathCooldownMs = 90000;
    uint32 _wipeCooldownMs = 300000;
    uint32 _groupLeaveCooldownMs = 300000;
    uint32 _encounterCooldownMs = 600000;
    uint32 _teamworkCooldownMs = 20 * 60 * 1000;

    BotRelationshipDelta _sharedNormalKillDelta = { 1, 0, 1, 1 };
    BotRelationshipDelta _sharedEliteKillDelta = { 2, 1, 2, 2 };
    BotRelationshipDelta _sharedBossKillDelta = { 5, 3, 5, 3 };
    BotRelationshipDelta _playerHealedBotDelta = { 3, 3, 1, 2 };
    BotRelationshipDelta _playerResurrectedBotDelta = { 6, 5, 2, 4 };
    BotRelationshipDelta _botHealedPlayerDelta = { 1, 1, 1, 2 };
    BotRelationshipDelta _botResurrectedPlayerDelta = { 2, 2, 1, 3 };
    BotRelationshipDelta _playerDiedDelta = { -1, -1, -2, 1 };
    BotRelationshipDelta _botDiedDelta = { -2, -2, -1, 1 };
    BotRelationshipDelta _sharedDeathDelta = { -1, 0, -1, 2 };
    BotRelationshipDelta _groupWipeDelta = { -3, -3, -3, 2 };
    BotRelationshipDelta _playerLeftGroupDelta = { -8, -6, -4, 0 };
    BotRelationshipDelta _dungeonCompletedDelta = { 8, 4, 8, 6 };
    BotRelationshipDelta _raidEncounterDelta = { 6, 3, 6, 4 };
    BotRelationshipDelta _sustainedTeamworkDelta = { 2, 2, 1, 4 };
    BotRelationshipDelta _repeatedPlayerDeathDelta = { -3, -2, -4, 1 };

    std::unordered_map<
        CooldownKey,
        uint32,
        CooldownKeyHash> _cooldowns;
    std::unordered_map<
        BotPlayerRelationshipKey,
        TimedAccumulator,
        BotPlayerRelationshipKeyHash> _normalKillAccumulators;
    std::unordered_map<
        CooldownKey,
        TimedAccumulator,
        CooldownKeyHash> _healAccumulators;
    std::unordered_map<
        BotPlayerRelationshipKey,
        std::deque<BotGameplayRecentEvent>,
        BotPlayerRelationshipKeyHash> _recentEvents;
    std::unordered_map<
        BotPlayerRelationshipKey,
        DeathWindow,
        BotPlayerRelationshipKeyHash> _deathWindows;
    std::unordered_map<
        BotPlayerRelationshipKey,
        std::deque<GameplayCapRecord>,
        BotPlayerRelationshipKeyHash> _capRecords;
    std::unordered_map<
        BotPlayerRelationshipKey,
        TeamworkState,
        BotPlayerRelationshipKeyHash> _teamworkStates;

    uint32 _lastTeamworkUpdateMs = 0;
    uint32 _lastCleanupMs = 0;
};

#define sBotGameplayTracker BotGameplayTracker::instance()

#endif
