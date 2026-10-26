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

std::string spire_meta_on_finish(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t finish_value, bool new_best_time) {
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
  auto finish_flags = spire_meta_flags_for_finish(finish, character->disp.visual.sh.char_class, new_best_time);
  uint8_t ladder_run = spire_meta_ladder_run(quest_number);
  if (finish.won && ladder_run) {
    finish_flags.emplace_back(spire_meta_ladder_flag(ladder_run)); // the next rung of this run (plan-unlockables §2)
  }
  for (uint16_t flag_num : finish_flags) {
    auto key = std::make_pair(spire_meta_flag_difficulty(flag_num, difficulty), flag_num);
    auto it = flags.find(key);
    bool was_set = (it != flags.end()) && it->second;
    flags[key] = true;
    set_str += std::format(" {:04X}{}", flag_num, was_set ? "" : "*");
    if (flag_num >= SPIRE_META_FLAG_PAY_WIN) {
      payout = true; // the payout flags are cleared by q925, so each win opens them again
    } else if ((flag_num == SPIRE_META_FLAG_RUN1_WON) || (flag_num == SPIRE_META_FLAG_RUN2_WON)) {
      if (!was_set && (difficulty < 3)) {
        std::string what = std::format("Spire Run {} on {}", (ladder_run == 1) ? "I" : "II",
            spire_ladder_difficulty_name(difficulty + 1));
        unlocked += unlocked.empty() ? what : std::format(", {}", what);
      }
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
    }
    ret.items.emplace_back(std::move(item));
  }
  for (const auto& [cls, row_json] : json.at("MetaHeat").get_dict("HAuto")) {
    auto& row = ret.heat[cls];
    for (const auto& v : row_json->as_list()) {
      row.emplace_back(v->is_null() ? -1 : static_cast<int32_t>(v->as_int()));
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

uint32_t spire_shop_heat(const SpireShopCatalogue& cat, uint8_t char_class, uint32_t strength) {
  auto it = cat.heat.find(name_for_char_class(char_class));
  if ((it == cat.heat.end()) || it->second.empty()) {
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
    const std::map<std::string, uint32_t>& upgrades, const uint8_t open_rung[2]) {
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
    const std::map<std::string, uint32_t>& upgrades, uint32_t balance, const uint8_t open_rung[2]) {
  auto r = spire_shop_item_refusal(cat, index, upgrades, open_rung);
  if (r != SpireShopRefusal::NONE) {
    return r;
  }
  return (balance < cat.items[index].price[spire_shop_level(upgrades, index)]) ? SpireShopRefusal::BALANCE
                                                                                : SpireShopRefusal::NONE;
}

uint32_t spire_shop_next_price(const SpireShopCatalogue& cat, size_t index,
    const std::map<std::string, uint32_t>& upgrades, const uint8_t open_rung[2]) {
  if (spire_shop_item_refusal(cat, index, upgrades, open_rung) != SpireShopRefusal::NONE) {
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
