#ifndef MOD_BOT_PERSONALITY_BOT_RELATIONSHIP_H
#define MOD_BOT_PERSONALITY_BOT_RELATIONSHIP_H

#include "BotPersonality.h"
#include "Define.h"

#include <cstddef>
#include <string>
#include <string_view>

enum class BotRelationshipEvent : uint8
{
    Greeting = 0,
    Thanks,
    Praise,
    Apology,
    Insult,
    HelpRequest,
    Conversation,
    RepeatedSpam
};

enum class BotRelationshipLevel : uint8
{
    Hostile = 0,
    Disliked,
    Wary,
    Neutral,
    Friendly,
    Trusted,
    Loyal
};

struct BotRelationship
{
    uint32 botGuid = 0;
    uint32 playerGuid = 0;

    int16 affinity = 0;
    int16 trust = 0;
    int16 respect = 0;
    int16 familiarity = 0;

    uint32 positiveInteractions = 0;
    uint32 negativeInteractions = 0;

    uint32 firstInteraction = 0;
    uint32 lastInteraction = 0;
};

struct BotRelationshipDelta
{
    int32 affinity = 0;
    int32 trust = 0;
    int32 respect = 0;
    int32 familiarity = 0;
};

struct BotPlayerRelationshipKey
{
    uint32 botGuid = 0;
    uint32 playerGuid = 0;

    bool operator==(BotPlayerRelationshipKey const& other) const
    {
        return botGuid == other.botGuid && playerGuid == other.playerGuid;
    }
};

struct BotPlayerRelationshipKeyHash
{
    std::size_t operator()(BotPlayerRelationshipKey const& key) const;
};

struct BotRelationshipListEntry
{
    uint32 playerGuid = 0;
    std::string playerName;
    BotRelationship relationship;
};

struct BotRelationshipSaveStats
{
    uint32 saved = 0;
    uint32 failed = 0;
    uint32 remainingDirty = 0;
};

char const* BotRelationshipEventToString(BotRelationshipEvent event);
bool BotRelationshipEventFromString(
    std::string_view text,
    BotRelationshipEvent& event);

BotRelationshipLevel GetRelationshipLevel(int32 affinity);
char const* RelationshipLevelToString(BotRelationshipLevel level);

bool IsTrusted(BotRelationship const& relationship);
bool IsRespected(BotRelationship const& relationship);
bool IsFamiliar(BotRelationship const& relationship);

BotRelationshipDelta CalculateRelationshipDelta(
    BotPersonality const& personality,
    BotRelationshipEvent event,
    BotRelationshipDelta baseDelta);

#endif
