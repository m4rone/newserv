#include "SpireMag.hh"

#include <algorithm>
#include <format>
#include <phosg/JSON.hh>
#include <stdexcept>

#include "ItemData.hh"
#include "ItemParameterTable.hh"
#include "Items.hh"
#include "MagMetadataTable.hh"
#include "SpireDrops.hh"
#include "StaticGameData.hh"

static SpireMagRow spire_mag_parse_row(const phosg::JSON& json, const std::string& where) {
  SpireMagRow row;
  int64_t id = json.get_int("Id");
  if ((id < 0) || (id > 0xFF)) {
    throw std::runtime_error(std::format("{}: Id {} is no data1[1] value", where, id));
  }
  row.id = id;
  uint16_t* stats[4] = {&row.def, &row.pow, &row.dex, &row.mind};
  const char* names[4] = {"Def", "Pow", "Dex", "Mind"};
  uint32_t sum = 0;
  for (size_t z = 0; z < 4; z++) {
    int64_t v = json.get_int(names[z]);
    if ((v < 0) || (v > 200)) {
      throw std::runtime_error(std::format("{}: {} {} outside 0-200", where, names[z], v));
    }
    *stats[z] = v;
    sum += v;
  }
  if (sum > 200) {
    throw std::runtime_error(std::format("{}: level {} above 200", where, sum));
  }
  return row;
}

static const char* const SPIRE_MAG_STAT_KEYS[4] = {"Def", "Pow", "Dex", "Mind"};

static SpireMagLevels spire_mag_parse_levels(const phosg::JSON& json, const std::string& where) {
  SpireMagLevels ret{};
  for (size_t z = 0; z < 4; z++) {
    int64_t v = json.get_int(SPIRE_MAG_STAT_KEYS[z]);
    if ((v < 0) || (v > 200)) {
      throw std::runtime_error(std::format("{}: {} {} outside 0-200", where, SPIRE_MAG_STAT_KEYS[z], v));
    }
    ret[z] = v;
  }
  return ret;
}

static SpireMagDirection spire_mag_parse_direction(const phosg::JSON& json, const std::string& where) {
  SpireMagDirection ret;
  ret.name = json.get_string("Name");
  ret.weights = spire_mag_parse_levels(json.at("Weights"), where + " Weights");
  ret.first = spire_mag_parse_levels(json.at("First"), where + " First");
  uint32_t sum = 0;
  for (size_t z = 0; z < 4; z++) {
    sum += ret.weights[z];
    if (ret.first[z] > ret.weights[z]) {
      throw std::runtime_error(std::format("{}: First {} {} above its weight {}", where, SPIRE_MAG_STAT_KEYS[z],
          ret.first[z], ret.weights[z]));
    }
  }
  if (sum != 200) {
    throw std::runtime_error(std::format("{}: Weights sum to {}, not 200", where, sum));
  }
  const std::string& main = json.get_string("Main");
  ret.main = 4;
  for (size_t z = 0; z < 4; z++) {
    if (main == SPIRE_MAG_STAT_KEYS[z]) {
      ret.main = z;
    }
  }
  if ((ret.main > 3) || !ret.weights[ret.main]) {
    throw std::runtime_error(std::format("{}: Main {} is not a stat the direction gives", where, main));
  }
  return ret;
}

static int64_t spire_mag_parse_range(const phosg::JSON& json, const char* key, int64_t low, int64_t high,
    const std::string& where) {
  int64_t v = json.get_int(key);
  if ((v < low) || (v > high)) {
    throw std::runtime_error(std::format("{}: {} {} outside {}-{}", where, key, v, low, high));
  }
  return v;
}

static SpireMagGrowth spire_mag_growth_from(const phosg::JSON& json) {
  SpireMagGrowth ret;
  const auto& feed = json.at("Feed");
  ret.feed_enabled = feed.get_bool("Enabled");
  ret.main_share = spire_mag_parse_range(feed, "MainShare", 0, 100, "Feed");

  const auto& lines = json.at("Lines");
  for (const char* arch : {"HU", "RA", "FO"}) {
    auto& by_number = ret.lines[arch];
    for (const auto& line_json : lines.get_list(arch)) {
      int64_t number = spire_mag_parse_range(*line_json, "Lean", SPIRE_MAG_DIRECTION_MAIN, SPIRE_MAG_DIRECTION_SHIELD,
          std::format("Lines {}", arch));
      auto dir = spire_mag_parse_direction(*line_json, std::format("Lines {} {}", arch, number));
      // the lines of HU and RA are also their androids': an android never gets MIND (mags.md par. 3a)
      if ((std::string(arch) != "FO") && dir.weights[SPIRE_MAG_MIND]) {
        throw std::runtime_error(std::format("Lines {} {}: MIND {} on a line the androids share", arch, number,
            dir.weights[SPIRE_MAG_MIND]));
      }
      if (!by_number.emplace(number, std::move(dir)).second) {
        throw std::runtime_error(std::format("Lines {}: Lean {} twice", arch, number));
      }
    }
    if (by_number.size() != 3) {
      throw std::runtime_error(std::format("Lines {} has {} lines, not Lean 2, 3 and 4", arch, by_number.size()));
    }
  }

  const auto& classic = json.at("Classic");
  for (uint8_t cls = 0; cls < 12; cls++) {
    const char* name = name_for_char_class(cls);
    auto dir = spire_mag_parse_direction(classic.at(name), std::format("Classic {}", name));
    if (spire_mag_class_is_android(cls) && dir.weights[SPIRE_MAG_MIND]) {
      throw std::runtime_error(std::format("Classic {}: MIND {} for an android", name, dir.weights[SPIRE_MAG_MIND]));
    }
    ret.classic.emplace(cls, std::move(dir));
  }

  const auto& forms = json.at("Forms");
  ret.forms.enabled = forms.get_bool("Enabled");
  ret.forms.min_level = spire_mag_parse_range(forms, "MinLevel", 1, 200, "Forms");
  const auto& odds = forms.at("Odds");
  ret.forms.epic_odds = spire_mag_parse_range(odds, "Epic", 0, 100, "Forms Odds");
  ret.forms.legendary_odds = spire_mag_parse_range(odds, "Legendary", 0, 100, "Forms Odds");
  ret.forms.heart_odds = spire_mag_parse_range(odds, "Heart", 0, 100, "Forms Odds");
  for (const auto& cell_json : forms.get_list("Cells")) {
    SpireMagForms::Cell cell;
    cell.name = cell_json->get_string("Name");
    cell.even = spire_mag_parse_range(*cell_json, "Even", 1, 0xFF, "Forms Cells " + cell.name);
    cell.odd = spire_mag_parse_range(*cell_json, "Odd", 1, 0xFF, "Forms Cells " + cell.name);
    ret.forms.cells.emplace_back(std::move(cell));
  }
  for (const auto& heart_json : forms.get_list("Hearts")) {
    SpireMagForms::Heart heart;
    heart.id = spire_mag_parse_range(*heart_json, "Id", 1, 0xFF, "Forms Hearts");
    heart.direct = heart_json->get_bool("Direct", false);
    ret.forms.hearts.emplace_back(heart);
  }
  return ret;
}

SpireMagGrowth spire_mag_parse_growth(const std::string& json_text) {
  return spire_mag_growth_from(phosg::JSON::parse(json_text));
}

SpireMagTable spire_mag_parse(const std::string& json_text) {
  auto json = phosg::JSON::parse(json_text);
  SpireMagTable ret;
  ret.enabled = json.get_bool("Enabled");
  for (const auto& [name, line_json] : json.get_dict("Classes")) {
    if ((name != "HU") && (name != "RA") && (name != "FO")) {
      throw std::runtime_error(std::format("class {} is not HU, RA or FO", name));
    }
    SpireMagLine line;
    size_t z = 0;
    for (const auto& row_json : line_json->get_list("Steps")) {
      line.steps.emplace_back(spire_mag_parse_row(*row_json, std::format("{} step {}", name, z++)));
    }
    if (line.steps.empty()) {
      throw std::runtime_error(std::format("class {} has no steps", name));
    }
    if (line_json->contains("StartFloors")) {
      for (const auto& floor_json : line_json->get_list("StartFloors")) {
        SpireMagFloor floor;
        floor.min_bonus = floor_json->get_int("MinBonus");
        floor.row = spire_mag_parse_row(*floor_json, std::format("{} floor {}", name, floor.min_bonus));
        if (!line.floors.empty() && (floor.min_bonus <= line.floors.back().min_bonus)) {
          throw std::runtime_error(std::format("class {}: StartFloors not ascending at {}", name, floor.min_bonus));
        }
        line.floors.emplace_back(floor);
      }
    }
    // F10: optional, and never fatal (an old or broken ShellSteps must not take the run mag away: NO_TABLE)
    if (!line_json->contains("ShellSteps")) {
      line.shell_note = "no ShellSteps";
    } else {
      try {
        std::vector<SpireMagRow> shell;
        size_t s = 0;
        for (const auto& row_json : line_json->get_list("ShellSteps")) {
          shell.emplace_back(spire_mag_parse_row(*row_json, std::format("{} shell step {}", name, s++)));
        }
        if (shell.size() != line.steps.size()) {
          line.shell_note = std::format("ShellSteps has {} rows, Steps {}", shell.size(), line.steps.size());
        } else {
          line.shell_steps = std::move(shell);
        }
      } catch (const std::exception& e) {
        line.shell_note = std::format("ShellSteps unreadable: {}", e.what());
      }
    }
    ret.classes.emplace(name, std::move(line));
  }
  for (const auto& k : json.get_list("BossRooms")) {
    int64_t v = k->as_int();
    if ((v < 1) || (v > 0xFF)) {
      throw std::runtime_error(std::format("boss room {} outside 1-255", v));
    }
    ret.boss_rooms.emplace(v);
  }
  // The first piece of mags.md: optional, and never fatal (as ShellSteps: a missing or broken key must not take the run
  // mag away)
  try {
    ret.growth = spire_mag_growth_from(json);
  } catch (const std::exception& e) {
    ret.growth = SpireMagGrowth{};
    ret.growth.note = std::format("growth keys unusable: {}", e.what());
  }
  return ret;
}

SpireMagColumn spire_mag_column(const SpireMagLine& line, uint8_t lean) {
  SpireMagColumn ret;
  ret.line = line;
  if (lean != SPIRE_RUN_MAG_LEAN_SHELL) {
    return ret;
  }
  if (!line.shell_steps.empty() && (line.shell_steps.size() == line.steps.size())) {
    ret.line.steps = line.shell_steps;
    ret.shell = true;
  } else {
    ret.fell_back = true;
  }
  return ret;
}

const char* spire_mag_archetype(uint8_t char_class) {
  switch (char_class) {
    case 0: // HUmar
    case 1: // HUnewearl
    case 2: // HUcast
    case 9: // HUcaseal
      return "HU";
    case 3: // RAmar
    case 4: // RAcast
    case 5: // RAcaseal
    case 11: // RAmarl
      return "RA";
    case 6: // FOmarl
    case 7: // FOnewm
    case 8: // FOnewearl
    case 10: // FOmar
      return "FO";
    default:
      return "";
  }
}

static uint16_t spire_mag_word(const SpireMagBytes& b, size_t which) { // which 0-3: DEF, POW, DEX, MIND
  return b[4 + which * 2] | (b[5 + which * 2] << 8);
}

static void spire_mag_set_word(SpireMagBytes& b, size_t which, uint16_t v) {
  v &= 0x7FFE;
  b[4 + which * 2] = v & 0xFF;
  b[5 + which * 2] = v >> 8;
}

uint32_t spire_mag_level_of(const SpireMagBytes& bytes) {
  uint32_t ret = 0;
  for (size_t z = 0; z < 4; z++) {
    ret += spire_mag_word(bytes, z) / 100;
  }
  return ret;
}

SpireMagBytes spire_mag_row_bytes(const SpireMagRow& row) {
  SpireMagBytes ret{};
  ret[0] = 0x02;
  ret[1] = row.id;
  ret[3] = 0; // photon blasts
  spire_mag_set_word(ret, 0, row.def * 100);
  spire_mag_set_word(ret, 1, row.pow * 100);
  spire_mag_set_word(ret, 2, row.dex * 100);
  spire_mag_set_word(ret, 3, row.mind * 100);
  ret[2] = spire_mag_level_of(ret);
  return ret;
}

uint32_t spire_mag_expected_step(size_t num_steps, uint32_t start_step, size_t stored_bosses, bool exam_new_boss) {
  if (!num_steps) {
    return 0;
  }
  uint64_t ret = static_cast<uint64_t>(start_step) + stored_bosses + (exam_new_boss ? 1 : 0);
  return std::min<uint64_t>(ret, num_steps - 1);
}

uint32_t spire_mag_start_step(const SpireMagTable& table, uint8_t k) {
  return std::count_if(table.boss_rooms.begin(), table.boss_rooms.end(), [&](uint8_t b) { return b < k; });
}

uint16_t SpireMagNeed::of(size_t which) const {
  return (which == 1) ? this->pow : (which == 2) ? this->dex : (which == 3) ? this->mind : 0;
}

size_t spire_mag_main_stat(const std::string& archetype, uint8_t lean) {
  if (lean == SPIRE_RUN_MAG_LEAN_SHELL) {
    return 0;
  }
  return (archetype == "FO") ? 3 : 1;
}

uint16_t spire_mag_stat_word(const SpireMagBytes& b, size_t which) {
  return spire_mag_word(b, which);
}

SpireMagBytes spire_mag_fit_200(SpireMagBytes bytes, const std::optional<SpireMagBytes>& current,
    const SpireMagNeed& need, size_t main_stat) {
  // the order of the stats not asked: DEF first, then the others in word order, the main stat last
  std::vector<size_t> order;
  for (size_t z : {0, 1, 2, 3}) {
    if (z != main_stat) {
      order.emplace_back(z);
    }
  }
  order.emplace_back(main_stat);
  auto cut_to = [&](size_t which, uint32_t lower) {
    uint32_t level = spire_mag_level_of(bytes);
    uint16_t w = spire_mag_word(bytes, which);
    while ((level > 200) && (w > lower)) {
      uint32_t cut = std::min<uint32_t>((level - 200) * 100, w - lower);
      w -= cut;
      spire_mag_set_word(bytes, which, w);
      w = spire_mag_word(bytes, which); // & 0x7FFE
      level = spire_mag_level_of(bytes);
    }
  };
  auto now = [&](size_t which) -> uint32_t { return current ? spire_mag_word(*current, which) : 0; };
  auto asked = [&](size_t which) { return need.of(which) > 0; };
  for (size_t z : order) { // 1: the rise of the stats not asked
    if (!asked(z)) {
      cut_to(z, now(z));
    }
  }
  for (size_t z : order) { // 2: the rise of the asked stats above their need
    if (asked(z)) {
      cut_to(z, std::max<uint32_t>(now(z), need.of(z) * 100));
    }
  }
  for (size_t z : order) { // 3: the stats not asked, down to 0
    if (!asked(z)) {
      cut_to(z, 0);
    }
  }
  for (size_t z : order) { // 4: what the mag holds of an asked stat above its need (the MIND of a learned disk, when a
    if (asked(z)) { // later reward weapon asks POW or DEX), before any asked stat goes below its need
      cut_to(z, need.of(z) * 100);
    }
  }
  for (size_t z : order) { // 5: the need alone is above 200
    if (asked(z)) {
      cut_to(z, std::min<uint32_t>(now(z), need.of(z) * 100));
    }
  }
  for (size_t z : order) {
    cut_to(z, 0);
  }
  bytes[2] = spire_mag_level_of(bytes);
  return bytes;
}

SpireMagBytes spire_mag_rule_b(const SpireMagLine& line, uint32_t step, uint32_t used_start_bonus,
    const std::optional<SpireMagBytes>& current, const SpireMagNeed& need, uint16_t feed_levels, size_t main_stat) {
  SpireMagLevels feed{};
  if (main_stat < feed.size()) {
    feed[main_stat] = feed_levels;
  }
  return spire_mag_rule_b_fed(line, step, used_start_bonus, current, need, feed, main_stat);
}

SpireMagBytes spire_mag_rule_b_fed(const SpireMagLine& line, uint32_t step, uint32_t used_start_bonus,
    const std::optional<SpireMagBytes>& current, const SpireMagNeed& need, const SpireMagLevels& feed, size_t main_stat) {
  if (line.steps.empty()) {
    throw std::logic_error("run mag line has no steps");
  }
  const auto& step_row = line.steps[std::min<size_t>(step, line.steps.size() - 1)];
  auto ret = spire_mag_row_bytes(step_row);
  // the highest floor the start bonus this run uses reaches (B-ONTWERP-8: never the highest bought)
  const SpireMagFloor* floor = nullptr;
  for (const auto& f : line.floors) {
    if (used_start_bonus >= f.min_bonus) {
      floor = &f;
    }
  }
  if (floor) {
    auto floor_bytes = spire_mag_row_bytes(floor->row);
    for (size_t z = 0; z < 4; z++) {
      spire_mag_set_word(ret, z, std::max(spire_mag_word(ret, z), spire_mag_word(floor_bytes, z)));
    }
    if (spire_mag_level_of(floor_bytes) > spire_mag_level_of(spire_mag_row_bytes(step_row))) {
      ret[1] = floor->row.id;
    }
  }
  // the mag the player holds, with the mag food on its stats (on the step row when there is none yet)
  std::optional<SpireMagBytes> held = current;
  if (std::any_of(feed.begin(), feed.end(), [](uint16_t v) { return v != 0; })) {
    SpireMagBytes fed = current ? *current : ret;
    for (size_t z = 0; z < 4; z++) {
      if (feed[z]) {
        uint32_t w = spire_mag_word(fed, z) + feed[z] * 100u;
        spire_mag_set_word(fed, z, std::min<uint32_t>(w, 20000));
      }
    }
    held = fed;
  }
  for (size_t z = 0; z < 4; z++) {
    uint32_t w = std::max<uint32_t>(spire_mag_word(ret, z), need.of(z) * 100u);
    if (held) {
      w = std::max<uint32_t>(w, spire_mag_word(*held, z));
    }
    spire_mag_set_word(ret, z, std::min<uint32_t>(w, 20000));
  }
  ret[3] = 0;
  // the cut keeps the mag as the player holds it (current, without the food: food above 200 is what goes first)
  return spire_mag_fit_200(ret, current, need, main_stat);
}

SpireMagBytes spire_mag_step_bytes(const SpireMagLine& line, uint32_t step, uint32_t used_start_bonus,
    const std::optional<SpireMagBytes>& last_mirrored) {
  return spire_mag_rule_b(line, step, used_start_bonus, last_mirrored, SpireMagNeed{}, 0, 1);
}

uint8_t spire_mag_evolve_id(const SpireMagBytes& bytes, uint8_t from_id, const SpireMagEvolveAt& evolve_at) {
  uint32_t level = spire_mag_level_of(bytes);
  auto b = bytes;
  b[1] = from_id;
  std::vector<uint8_t> rules;
  if (level >= 10) {
    rules.emplace_back(10);
  }
  if (level >= 35) {
    rules.emplace_back(35);
  }
  if (level >= 50) {
    rules.emplace_back(std::min<uint32_t>(level - (level % 5), 200));
  }
  for (uint8_t at : rules) {
    b[1] = evolve_at(b, at);
  }
  return b[1];
}

// ---- Mags, the first piece (SpireMag.hh)

const char* spire_mag_stat_name(size_t which) {
  static constexpr const char* names[4] = {"DEF", "POW", "DEX", "MIND"};
  return (which < 4) ? names[which] : "?";
}

bool spire_mag_class_is_android(uint8_t char_class) {
  return (char_class < 12) && char_class_is_android(char_class);
}

SpireMagSega spire_mag_sega_for(std::shared_ptr<const ItemParameterTable> item_parameter_table,
    std::shared_ptr<const MagMetadataTable> mag_metadata_table, uint8_t char_class, uint8_t section_id) {
  SpireMagSega ret;
  auto ipt = item_parameter_table;
  auto mmt = mag_metadata_table;
  ret.evolve_at = [ipt, mmt, char_class, section_id](const SpireMagBytes& bytes, uint8_t level) -> uint8_t {
    if (!ipt || !mmt) {
      return bytes[1];
    }
    ItemData item;
    for (size_t z = 0; z < 12; z++) {
      item.data1[z] = bytes[z];
    }
    try {
      apply_mag_evolution(item, ipt, mmt, char_class, section_id, true, level);
    } catch (const std::exception&) {
      return bytes[1]; // no evolution known for this kind: it stays
    }
    return item.data1[1];
  };
  ret.evolution_number = [mmt](uint8_t id) -> uint8_t {
    try {
      return mmt ? mmt->get_evolution_number(id) : 0xFF;
    } catch (const std::exception&) {
      return 0xFF;
    }
  };
  ret.usable = [ipt, char_class](uint8_t id) -> bool {
    try {
      uint8_t bits = spire_weapon_usability_bits(char_class);
      return ipt && ((ipt->get_mag(id).usability_flags & bits) == bits);
    } catch (const std::exception&) {
      return false;
    }
  };
  ret.item_level = [ipt, char_class](uint8_t form, uint8_t from) -> std::optional<uint8_t> {
    std::optional<uint8_t> at;
    try {
      for (size_t z = 0; ipt && (z < ipt->num_item_combinations()); z++) {
        const auto& combo = ipt->get_item_combination(z);
        // a used item that turns the worn mag `from` into `form`, asking only a mag level (a combination that asks a
        // character level is not counted: the level of the run copy is not Sega's condition for a heart)
        if ((combo.result_item[0] != 0x02) || (combo.result_item[1] != form) || (combo.equipped_item[0] != 0x02) ||
            ((combo.equipped_item[1] != from) && (combo.equipped_item[1] != 0xFF)) ||
            ((combo.char_class != 0xFF) && (combo.char_class != char_class)) || (combo.level != 0xFF)) {
          continue;
        }
        uint8_t mag_level = (combo.mag_level == 0xFF) ? 0 : combo.mag_level;
        at = at ? std::min(*at, mag_level) : mag_level;
      }
    } catch (const std::exception&) {
      return std::nullopt;
    }
    return at;
  };
  return ret;
}

const SpireMagDirection* spire_mag_direction(const SpireMagGrowth& growth, uint8_t char_class, uint8_t direction) {
  if (!growth.note.empty()) {
    return nullptr;
  }
  if ((direction >= SPIRE_MAG_DIRECTION_MAIN) && (direction <= SPIRE_MAG_DIRECTION_SHIELD)) {
    auto arch_it = growth.lines.find(spire_mag_archetype(char_class));
    if (arch_it != growth.lines.end()) {
      auto dir_it = arch_it->second.find(direction);
      if (dir_it != arch_it->second.end()) {
        return &dir_it->second;
      }
    }
  }
  auto classic_it = growth.classic.find(char_class);
  return (classic_it != growth.classic.end()) ? &classic_it->second : nullptr;
}

uint8_t spire_mag_direction_of_lean(uint8_t lean) {
  return (lean == SPIRE_RUN_MAG_LEAN_SHELL) ? SPIRE_MAG_DIRECTION_SHIELD : SPIRE_MAG_DIRECTION_MAIN;
}

SpireMagLevels spire_mag_spread(const SpireMagDirection& direction, uint32_t level) {
  SpireMagLevels ret{};
  uint32_t left = std::min<uint32_t>(level, 200);
  SpireMagLevels rest{};
  uint32_t total = 0;
  for (size_t z = 0; z < 4; z++) {
    uint16_t first = std::min(direction.first[z], direction.weights[z]);
    ret[z] = std::min<uint32_t>(first, left);
    left -= ret[z];
    rest[z] = direction.weights[z] - first;
    total += rest[z];
  }
  // one level at a time, to the stat that lags its share of the rest most (the larger share first on a tie): the same
  // order for every level, so a higher level never has less of a stat
  SpireMagLevels given{};
  for (uint32_t n = 1; (n <= left) && total; n++) {
    size_t best = 4;
    int64_t best_lag = 0;
    for (size_t z = 0; z < 4; z++) {
      if (given[z] >= rest[z]) {
        continue;
      }
      int64_t lag = static_cast<int64_t>(rest[z]) * n - static_cast<int64_t>(given[z]) * total;
      if ((best > 3) || (lag > best_lag) || ((lag == best_lag) && (rest[z] > rest[best]))) {
        best = z;
        best_lag = lag;
      }
    }
    if (best > 3) {
      break; // the weights are spent
    }
    given[best]++;
  }
  for (size_t z = 0; z < 4; z++) {
    ret[z] += given[z];
  }
  return ret;
}

SpireMagBytes spire_mag_levels_bytes(const SpireMagLevels& levels, uint8_t id) {
  SpireMagRow row;
  row.id = id;
  row.def = levels[SPIRE_MAG_DEF];
  row.pow = levels[SPIRE_MAG_POW];
  row.dex = levels[SPIRE_MAG_DEX];
  row.mind = levels[SPIRE_MAG_MIND];
  return spire_mag_row_bytes(row);
}

SpireMagBytes spire_mag_feed(SpireMagBytes mag, size_t which, uint16_t levels) {
  uint32_t level = spire_mag_level_of(mag);
  if ((which < 4) && (level < 200)) {
    uint32_t add = std::min<uint32_t>(levels, 200 - level);
    spire_mag_set_word(mag, which, spire_mag_word(mag, which) + add * 100);
  }
  mag[2] = spire_mag_level_of(mag);
  return mag;
}

uint8_t spire_mag_fourth_form(const SpireMagSega& sega) {
  if (!sega.evolve_at || !sega.evolution_number) {
    return 0;
  }
  // A level 100 mag with four equal stats meets the rule of every section id group (POW + MIND = DEF + DEX, POW + DEX =
  // MIND + DEF, POW + DEF = MIND + DEX), and the kind 00 is one Sega's evolution still renames
  uint8_t id = sega.evolve_at(spire_mag_levels_bytes({25, 25, 25, 25}, 0x00), 100);
  return (id && (sega.evolution_number(id) == 4)) ? id : 0;
}

std::vector<size_t> spire_mag_food_stats(const SpireMagGrowth& growth, uint8_t char_class) {
  std::vector<size_t> ret;
  const auto* classic = spire_mag_direction(growth, char_class, SPIRE_MAG_DIRECTION_CLASSIC);
  for (size_t z = 0; classic && (z < 4); z++) {
    if (classic->weights[z] && !((z == SPIRE_MAG_MIND) && spire_mag_class_is_android(char_class))) {
      ret.emplace_back(z);
    }
  }
  return ret;
}

SpireMagFoodOffer spire_mag_food_offer(const SpireMagGrowth& growth, uint8_t char_class, uint8_t section_id,
    uint8_t direction, uint8_t tier, uint16_t tier_levels, const std::optional<SpireMagBytes>& mag, size_t stat_before,
    const SpireMagSega& sega, const std::function<uint32_t(uint32_t)>& rnd) {
  SpireMagFoodOffer ret;
  ret.stat = (stat_before < 4) ? stat_before : SPIRE_MAG_POW;
  ret.levels = tier_levels;
  auto roll = [&](uint32_t n) -> uint32_t { return rnd(n) % n; };

  // the levels: the direction's main stat, or another stat the class uses
  const auto* dir = growth.feed_enabled ? spire_mag_direction(growth, char_class, direction) : nullptr;
  auto stats = spire_mag_food_stats(growth, char_class);
  if (dir && !stats.empty()) {
    ret.directed = true;
    std::vector<size_t> others;
    for (size_t z : stats) {
      if (z != dir->main) {
        others.emplace_back(z);
      }
    }
    bool has_main = others.size() != stats.size();
    if (has_main && (others.empty() || (roll(100) < growth.main_share))) {
      ret.stat = dir->main;
    } else {
      ret.stat = others[roll(others.size())];
    }
  }

  // a form instead of the levels
  const auto& forms = growth.forms;
  if (!growth.note.empty() || !forms.enabled || !mag || (tier < SPIRE_MAG_TIER_EPIC) ||
      (spire_mag_level_of(*mag) < forms.min_level) || !sega.evolution_number || !sega.usable ||
      (sega.evolution_number((*mag)[1]) >= 4)) {
    return ret;
  }
  uint8_t held = (*mag)[1];
  auto can = [&](uint8_t id) -> bool {
    return id && (id != held) && sega.usable(id) && (sega.evolution_number(id) == 4);
  };
  auto give = [&](SpireMagFormSource source, uint8_t id) {
    ret.form = source;
    ret.form_id = id;
    ret.levels = 0;
  };
  if (tier == SPIRE_MAG_TIER_EPIC) {
    if (roll(100) >= forms.epic_odds) {
      return ret;
    }
    std::vector<uint8_t> ids;
    for (const auto& cell : forms.cells) {
      uint8_t id = (section_id & 1) ? cell.odd : cell.even;
      if (can(id)) {
        ids.emplace_back(id);
      }
    }
    if (!ids.empty()) {
      give(SpireMagFormSource::CELL, ids[roll(ids.size())]);
    }
    return ret;
  }
  if (roll(100) >= forms.legendary_odds) {
    return ret;
  }
  uint32_t level = spire_mag_level_of(*mag);
  std::vector<uint8_t> hearts;
  for (const auto& heart : forms.hearts) {
    if (!can(heart.id)) {
      continue;
    }
    auto at = (!heart.direct && sega.item_level) ? sega.item_level(heart.id, held) : std::nullopt;
    if (heart.direct || (at && (level >= *at))) {
      hearts.emplace_back(heart.id);
    }
  }
  uint8_t fourth = spire_mag_fourth_form(sega);
  if (!can(fourth)) {
    fourth = 0;
  }
  if (!hearts.empty() && (!fourth || (roll(100) < forms.heart_odds))) {
    give(SpireMagFormSource::HEART, hearts[roll(hearts.size())]);
  } else if (fourth) {
    give(SpireMagFormSource::FOURTH, fourth);
  }
  return ret;
}

std::string spire_mag_food_name(const SpireMagFoodOffer& offer, const std::string& form_name) {
  if (offer.form != SpireMagFormSource::NONE) {
    return std::format("Mag form {}", form_name.empty() ? std::format("{:02X}", offer.form_id) : form_name);
  }
  if (!offer.directed) {
    return std::format("Mag food +{}", offer.levels);
  }
  return std::format("Mag {} +{}", spire_mag_stat_name(offer.stat), offer.levels);
}

SpireMagBytes spire_mag_food_apply(const SpireMagBytes& mag, const SpireMagFoodOffer& offer, const SpireMagSega& sega) {
  if (offer.form != SpireMagFormSource::NONE) {
    auto ret = mag;
    ret[1] = offer.form_id;
    return ret;
  }
  auto ret = spire_mag_feed(mag, offer.stat, offer.levels);
  if (sega.evolve_at) {
    ret[1] = spire_mag_evolve_id(ret, mag[1], sega.evolve_at);
  }
  return ret;
}

SpireRunMagRefusal spire_run_mag_refusal(const SpireRunMagState& st) {
  using R = SpireRunMagRefusal;
  if (!st.gc_v3) {
    return R::NOT_GC_V3;
  }
  if (!st.spire_game) {
    return R::NO_SPIRE_GAME;
  }
  if (!st.has_overlay) {
    return R::NO_OVERLAY;
  }
  if (st.restore_pending) {
    return R::RESTORE_PENDING;
  }
  if (!st.run_in_progress) {
    return R::MARKER;
  }
  if (st.base_returned) {
    return R::BASE_RETURNED;
  }
  if (st.run_again) {
    return R::RUN_AGAIN;
  }
  if (st.refreshed_by_61) {
    return R::REFRESHED_BY_61;
  }
  if (!st.enabled) {
    return R::DISABLED;
  }
  if (!st.has_table) {
    return R::NO_TABLE;
  }
  if (st.overlay_mags > 1) {
    return R::TWO_MAGS;
  }
  return R::NONE;
}

const char* spire_run_mag_refusal_name(SpireRunMagRefusal r) {
  switch (r) {
    case SpireRunMagRefusal::NONE:
      return "NONE";
    case SpireRunMagRefusal::NOT_GC_V3:
      return "NOT_GC_V3";
    case SpireRunMagRefusal::NO_SPIRE_GAME:
      return "NO_SPIRE_GAME";
    case SpireRunMagRefusal::NO_OVERLAY:
      return "NO_OVERLAY";
    case SpireRunMagRefusal::RESTORE_PENDING:
      return "RESTORE_PENDING";
    case SpireRunMagRefusal::MARKER:
      return "MARKER";
    case SpireRunMagRefusal::BASE_RETURNED:
      return "BASE_RETURNED";
    case SpireRunMagRefusal::RUN_AGAIN:
      return "RUN_AGAIN";
    case SpireRunMagRefusal::REFRESHED_BY_61:
      return "REFRESHED_BY_61";
    case SpireRunMagRefusal::DISABLED:
      return "DISABLED";
    case SpireRunMagRefusal::NO_TABLE:
      return "NO_TABLE";
    case SpireRunMagRefusal::TWO_MAGS:
      return "TWO_MAGS";
    case SpireRunMagRefusal::NO_LEDGER:
      return "NO_LEDGER";
  }
  return "?";
}

uint32_t spire_run_mag_answer(uint32_t tag, uint8_t old_type, bool result) {
  return tag | (static_cast<uint32_t>(old_type) << 8) | (result ? 1 : 0);
}

SpireRunMagDecision spire_run_mag_decide(SpireRunMagLedger& ledger, const SpireMagLine& line, uint8_t sub, uint8_t k,
    const std::set<uint8_t>& server_boss_rooms, uint32_t used_start_bonus, size_t main_stat,
    const SpireMagEvolveAt& evolve_at) {
  SpireRunMagDecision ret;
  ledger.boss_rooms.insert(server_boss_rooms.begin(), server_boss_rooms.end());
  bool exam_new_boss = (sub == SPIRE_RUN_MAG_EXAM) && k && !ledger.boss_rooms.count(k);
  ret.expected_step = spire_mag_expected_step(line.steps.size(), ledger.start_step, ledger.boss_rooms.size(),
      exam_new_boss);
  if ((sub == SPIRE_RUN_MAG_EXAM) && k) {
    ledger.boss_rooms.emplace(k);
  }
  bool new_step = ledger.issued_step < static_cast<int32_t>(ret.expected_step);
  if (new_step || ledger.boost_due) {
    ret.result = true;
    ret.bytes = spire_mag_rule_b(line, std::max<int64_t>(ret.expected_step, ledger.issued_step), used_start_bonus,
        ledger.last_mirrored, ledger.need, ledger.feed_levels, main_stat);
    if (ledger.boost_due && evolve_at) {
      uint8_t from = ledger.last_mirrored ? (*ledger.last_mirrored)[1] : ret.bytes[1];
      ret.bytes[1] = spire_mag_evolve_id(ret.bytes, from, evolve_at);
    }
    ledger.offered_boost = ledger.boost_due;
    // B-REGIE-14-10: the answer issues the step; a RESULT 0 gives it back. An earlier offer whose RESULT never came
    // stays issued (the client holds that mag), so it is what a RESULT 0 of this answer returns to.
    ledger.step_before_offer = ledger.issued_step;
    ledger.issued_step = std::max<int32_t>(ret.expected_step, ledger.issued_step);
    ledger.offered_step = ret.expected_step;
    ledger.offered = ret.bytes;
  }
  return ret;
}

uint8_t spire_run_mag_lean_bits(uint32_t value) {
  return (value >> 8) & 0xFF;
}

uint8_t spire_run_mag_saved_lean(int64_t value) {
  return ((value >= SPIRE_RUN_MAG_LEAN_NONE) && (value <= SPIRE_RUN_MAG_LEAN_SHELL)) ? value : SPIRE_RUN_MAG_LEAN_NONE;
}

SpireRunMagLeanStore spire_run_mag_lean_store(SpireRunMagLedger& ledger, uint8_t lean) {
  if (lean == SPIRE_RUN_MAG_LEAN_NONE) {
    return SpireRunMagLeanStore::NONE;
  }
  if (lean > SPIRE_RUN_MAG_LEAN_SHELL) {
    return SpireRunMagLeanStore::INVALID;
  }
  if (ledger.issued_step > 0) {
    return SpireRunMagLeanStore::AFTER_STEP;
  }
  ledger.lean = lean;
  return SpireRunMagLeanStore::STORED;
}

const char* spire_run_mag_lean_store_name(SpireRunMagLeanStore r) {
  switch (r) {
    case SpireRunMagLeanStore::NONE:
      return "none";
    case SpireRunMagLeanStore::STORED:
      return "stored";
    case SpireRunMagLeanStore::AFTER_STEP:
      return "ignored (a step above the kit's is issued)";
    case SpireRunMagLeanStore::INVALID:
      return "ignored (not 0, 1 or 2)";
  }
  return "?";
}

SpireRunMagHandled spire_run_mag_handle(SpireRunMagLedger& ledger, const SpireMagTable& table, const SpireMagLine& line,
    uint8_t sub, uint8_t k, const std::vector<SpireMagBytes>& overlay_mags, const std::set<uint8_t>& server_boss_rooms,
    uint32_t used_start_bonus, uint8_t lean_bits, const std::string& archetype, const SpireMagEvolveAt& evolve_at) {
  SpireRunMagHandled ret;
  if (ledger.issued_step < 0 && ledger.boss_rooms.empty() && !ledger.offered) {
    ledger.start_step = spire_mag_start_step(table, k);
  }
  ret.lean_store = spire_run_mag_lean_store(ledger, lean_bits); // F10: before the decision
  ret.followed_mirror = spire_run_mag_follow_mirror(ledger, overlay_mags);
  if (overlay_mags.size() == 1) {
    ret.old_type = overlay_mags[0][1];
  }
  auto column = spire_mag_column(line, ledger.lean);
  ret.shell = column.shell;
  ret.shell_fell_back = column.fell_back;
  ret.decision = spire_run_mag_decide(ledger, column.line, sub, k, server_boss_rooms, used_start_bonus,
      spire_mag_main_stat(archetype, ledger.lean), evolve_at);
  if (ret.decision.result) {
    ledger.offered_shell = column.shell;
  }
  ret.answer = spire_run_mag_answer(SPIRE_RUN_MAG_TAG, ret.old_type, ret.decision.result);
  return ret;
}

bool spire_run_mag_savestate_follow(
    SpireRunMagLedger* ledger, bool refreshed_by_61, const std::vector<SpireMagBytes>& overlay_mags) {
  return ledger && !refreshed_by_61 && spire_run_mag_follow_mirror(*ledger, overlay_mags);
}

bool spire_run_mag_result(SpireRunMagLedger& ledger, bool created) {
  if (!ledger.offered) {
    return false;
  }
  if (created) {
    ledger.last_mirrored = ledger.offered;
    if (ledger.offered_boost) { // rule B: the boost is on the client now
      ledger.boost_due = false;
      ledger.feed_levels = 0;
    }
  } else {
    ledger.issued_step = ledger.step_before_offer;
  }
  ledger.offered_boost = false;
  ledger.offered.reset();
  ledger.offered_shell = false;
  ledger.offered_step = -1;
  ledger.step_before_offer = -1;
  return true;
}

bool spire_run_mag_pack_full_notice(
    SpireRunMagPackFullNotices& told, uint32_t run_id, bool known, bool created, int32_t offered_step) {
  if (!known || created) {
    return false;
  }
  if (told.run_id != run_id) {
    told.run_id = run_id;
    told.steps.clear();
  }
  return told.steps.emplace(offered_step).second;
}

SpireRunMagPackFullDecision spire_run_mag_pack_full_decide(SpireRunMagPackFullTold& told, const std::string& key,
    uint32_t run_id, bool known, bool created, int32_t offered_step, int32_t step_before_offer) {
  SpireRunMagPackFullDecision ret;
  if (!known || created) {
    ret.note = "; not a settled pack-full result, no notice";
    return ret;
  }
  if (key.empty() || !run_id) {
    ret.note = "; no client key or run id, no notice";
    return ret;
  }
  if ((told.size() >= SPIRE_RUN_MAG_PACK_FULL_KEYS_MAX) && !told.count(key)) {
    told.clear();
    ret.wiped = true;
  }
  bool tell = spire_run_mag_pack_full_notice(told[key], run_id, known, created, offered_step);
  if (!tell) {
    ret.note = "; pack-full notice for this step already sent in this run";
    return ret;
  }
  ret.tell = true;
  ret.text = (step_before_offer < 0) ? SPIRE_RUN_MAG_PACK_FULL_FIRST_TEXT : SPIRE_RUN_MAG_PACK_FULL_TEXT;
  ret.note = ret.wiped ? "; pack-full notice sent (notice records wiped at the key limit)" : "; pack-full notice sent";
  return ret;
}

bool spire_run_mag_follow_mirror(SpireRunMagLedger& ledger, const std::vector<SpireMagBytes>& overlay_mags) {
  if ((overlay_mags.size() == 1) && ledger.offered && (overlay_mags[0] == *ledger.offered)) {
    // the offered mag was created and its RESULT lost: confirmed as by a RESULT 1
    ledger.last_mirrored = ledger.offered;
    if (ledger.offered_boost) {
      ledger.boost_due = false;
      ledger.feed_levels = 0;
    }
    ledger.offered_boost = false;
    ledger.offered.reset();
    ledger.offered_shell = false;
    ledger.offered_step = -1;
    ledger.step_before_offer = -1;
    return true;
  }
  if ((overlay_mags.size() != 1) || (ledger.issued_step < 0) || !ledger.last_mirrored) {
    return false;
  }
  const auto& mag = overlay_mags[0];
  if ((mag == *ledger.last_mirrored) || (spire_mag_level_of(mag) < spire_mag_level_of(*ledger.last_mirrored))) {
    return false;
  }
  ledger.last_mirrored = mag;
  return true;
}

std::string spire_run_mag_bytes_hex(const SpireMagBytes& bytes) {
  std::string ret;
  for (uint8_t b : bytes) {
    ret += std::format("{:02X}", b);
  }
  return ret;
}

SpireMagBytes spire_run_mag_bytes_from_hex(const std::string& hex) {
  if (hex.size() != 24) {
    throw std::runtime_error(std::format("run mag bytes {} are not 24 hex digits", hex));
  }
  SpireMagBytes ret{};
  for (size_t z = 0; z < 12; z++) {
    ret[z] = std::stoul(hex.substr(z * 2, 2), nullptr, 16);
  }
  return ret;
}

std::string spire_run_mag_evolve_text(const std::string& sega_name, bool shell, bool server_equips) {
  return std::format("Your mag evolves: {}{}.{}", sega_name, shell ? " (Shell)" : "",
      server_equips ? "" : " Equip it: Item Pack, Mag.");
}

std::string spire_kit_use_text(bool server_equips, uint32_t grind, uint32_t hp_mat_level) {
  if (!server_equips) {
    return SPIRE_KIT_USE_TEXT;
  }
  std::string ground = grind ? std::format("Your kit weapon is ground +{}.", grind) : "";
  std::string materials = hp_mat_level ? "Use your HP Materials now (Item Pack)." : "";
  return (ground.empty() || materials.empty()) ? (ground + materials) : (ground + " " + materials);
}

std::string spire_run_mag_reminder_text(bool shell) {
  return shell ? SPIRE_RUN_MAG_REMINDER_SHELL_TEXT : SPIRE_RUN_MAG_REMINDER_TEXT;
}

bool spire_kit_use_text_due(bool start, uint32_t hp_mat_level, uint32_t atp_grind_level, bool resumed, bool already_sent) {
  return start && (hp_mat_level || atp_grind_level) && !resumed && !already_sent;
}

bool spire_kit_ticker_waits_for_c13(const SpireKitMagTexts& st, bool server_equips) {
  return server_equips && (st.kit_grind < 0);
}

std::string spire_kit_ticker_take(SpireKitMagTexts& st, uint32_t hp_mat_level, uint32_t) {
  st.kit_sent = true;
  st.kit_hp_mat = hp_mat_level;
  if (st.kit_grind < 0) {
    return SPIRE_KIT_USE_TEXT; // no 'C' 13 (yet): the quest's kit as before bundle 9, or spire_kit_c13_take follows
  }
  st.kit_b9_sent = true;
  return spire_kit_use_text(true, static_cast<uint32_t>(st.kit_grind), hp_mat_level);
}

std::string spire_kit_c13_take(SpireKitMagTexts& st, uint8_t grind) {
  st.kit_grind = grind;
  if (!st.kit_sent || st.kit_b9_sent) {
    return "";
  }
  st.kit_b9_sent = true;
  return spire_kit_use_text(true, grind, st.kit_hp_mat);
}

void spire_kit_ticker_starts(SpireKitMagTexts& st) {
  st.kit_pending = true;
}

bool spire_kit_equip_hint_due(SpireKitMagTexts& st, uint32_t item_id) {
  if (!st.kit_pending) {
    return true;
  }
  st.equip_hint_held = item_id;
  return false;
}

std::optional<uint32_t> spire_kit_ticker_ends(SpireKitMagTexts& st) {
  st.kit_pending = false;
  auto ret = st.equip_hint_held;
  st.equip_hint_held.reset();
  return ret;
}

void spire_run_mag_c14_take(SpireKitMagTexts& st, bool equip_sent) {
  if (equip_sent) {
    st.mag_c14_seen = true;
  }
}

std::string spire_run_mag_evolve_text_take(const SpireKitMagTexts& st, const std::string& sega_name, bool shell) {
  return spire_run_mag_evolve_text(sega_name, shell, st.mag_c14_seen);
}

std::string spire_run_mag_reminder_text_take(const SpireKitMagTexts& st, bool shell) {
  return st.mag_c14_seen ? "" : spire_run_mag_reminder_text(shell);
}

bool spire_boss_burst_text_due(uint8_t room_k, const std::set<uint8_t>& boss_rooms, std::set<uint8_t>& sent_rooms) {
  if ((room_k < SPIRE_BOSS_BURST_FROM_K) || !boss_rooms.count(room_k)) {
    return false;
  }
  return sent_rooms.emplace(room_k).second;
}

bool spire_run_mag_command_refreshes(uint16_t command) {
  return command == 0x61;
}
