# mod-bot-personality

Persistent Playerbot personality, relationship memory, and template dialogue
for AzerothCore.

Phase 1 gives each online Playerbot a deterministic, persisted personality
record. Phase 2 adds a small template-based direct-whisper response system
driven by the bot's existing traits and archetype. Phase 3 adds persistent,
independent relationships between each Playerbot and each real player.

The module still does not implement mood, combat reactions, party banter, LLM
integration, HTTP calls, external APIs, background workers, or free-form
language generation.

## Installation

Place this directory under `modules/` and build AzerothCore with modules
enabled, for example `-DMODULES=static`. The AzerothCore module loader
discovers the module from the `mod-bot-personality` directory and calls
`Addmod_bot_personalityScripts()`.

## Database

The module adds a characters-database table:

```sql
CREATE TABLE IF NOT EXISTS `bot_personality` (
    `bot_guid` INT UNSIGNED NOT NULL,
    `archetype` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `friendliness` TINYINT NOT NULL DEFAULT 0,
    `confidence` TINYINT NOT NULL DEFAULT 0,
    `patience` TINYINT NOT NULL DEFAULT 0,
    `humour` TINYINT NOT NULL DEFAULT 0,
    `competitiveness` TINYINT NOT NULL DEFAULT 0,
    `greed` TINYINT NOT NULL DEFAULT 0,
    `bravery` TINYINT NOT NULL DEFAULT 0,
    `talkativeness` TINYINT NOT NULL DEFAULT 0,
    `speech_style` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`bot_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

Phase 3 adds a second characters-database table:

```sql
CREATE TABLE IF NOT EXISTS `bot_relationship` (
    `bot_guid` INT UNSIGNED NOT NULL,
    `player_guid` INT UNSIGNED NOT NULL,
    `affinity` SMALLINT NOT NULL DEFAULT 0,
    `trust` SMALLINT NOT NULL DEFAULT 0,
    `respect` SMALLINT NOT NULL DEFAULT 0,
    `familiarity` SMALLINT NOT NULL DEFAULT 0,
    `positive_interactions` INT UNSIGNED NOT NULL DEFAULT 0,
    `negative_interactions` INT UNSIGNED NOT NULL DEFAULT 0,
    `first_interaction` INT UNSIGNED NOT NULL DEFAULT 0,
    `last_interaction` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`bot_guid`, `player_guid`),
    INDEX `idx_player_guid` (`player_guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

When automatic module SQL updates are enabled, AzerothCore scans
`modules/mod-bot-personality/data/sql/characters/` and applies the schema to the
characters database.

## Config

`conf/bot_personality.conf.dist` provides:

- `BotPersonality.Enable`: enables or disables the module.
- `BotPersonality.GenerateOnLogin`: loads or creates personalities for bots on
  login.
- `BotPersonality.SaveGenerated`: saves newly generated personalities.
- `BotPersonality.DebugLogging`: enables additional diagnostics.
- `BotPersonality.Variation`: deterministic trait variation, clamped to `0-40`.

Phase 2 chat options:

- `BotPersonality.Chat.Enable`: enables template-based chat.
- `BotPersonality.Chat.RespondToWhispers`: enables direct whisper replies.
- `BotPersonality.Chat.RespondToUnknown`: enables low-probability unknown
  intent replies.
- `BotPersonality.Chat.ForceResponseForDebug`: makes eligible messages answer
  with 100% probability.
- `BotPersonality.Chat.PairCooldownMs`: per bot/player cooldown.
- `BotPersonality.Chat.BotCooldownMs`: per-bot global cooldown.
- `BotPersonality.Chat.DuplicateWindowMs`: duplicate-message suppression
  window.
- `BotPersonality.Chat.MaxResponsesPerMinute`: rolling per-bot rate limit.
- `BotPersonality.Chat.MaxInputLength`: maximum processed whisper length.
- `BotPersonality.Chat.MaxOutputLength`: maximum generated reply length.
- `BotPersonality.Chat.MinimumGreetingChance`: minimum greeting chance.
- `BotPersonality.Chat.MinimumHelpChance`: minimum help-request chance.
- `BotPersonality.Chat.MinimumIdentityChance`: minimum identity-question
  chance.
- `BotPersonality.Chat.MinimumThanksChance`: minimum thanks chance.
- `BotPersonality.Chat.MinimumInsultChance`: minimum insult-response chance.
- `BotPersonality.Chat.UnknownChance`: chance for unknown-intent replies.
- `BotPersonality.Chat.ReplyDelay.*`: simulated read/typing delay before
  replies.
- `BotPersonality.Chat.DebugLogging`: redacted chat diagnostics.

Phase 3 relationship options:

- `BotPersonality.Relationship.Enable`: enables persistent relationships.
- `BotPersonality.Relationship.UpdateFromChat`: enables chat-driven changes.
- `BotPersonality.Relationship.DebugLogging`: relationship diagnostics.
- `BotPersonality.Relationship.Minimum` and `.Maximum`: value clamp range.
- `BotPersonality.Relationship.SaveIntervalSeconds`: dirty save interval.
- `BotPersonality.Relationship.CacheExpiryMinutes`: idle cache expiry.
- `BotPersonality.Relationship.MaxCachedEntries`: cache size limit.
- `BotPersonality.Relationship.RepeatWindowSeconds`: repeat window.
- `BotPersonality.Relationship.SecondRepeatPercent`: second repeat scaling.
- `BotPersonality.Relationship.ThirdRepeatPercent`: third repeat scaling.
- `BotPersonality.Relationship.FurtherRepeatPercent`: later repeat scaling.
- `BotPersonality.Relationship.MaxPositiveChatGainPerDay`: positive cap.
- `BotPersonality.Relationship.MaxNegativeChatLossPerDay`: negative cap.
- `BotPersonality.Relationship.MaxFamiliarityGainPerDay`: familiarity cap.
- `BotPersonality.Relationship.*Affinity`, `*Trust`, `*Respect`, and
  `*Familiarity`: base event deltas.

## Commands

All commands require GM access unless noted.

```text
.botpersonality show <botName>
.botpersonality regenerate <botName>
.botpersonality set <botName> <trait> <value>
.botpersonality archetype <botName> <archetype> [reset]
.botpersonality reload <botName>
.botpersonality clearcache
.botpersonality chat test <botName> <intent> [send]
.botpersonality chat parse <message>
.botpersonality chat cooldowns
.botpersonality chat clearcooldowns
.botpersonality chat force <botName> <playerName> <intent>
.botpersonality relationship show <botName> <playerName>
.botpersonality relationship set <botName> <playerName> <field> <value>
.botpersonality relationship adjust <botName> <playerName> <field> <amount>
.botpersonality relationship reset <botName> <playerName>
.botpersonality relationship reload <botName> <playerName>
.botpersonality relationship list <botName> [limit]
.botpersonality relationship save
.botpersonality relationship clearcache
```

`clearcache` requires administrator access. Commands support online bots. Real
players and offline characters are rejected without creating a personality.

`chat test` generates a response using the bot's real personality and the
template provider. It only sends a whisper when the explicit `send` argument is
present. `chat force` sends one test whisper to an explicit online real player
target and bypasses response probability, but still validates bot/player
targets. `chat clearcooldowns` clears only in-memory chat cooldowns,
duplicate records, rate windows, pending delayed replies, and recent response
history.

`relationship show` does not create a missing row. `relationship set` and
`relationship adjust` are explicit GM edits and may create a neutral row before
saving the requested value. `relationship reset` deletes only the selected
bot/player relationship. `relationship clearcache` saves dirty relationship
entries and clears only relationship cache and transient anti-farming state.

## Traits

Traits are signed values from `-100` to `100`:

- friendliness
- confidence
- patience
- humour
- competitiveness
- greed
- bravery
- talkativeness

`speechstyle` is an unsigned style slot clamped to `0-10`.

## Deterministic Generation

Generation uses the bot character GUID counter as the only seed. The module
selects an archetype, applies archetype defaults, applies small class and race
modifiers, adds deterministic per-bot variation, clamps values, caches the
result, and saves it to the characters database when configured.

Deleting a generated row and regenerating it with the same code and config
produces the same personality for the same bot.

## Phase 2 Whisper Chat

Phase 2 only responds to non-addon direct whispers from real players to
Playerbots:

```text
/whisper BotName hello
```

The bot may answer with a normal player whisper. Bots do not proactively speak
in party chat, raid chat, say, yell, guild chat, battleground chat, or world
channels.

Supported intents:

- Greeting
- Farewell
- Thanks
- Praise
- Apology
- Insult
- HelpRequest
- IdentityQuestion
- WellbeingQuestion
- Agreement
- Disagreement
- Unknown

The parser lowercases UTF-8 safely where possible, trims whitespace, collapses
repeated spaces, ignores simple punctuation, and matches centralized phrase
lists. Insult detection is conservative and requires direct-address style
phrases such as `you are stupid`, `bad bot`, or `you suck`; it does not treat a
sentence like `that boss was stupid` as a bot-directed insult.

## Tone Selection

Tone is selected from traits and archetype, not archetype alone:

- high friendliness tends toward Warm;
- high friendliness plus talkativeness tends toward Enthusiastic;
- high humour plus confidence tends toward Sarcastic;
- low confidence or the Nervous archetype tends toward Nervous;
- high confidence plus low friendliness tends toward Arrogant;
- low talkativeness plus patience, Stoic, or Veteran tends toward Stoic;
- low friendliness tends toward Cold;
- otherwise the tone is Neutral.

Intent can adjust the result. Apologies soften sarcastic and arrogant bots,
insults make nervous bots defensive, and praise can make arrogant bots answer
more sharply.

## Phase 3 Relationships

Relationships are separate from personality. Personality describes who the bot
is; relationship describes how that bot currently feels about one real player.

Each relationship stores:

- affinity: general like or dislike;
- trust: whether the player seems reliable and sincere;
- respect: whether the player seems competent or worthy;
- familiarity: how much shared interaction the bot remembers;
- positive and negative interaction counters;
- first and last interaction timestamps.

Values default to `-1000` through `1000` and are clamped by configuration.
The main relationship level is derived from affinity:

- Hostile: `-1000` to `-601`
- Disliked: `-600` to `-301`
- Wary: `-300` to `-101`
- Neutral: `-100` to `149`
- Friendly: `150` to `399`
- Trusted: `400` to `699`
- Loyal: `700` to `1000`

Chat intent maps to relationship events:

- Greeting -> Greeting
- Thanks -> Thanks
- Praise -> Praise
- Apology -> Apology
- Insult -> Insult
- HelpRequest -> HelpRequest
- Farewell, identity, wellbeing, agreement, and disagreement -> Conversation
- Unknown -> Conversation only when a reply is actually generated
- repeated exact duplicate spam -> RepeatedSpam

Base deltas are centralized in config. Personality then applies bounded
scaling: friendliness improves positive affinity gains, patience and humour
soften insults, low patience sharpens penalties, nervous bots react more to
insults and apologies, helpful bots appreciate thanks more, and arrogant bots
gain less affinity but more respect from praise.

Repeated events are tracked per bot/player pair in memory. Within the repeat
window, the first event receives full effect, the second uses the configured
second-repeat percent, the third uses the third-repeat percent, and later
positive repeats use the further-repeat percent. Negative insults and spam keep
at least half effect so repeated abuse cannot avoid consequences entirely.

Rolling in-memory 24-hour caps limit positive affinity/trust/respect gains,
negative affinity/trust/respect losses, and familiarity gains per bot/player
pair. These caps reset on worldserver restart; the relationship values
themselves persist in the characters database.

Dirty relationships are cached and batch-saved on the configured interval,
clean shutdown, selected player/bot logout, explicit GM save, cache clear, and
cache eviction. Cache entries expire after the configured idle time and are
bounded by `MaxCachedEntries`. Dirty entries are saved before eviction.

## Dialogue Integration

The whisper pipeline is:

```text
validate real-player sender and Playerbot recipient
reject addon, empty, invalid, or Playerbots command messages
parse intent and suppress exact duplicate farming
apply one relationship event for accepted conversational input
copy relationship data into the dialogue context
choose tone from personality, intent, and relationship
select relationship-aware templates with normal template fallback
apply existing response chance, cooldowns, and delayed whisper sending
```

Relationship templates use broad negative, neutral, positive, and loyal bands
instead of every possible intent/tone/level combination. If no relationship
template matches, Phase 2 personality-only templates remain the fallback.

## Manual Relationship Test

Use one bot and two online real players:

```text
1. .botpersonality relationship set BotName PlayerA affinity 400
2. .botpersonality relationship set BotName PlayerB affinity -400
3. Whisper "hello" from PlayerA to BotName.
4. Whisper "hello" from PlayerB to BotName.
5. Compare warm/positive wording against guarded/negative wording.
6. Restart worldserver.
7. Repeat both whispers.
8. Confirm .botpersonality relationship show preserved both rows.
```

## Command Compatibility

Existing Playerbots commands remain owned by `mod-playerbots`. The chat hook
never blocks the whisper and never executes bot commands. Before generating a
personality response it probes the recipient bot's Playerbots trigger registry
with the same exact-then-leading-phrase shape used by
`ExternalEventHelper::ParseChatCommand`, while also respecting the configured
Playerbots command prefix, command separator, chat target prefixes, `reset`,
`logout`, `debug`, `do`, and item-link auto-trade detection.

If Playerbots recognizes a whisper as a command, personality chat remains
silent and allows Playerbots to handle it normally.

## Cooldowns And Spam Control

Chat state is in memory only and is cleared by server restart or
`.botpersonality chat clearcooldowns`. It is never written to the database.

The manager enforces:

- per bot/player pair cooldown;
- per-bot global cooldown;
- duplicate-message suppression for the same player and bot;
- maximum successful replies per bot in a rolling one-minute window;
- talkativeness-based response chance;
- lower unknown-intent response chance;
- recent-response tracking to avoid repeating the same line when alternatives
  exist.

Cooldown maps and recent-response maps are lazily cleaned during chat handling
and capped to bounded sizes. They store GUID counters, message hashes, times,
and response text only; they do not hold raw `Player*` pointers.

## Limitations

Phase 3 is still intentionally template-only and chat-only. It does not call
external services, run background threads, execute SQL from chat, issue
Playerbots commands, change AI state, store chat history, model mood, react to
combat, track kills, deaths, healing, resurrection, wipes, boss kills, dungeon
completion, loot events, leadership, or generate free-form text. Help-request
replies are conversational only; actual bot control still requires normal
Playerbots commands.
