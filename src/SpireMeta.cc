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


// Always the real character (allow_overlay false): a Battle overlay keeps name, class and section ID, so its id is the
// real character's anyway, but the file is never chosen from the overlay's data.
static std::string spire_meta_filename(std::shared_ptr<Client> c, uint32_t serial_number) {
  auto character = c->character_file(false, false);
  return spire_meta_filename_for(serial_number, character ? &character->disp : nullptr);
}

std::string spire_meta_filename_for(uint32_t serial_number, const PlayerDispDataV4* disp) {
  if (!disp) {
    return "";
  }
  return std::format("{}/{:010}.{}.meta.json", SPIRE_BRIDGE_DIRECTORY, serial_number, spire_guard_character_id(*disp));
}

std::vector<SpireMetaDelivery> spire_meta_delivery_entries(const SpireMetaFile& file, uint8_t difficulty,
    bool just_adopted) {
  std::vector<SpireMetaDelivery> ret;
  for (const auto& [key, is_set] : file.flags) {
    ret.emplace_back(SpireMetaDelivery{key.first, key.second, is_set});
  }
  // B-REGIE-10-2: the delivery that adopted the file clears nothing (the character keeps what it has), and an UNKNOWN
  // file never clears a flag without an entry: the server never saw it, so it may be the character's own
  if (just_adopted || (file.adopted == SpireMetaAdopted::UNKNOWN)) {
    return ret;
  }
  // Audit S-6: what the server owns but this character never wrote is cleared on the game's difficulty, so a flag the
  // leader's join sync or an earlier character put into the client's game is not taken over
  for (uint16_t flag_num = SPIRE_BRIDGE_FIRST_FLAG; flag_num <= SPIRE_META_SERVER_LAST_FLAG; flag_num++) {
    if (!file.flags.count(std::make_pair(difficulty, flag_num))) {
      ret.emplace_back(SpireMetaDelivery{difficulty, flag_num, false});
    }
  }
  return ret;
}

bool spire_meta_flag_is_shared(bool meta_game, uint16_t flag_num) {
  return !meta_game || (flag_num < SPIRE_BRIDGE_FIRST_FLAG) || (flag_num > SPIRE_BRIDGE_LAST_FLAG);
}

SpireMetaFile spire_meta_load_file(const std::string& filename) {
  SpireMetaFile ret;
  if (!std::filesystem::is_regular_file(filename)) {
    return ret;
  }
  ret.exists = true;
  auto json = phosg::JSON::parse(phosg::load_file(filename));
  std::string adopted = json.get_string("Adopted", "");
  if (adopted == "unknown") {
    ret.adopted = SpireMetaAdopted::UNKNOWN;
  } else if (adopted == "known") {
    ret.adopted = SpireMetaAdopted::KNOWN;
  }
  for (const auto& entry_json : json.get_list("Flags")) {
    uint8_t difficulty = entry_json->get_int("Difficulty");
    uint16_t flag_num = entry_json->get_int("Flag");
    if ((difficulty <= 3) && (flag_num >= SPIRE_BRIDGE_FIRST_FLAG) && (flag_num <= SPIRE_BRIDGE_LAST_FLAG)) {
      ret.flags[std::make_pair(difficulty, flag_num)] = entry_json->get_bool("Set");
    }
  }
  return ret;
}

SpireMetaFlags spire_meta_load(const std::string& filename) {
  return spire_meta_load_file(filename).flags;
}

SpireMetaFile spire_meta_adopt(const QuestFlags* current) {
  SpireMetaFile ret;
  ret.exists = true;
  if (!current) {
    ret.adopted = SpireMetaAdopted::UNKNOWN;
    return ret;
  }
  ret.adopted = SpireMetaAdopted::KNOWN;
  for (uint8_t difficulty = 0; difficulty <= 3; difficulty++) {
    for (uint16_t flag_num = SPIRE_BRIDGE_FIRST_FLAG; flag_num <= SPIRE_META_SERVER_LAST_FLAG; flag_num++) {
      if (current->get(static_cast<Difficulty>(difficulty), flag_num)) {
        ret.flags[std::make_pair(difficulty, flag_num)] = true;
      }
    }
  }
  return ret;
}

static void spire_meta_save(const std::string& filename, uint32_t serial_number, const SpireMetaFile& file) {
  phosg::JSON flags_json = phosg::JSON::list();
  for (const auto& [key, is_set] : file.flags) {
    flags_json.emplace_back(phosg::JSON::dict({{"Difficulty", key.first}, {"Flag", key.second}, {"Set", is_set}}));
  }
  auto json = phosg::JSON::dict({{"SerialNumber", serial_number}, {"Flags", std::move(flags_json)}});
  if (file.adopted == SpireMetaAdopted::UNKNOWN) {
    json.emplace("Adopted", "unknown");
  } else if (file.adopted == SpireMetaAdopted::KNOWN) {
    json.emplace("Adopted", "known");
  }
  spire_bridge_write_durable(filename,
      json.serialize(phosg::JSON::SerializeOption::FORMAT | phosg::JSON::SerializeOption::HEX_INTEGERS));
}

// B-REGIE-10-2: the character's file, adopted (and written) first if it has none. The server does not know a GC v3
// character's quest flags (see SpireMeta.hh), so the adoption is UNKNOWN. Sets *adopted if it adopted the file now.
static SpireMetaFile spire_meta_load_or_adopt(
    std::shared_ptr<Client> c, const std::string& filename, uint32_t serial_number, bool* adopted) {
  auto file = spire_meta_load_file(filename);
  *adopted = !file.exists;
  if (*adopted) {
    file = spire_meta_adopt(nullptr);
    spire_meta_save(filename, serial_number, file);
    c->log.info_f("Spire meta: adopted {} (Adopted: unknown); no flag cleared that the server never saw", filename);
  }
  return file;
}

bool spire_meta_record_flag(std::shared_ptr<Client> c, uint32_t serial_number, uint8_t difficulty, uint16_t flag_num,
    bool is_set) {
  std::string filename = spire_meta_filename(c, serial_number);
  if (filename.empty()) {
    return false;
  }
  bool adopted;
  auto file = spire_meta_load_or_adopt(c, filename, serial_number, &adopted);
  auto& flags = file.flags;
  flags[std::make_pair(difficulty, flag_num)] = is_set;
  spire_meta_save(filename, serial_number, file);
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
    bool adopted;
    auto file = spire_meta_load_or_adopt(c, filename, serial_number, &adopted);
    auto entries = spire_meta_delivery_entries(file, static_cast<uint8_t>(l->difficulty), adopted);
    if (entries.empty()) {
      c->log.info_f("Spire meta: nothing to deliver from {} ({})", filename, adopted ? "just adopted" : "no entries");
      return;
    }
    // Same 6x75 form and 6D path as the bridge's delivery (send_game_flag_state_t); action 0 sets, 1 clears
    phosg::StringWriter w;
    for (const auto& e : entries) {
      w.put(G_UpdateQuestFlag_V3_BB_6x75{
          {{0x75, 0x03, 0x0000}, e.flag, static_cast<uint16_t>(e.set ? 0 : 1)}, e.difficulty, 0});
    }
    if (c->game_join_command_queue) {
      auto& cmd = c->game_join_command_queue->emplace_back();
      cmd.command = 0x006D;
      cmd.flag = c->lobby_client_id;
      cmd.data = std::move(w.str());
    } else {
      send_command(c, 0x6D, c->lobby_client_id, w.str());
    }
    c->log.info_f("Spire meta: delivered {} quest flag(s) from {} ({} stored) in {} game", entries.size(), filename,
        file.flags.size(),
        (l->mode == GameMode::BATTLE) ? "Battle" : "non-Battle");
  } catch (const std::exception& e) {
    c->log.warning_f("Spire meta: failed to deliver quest flags: {}", e.what());
  }
}

bool spire_meta_delivery_applies(bool is_game, bool has_quest, bool spire_quest, bool meta_quest) {
  return is_game && has_quest && (spire_quest || meta_quest);
}

bool spire_meta_record_applies(bool bridged_quest, bool meta_quest, uint16_t flag_num, bool is_set) {
  if (bridged_quest) {
    return true;
  }
  return meta_quest && !is_set && (flag_num >= SPIRE_META_FLAG_PAY_WIN) && (flag_num <= SPIRE_META_FLAG_PAY_RECORD);
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
  bool adopted;
  auto file = spire_meta_load_or_adopt(c, filename, serial_number, &adopted);
  auto& flags = file.flags;
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
  spire_meta_save(filename, serial_number, file);
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
