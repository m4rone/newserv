#include "SpireMeta.hh"

#include <algorithm>
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
  if (json.contains("BaseRung")) { // v49 (B-BASIS-13)
    for (uint8_t run = 1; run <= 2; run++) {
      int64_t rung = json.at("BaseRung").get_int(std::to_string(run), 0);
      if (rung > 0) {
        ret.base_rung[run] = std::min<int64_t>(rung, 3);
      }
    }
  }
  if (json.contains("Upgrades")) { // v50 (B-BASIS-2)
    for (const auto& [name, level_json] : json.get_dict("Upgrades")) {
      int64_t level = level_json->as_int();
      if (level > 0) {
        ret.upgrades[name] = std::min<int64_t>(level, 0xFFFF);
      }
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
  if (!file.base_rung.empty()) { // v49 (B-BASIS-13): kept through every flag write, since each one loads the file first
    auto base_rung_json = phosg::JSON::dict();
    for (const auto& [run, rung] : file.base_rung) {
      base_rung_json.emplace(std::to_string(run), rung);
    }
    json.emplace("BaseRung", std::move(base_rung_json));
  }
  if (!file.upgrades.empty()) { // v50 (B-BASIS-2): kept through every flag write, as BaseRung
    auto upgrades_json = phosg::JSON::dict();
    for (const auto& [name, level] : file.upgrades) {
      upgrades_json.emplace(name, level);
    }
    json.emplace("Upgrades", std::move(upgrades_json));
  }
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
  ret.pacts = std::min<uint8_t>((finish_value >> SPIRE_META_FINISH_PACTS_SHIFT) & 7, SPIRE_META_FINISH_PACTS_MAX);
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

uint8_t spire_meta_ladder_run(uint32_t quest_number) {
  if ((quest_number == 920) || (quest_number == 921) || ((quest_number >= 981) && (quest_number <= 983))) {
    return 1;
  }
  if ((quest_number == 922) || ((quest_number >= 984) && (quest_number <= 986))) {
    return 2;
  }
  return 0;
}

uint8_t spire_meta_ladder_quest_rung(uint32_t quest_number) {
  if ((quest_number >= 981) && (quest_number <= 986)) {
    return (quest_number - 981) % 3 + 1;
  }
  return 0;
}

uint16_t spire_meta_ladder_flag(uint8_t run) {
  return (run == 1) ? SPIRE_META_FLAG_RUN1_WON : (run == 2) ? SPIRE_META_FLAG_RUN2_WON : 0;
}

uint8_t spire_meta_ladder_rung(const SpireMetaFlags& flags, uint8_t run) {
  uint16_t flag_num = spire_meta_ladder_flag(run);
  uint8_t ret = 0;
  if (!flag_num) {
    return ret;
  }
  // a rung opens only from the one below it: a won Hard with an unwon Normal (a file edited by hand) still opens VH,
  // since the win on Hard needed Hard open; the highest won difficulty decides
  for (uint8_t d = 0; d < 3; d++) {
    auto it = flags.find(std::make_pair(d, flag_num));
    if ((it != flags.end()) && it->second) {
      ret = d + 1;
    }
  }
  return ret;
}

uint8_t spire_meta_base_rung(const SpireMetaFile& file, uint8_t run) {
  auto it = file.base_rung.find(run);
  return (it == file.base_rung.end()) ? 0 : std::min<uint8_t>(it->second, 3);
}

uint8_t spire_meta_open_rung(uint8_t won_rung, uint8_t base_rung) {
  return std::min<uint8_t>(std::max(won_rung, base_rung), 3);
}

uint8_t spire_meta_open_rung(const SpireMetaFile& file, uint8_t run) {
  if (!spire_meta_ladder_flag(run)) {
    return 0; // a quest of neither run has no ladder
  }
  return spire_meta_open_rung(spire_meta_ladder_rung(file.flags, run), spire_meta_base_rung(file, run));
}

uint8_t spire_meta_flag_difficulty(uint16_t flag_num, uint8_t game_difficulty) {
  return ((flag_num >= SPIRE_META_FLAG_PAY_WIN) && (flag_num <= SPIRE_META_FLAG_PAY_RECORD)) ? 0 : game_difficulty;
}

SpireLadderGate spire_meta_ladder_gate(bool battle_game, bool spire_character, uint8_t rung, uint8_t difficulty,
    uint32_t char_level, uint32_t min_level) {
  if (min_level <= char_level) {
    return SpireLadderGate::LEVEL_OK;
  }
  return (battle_game && spire_character && (difficulty <= rung)) ? SpireLadderGate::RUNG_OPEN : SpireLadderGate::REFUSED;
}

static const char* spire_ladder_difficulty_name(uint8_t difficulty) {
  static const char* names[4] = {"Normal", "Hard", "Very Hard", "Ultimate"};
  return names[difficulty & 3];
}

std::string spire_meta_ladder_refuse_text(uint8_t difficulty, uint32_t min_level) {
  // one line, Sega's message box words first; the Spire part names the win that opens the difficulty without the level
  return std::format("$C7{} needs level {}. Spire: win Spire Run on {} to open it.", spire_ladder_difficulty_name(difficulty),
      static_cast<size_t>(min_level + 1), spire_ladder_difficulty_name(difficulty ? (difficulty - 1) : 0));
}

std::string spire_meta_ladder_menu_note(uint8_t rung1, uint8_t rung2, uint8_t difficulty) {
  if (!difficulty) {
    return "";
  }
  const char* below = spire_ladder_difficulty_name(difficulty - 1);
  const char* here = spire_ladder_difficulty_name(difficulty);
  bool open1 = difficulty <= rung1, open2 = difficulty <= rung2;
  if (open1 && open2) {
    return "";
  } else if (!open1 && !open2) {
    return std::format("Spire Run on {} opens after a win on {}.", here, below);
  } else if (!open1) {
    return std::format("Spire Run I on {} opens after a win on {}.", here, below);
  }
  return std::format("Spire Run II on {} opens after a win on {}.", here, below);
}

uint8_t spire_meta_ladder_rung_for_client(std::shared_ptr<const Client> c, uint8_t run) {
  try {
    if (!c || (c->version() != Version::GC_V3) || !c->login || !c->login->gc_license) {
      return 0;
    }
    auto character = c->character_file(false, false);
    std::string filename = spire_meta_filename_for(c->login->gc_license->serial_number,
        character ? &character->disp : nullptr);
    return filename.empty() ? 0 : spire_meta_open_rung(spire_meta_load_file(filename), run); // v49: won or BaseRung
  } catch (const std::exception& e) {
    c->log.warning_f("Spire meta: ladder rung unreadable: {}; Normal only", e.what());
    return 0;
  }
}

SpireMetaFinishApplied spire_meta_apply_finish(SpireMetaFlags& flags, const std::vector<uint16_t>& finish_flags,
    uint8_t difficulty, uint8_t ladder_run, bool open_payout) {
  SpireMetaFinishApplied ret;
  for (uint16_t flag_num : finish_flags) {
    auto key = std::make_pair(spire_meta_flag_difficulty(flag_num, difficulty), flag_num);
    auto it = flags.find(key);
    bool was_set = (it != flags.end()) && it->second;
    if ((flag_num >= SPIRE_META_FLAG_PAY_WIN) && !open_payout) {
      ret.set_str += std::format(" {:04X}(repeat: not set)", flag_num);
      continue;
    }
    flags[key] = true;
    ret.set_str += std::format(" {:04X}{}", flag_num, was_set ? "" : "*");
    if (flag_num >= SPIRE_META_FLAG_PAY_WIN) {
      ret.payout = true; // the payout flags are cleared by q925, so each won run opens them again at its first won 'F'
    } else if ((flag_num == SPIRE_META_FLAG_RUN1_WON) || (flag_num == SPIRE_META_FLAG_RUN2_WON)) {
      if (!was_set && (difficulty < 3)) {
        std::string what = std::format("Spire Run {} on {}", (ladder_run == 1) ? "I" : "II",
            spire_ladder_difficulty_name(difficulty + 1));
        ret.unlocked += ret.unlocked.empty() ? what : std::format(", {}", what);
      }
    } else if (!was_set) {
      if (const char* name = spire_meta_flag_name(flag_num)) {
        ret.unlocked += ret.unlocked.empty() ? name : std::format(", {}", name);
      }
    }
  }
  return ret;
}

std::string spire_meta_on_finish(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t finish_value, bool new_best_time, bool open_payout) {
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
  auto finish_flags = spire_meta_flags_for_finish(finish, character->disp.visual.sh.char_class, new_best_time);
  uint8_t ladder_run = spire_meta_ladder_run(quest_number);
  if (finish.won && ladder_run) {
    finish_flags.emplace_back(spire_meta_ladder_flag(ladder_run)); // the next rung of this run (plan-unlockables §2)
  }
  auto applied = spire_meta_apply_finish(file.flags, finish_flags, difficulty, ladder_run, open_payout);
  const auto& unlocked = applied.unlocked;
  bool payout = applied.payout;
  const auto& set_str = applied.set_str;
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
    text += difficulty ? "\nReward waiting on Normal: see the Quartermaster." : "\nReward waiting: see the Quartermaster.";
  }
  return text;
}

// v50: the base shop (SpireMeta.hh)

SpireShopCatalogue spire_shop_parse(const std::string& json_text) {
  auto json = phosg::JSON::parse(json_text);
  SpireShopCatalogue ret;
  const auto& items_json = json.at("Items");
  for (size_t z = 0; z < SPIRE_SHOP_NUM_ITEMS; z++) {
    const char* name = SPIRE_SHOP_ITEM_NAMES[z];
    if (!items_json.contains(name)) {
      throw std::runtime_error(std::format("catalogue has no item {}", name));
    }
    const auto& item_json = items_json.at(name);
    SpireShopItem item;
    item.max = item_json.get_int("Max");
    item.on_sale = item_json.get_int("OnSale", item.max);
    item.rung = (z >= 4) ? (z - 3) : 0;
    if (item.on_sale > item.max) {
      throw std::runtime_error(std::format("item {}: OnSale {} above Max {}", name, item.on_sale, item.max));
    }
    for (const auto& v : item_json.get_list("Price")) {
      item.price.emplace_back(v->as_int());
    }
    if (item.price.size() < item.on_sale) {
      throw std::runtime_error(std::format("item {}: {} price(s) for {} level(s) on sale", name, item.price.size(),
          item.on_sale));
    }
    if (item.rung) {
      item.p_needed = item_json.get_int("PNeeded");
    } else {
      for (const auto& v : item_json.get_list("P")) {
        item.strength.emplace_back(v->as_int());
      }
      if (item.strength.size() != item.max + 1) {
        throw std::runtime_error(std::format("item {}: P has {} entries for levels 0-{}", name, item.strength.size(),
            item.max));
      }
      // route B F3: what each level gives; without "Levels" level n gives n
      if (item_json.contains("Levels")) {
        for (const auto& v : item_json.get_list("Levels")) {
          item.levels.emplace_back(v->as_int());
        }
        if (item.levels.size() != item.max + 1) {
          throw std::runtime_error(std::format("item {}: Levels has {} entries for levels 0-{}", name,
              item.levels.size(), item.max));
        }
      } else {
        for (uint32_t level = 0; level <= item.max; level++) {
          item.levels.emplace_back(level);
        }
      }
    }
    ret.items.emplace_back(std::move(item));
  }
  // route B F2: the splinter payout, strict (a typo must not quietly fall back to the old payout)
  if (json.contains("Payout")) {
    const auto& payout_json = json.at("Payout");
    if (!payout_json.is_dict()) {
      throw std::runtime_error("Payout is no dict");
    }
    for (const auto& [key, _] : payout_json.as_dict()) {
      if (!key.starts_with("_") && (key != "PerRoom") && (key != "PerBoss") && (key != "PactTenths")) {
        throw std::runtime_error(std::format("Payout has an unknown key {}", key));
      }
    }
    auto value = [&](const char* key, uint32_t max) -> uint32_t {
      if (!payout_json.contains(key)) {
        throw std::runtime_error(std::format("Payout has no {}", key));
      }
      const auto& v = payout_json.at(key);
      if (!v.is_int() || (v.as_int() < 0) || (v.as_int() > static_cast<int64_t>(max))) {
        throw std::runtime_error(std::format("Payout {} is no integer in 0-{}", key, max));
      }
      return v.as_int();
    };
    ret.payout = SpireShopPayout{
        .per_room = value("PerRoom", SPIRE_SHOP_PAYOUT_PER_ROOM_MAX),
        .per_boss = value("PerBoss", SPIRE_SHOP_PAYOUT_PER_ROOM_MAX),
        .pact_tenths = value("PactTenths", SPIRE_SHOP_PAYOUT_PACT_TENTHS_MAX)};
  }
  const auto& heat_json = json.at("MetaHeat");
  for (size_t rung = 0; rung < ret.heat.size(); rung++) {
    if ((rung > 0) && !heat_json.contains(SPIRE_SHOP_HEAT_KEYS[rung])) {
      continue; // v54: a rung without its table plays H 0
    }
    for (const auto& [cls, row_json] : heat_json.get_dict(SPIRE_SHOP_HEAT_KEYS[rung])) {
      auto& row = ret.heat[rung][cls];
      for (const auto& v : row_json->as_list()) {
        row.emplace_back(v->is_null() ? -1 : static_cast<int32_t>(v->as_int()));
      }
    }
  }
  return ret;
}

uint32_t spire_shop_level(const std::map<std::string, uint32_t>& upgrades, size_t index) {
  if (index >= SPIRE_SHOP_NUM_ITEMS) {
    return 0;
  }
  auto it = upgrades.find(SPIRE_SHOP_ITEM_NAMES[index]);
  return (it == upgrades.end()) ? 0 : it->second;
}

uint32_t spire_shop_strength(const SpireShopCatalogue& cat, const std::map<std::string, uint32_t>& upgrades) {
  uint32_t ret = 0;
  for (size_t z = 0; z < cat.items.size(); z++) {
    const auto& item = cat.items[z];
    if (!item.strength.empty()) {
      ret += item.strength[std::min<size_t>(spire_shop_level(upgrades, z), item.strength.size() - 1)];
    }
  }
  return ret;
}

uint32_t spire_shop_heat(const SpireShopCatalogue& cat, uint8_t char_class, uint32_t strength, uint8_t rung) {
  if (rung >= cat.heat.size()) {
    return 0;
  }
  const auto& table = cat.heat[rung];
  auto it = table.find(name_for_char_class(char_class));
  if ((it == table.end()) || it->second.empty()) {
    return 0;
  }
  // P past the table: its last column; a cell without a heat (no H keeps the band): the last heat at a lower P
  for (size_t p = std::min<size_t>(strength, it->second.size() - 1) + 1; p > 0; p--) {
    if (it->second[p - 1] >= 0) {
      return it->second[p - 1];
    }
  }
  return 0;
}

const char* spire_shop_refusal_name(SpireShopRefusal r) {
  switch (r) {
    case SpireShopRefusal::NONE:
      return "none";
    case SpireShopRefusal::NO_CHARACTER:
      return "no character";
    case SpireShopRefusal::NO_CATALOGUE:
      return "no catalogue";
    case SpireShopRefusal::UNKNOWN_ITEM:
      return "unknown item or request";
    case SpireShopRefusal::MAX_LEVEL:
      return "at its cap";
    case SpireShopRefusal::NOT_ON_SALE:
      return "next level not on sale";
    case SpireShopRefusal::STRENGTH:
      return "strength below the threshold";
    case SpireShopRefusal::RUNG_CLOSED:
      return "the rung below is not open";
    case SpireShopRefusal::ALREADY_OPEN:
      return "already open for both runs";
    case SpireShopRefusal::BALANCE:
      return "not enough splinters";
    case SpireShopRefusal::FILE_ERROR:
      return "file error";
  }
  return "unknown";
}

// Every refusal but the balance
static SpireShopRefusal spire_shop_item_refusal(const SpireShopCatalogue& cat, size_t index,
    const std::map<std::string, uint32_t>& upgrades, const uint8_t open_rung[2], bool kit2_owned) {
  if (index >= cat.items.size()) {
    return SpireShopRefusal::UNKNOWN_ITEM;
  }
  const auto& item = cat.items[index];
  uint32_t level = spire_shop_level(upgrades, index);
  if (level >= item.max) {
    return SpireShopRefusal::MAX_LEVEL;
  }
  if (level >= item.on_sale) {
    return SpireShopRefusal::NOT_ON_SALE;
  }
  if ((index == 1) && kit2_owned) { // SPIRE_SHOP_ITEM_NAMES[1] = kit_tier
    return SpireShopRefusal::ALREADY_OPEN; // route B F3: the second kit is the character's already
  }
  if (item.rung) {
    if (std::min(open_rung[0], open_rung[1]) >= item.rung) {
      return SpireShopRefusal::ALREADY_OPEN; // nothing to open: never take splinters for it
    }
    if (spire_shop_strength(cat, upgrades) < item.p_needed) {
      return SpireShopRefusal::STRENGTH;
    }
    if (std::max(open_rung[0], open_rung[1]) + 1 < item.rung) {
      return SpireShopRefusal::RUNG_CLOSED;
    }
  }
  return SpireShopRefusal::NONE;
}

SpireShopRefusal spire_shop_buy_refusal(const SpireShopCatalogue& cat, size_t index,
    const std::map<std::string, uint32_t>& upgrades, uint32_t balance, const uint8_t open_rung[2], bool kit2_owned) {
  auto r = spire_shop_item_refusal(cat, index, upgrades, open_rung, kit2_owned);
  if (r != SpireShopRefusal::NONE) {
    return r;
  }
  return (balance < cat.items[index].price[spire_shop_level(upgrades, index)]) ? SpireShopRefusal::BALANCE
                                                                                : SpireShopRefusal::NONE;
}

uint32_t spire_shop_next_price(const SpireShopCatalogue& cat, size_t index,
    const std::map<std::string, uint32_t>& upgrades, const uint8_t open_rung[2], bool kit2_owned) {
  if (spire_shop_item_refusal(cat, index, upgrades, open_rung, kit2_owned) != SpireShopRefusal::NONE) {
    return 0;
  }
  return cat.items[index].price[spire_shop_level(upgrades, index)];
}

uint32_t spire_shop_apply_buy(const SpireShopCatalogue& cat, size_t index, SpireMetaFile& file) {
  const auto& item = cat.items.at(index);
  uint32_t level = spire_shop_level(file.upgrades, index);
  uint32_t price = item.price.at(level);
  file.upgrades[SPIRE_SHOP_ITEM_NAMES[index]] = level + 1;
  if (item.rung) {
    for (uint8_t run = 1; run <= 2; run++) {
      file.base_rung[run] = std::max<uint8_t>(spire_meta_base_rung(file, run), item.rung);
    }
  }
  return price;
}

bool spire_meta_kit2_owned(const SpireMetaFile& file, uint8_t char_class) {
  uint16_t flag;
  if (char_class_is_hunter(char_class)) {
    flag = SPIRE_META_FLAG_KIT2_FIRST;
  } else if (char_class_is_ranger(char_class)) {
    flag = SPIRE_META_FLAG_KIT2_FIRST + 1;
  } else if (char_class_is_force(char_class)) {
    flag = SPIRE_META_FLAG_KIT2_FIRST + 2;
  } else {
    return false;
  }
  auto it = file.flags.find(std::make_pair(uint8_t(0), flag)); // Normal
  return (it != file.flags.end()) && it->second;
}

SpireMetaFile spire_meta_load_for_client(std::shared_ptr<Client> c, uint32_t serial_number, std::string* filename) {
  *filename = spire_meta_filename(c, serial_number);
  if (filename->empty()) {
    return SpireMetaFile();
  }
  bool adopted;
  return spire_meta_load_or_adopt(c, *filename, serial_number, &adopted);
}

void spire_meta_save_file(const std::string& filename, uint32_t serial_number, const SpireMetaFile& file) {
  spire_meta_save(filename, serial_number, file);
}

// v52: the start_level upgrade (SpireMeta.hh); route B F3: Levels[min(owned, Max)]
uint32_t spire_start_level_bonus(const SpireShopCatalogue* cat, const std::map<std::string, uint32_t>& upgrades) {
  uint32_t owned = spire_shop_level(upgrades, 0); // SPIRE_SHOP_ITEM_NAMES[0] = start_level
  if (cat && !cat->items.empty() && !cat->items[0].levels.empty()) {
    const auto& levels = cat->items[0].levels; // max + 1 entries (spire_shop_parse)
    return levels[std::min<size_t>(owned, levels.size() - 1)];
  }
  return SPIRE_START_LEVEL_FALLBACK_LEVELS[std::min<size_t>(owned, SPIRE_START_LEVEL_FALLBACK_LEVELS.size() - 1)];
}

uint32_t spire_start_level_char_level(uint32_t base_char_level, uint32_t bonus) {
  return std::min<uint64_t>(static_cast<uint64_t>(base_char_level) + bonus, SPIRE_START_LEVEL_LEVEL_CAP);
}

size_t spire_start_level_patch_bin(std::string& decompressed_bin, uint32_t base_char_level, uint32_t new_char_level) {
  auto le32 = [](uint32_t v) -> std::string {
    return std::string{static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
        static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
  };
  std::string pattern = "\x49" + le32(base_char_level) + "\xF8\x71";
  size_t count = 0;
  size_t found = std::string::npos;
  for (size_t pos = decompressed_bin.find(pattern); pos != std::string::npos;
       pos = decompressed_bin.find(pattern, pos + 1)) {
    count++;
    found = pos;
  }
  if (count == 1) {
    decompressed_bin.replace(found + 1, 4, le32(new_char_level));
  }
  return count;
}

// v53: the server-side heat steps (SpireMeta.hh)
static std::string spire_heat_hex_bytes(const std::string& hex) {
  auto nibble = [&](char ch) -> uint8_t {
    if ((ch >= '0') && (ch <= '9')) {
      return ch - '0';
    }
    if ((ch >= 'A') && (ch <= 'F')) {
      return ch - 'A' + 10;
    }
    if ((ch >= 'a') && (ch <= 'f')) {
      return ch - 'a' + 10;
    }
    throw std::runtime_error(std::format("value {} is not hex", hex));
  };
  if (hex.empty() || (hex.size() & 1)) {
    throw std::runtime_error(std::format("value {} has no whole bytes", hex));
  }
  std::string ret;
  for (size_t z = 0; z < hex.size(); z += 2) {
    ret.push_back(static_cast<char>((nibble(hex[z]) << 4) | nibble(hex[z + 1])));
  }
  return ret;
}

SpireHeatPatch spire_heat_patch_parse(const std::string& json_text) {
  auto json = phosg::JSON::parse(json_text);
  SpireHeatPatch ret;
  ret.quest_number = json.get_int("Quest");
  int64_t bin_size = json.get_int("BinSize");
  int64_t heat_max = json.get_int("HeatMax");
  if ((bin_size <= 0) || (heat_max < 0) || (heat_max > 255)) {
    throw std::runtime_error(std::format("BinSize {} or HeatMax {} out of range", bin_size, heat_max));
  }
  ret.bin_size = bin_size;
  ret.heat_max = heat_max;
  for (const auto& field_json : json.get_list("Fields")) {
    SpireHeatPatch::Field field;
    int64_t offset = field_json->get_int("Offset");
    for (const auto& v : field_json->get_list("Values")) {
      field.values.emplace_back(spire_heat_hex_bytes(v->as_string()));
    }
    if (field.values.size() != ret.heat_max + 1) {
      throw std::runtime_error(std::format("field at {}: {} value(s) for H 0..{}", offset, field.values.size(),
          ret.heat_max));
    }
    size_t width = field.values[0].size();
    for (const auto& v : field.values) {
      if (v.size() != width) {
        throw std::runtime_error(std::format("field at {}: values of {} and {} bytes", offset, width, v.size()));
      }
    }
    if (field_json->contains("Width") && (field_json->get_int("Width") != static_cast<int64_t>(width))) {
      throw std::runtime_error(std::format("field at {}: Width {} but {}-byte values", offset,
          field_json->get_int("Width"), width));
    }
    if ((offset < 0) || (static_cast<uint64_t>(offset) + width > ret.bin_size)) {
      throw std::runtime_error(std::format("field at {} (+{}) outside the {}-byte .bin", offset, width, ret.bin_size));
    }
    field.offset = offset;
    ret.fields.emplace_back(std::move(field));
  }
  std::sort(ret.fields.begin(), ret.fields.end(), [](const auto& a, const auto& b) { return a.offset < b.offset; });
  for (size_t z = 1; z < ret.fields.size(); z++) {
    const auto& prev = ret.fields[z - 1];
    if (prev.offset + prev.values[0].size() > ret.fields[z].offset) {
      throw std::runtime_error(std::format("fields at {} and {} overlap", prev.offset, ret.fields[z].offset));
    }
  }
  return ret;
}

uint32_t spire_heat_patch_bin(std::string& decompressed_bin, const SpireHeatPatch& patch, uint32_t heat) {
  if (decompressed_bin.size() != patch.bin_size) {
    throw std::runtime_error(std::format("the .bin has {} bytes, the heat patch is for {}", decompressed_bin.size(),
        patch.bin_size));
  }
  for (const auto& field : patch.fields) {
    if (decompressed_bin.compare(field.offset, field.values[0].size(), field.values[0]) != 0) {
      throw std::runtime_error(std::format("the .bin at {} does not hold the H 0 bytes of the heat patch",
          field.offset));
    }
  }
  uint32_t step = std::min(heat, patch.heat_max);
  for (const auto& field : patch.fields) {
    decompressed_bin.replace(field.offset, field.values[step].size(), field.values[step]);
  }
  return step;
}
