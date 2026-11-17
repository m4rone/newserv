#include "SpireLevel.hh"

#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>

#include <stdint.h>

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "SpireMeta.hh"
#include "SpireRewardKinds.hh"

// Spire leveling: the table's parse, the pure rule of piece 1 and its registration on the seam's H2 (SpireLevel.hh).

SpireLevelTable spire_level_parse(const std::string& json_text) {
  auto json = phosg::JSON::parse(json_text);
  SpireLevelTable ret;
  ret.enabled = json.get_bool("Enabled");
  const auto& rungs = json.get_list("RungLevels");
  if (rungs.size() != ret.rung_levels.size()) {
    throw std::runtime_error(std::format("RungLevels has {} levels, not {}", rungs.size(), ret.rung_levels.size()));
  }
  for (size_t z = 0; z < ret.rung_levels.size(); z++) {
    int64_t v = rungs[z]->as_int();
    if ((v < 0) || (v > 199) || (z ? (v < static_cast<int64_t>(ret.rung_levels[z - 1])) : (v != 0))) {
      throw std::runtime_error(std::format("RungLevels[{}] is {}: Normal's is 0, the others 0-199 and never falling", z, v));
    }
    ret.rung_levels[z] = v;
  }
  for (const auto& [name, episode_json] : json.get_dict("Episodes")) {
    if ((name != "1") && (name != "2")) {
      throw std::runtime_error(std::format("Episodes {}: an episode is 1 or 2", name));
    }
    const auto& levels = episode_json->get_list("RoomEndLevels");
    if (levels.empty() || (levels.size() > SPIRE_LEVEL_EPISODE_ROOMS)) {
      throw std::runtime_error(std::format("Episodes {} RoomEndLevels has {} rooms, not 1-{}", name, levels.size(),
          SPIRE_LEVEL_EPISODE_ROOMS));
    }
    std::vector<uint32_t> row;
    for (const auto& level_json : levels) {
      int64_t v = level_json->as_int();
      if ((v < 1) || (v > 200) || (!row.empty() && (v < static_cast<int64_t>(row.back())))) {
        throw std::runtime_error(std::format("Episodes {} RoomEndLevels[{}] is {}: a level is 1-200 and never falls",
            name, row.size(), v));
      }
      row.emplace_back(v);
    }
    ret.room_end_levels.emplace(static_cast<uint8_t>(name[0] - '0'), std::move(row));
  }
  return ret;
}

uint32_t spire_level_key(const SpireLevelTable& table, uint32_t level, uint8_t rung) {
  uint32_t rung_levels = table.rung_levels[rung & 3];
  return (level > rung_levels) ? (level - rung_levels) : 0;
}

uint32_t spire_level_reward_room_index(const SpireLevelTable& table, uint32_t k, uint32_t room_index, uint32_t level,
    uint8_t rung) {
  if (!table.enabled) {
    return room_index;
  }
  auto it = table.room_end_levels.find(spire_level_episode_of_room(k));
  if (it == table.room_end_levels.end()) {
    return room_index;
  }
  uint32_t key = spire_level_key(table, level, rung);
  uint32_t ret = room_index;
  for (size_t z = 0; (z < it->second.size()) && (it->second[z] <= key); z++) {
    ret = std::max<uint32_t>(ret, z + 1);
  }
  return ret;
}

namespace {

// The file the table is read from, and the reason it was last refused for (logged once per new reason)
struct TableFile {
  std::string path = SPIRE_LEVELING_FILE;
  std::string refused;
};

// A static inside a function, as the seam's registries: built at its first use, whichever translation unit starts first
TableFile& table_file() {
  static TableFile file;
  return file;
}

// The table as its file gives it now; nullopt when it cannot be read (then nothing changes)
std::optional<SpireLevelTable> table_now() {
  auto& file = table_file();
  try {
    auto ret = spire_level_parse(phosg::load_file(file.path));
    file.refused.clear();
    return ret;
  } catch (const std::exception& e) {
    if (file.refused != e.what()) {
      file.refused = e.what();
      phosg::log_warning_f("Spire level: {} is unreadable ({}); every offer stays on the quest's own room", file.path,
          e.what());
    }
    return std::nullopt;
  }
}

// H2 reward_room: the room index an offer reckons with follows the run copy's level. The rung (trede) is not this
// mechanic's in piece 1 and stays as it came.
void reward_room(const SpireMechRewardRoom& at, uint32_t& room_index, uint32_t&) {
  if (!at.c || !at.l) {
    return; // no client or game (a test of the hook alone): no run copy to read
  }
  auto overlay = at.c->has_overlay() ? at.c->character_file(false, true) : nullptr;
  if (!overlay) {
    return;
  }
  auto table = table_now();
  if (!table) {
    return;
  }
  uint32_t quest_number = at.l->quest ? at.l->quest->meta.quest_number : 0;
  uint8_t rung = spire_meta_ladder_quest_rung(quest_number);
  uint32_t level = static_cast<uint32_t>(overlay->disp.stats.level) + 1;
  uint32_t to = spire_level_reward_room_index(*table, at.k, room_index, level, rung);
  if (to == room_index) {
    return;
  }
  at.c->log.info_f("Spire level: the reward of room {} for serial {:010} reckons with room index {} instead of {}: the "
                   "run copy is Lv{} on {} (Lv{} on Normal's scale, start level bonus {})",
      at.k, at.serial_number, to, room_index, level, spire_meta_difficulty_name(rung),
      spire_level_key(*table, level, rung),
      spire_start_level_run_bonus(at.c.get(), at.l->lobby_id, quest_number).value_or(0));
  room_index = to;
}

[[maybe_unused]] const bool reward_room_registered = spire_mech_register_reward_room(
    {"leveling", SPIRE_LEVELING_SWITCH, SPIRE_MECH_ORDER_LEVELING, reward_room});

} // namespace

void spire_level_set_table_file(const std::string& path) {
  table_file() = TableFile{};
  if (!path.empty()) {
    table_file().path = path;
  }
}
