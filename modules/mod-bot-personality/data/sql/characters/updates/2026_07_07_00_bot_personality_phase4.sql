ALTER TABLE `bot_relationship`
    ADD COLUMN `shared_normal_kills`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `negative_interactions`,
    ADD COLUMN `shared_elite_kills`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `shared_normal_kills`,
    ADD COLUMN `shared_boss_kills`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `shared_elite_kills`,
    ADD COLUMN `player_healed_bot_events`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `shared_boss_kills`,
    ADD COLUMN `player_resurrected_bot_events`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `player_healed_bot_events`,
    ADD COLUMN `bot_healed_player_events`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `player_resurrected_bot_events`,
    ADD COLUMN `bot_resurrected_player_events`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `bot_healed_player_events`,
    ADD COLUMN `player_deaths`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `bot_resurrected_player_events`,
    ADD COLUMN `bot_deaths`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `player_deaths`,
    ADD COLUMN `shared_deaths`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `bot_deaths`,
    ADD COLUMN `group_wipes`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `shared_deaths`,
    ADD COLUMN `dungeons_completed`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `group_wipes`,
    ADD COLUMN `raid_encounters_completed`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `dungeons_completed`,
    ADD COLUMN `sustained_teamwork_events`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `raid_encounters_completed`;
