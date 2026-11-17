#pragma once

#include <stdint.h>

#include <array>
#include <map>
#include <string>
#include <vector>

// Spire leveling (pso-roguelike new-features/roguelike/design/mechanics/leveling.md): the level of the run copy as the
// key of what a run gives. Owner: the leveling mechanic; switch "leveling" (design/mechanics/integratie.md par. 5 and
// 7). The pure parts live here, for the static tests (SpireTestsLevel.cc); the registrations on the seam's hooks
// (SpireMechanics.hh) in SpireLevel.cc.
//
// Piece 1 (design par. 2 point 2 and par. 7), on H2 reward_room: the reward at the level the copy really stands on.
//   key level  = the copy's level less the levels of the quest's difficulty (RungLevels), its level on Normal's scale
//   room index = the last room of the episode whose end level the key level has reached, never below the quest's own
// The end level of room j is the level a copy without bought start levels has on Normal when the room is cleared, the
// moment its reward is drawn. So a copy on the curve keeps the quest's own room, and one that is ahead (bought start
// levels, the elite path) is offered the weapon and the disk row of the room its level belongs to. The reward grid's
// weapon levels are computed on that same column (WeaponLevels: W2-W6 start at rooms 1, 3, 5, 8, 11 with run level 4,
// 8, 11, 15, 19), so the copy always has at least the level the grid computed the weapon for. The difficulty does not
// count twice: its levels are taken off here, and its own weapon steps (RungWeaponSteps) lift the W after this.

constexpr const char* SPIRE_LEVELING_FILE = "system/spire-leveling.json";
constexpr const char* SPIRE_LEVELING_SWITCH = "leveling";
constexpr uint32_t SPIRE_LEVEL_EPISODE_ROOMS = 15; // the rooms of an episode (SpireRewards.hh spire_reward_room_index)

struct SpireLevelTable {
  bool enabled = false; // the mechanic's own kill switch; it also needs SPIRE_LEVELING_SWITCH
  // The levels a difficulty's run starts above Normal's, by ladder rung (SpireMeta.hh spire_meta_ladder_quest_rung:
  // 0 Normal, 1 Hard, 2 Very Hard, 3 Ultimate)
  std::array<uint32_t, 4> rung_levels{};
  // Per episode (1, 2) the end level of every room on Normal, by room index - 1, never falling. An episode without a
  // row changes nothing.
  std::map<uint8_t, std::vector<uint32_t>> room_end_levels;
};
// Strict: throws on a missing key or a value out of range (RungLevels: four levels 0-199, Normal's 0, never falling;
// Episodes: "1" or "2", each with 1 to SPIRE_LEVEL_EPISODE_ROOMS RoomEndLevels of 1-200, never falling). A key it does
// not know is left alone: a later piece of the mechanic adds its own.
SpireLevelTable spire_level_parse(const std::string& json_text);

// The episode of a quest's room k (k16-k30 are Episode 2, as spire_reward_room_index maps them)
constexpr uint8_t spire_level_episode_of_room(uint32_t k) {
  return (k > SPIRE_LEVEL_EPISODE_ROOMS) ? 2 : 1;
}
// The copy's level on Normal's scale: level (1-based, as the player reads it) less the rung's levels; 0 below them
uint32_t spire_level_key(const SpireLevelTable& table, uint32_t level, uint8_t rung);
// The room index an offer of the quest's room k reckons with. room_index: the quest's own (spire_reward_room_index(k))
// or what a registrant before this one left. Never below room_index; room_index itself when the table is off or has
// no row for k's episode.
uint32_t spire_level_reward_room_index(const SpireLevelTable& table, uint32_t k, uint32_t room_index, uint32_t level,
    uint8_t rung);

// Tests only: reads the table from another file ("" = SPIRE_LEVELING_FILE again)
void spire_level_set_table_file(const std::string& path);
