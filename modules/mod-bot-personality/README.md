# mod-bot-personality

Phase 1 of a persistent Playerbot personality system for AzerothCore.

This module gives each online Playerbot a deterministic, persisted personality
record. It does not implement chat generation, mood, relationships, sentiment,
LLM integration, combat reactions, party banter, or background processing.

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

## Commands

All commands require GM access unless noted.

```text
.botpersonality show <botName>
.botpersonality regenerate <botName>
.botpersonality set <botName> <trait> <value>
.botpersonality archetype <botName> <archetype> [reset]
.botpersonality reload <botName>
.botpersonality clearcache
```

`clearcache` requires administrator access. Commands support online bots. Real
players and offline characters are rejected without creating a personality.

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
