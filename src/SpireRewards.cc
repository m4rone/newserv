#include "SpireRewards.hh"

#include <algorithm>
#include <filesystem>
#include <format>
#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <stdexcept>

SpireAtaModel spire_ata_model_for_name(const std::string& name) {
  if (name == "table") {
    return SpireAtaModel::TABLE;
  } else if (name == "screen_weapon") {
    return SpireAtaModel::SCREEN_WEAPON;
  } else if (name == "screen_bare") {
    return SpireAtaModel::SCREEN_BARE;
  } else if (name == "safe") {
    return SpireAtaModel::SAFE;
  }
  throw std::runtime_error(std::format("AtaModel {} is not table, screen_weapon, screen_bare or safe", name));
}

const char* spire_ata_model_name(SpireAtaModel model) {
  switch (model) {
    case SpireAtaModel::TABLE:
      return "table";
    case SpireAtaModel::SCREEN_WEAPON:
      return "screen_weapon";
    case SpireAtaModel::SCREEN_BARE:
      return "screen_bare";
    case SpireAtaModel::SAFE:
      return "safe";
  }
  return "?";
}

// ceil(a / b) for a > 0, else 0
static uint32_t spire_ceil_div(int64_t a, int64_t b) {
  return (a <= 0) ? 0 : static_cast<uint32_t>((a + b - 1) / b);
}

uint32_t spire_ata_dex_needed(SpireAtaModel model, uint32_t ata, uint32_t weapon_ata, uint32_t required) {
  if (!required) {
    return 0;
  }
  // in tenths of a screen point: screen = 600 + table + 10 x weapon ATA; a DEX level is 5 tenths on the screen
  int64_t screen_weapon = 600 + static_cast<int64_t>(ata) + 10 * static_cast<int64_t>(weapon_ata);
  int64_t screen_bare = 600 + static_cast<int64_t>(ata);
  uint32_t table = spire_ceil_div(static_cast<int64_t>(required) - ata, 5);
  uint32_t sw = spire_ceil_div(10 * static_cast<int64_t>(required) - screen_weapon, 5);
  uint32_t sb = spire_ceil_div(10 * static_cast<int64_t>(required) - screen_bare, 5);
  switch (model) {
    case SpireAtaModel::TABLE:
      return table;
    case SpireAtaModel::SCREEN_WEAPON:
      return sw;
    case SpireAtaModel::SCREEN_BARE:
      return sb;
    case SpireAtaModel::SAFE:
      return std::max(table, sw);
  }
  return std::max(table, sw);
}

SpireAtaModel spire_ata_model_configured() {
  static std::filesystem::file_time_type cached_time{};
  static SpireAtaModel cached = SpireAtaModel::SAFE;
  static bool cached_valid = false;
  try {
    if (!std::filesystem::is_regular_file(SPIRE_REWARDS_FILE)) {
      cached_valid = false;
      return SpireAtaModel::SAFE;
    }
    auto t = std::filesystem::last_write_time(SPIRE_REWARDS_FILE);
    if (!cached_valid || (t != cached_time)) {
      auto json = phosg::JSON::parse(phosg::load_file(SPIRE_REWARDS_FILE));
      cached = json.contains("AtaModel") ? spire_ata_model_for_name(json.get_string("AtaModel")) : SpireAtaModel::SAFE;
      cached_time = t;
      cached_valid = true;
    }
    return cached;
  } catch (const std::exception&) {
    cached_valid = false;
    return SpireAtaModel::SAFE;
  }
}

static constexpr const char* TIER_NAMES[4] = {"Normal", "Rare", "Epic", "Legendary"};

const char* spire_reward_tier_name(SpireRewardTier tier) {
  return TIER_NAMES[static_cast<size_t>(tier) & 3];
}

const char* spire_reward_tier_label(SpireRewardTier tier) {
  static constexpr const char* labels[4] = {"NORMAL", "RARE", "EPIC", "LEGEND"};
  return labels[static_cast<size_t>(tier) & 3];
}

const char* spire_reward_tier_color(SpireRewardTier tier) {
  // the ticker's colour codes as the Spire's other lines use them ($C7 the plain text colour, $C6 yellow); $C1 blue
  static constexpr const char* colors[4] = {"$C7", "$C1", "$C6", "$C6"};
  return colors[static_cast<size_t>(tier) & 3];
}

const char* spire_reward_kind_name(SpireRewardKind kind) {
  switch (kind) {
    case SpireRewardKind::NONE:
      return "none";
    case SpireRewardKind::WEAPON:
      return "Wapen";
    case SpireRewardKind::NEW_LINE:
      return "Nieuwe lijn";
    case SpireRewardKind::MAG_FOOD:
      return "Mag-voer";
    case SpireRewardKind::UNIT:
      return "Kracht";
  }
  return "?";
}

static uint32_t spire_code_of(const std::string& hex) {
  if (hex.size() != 6) {
    throw std::runtime_error(std::format("item code {} is not 6 hex digits", hex));
  }
  return std::stoul(hex, nullptr, 16);
}

static uint32_t spire_get_u(const phosg::JSON& json, const std::string& key, uint32_t max, uint32_t def = 0xFFFFFFFF) {
  int64_t v = (def == 0xFFFFFFFF) ? json.get_int(key) : json.get_int(key, def);
  if ((v < 0) || (v > max)) {
    throw std::runtime_error(std::format("{} {} outside 0-{}", key, v, max));
  }
  return v;
}

SpireRewardTable spire_rewards_parse(const std::string& json_text) {
  auto json = phosg::JSON::parse(json_text);
  SpireRewardTable ret;
  ret.enabled = json.get_bool("Enabled");
  ret.ata_model = spire_ata_model_for_name(json.get_string("AtaModel"));
  ret.source = json.get_string("Source", "");
  const auto& odds = json.at("TierOdds");
  ret.rare_width = spire_get_u(odds, "RareWidth", 100);
  ret.epic_base = spire_get_u(odds, "EpicBase", 100);
  ret.epic_per_room = spire_get_u(odds, "EpicPerRoom", 100);
  ret.epic_elite = spire_get_u(odds, "EpicElite", 100);
  ret.legendary_from_trede = spire_get_u(odds, "LegendaryFromTrede", 100);
  ret.legendary_room = spire_get_u(odds, "LegendaryRoom", 255);
  ret.legendary_room_bonus = spire_get_u(odds, "LegendaryRoomBonus", 100);
  ret.legendary_elite = spire_get_u(odds, "LegendaryElite", 100);
  ret.cap = spire_get_u(odds, "Cap", 100);
  const auto& tr = json.at("TredeRooms");
  ret.trede1_room = spire_get_u(tr, "Dragon", 255);
  ret.trede2_room = spire_get_u(tr, "DeRolLe", 255);
  for (size_t z = 0; z < 4; z++) {
    ret.mag_food[z] = spire_get_u(json.at("MagFood"), TIER_NAMES[z], 200);
  }
  for (const auto& [k, w] : json.get_dict("RoomW")) {
    ret.room_w.emplace(std::stoul(k), spire_get_u(json.at("RoomW"), k, 6));
  }
  for (const auto& [g, line] : json.get_dict("LineGroups")) {
    ret.line_groups.emplace(std::stoul(g, nullptr, 16), line->as_string());
  }
  for (const auto& [code, line] : json.get_dict("LineOf")) {
    ret.line_of.emplace(spire_code_of(code), line->as_string());
  }
  for (const auto& [code, it_json] : json.get_dict("Items")) {
    SpireRewardItemInfo info;
    info.name = it_json->get_string("Name");
    info.stars = spire_get_u(*it_json, "Stars", 12);
    info.atp = spire_get_u(*it_json, "Atp", 2000);
    info.ata = spire_get_u(*it_json, "Ata", 2000);
    info.mst = spire_get_u(*it_json, "Mst", 2000);
    info.weapon_ata = spire_get_u(*it_json, "WeaponAta", 2000);
    info.max_grind = spire_get_u(*it_json, "MaxGrind", 255);
    ret.items.emplace(spire_code_of(code), std::move(info));
  }
  for (const auto& [name, cl_json] : json.get_dict("Classes")) {
    SpireRewardClass cls;
    cls.name = name;
    cls.archetype = cl_json->get_string("Archetype");
    uint8_t index = spire_get_u(*cl_json, "Index", 11);
    for (const auto& [ln, l_json] : cl_json->get_dict("Lines")) {
      SpireRewardLine line;
      line.kit = spire_code_of(l_json->get_string("Kit"));
      for (const auto& [w, w_json] : l_json->get_dict("W")) {
        SpireRewardWeaponLevel wl;
        wl.run_level = spire_get_u(*w_json, "RunLevel", 200);
        const auto& st = w_json->at("RunStats");
        wl.run_atp = spire_get_u(st, "ATP", 5000);
        wl.run_ata = spire_get_u(st, "ATA", 5000);
        wl.run_mst = spire_get_u(st, "MST", 5000);
        for (size_t z = 0; z < 4; z++) {
          const auto& c_json = w_json->at("Tiers").at(TIER_NAMES[z]);
          auto& cell = wl.tiers[z];
          cell.code = spire_code_of(c_json.get_string("Code"));
          if (!ret.items.count(cell.code)) {
            throw std::runtime_error(std::format("{} {} {} {}: item {:06X} not in Items", name, ln, w, TIER_NAMES[z], cell.code));
          }
          cell.name = c_json.get_string("Name");
          cell.grind = spire_get_u(c_json, "Grind", 255, 0);
          cell.hit = spire_get_u(c_json, "Hit", 100, 0);
          cell.fallback = c_json.get_bool("Fallback", false);
          if (c_json.contains("Specials")) {
            for (const auto& sp : c_json.get_list("Specials")) {
              int64_t id = sp->get_int(0);
              if ((id < 1) || (id > 0x28)) {
                throw std::runtime_error(std::format("{} {} {}: special {} outside 1-40", name, ln, w, id));
              }
              cell.specials.emplace_back(SpireRewardSpecial{static_cast<uint8_t>(id), sp->get_string(1)});
            }
          }
          const auto& gm = c_json.get_list("GridMag");
          for (size_t m = 0; (m < 4) && (m < gm.size()); m++) {
            cell.grid_mag[m] = gm[m]->as_int();
          }
        }
        uint32_t wn = std::stoul(w.substr(1));
        if ((w.size() != 2) || (w[0] != 'W') || (wn < 2) || (wn > 6)) {
          throw std::runtime_error(std::format("{} {}: weapon level {} is not W2-W6", name, ln, w));
        }
        line.w.emplace(wn, std::move(wl));
      }
      cls.line_order.emplace_back(ln);
      cls.lines.emplace(ln, std::move(line));
    }
    // review r4: phosg's JSON dict is unordered, so the file's order of the lines is lost here; sorted, with Saber (the
    // line every class carries, design par. 1) first, the class's fallback line is Saber and never the hash's pick
    // (it was Mechgun for the hunters, Shot for the rangers, Wand for the forces)
    std::sort(cls.line_order.begin(), cls.line_order.end(), [](const std::string& a, const std::string& b) {
      return ((a == "Saber") != (b == "Saber")) ? (a == "Saber") : (a < b);
    });
    ret.classes.emplace(index, std::move(cls));
  }
  if (json.contains("Units")) { // part B; a part A file has none
    const auto& units = json.at("Units");
    for (const auto& [code, u_json] : units.get_dict("Items")) {
      SpireRewardUnit u;
      u.name = u_json->get_string("Name");
      u.stars = spire_get_u(*u_json, "Stars", 12);
      u.stat = spire_get_u(*u_json, "Stat", 0xFFFF);
      u.amount = spire_get_u(*u_json, "Amount", 0xFFFF);
      uint32_t c = spire_code_of(code);
      if ((c >> 8) != 0x0103) {
        throw std::runtime_error(std::format("Units Items {} is not a unit (0103xx)", code));
      }
      ret.units.emplace(c, std::move(u));
    }
    for (const auto& [arch, a_json] : units.get_dict("Archetypes")) {
      std::array<std::vector<uint32_t>, 4> tiers;
      for (size_t z = 0; z < 4; z++) {
        for (const auto& code : a_json->get_list(TIER_NAMES[z])) {
          uint32_t c = spire_code_of(code->as_string());
          if (!ret.units.count(c)) {
            throw std::runtime_error(std::format("Units {} {}: {:06X} not in Units Items", arch, TIER_NAMES[z], c));
          }
          tiers[z].emplace_back(c);
        }
        if (tiers[z].empty()) {
          throw std::runtime_error(std::format("Units {} {}: no unit", arch, TIER_NAMES[z]));
        }
      }
      ret.unit_tiers.emplace(arch, std::move(tiers));
    }
  }
  return ret;
}

uint32_t spire_reward_trede(const SpireRewardTable& table, const std::set<uint8_t>& bosses_beaten,
    const std::set<uint8_t>& run_boss_rooms) {
  auto beaten = [&](uint8_t k) { return bosses_beaten.count(k) || run_boss_rooms.count(k); };
  return beaten(table.trede2_room) ? 2 : beaten(table.trede1_room) ? 1 : 0;
}

SpireRewardOdds spire_reward_odds(const SpireRewardTable& table, uint32_t k, uint32_t trede, bool elite, uint32_t rare_bonus) {
  SpireRewardOdds ret;
  uint32_t l = 0;
  if (trede >= table.legendary_from_trede) {
    l = trede - 1;
    if (k >= table.legendary_room) {
      l += table.legendary_room_bonus;
    }
    if (elite) {
      l += table.legendary_elite;
    }
  }
  uint32_t e = table.epic_base + table.epic_per_room * k + trede + rare_bonus + (elite ? table.epic_elite : 0);
  l = std::min(l, table.cap);
  e = std::min(e, table.cap - l);
  ret.legendary = l;
  ret.epic = l + e;
  ret.rare = std::min<uint32_t>(l + e + table.rare_width, 100);
  return ret;
}

SpireRewardTier spire_reward_tier_for_roll(const SpireRewardOdds& odds, uint32_t roll) {
  if (roll < odds.legendary) {
    return SpireRewardTier::LEGENDARY;
  } else if (roll < odds.epic) {
    return SpireRewardTier::EPIC;
  } else if (roll < odds.rare) {
    return SpireRewardTier::RARE;
  }
  return SpireRewardTier::NORMAL;
}

uint32_t spire_reward_room_index(uint32_t k) {
  return (k > 15) ? (k - 15) : std::max<uint32_t>(k, 1);
}

uint8_t spire_reward_w(const SpireRewardTable& table, uint32_t room_index) {
  auto it = table.room_w.find(room_index);
  if (it != table.room_w.end()) {
    return it->second;
  }
  uint8_t ret = 2;
  for (const auto& [k, w] : table.room_w) {
    if (k <= room_index) {
      ret = std::max(ret, w);
    }
  }
  return ret;
}

std::string spire_reward_line_of(const SpireRewardTable& table, const SpireRewardClass& cls, const uint8_t* data1) {
  if (!data1 || (data1[0] != 0x00)) {
    return "";
  }
  uint32_t code = (data1[1] << 8) | data1[2];
  auto it = table.line_of.find(code);
  std::string ret;
  if (it != table.line_of.end()) {
    ret = it->second;
  } else {
    auto g_it = table.line_groups.find(data1[1]);
    if (g_it != table.line_groups.end()) {
      ret = g_it->second;
    }
  }
  return cls.lines.count(ret) ? ret : "";
}

SpireMagBytes spire_reward_weapon_bytes(const SpireRewardTable& table, const SpireRewardCell& cell, uint8_t special) {
  SpireMagBytes ret{};
  ret[0] = (cell.code >> 16) & 0xFF;
  ret[1] = (cell.code >> 8) & 0xFF;
  ret[2] = cell.code & 0xFF;
  uint32_t max_grind = table.items.at(cell.code).max_grind;
  ret[3] = std::min<uint32_t>(cell.grind, max_grind);
  ret[4] = special;
  if (cell.hit) {
    ret[6] = 0x05; // attribute 5: Hit
    ret[7] = cell.hit;
  }
  return ret;
}

static std::string spire_reward_weapon_name(const SpireRewardCell& cell, const SpireMagBytes& bytes, const std::string& special) {
  std::string ret = cell.name;
  if (bytes[3]) {
    ret += std::format(" +{}", bytes[3]);
  }
  if (!special.empty()) {
    ret += " " + special;
  }
  return ret;
}

SpireRewardOffer spire_reward_offer(const SpireRewardTable& table, SpireRewardKind kind, uint8_t char_class,
    const uint8_t* held, uint32_t room_index, SpireRewardTier tier, const std::function<uint32_t(uint32_t)>& rnd) {
  SpireRewardOffer ret;
  ret.kind = kind;
  ret.tier = tier;
  ret.room_index = room_index;
  ret.w = spire_reward_w(table, room_index);
  auto cls_it = table.classes.find(char_class);
  if (cls_it == table.classes.end()) {
    throw std::runtime_error(std::format("class {} is not in the reward grid", char_class));
  }
  const auto& cls = cls_it->second;
  if (cls.line_order.empty()) {
    throw std::runtime_error(std::format("class {} has no line", cls.name));
  }
  ret.from_line = spire_reward_line_of(table, cls, held);
  if (ret.from_line.empty()) {
    ret.from_line = cls.line_order.front(); // no weapon of a known line: the class's first line (Saber for all)
  }
  const std::string tier_label = spire_reward_tier_label(tier);
  if (kind == SpireRewardKind::MAG_FOOD) {
    ret.food_levels = table.mag_food[static_cast<size_t>(tier)];
    ret.item_name = std::format("Mag food +{}", ret.food_levels);
    ret.label = tier_label + " " + ret.item_name;
    return ret;
  }
  if (kind == SpireRewardKind::UNIT) {
    auto a_it = table.unit_tiers.find(cls.archetype);
    if (a_it == table.unit_tiers.end()) {
      throw std::runtime_error(std::format("archetype {} has no units in the reward grid", cls.archetype));
    }
    const auto& codes = a_it->second[static_cast<size_t>(tier) & 3];
    uint32_t code = codes[rnd(codes.size()) % codes.size()];
    ret.item[0] = (code >> 16) & 0xFF;
    ret.item[1] = (code >> 8) & 0xFF;
    ret.item[2] = code & 0xFF;
    ret.item_name = table.units.at(code).name;
    ret.label = tier_label + " " + ret.item_name;
    return ret;
  }
  ret.line = ret.from_line;
  if (kind == SpireRewardKind::NEW_LINE) {
    std::vector<std::string> others;
    for (const auto& ln : cls.line_order) {
      if (ln != ret.from_line) {
        others.emplace_back(ln);
      }
    }
    if (!others.empty()) {
      ret.line = others[rnd(others.size()) % others.size()];
    }
  }
  const auto& line = cls.lines.at(ret.line);
  auto w_it = line.w.find(ret.w);
  if (w_it == line.w.end()) {
    throw std::runtime_error(std::format("{} {} has no W{}", cls.name, ret.line, ret.w));
  }
  const auto& cell = w_it->second.tiers[static_cast<size_t>(tier)];
  std::string special_name;
  uint8_t special = 0;
  if (!cell.specials.empty()) {
    const auto& sp = cell.specials[rnd(cell.specials.size()) % cell.specials.size()];
    special = sp.id;
    special_name = sp.name;
  }
  ret.item = spire_reward_weapon_bytes(table, cell, special);
  ret.info = table.items.at(cell.code);
  ret.item_name = spire_reward_weapon_name(cell, ret.item, special_name);
  ret.label = tier_label + " " + ret.item_name;
  return ret;
}

SpireMagNeed spire_reward_mag_need(const SpireRewardItemInfo& info, uint32_t atp, uint32_t ata, uint32_t mst,
    SpireAtaModel model) {
  SpireMagNeed ret;
  ret.pow = std::min<uint32_t>(spire_ceil_div(static_cast<int64_t>(info.atp) - atp, 2), 200);
  ret.mind = std::min<uint32_t>(spire_ceil_div(static_cast<int64_t>(info.mst) - mst, 2), 200);
  ret.dex = std::min<uint32_t>(spire_ata_dex_needed(model, ata, info.weapon_ata, info.ata), 200);
  return ret;
}

bool spire_reward_meets(const SpireRewardItemInfo& info, uint32_t atp, uint32_t ata, uint32_t mst, const SpireMagBytes* mag,
    SpireAtaModel model) {
  auto need = spire_reward_mag_need(info, atp, ata, mst, model);
  uint32_t pow = mag ? spire_mag_stat_word(*mag, 1) / 100 : 0;
  uint32_t dex = mag ? spire_mag_stat_word(*mag, 2) / 100 : 0;
  uint32_t mind = mag ? spire_mag_stat_word(*mag, 3) / 100 : 0;
  return (pow >= need.pow) && (dex >= need.dex) && (mind >= need.mind);
}
