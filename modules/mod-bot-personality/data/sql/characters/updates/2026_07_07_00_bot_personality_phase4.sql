ALTER TABLE `bot_relationship`
    ADD COLUMN IF NOT EXISTS `shared_normal_kills`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `negative_interactions`,
    ADD COLUMN IF NOT EXISTS `shared_elite_kills`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `shared_normal_kills`,
    ADD COLUMN IF NOT EXISTS `shared_boss_kills`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `shared_elite_kills`,
    ADD COLUMN IF NOT EXISTS `player_healed_bot_events`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `shared_boss_kills`,
    ADD COLUMN IF NOT EXISTS `player_resurrected_bot_events`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `player_healed_bot_events`,
    ADD COLUMN IF NOT EXISTS `bot_healed_player_events`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `player_resurrected_bot_events`,
    ADD COLUMN IF NOT EXISTS `bot_resurrected_player_events`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `bot_healed_player_events`,
    ADD COLUMN IF NOT EXISTS `player_deaths`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `bot_resurrected_player_events`,
    ADD COLUMN IF NOT EXISTS `bot_deaths`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `player_deaths`,
    ADD COLUMN IF NOT EXISTS `shared_deaths`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `bot_deaths`,
    ADD COLUMN IF NOT EXISTS `group_wipes`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `shared_deaths`,
    ADD COLUMN IF NOT EXISTS `dungeons_completed`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `group_wipes`,
    ADD COLUMN IF NOT EXISTS `raid_encounters_completed`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `dungeons_completed`,
    ADD COLUMN IF NOT EXISTS `sustained_teamwork_events`
        INT UNSIGNED NOT NULL DEFAULT 0
        AFTER `raid_encounters_completed`;
