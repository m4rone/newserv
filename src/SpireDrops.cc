#include "SpireDrops.hh"

#include <algorithm>
#include <climits>
#include <format>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "Client.hh"
#include "Lobby.hh"
#include "ServerState.hh"
#include "SpireFlagBridge.hh"
#include "StaticGameData.hh"

SpireDropsConfig SpireDropsConfig::from_json(const phosg::JSON& json) {
  SpireDropsConfig ret;
  ret.enabled = true;
  try {
    ret.drop_mode = phosg::enum_for_name<ServerDropMode>(json.get_string("DropMode").c_str());
  } catch (const std::out_of_range&) {
  }
  if ((ret.drop_mode != ServerDropMode::SERVER_SHARED) && (ret.drop_mode != ServerDropMode::SERVER_PRIVATE)) {
    throw std::runtime_error("SpireDrops: DropMode must be SERVER_PRIVATE or SERVER_SHARED");
  }
  ret.only_run_items = json.get_bool("OnlyRunItems", true);
  int64_t ahead = json.get_int("LevelsAhead", 5), pity = json.get_int("PityDrops", 8);
  if ((ahead < 0) || (ahead > 198) || (pity < 0) || (pity > 1000)) {
    throw std::runtime_error("SpireDrops: LevelsAhead 0-198, PityDrops 0-1000");
  }
  ret.levels_ahead = ahead;
  ret.pity_drops = pity;
  ret.boss_weapon_per_player = json.get_bool("BossWeaponPerPlayer", true);
  // F8: missing keys are 0, which adds no grind and no Hit (the drops as before F8)
  auto get_u8 = [&](const char* key) -> uint32_t {
    int64_t v = json.get_int(key, 0);
    if ((v < 0) || (v > 255)) {
      throw std::runtime_error(std::format("SpireDrops: {} 0-255", key));
    }
    return v;
  };
  ret.boss_grind_base = get_u8("BossGrindBase");
  ret.boss_grind_per_k = get_u8("BossGrindPerK");
  ret.boss_hit_base = get_u8("BossHitBase");
  ret.boss_hit_per_k = get_u8("BossHitPerK");
  ret.pick_from_k = get_u8("PickFromK");
  return ret;
}

std::string SpireDropsConfig::str() const {
  return std::format("DropMode {}, OnlyRunItems {}, LevelsAhead {}, PityDrops {}, BossWeaponPerPlayer {}, "
                     "BossGrindBase {}, BossGrindPerK {}, BossHitBase {}, BossHitPerK {}, PickFromK {}",
      phosg::name_for_enum(this->drop_mode), this->only_run_items, this->levels_ahead, this->pity_drops,
      this->boss_weapon_per_player, this->boss_grind_base, this->boss_grind_per_k, this->boss_hit_base,
      this->boss_hit_per_k, this->pick_from_k);
}

uint8_t spire_weapon_usability_bits(uint8_t char_class) {
  // Weapon::usability_flags: 01 hunter, 02 ranger, 04 force, 08 human, 10 android, 20 newman, 40 male, 80 female
  return (char_class_is_hunter(char_class) ? 0x01 : 0) | (char_class_is_ranger(char_class) ? 0x02 : 0) |
      (char_class_is_force(char_class) ? 0x04 : 0) | (char_class_is_human(char_class) ? 0x08 : 0) |
      (char_class_is_android(char_class) ? 0x10 : 0) | (char_class_is_newman(char_class) ? 0x20 : 0) |
      (char_class_is_male(char_class) ? 0x40 : 0x80);
}

bool spire_drops_is_run_item(const ItemData& item) {
  switch (item.data1[0]) {
    case 0x00: // weapon
    case 0x04: // meseta
      return true;
    case 0x01: // 01 frame, 02 barrier; 03 units are not on the list
      return (item.data1[1] == 0x01) || (item.data1[1] == 0x02);
    case 0x03: // 00 mates, 01 fluids, 03 Sol, 04 Moon, 05 Star Atomizer
      return (item.data1[1] <= 0x01) || ((item.data1[1] >= 0x03) && (item.data1[1] <= 0x05));
    default: // 02 mags
      return false;
  }
}

CharacterStats spire_drops_stats_ahead(
    const LevelTable& level_table, uint8_t char_class, uint32_t level, const CharacterStats& now, uint32_t levels_ahead) {
  CharacterStats ret = now;
  for (uint32_t l = level + 1; (l <= level + levels_ahead) && (l < 200); l++) {
    const auto& d = level_table.stats_delta_for_level(char_class, l);
    ret.atp += d.atp;
    ret.mst += d.mst;
    ret.ata += d.ata;
  }
  return ret;
}

static bool spire_drops_stats_meet(const ItemParameterTable::Weapon& w, const CharacterStats& s) {
  return (s.atp >= w.atp_required) && (s.ata >= w.ata_required) && (s.mst >= w.mst_required);
}

SpireWeaponFit spire_drops_weapon_fit(const ItemParameterTable& pmt, const ItemData& item, const SpireDropPlayer& p) {
  if (item.empty() || (item.data1[0] != 0x00)) {
    return SpireWeaponFit::NOT_A_WEAPON;
  }
  try {
    const auto& w = pmt.get_weapon(item.data1[1], item.data1[2]);
    uint8_t bits = spire_weapon_usability_bits(p.char_class);
    if ((w.usability_flags & bits) != bits) {
      return SpireWeaponFit::NO;
    }
    if (spire_drops_stats_meet(w, p.now)) {
      return SpireWeaponFit::NOW;
    }
    return spire_drops_stats_meet(w, p.ahead) ? SpireWeaponFit::AHEAD : SpireWeaponFit::NO;
  } catch (const std::exception&) {
    return SpireWeaponFit::NO; // not in Sega's table: nothing the player can equip
  }
}

ItemData spire_drops_pick_weapon(const ItemParameterTable& pmt, const SpireDropPlayer& p, uint32_t random) {
  std::vector<ItemData> best; // per weapon group, its last (strongest) common weapon the player can equip now
  for (uint8_t group = 0x01; (group <= 0x0C) && (group < pmt.num_weapon_classes()); group++) {
    ItemData group_best;
    size_t count = pmt.num_weapons_in_class(group);
    for (size_t index = 0; index < count && index < 0x100; index++) {
      ItemData w;
      w.data1[0] = 0x00;
      w.data1[1] = group;
      w.data1[2] = index;
      if (!pmt.is_item_rare(w) && (spire_drops_weapon_fit(pmt, w, p) == SpireWeaponFit::NOW)) {
        group_best = w;
      }
    }
    if (!group_best.empty()) {
      best.emplace_back(group_best);
    }
  }
  if (!best.empty()) {
    return best[random % best.size()];
  }
  // Nothing fits now (a level-1 ranger in Sega's table: ATA 40, the Handgun asks 68): the common weapons of his class
  // he lacks the least for (ATP, ATA and MST short, summed), the start weapon of his class among them
  uint32_t least = 0xFFFFFFFF;
  uint8_t bits = spire_weapon_usability_bits(p.char_class);
  for (uint8_t group = 0x01; (group <= 0x0C) && (group < pmt.num_weapon_classes()); group++) {
    size_t count = pmt.num_weapons_in_class(group);
    for (size_t index = 0; index < count && index < 0x100; index++) {
      ItemData w;
      w.data1[1] = group;
      w.data1[2] = index;
      if (pmt.is_item_rare(w)) {
        continue;
      }
      const auto& def = pmt.get_weapon(group, index);
      if ((def.usability_flags & bits) != bits) {
        continue;
      }
      auto short_of = [](uint16_t need, uint16_t have) -> uint32_t { return (need > have) ? (need - have) : 0; };
      uint32_t lack = short_of(def.atp_required, p.now.atp) + short_of(def.ata_required, p.now.ata) +
          short_of(def.mst_required, p.now.mst);
      if (lack < least) {
        least = lack;
        best.clear();
      }
      if (lack == least) {
        best.emplace_back(w);
      }
    }
  }
  return best.empty() ? ItemData() : best[random % best.size()];
}

SpireWeaponBoost spire_drops_boss_boost(const SpireDropsConfig& cfg, uint32_t k) {
  SpireWeaponBoost ret;
  if (k) {
    ret.grind = std::min<uint32_t>(255, cfg.boss_grind_base + cfg.boss_grind_per_k * k);
    ret.hit = std::min<uint32_t>(SPIRE_DROPS_BOSS_HIT_MAX, cfg.boss_hit_base + cfg.boss_hit_per_k * k);
  }
  return ret;
}

SpireWeaponBoost spire_drops_pick_boost(const SpireDropsConfig& cfg, uint32_t k) {
  SpireWeaponBoost ret;
  if (k && cfg.pick_from_k && (k >= cfg.pick_from_k)) {
    ret.grind = std::min<uint32_t>(255, SPIRE_DROPS_PICK_GRIND_BASE + SPIRE_DROPS_PICK_GRIND_PER_K * k);
    ret.hit = std::min<uint32_t>(SPIRE_DROPS_PICK_HIT_MAX, SPIRE_DROPS_PICK_HIT_BASE + SPIRE_DROPS_PICK_HIT_PER_K * k);
  }
  return ret;
}

bool spire_drops_boost_weapon(const ItemParameterTable& pmt, ItemData& item, SpireWeaponBoost boost) {
  if (item.empty() || (item.data1[0] != 0x00)) {
    return false;
  }
  uint8_t max_grind;
  try {
    max_grind = pmt.get_weapon(item.data1[1], item.data1[2]).max_grind;
  } catch (const std::exception&) {
    return false; // not in Sega's table: left as it is
  }
  bool changed = false;
  uint8_t grind = std::min(max_grind, boost.grind);
  if (grind > item.data1[3]) {
    item.data1[3] = grind;
    changed = true;
  }
  if (boost.hit) {
    // Weapon attributes: (type, percent) in data1[6..7], [8..9], [10..11]; type 5 is Hit
    size_t slot = 0;
    for (size_t z = 6; z <= 10; z += 2) {
      if (item.data1[z] == SPIRE_WEAPON_ATTRIBUTE_HIT) {
        slot = z;
        break;
      }
      if (!slot && (item.data1[z] == 0)) {
        slot = z; // the first empty slot, unless a later slot already holds Hit
      }
    }
    if (slot) {
      int8_t own = (item.data1[slot] == SPIRE_WEAPON_ATTRIBUTE_HIT) ? static_cast<int8_t>(item.data1[slot + 1]) : 0;
      if ((item.data1[slot] != SPIRE_WEAPON_ATTRIBUTE_HIT) || (own < static_cast<int8_t>(boost.hit))) {
        item.data1[slot] = SPIRE_WEAPON_ATTRIBUTE_HIT;
        item.data1[slot + 1] = boost.hit;
        changed = true;
      }
    }
  }
  return changed;
}

uint8_t spire_drops_floor_k(
    const std::map<uint8_t, std::map<std::pair<uint8_t, int32_t>, uint8_t>>& rooms_by_episode, Episode episode,
    uint8_t floor) {
  auto ep_it = rooms_by_episode.find(static_cast<uint8_t>(episode));
  if (ep_it == rooms_by_episode.end()) {
    return 0;
  }
  uint8_t ret = 0;
  for (auto it = ep_it->second.lower_bound(std::make_pair(floor, INT32_MIN));
      (it != ep_it->second.end()) && (it->first.first == floor); it++) {
    ret = std::max(ret, it->second);
  }
  return ret;
}

const char* spire_drops_filter_item(const SpireDropsConfig& cfg, const ItemParameterTable& pmt, ItemData& item,
    const SpireDropPlayer& p, bool is_boss, uint32_t& pity, uint32_t random, uint32_t k) {
  auto fit = spire_drops_weapon_fit(pmt, item, p);
  // F8: a picked weapon gets the pick grind and Hit of room k, a boss weapon the boss grind and Hit
  auto give_weapon = [&](const char* why, const char* why_none, bool boss = false) -> const char* {
    ItemData w = spire_drops_pick_weapon(pmt, p, random);
    if (w.empty()) {
      return why_none;
    }
    spire_drops_boost_weapon(pmt, w, boss ? spire_drops_boss_boost(cfg, k) : spire_drops_pick_boost(cfg, k));
    item = w;
    pity = 0;
    return why;
  };

  if (is_boss && cfg.boss_weapon_per_player) { // item 28: exactly one weapon each player can equip now
    if (fit == SpireWeaponFit::NOW) {
      pity = 0;
      return spire_drops_boost_weapon(pmt, item, spire_drops_boss_boost(cfg, k))
          ? "boss: Sega's weapon, with the boss grind and Hit"
          : nullptr;
    }
    const char* why = give_weapon("boss: one weapon this player can equip", nullptr, true);
    if (why) {
      return why;
    } // no weapon fits at all: the normal rules below
  }
  if (item.empty()) {
    return nullptr;
  }
  if (cfg.only_run_items && !spire_drops_is_run_item(item)) { // item 30
    item.clear();
    return "not a run item (rule book item 30)";
  }
  if (fit == SpireWeaponFit::NOW) {
    pity = 0;
    return nullptr;
  }
  if (fit == SpireWeaponFit::NO) { // items 25-26
    if (!give_weapon("weapon this player cannot equip, replaced", nullptr)) {
      item.clear();
      return "weapon this player cannot equip, removed (no weapon fits)";
    }
    return "weapon this player cannot equip, replaced";
  }
  // AHEAD weapon or another run item: item 27
  if (cfg.pity_drops && (pity >= cfg.pity_drops)) {
    const char* why = give_weapon("pity: no usable weapon in the last drops", nullptr);
    if (why) {
      return why;
    }
  }
  pity++;
  return nullptr;
}

bool spire_drops_is_boss_drop(EnemyType type, uint16_t child_index) {
  if (child_index != 0) { // a child of a boss set (Gol Dragon's 5, Barba Ray's trailing BARBA_RAY): the head drops
    return false;
  }
  switch (type) {
    case EnemyType::VOL_OPT_1:
    case EnemyType::VOL_OPT_AMP:
    case EnemyType::VOL_OPT_CORE:
    case EnemyType::VOL_OPT_MONITOR:
    case EnemyType::VOL_OPT_PILLAR:
    case EnemyType::DARK_FALZ_1: // Dark Falz: only the final phase (DF2 on Normal, DF3 above) is the boss drop
    case EnemyType::BARBA_RAY_JOINT: // Barba Ray: its 10 joints carry Sega's boss flag; only BARBA_RAY is the boss drop
      return false;
    default:
      return type_definition_for_enemy(type).is_boss();
  }
}

static std::unordered_map<const Client*, uint32_t> spire_drops_pity;

static const SpireDropsConfig* spire_drops_config_for(std::shared_ptr<const Client> c, std::shared_ptr<const Lobby> l) {
  if (!c || !l || !spire_game_is_spire(c, l)) {
    return nullptr;
  }
  try {
    const auto& cfg = c->require_server_state()->data->spire_drops;
    return cfg.enabled ? &cfg : nullptr;
  } catch (const std::exception&) {
    return nullptr;
  }
}

void spire_drops_quest_loaded(std::shared_ptr<Lobby> l, std::shared_ptr<const Client> leader_c) {
  for (const auto& lc : l->clients) {
    if (lc) {
      spire_drops_pity.erase(lc.get());
    }
  }
  const auto* cfg = spire_drops_config_for(leader_c, l);
  if (!cfg) {
    return;
  }
  l->allowed_drop_modes |= (1 << static_cast<size_t>(cfg->drop_mode));
  l->drop_mode = cfg->drop_mode;
  l->log.info_f("Spire drops: q{} uses the server's drops ({})", l->quest->meta.quest_number, cfg->str());
}

bool spire_drops_filter(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, ItemData& item, bool is_boss) {
  const auto* cfg = spire_drops_config_for(c, l);
  if (!cfg) {
    return false;
  }
  try {
    auto s = c->require_server_state();
    auto p = c->character_file();
    auto pmt = s->data->item_parameter_table(c->version());
    auto level_table = s->data->level_table(c->version());
    SpireDropPlayer player;
    player.char_class = p->disp.visual.sh.char_class;
    player.now = p->disp.stats.char_stats;
    player.ahead = spire_drops_stats_ahead(
        *level_table, player.char_class, p->disp.stats.level, player.now, cfg->levels_ahead);
    ItemData before = item;
    uint32_t& pity = spire_drops_pity[c.get()];
    uint32_t pity_before = pity;
    uint8_t k = spire_drops_floor_k(s->data->spire_checkpoint_rooms, l->episode, c->floor);
    const char* why = spire_drops_filter_item(*cfg, *pmt, item, player, is_boss, pity, l->rand_crypt->next(), k);
    if (why) {
      l->log.info_f("Spire drops: {} for {} (Lv{}, room {}, pity {}->{}): {} -> {}", why, c->channel->name,
          p->disp.stats.level + 1, k, pity_before, pity, before.empty() ? "nothing" : s->data->describe_item(c->version(), before),
          item.empty() ? "nothing" : s->data->describe_item(c->version(), item));
    }
    return why != nullptr;
  } catch (const std::exception& e) {
    l->log.warning_f("Spire drops: filter skipped for {}: {}", c->channel->name, e.what());
    return false;
  }
}
