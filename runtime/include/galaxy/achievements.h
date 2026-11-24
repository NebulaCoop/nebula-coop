#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

// Nebula's achievements. The game checks them against its save data (NebulaAchievements) and asks the host to unlock
// them. Online co-op keeps them with the save (set_file): one unlocked in a save is every player's who plays it.
namespace galaxy::achievements {

enum class Group { Story, Challenge, Extra };

struct Info {
    const char* id;
    Group group;
    const char* name;  // English, for the host's log
};

// The catalogue, grouped: story, challenges, extras.
[[nodiscard]] std::span<const Info> list() noexcept;
[[nodiscard]] const Info* find(std::string_view id) noexcept;
// True when `id` is in the catalogue and was not unlocked yet; it is then unlocked, and written to the file in the
// background (several unlocked together, once).
bool unlock(std::string_view id);
// The same, unlocked at `when` (seconds since 1970, UTC) rather than now: another player's unlock in the co-op save,
// on the day it was unlocked there.
bool unlock_at(std::string_view id, std::int64_t when);
// A mod's achievement ("mod:<mod id>/<id>"): as unlock, for any key of that form.
bool unlock_mod(std::string_view key);
[[nodiscard]] bool unlocked(std::string_view id);
// When it was unlocked, in seconds since 1970 (UTC); 0 when locked or unlocked at a time that cannot be read.
[[nodiscard]] std::int64_t unlocked_time(std::string_view id);
// The day it was unlocked, in the player's time zone: year * 10000 + month * 100 + day; 0 when locked; 1 when
// unlocked on a day that cannot be read.
[[nodiscard]] int unlocked_date(std::string_view id);

// Writes what is not written yet, now (before the process ends).
void flush();

// Save export and import (save_transfer): the unlocks as the file holds them ("<id>=<when>" lines), and another
// install's merged in: each unlock this one lacks is added, and one both hold keeps the earlier time. Lines that are not
// an unlock of the catalogue or of a mod ("mod:...") are passed over. Returns how many unlocks were added or changed.
[[nodiscard]] std::string text();
unsigned merge(std::string_view text);
// An import's unlocks for a run that is not this one (Android's launcher hands them to the game's process): this file
// beside achievements.ini is merged when the unlocks are first read, then removed.
inline constexpr std::string_view kImportFileName = "achievements.ini.import";

// Writes what is waiting, then sets the file (empty: achievements.ini in the settings folder) and drops the cache: the
// played co-op save's own.
void set_file(const std::filesystem::path& path);

// Tests: writes what is waiting, then sets the file (empty: achievements.ini in the settings folder) and drops the
// cache.
void set_path_for_tests(const std::filesystem::path& path);
}  // namespace galaxy::achievements
