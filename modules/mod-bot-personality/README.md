# mod-bot-personality

Persistent Playerbot personality and template dialogue for AzerothCore.

Phase 1 gives each online Playerbot a deterministic, persisted personality
record. Phase 2 adds a small template-based direct-whisper response system
driven by the bot's existing traits and archetype.

The module still does not implement mood, relationships, affinity, long-term
memory, combat reactions, party banter, LLM integration, HTTP calls, external
APIs, background workers, or free-form language generation.

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
- `BotPersonality.Chat.DebugLogging`: redacted chat diagnostics.

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
```

`clearcache` requires administrator access. Commands support online bots. Real
players and offline characters are rejected without creating a personality.

`chat test` generates a response using the bot's real personality and the
template provider. It only sends a whisper when the explicit `send` argument is
present. `chat force` sends one test whisper to an explicit online real player
target and bypasses response probability, but still validates bot/player
targets. `chat clearcooldowns` clears only in-memory chat cooldowns,
duplicate records, rate windows, and recent response history.

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

Phase 2 is intentionally template-only. It does not call external services, run
background threads, execute SQL from chat, issue Playerbots commands, change AI
state, track relationships, store chat history, model mood, react to combat, or
generate free-form text. Help-request replies are conversational only; actual
bot control still requires normal Playerbots commands.
