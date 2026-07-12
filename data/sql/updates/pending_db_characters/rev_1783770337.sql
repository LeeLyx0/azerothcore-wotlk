CREATE TABLE IF NOT EXISTS `bot_memory` (
    `memory_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `bot_guid` INT UNSIGNED NOT NULL,
    `player_guid` INT UNSIGNED NOT NULL,
    `memory_type` TINYINT UNSIGNED NOT NULL,
    `memory_source` TINYINT UNSIGNED NOT NULL,
    `summary` VARCHAR(512) NOT NULL,
    `subject_key` VARCHAR(128) NOT NULL DEFAULT '',
    `importance` SMALLINT NOT NULL DEFAULT 0,
    `confidence` SMALLINT NOT NULL DEFAULT 100,
    `reinforcement_count` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    `created_at` INT UNSIGNED NOT NULL,
    `updated_at` INT UNSIGNED NOT NULL,
    `last_recalled_at` INT UNSIGNED NOT NULL DEFAULT 0,
    `expires_at` INT UNSIGNED NOT NULL DEFAULT 0,
    `source_event_id` INT UNSIGNED NOT NULL DEFAULT 0,
    `source_reference` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `pinned` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `negative` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`memory_id`),
    INDEX `idx_bot_player` (`bot_guid`, `player_guid`),
    INDEX `idx_bot_player_type`
        (`bot_guid`, `player_guid`, `memory_type`),
    INDEX `idx_expiry` (`expires_at`),
    INDEX `idx_importance`
        (`bot_guid`, `player_guid`, `importance`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
