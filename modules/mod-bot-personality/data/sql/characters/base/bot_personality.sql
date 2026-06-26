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
