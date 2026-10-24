#include "SpireMeta.hh"

#include <filesystem>
#include <format>
#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>

#include "CommandFormats.hh"
#include "SendCommands.hh"
#include "SpireFlagBridge.hh"
#include "StaticGameData.hh"

// Keyed by (difficulty, flag number); true = set, false = cleared. Same meaning as the bridge's flag entries.
using SpireMetaFlags = std::map<std::pair<uint8_t, uint16_t>, bool>;

static std::string spire_meta_filename(std::shared_ptr<Client> c, uint32_t serial_number) {
  auto character = c->character_file(false, false);
  if (!character) {
    return "";
  }
  return std::format("{}/{:010}.{}.meta.json", SPIRE_BRIDGE_DIRECTORY, serial_number,
      spire_guard_character_id(character->disp));
}

static SpireMetaFlags spire_meta_load(const std::string& filename) {
  SpireMetaFlags ret;
  if (!std::filesystem::is_regular_file(filename)) {
    return ret;
  }
  auto json = phosg::JSON::parse(phosg::load_file(filename));
  for (const auto& entry_json : json.get_list("Flags")) {
    uint8_t difficulty = entry_json->get_int("Difficulty");
    uint16_t flag_num = entry_json->get_int("Flag");
    if ((difficulty <= 3) && (flag_num >= SPIRE_BRIDGE_FIRST_FLAG) && (flag_num <= SPIRE_BRIDGE_LAST_FLAG)) {
      ret[std::make_pair(difficulty, flag_num)] = entry_json->get_bool("Set");
    }
  }
  return ret;
}

static void spire_meta_save(const std::string& filename, uint32_t serial_number, const SpireMetaFlags& flags) {
  phosg::JSON flags_json = phosg::JSON::list();
  for (const auto& [key, is_set] : flags) {
    flags_json.emplace_back(phosg::JSON::dict({{"Difficulty", key.first}, {"Flag", key.second}, {"Set", is_set}}));
  }
  auto json = phosg::JSON::dict({{"SerialNumber", serial_number}, {"Flags", std::move(flags_json)}});
  spire_bridge_write_durable(filename,
      json.serialize(phosg::JSON::SerializeOption::FORMAT | phosg::JSON::SerializeOption::HEX_INTEGERS));
}

bool spire_meta_record_flag(std::shared_ptr<Client> c, uint32_t serial_number, uint8_t difficulty, uint16_t flag_num,
    bool is_set) {
  std::string filename = spire_meta_filename(c, serial_number);
  if (filename.empty()) {
    return false;
  }
  auto flags = spire_meta_load(filename);
  flags[std::make_pair(difficulty, flag_num)] = is_set;
  spire_meta_save(filename, serial_number, flags);
  c->log.info_f("Spire meta: recorded {} quest flag {}:{:04X} in {} ({} stored)", is_set ? "set" : "cleared",
      name_for_difficulty(static_cast<Difficulty>(difficulty)), flag_num, filename, flags.size());
  return true;
}

void spire_meta_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number) {
  try {
    std::string filename = spire_meta_filename(c, serial_number);
    if (filename.empty()) {
      c->log.info_f("Spire meta: no character data for serial {:010}; no flags delivered", serial_number);
      return;
    }
    auto flags = spire_meta_load(filename);
    if (flags.empty()) {
      return;
    }
    // Same 6x75 form and 6D path as the bridge's delivery (send_game_flag_state_t); action 0 sets, 1 clears
    phosg::StringWriter w;
    for (const auto& [key, is_set] : flags) {
      w.put(G_UpdateQuestFlag_V3_BB_6x75{
          {{0x75, 0x03, 0x0000}, key.second, static_cast<uint16_t>(is_set ? 0 : 1)}, key.first, 0});
    }
    if (c->game_join_command_queue) {
      auto& cmd = c->game_join_command_queue->emplace_back();
      cmd.command = 0x006D;
      cmd.flag = c->lobby_client_id;
      cmd.data = std::move(w.str());
    } else {
      send_command(c, 0x6D, c->lobby_client_id, w.str());
    }
    c->log.info_f("Spire meta: delivered {} quest flag(s) from {} in {} game", flags.size(), filename,
        (l->mode == GameMode::BATTLE) ? "Battle" : "non-Battle");
  } catch (const std::exception& e) {
    c->log.warning_f("Spire meta: failed to deliver quest flags: {}", e.what());
  }
}

SpireMetaFinish spire_meta_decode_finish(uint32_t finish_value) {
  SpireMetaFinish ret;
  ret.won = (finish_value >> 23) & 1;
  ret.exams = (finish_value >> SPIRE_META_FINISH_EXAMS_SHIFT) & 3;
  ret.deathless = finish_value & SPIRE_META_FINISH_DEATHLESS;
  if (ret.won) {
    ret.exams = 2; // a quest from before v24 sends only the won bit; a win always passed both exams
  }
  if (ret.exams > 2) {
    ret.exams = 2;
  }
  return ret;
}

std::vector<uint16_t> spire_meta_flags_for_finish(const SpireMetaFinish& f, uint8_t char_class, bool new_best_time) {
  // Same rules as score.py score_and_persist (E04 design § Flags, E06 design § Flagtabel)
  std::vector<uint16_t> ret{SPIRE_META_FLAG_FIRST_RUN};
  if (f.exams >= 1) {
    ret.insert(ret.end(), {SPIRE_META_FLAG_DRAGON, SPIRE_META_FLAG_PACT_FIRST, SPIRE_META_FLAG_PACT_FIRST + 1});
  }
  if (f.exams >= 2) {
    ret.insert(ret.end(), {SPIRE_META_FLAG_DEROLLE, SPIRE_META_FLAG_PACT_FIRST + 2, SPIRE_META_FLAG_PACT_FIRST + 3,
                              SPIRE_META_FLAG_PAY_WIN});
    if (f.deathless) {
      ret.insert(ret.end(), {SPIRE_META_FLAG_DEATHLESS_WIN, SPIRE_META_FLAG_PAY_DEATHLESS});
      if (char_class_is_hunter(char_class)) {
        ret.emplace_back(SPIRE_META_FLAG_KIT2_FIRST);
      } else if (char_class_is_ranger(char_class)) {
        ret.emplace_back(SPIRE_META_FLAG_KIT2_FIRST + 1);
      } else if (char_class_is_force(char_class)) {
        ret.emplace_back(SPIRE_META_FLAG_KIT2_FIRST + 2);
      }
    }
    if (new_best_time) {
      ret.emplace_back(SPIRE_META_FLAG_PAY_RECORD);
    }
  }
  return ret;
}

const char* spire_meta_flag_name(uint16_t flag_num) {
  switch (flag_num) {
    case SPIRE_META_FLAG_PACT_FIRST:
      return "Iron man";
    case SPIRE_META_FLAG_PACT_FIRST + 1:
      return "Haste";
    case SPIRE_META_FLAG_PACT_FIRST + 2:
      return "Bare";
    case SPIRE_META_FLAG_PACT_FIRST + 3:
      return "Hungry";
    case SPIRE_META_FLAG_KIT2_FIRST:
    case SPIRE_META_FLAG_KIT2_FIRST + 1:
    case SPIRE_META_FLAG_KIT2_FIRST + 2:
      return "Second kit";
    default:
      return nullptr;
  }
}

std::string spire_meta_on_finish(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t finish_value, bool new_best_time) {
  std::string filename = spire_meta_filename(c, serial_number);
  auto character = c->character_file(false, false);
  if (filename.empty() || !character) {
    c->log.info_f("Spire meta: no character data for serial {:010}; no unlocks", serial_number);
    return "";
  }
  auto finish = spire_meta_decode_finish(finish_value);
  uint8_t difficulty = static_cast<uint8_t>(l->difficulty);
  auto flags = spire_meta_load(filename);
  std::string unlocked;
  bool payout = false;
  std::string set_str;
  for (uint16_t flag_num : spire_meta_flags_for_finish(finish, character->disp.visual.sh.char_class, new_best_time)) {
    auto key = std::make_pair(difficulty, flag_num);
    auto it = flags.find(key);
    bool was_set = (it != flags.end()) && it->second;
    flags[key] = true;
    set_str += std::format(" {:04X}{}", flag_num, was_set ? "" : "*");
    if (flag_num >= SPIRE_META_FLAG_PAY_WIN) {
      payout = true; // the payout flags are cleared by q925, so each win opens them again
    } else if (!was_set) {
      if (const char* name = spire_meta_flag_name(flag_num)) {
        unlocked += unlocked.empty() ? name : std::format(", {}", name);
      }
    }
  }
  spire_meta_save(filename, serial_number, flags);
  spire_meta_deliver_flags(c, l, serial_number);
  c->log.info_f("Spire meta: finish {:08X} (exams {}, {}, {}) on {}: flags{} (* = new) in {}", finish_value,
      finish.exams, finish.won ? "won" : "not won", finish.deathless ? "deathless" : "deaths",
      name_for_difficulty(l->difficulty), set_str, filename);

  std::string text;
  if (!unlocked.empty()) {
    text += std::format("\nUnlocked: {}.", unlocked);
  }
  if (payout) {
    text += "\nReward waiting: see the Quartermaster.";
  }
  return text;
}
