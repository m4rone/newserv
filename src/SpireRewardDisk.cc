#include "SpireRewardKinds.hh"

#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>

#include <stdint.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ServerState.hh"

// Reward kind 4 (DISK: a technique disk the run copy can learn and reach). Owner: the force mechanic; switch
// "force.schijf" (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 3, 5 and 7).
// Moved literally out of SpireRewards.cc and SpireFlagBridge.cc by the seam (docs/review-loop/goals/naad.md point 3):
// the text of every block below is what stood there, the indentation and the function around it aside. New are the
// lines that bind the old names to what the core hands over (const auto& table = in.table;) and the registration.

uint8_t spire_reward_disk_level(const SpireRewardTech& tech, uint32_t budget, uint8_t known, uint8_t class_max,
    const SpireRewardDiskState& state) {
  if (class_max == 0xFF) {
    return 0; // the class cannot learn it
  }
  uint8_t max_level = std::min<uint8_t>(class_max, 29) + 1;
  uint8_t known_level = (known == 0xFF) ? 0 : (std::min<uint8_t>(known, 29) + 1);
  if (state.mst_cap) {
    budget = std::min(budget, state.mst_cap);
  }
  uint8_t fit = max_level; // the highest level the budget meets, 0 for none
  for (; fit && (spire_reward_disk_mst(tech, fit) > budget); fit--) {
  }
  // a technique the copy knows comes at least two levels higher (KV par. 4: "disk +2 levels"), one at the class's top
  uint8_t floor = known_level ? std::min<uint8_t>(known_level + 2, max_level) : 1;
  uint8_t level = std::max(fit, floor);
  if (level <= known_level) {
    return 0; // the copy knows the class's highest already
  }
  level = std::min<uint8_t>(level + state.extra_levels, max_level); // a drawn curse's levels
  for (; level > known_level; level--) {
    uint32_t mst = spire_reward_disk_mst(tech, level);
    if ((!state.mst_cap || (mst <= state.mst_cap)) && (spire_reward_disk_mind(mst, state.mst) <= state.mind_room)) {
      return level;
    }
  }
  return 0; // no level above what the copy knows that it could use
}

namespace {

// The section of kind 4: Disks, the techniques with the MST each level asks and Sega's disks per room and tier
void spire_reward_disk_parse(const phosg::JSON& json, SpireRewardTable& ret) {
  if (json.contains("Disks")) { // part B, the disk; a file without it gives a DISK entry its own item
    const auto& disks = json.at("Disks");
    const auto& techs = disks.get_list("Techs");
    if (techs.size() != SPIRE_TECH_COUNT) {
      throw std::runtime_error(std::format("Disks Techs has {} techniques, not {}", techs.size(), SPIRE_TECH_COUNT));
    }
    for (size_t t = 0; t < SPIRE_TECH_COUNT; t++) {
      ret.techs[t].name = techs[t]->get_string("Name");
      const auto& req = techs[t]->get_list("Req");
      if (ret.techs[t].name.empty() || (req.size() != SPIRE_TECH_LEVELS)) {
        throw std::runtime_error(std::format("Disks Techs {}: no name, or Req has {} levels, not {}", t, req.size(),
            SPIRE_TECH_LEVELS));
      }
      for (size_t z = 0; z < SPIRE_TECH_LEVELS; z++) {
        int64_t v = req[z]->as_int();
        if ((v < 1) || (v > 5000) || (z && (v < ret.techs[t].req[z - 1]))) {
          throw std::runtime_error(std::format("Disks Techs {} Req level {}: {} is not 1-5000 or falls", t, z + 1, v));
        }
        ret.techs[t].req[z] = v;
      }
    }
    for (const auto& [room, r_json] : disks.get_dict("Rooms")) {
      const auto& rows = r_json->as_list();
      if (rows.size() != SPIRE_TECH_COUNT) {
        throw std::runtime_error(std::format("Disks Rooms {}: {} techniques, not {}", room, rows.size(), SPIRE_TECH_COUNT));
      }
      std::array<std::array<uint8_t, 4>, SPIRE_TECH_COUNT> levels{};
      for (size_t t = 0; t < SPIRE_TECH_COUNT; t++) {
        const auto& row = rows[t]->as_list();
        if (row.size() != 4) {
          throw std::runtime_error(std::format("Disks Rooms {} technique {}: {} tiers, not 4", room, t, row.size()));
        }
        for (size_t z = 0; z < 4; z++) {
          int64_t v = row[z]->as_int();
          if ((v < 0) || (v > 30)) {
            throw std::runtime_error(std::format("Disks Rooms {} technique {} {}: level {} outside 0-30", room, t,
                TIER_NAMES[z], v));
          }
          levels[t][z] = v;
        }
      }
      uint32_t index = std::stoul(room);
      if (!index) {
        throw std::runtime_error("Disks Rooms 0: the first room is 1");
      }
      ret.disk_levels.emplace(index, levels);
    }
    if (ret.disk_levels.empty()) {
      throw std::runtime_error("Disks Rooms is empty");
    }
  }
}

// The MIND levels the run mag is planned to have at this point of the run (SpireRewardDiskState mind_plan): the row of
// the step the ledger expects, in the column of its lean, merged with the floor of the run's bought start levels; 0
// without a ledger or a usable mag table
static uint16_t spire_reward_mind_plan(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l,
    const SpireRunMagLedger* led, uint8_t char_class) {
  if (!led) {
    return 0;
  }
  try {
    auto table = spire_mag_parse(phosg::load_file(SPIRE_RUN_MAG_FILE));
    auto it = table.classes.find(spire_mag_archetype(char_class));
    if (!table.enabled || (it == table.classes.end())) {
      return 0;
    }
    auto column = spire_mag_column(it->second, led->lean);
    uint32_t step = spire_mag_expected_step(column.line.steps.size(), led->start_step, led->boss_rooms.size(), false);
    uint32_t bonus = l->quest ? spire_start_level_run_bonus(c.get(), l->lobby_id, l->quest->meta.quest_number).value_or(0) : 0;
    return spire_mag_stat_word(spire_mag_step_bytes(column.line, step, bonus, std::nullopt), 3) / 100;
  } catch (const std::exception&) {
    return 0;
  }
}

// What a DISK offer needs to know of the run copy (SpireRewardDiskState), read from the client at the bridge
SpireRewardDiskState spire_reward_disk_state(const SpireRewardLive& live) {
  const auto& c = live.c;
  const auto& l = live.l;
  const auto& character = live.character;
  const auto& overlay = live.overlay;
  const auto& entry = live.entry;
  const auto& curse = live.curse;
  std::optional<SpireRewardDiskState> disk;
  { // part B: what the run copy knows, may learn and can reach with its mag
    disk.emplace();
    auto s = c->require_server_state();
    auto ipt = s->data->item_parameter_table(Version::GC_V3);
    uint8_t char_class = character->disp.visual.sh.char_class;
    for (size_t t = 0; t < SPIRE_TECH_COUNT; t++) {
      disk->known[t] = overlay->get_technique_level(t);
      disk->class_max[t] = ipt->get_max_tech_level(char_class, t);
    }
    disk->mst = overlay->disp.stats.char_stats.mst + spire_reward_worn(ipt, overlay->inventory).bonus.mst;
    disk->mst_cap = s->data->level_table(c->version())->max_stats_for_class(char_class).char_stats.mst;
    auto* run = spire_game_run_for(l);
    auto* led = run ? spire_run_mag_ledger_for(c, run->run_id, false) : nullptr;
    uint32_t asked = led ? (led->need.pow + led->need.dex) : 0; // the reward weapon's POW and DEX stay (rule B)
    disk->mind_room = (asked < 200) ? (200 - asked) : 0;
    auto mag = spire_reward_run_mag(overlay->inventory);
    disk->mind_now = mag ? (spire_mag_stat_word(*mag, 3) / 100) : 0;
    disk->mind_plan = spire_reward_mind_plan(c, l, led, char_class);
    // a line that was a fixed disk ("Foie"): its own item names the technique
    if (entry.has_own_bytes && (entry.own_bytes[0] == 0x03) && (entry.own_bytes[1] == 0x02)) {
      disk->line_tech = entry.own_bytes[4];
    }
    disk->extra_levels = entry.rare ? spire_reward_curse_disk_levels(curse) : 0;
  }
  return *disk;
}

// The offer of kind 4: one technique the copy can learn and reach, at the level its MST budget meets
std::optional<SpireRewardOffer> spire_reward_disk_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
  const auto& table = in.table;
  const auto& cls = in.cls;
  const auto& room_index = in.room_index;
  const auto& tier = in.tier;
  const auto& tier_label = in.tier_label;
  const auto& rnd = in.rnd;
  const auto& rung = in.rung;
  // a test hands the run copy's state itself (in.disk); at the bridge the kind reads it from the client
  const SpireRewardDiskState* disk = in.disk;
  std::optional<SpireRewardDiskState> live_disk;
  if (!disk && in.live) {
    live_disk = spire_reward_disk_state(*in.live);
    disk = &*live_disk;
    in.live->log_detail = std::format(" (the line's own technique: {}; MST {}, mag MIND {} worn and {} planned, room for {}; a curse "
                    "adds {} level(s))",
            (disk->line_tech < SPIRE_TECH_COUNT) ? table.techs[disk->line_tech].name : std::string("none"), disk->mst,
            disk->mind_now, disk->mind_plan, disk->mind_room, disk->extra_levels);
  }
  if (!disk) {
    throw std::runtime_error("a disk offer without the run copy's techniques");
  }
  if (table.disk_levels.empty()) {
    throw std::runtime_error("the reward grid has no Disks");
  }
  // the room's row; past the last room of the grid, the last one (as spire_reward_w)
  auto r_it = table.disk_levels.upper_bound(room_index);
  if (r_it != table.disk_levels.begin()) {
    r_it--;
  }
  size_t z = static_cast<size_t>(tier) & 3;
  // the techniques Sega drops here: on a rung its disks of that difficulty, which the grid holds in a later column
  size_t column = std::min<size_t>(z + table.rung_disk_columns[rung & 3], 3);
  uint32_t budget = spire_reward_disk_budget(*disk, table.tier_disk_mind[z]);
  auto level_of = [&](size_t t) -> uint8_t {
    return spire_reward_disk_level(table.techs[t], budget, disk->known[t], disk->class_max[t], *disk);
  };
  std::vector<std::pair<uint8_t, uint8_t>> can; // technique, level
  // a line that names a disk of its own keeps that technique while the copy can still grow in it and reach it,
  // also where Sega drops none of it; only then any technique Sega drops here
  if (disk->line_tech < SPIRE_TECH_COUNT) {
    uint8_t level = level_of(disk->line_tech);
    if (level) {
      can.emplace_back(disk->line_tech, level);
    }
  }
  if (can.empty()) {
    for (size_t t = 0; t < SPIRE_TECH_COUNT; t++) {
      uint8_t level = r_it->second[t][column] ? level_of(t) : 0;
      if (level) {
        can.emplace_back(static_cast<uint8_t>(t), level);
      }
    }
  }
  if (can.empty()) {
    throw std::runtime_error(std::format("no technique disk for {} at room index {} {}: every technique of the tier "
                                         "is known, not the class's, or asks more MST than the copy can reach",
        cls.name, room_index, tier_label));
  }
  const auto& [tech, level] = can[rnd(can.size()) % can.size()];
  ret.tech = tech;
  ret.tech_level = level;
  ret.mst_required = spire_reward_disk_mst(table.techs[tech], level);
  ret.item[0] = 0x03;
  ret.item[1] = 0x02;
  ret.item[2] = level - 1;
  ret.item[4] = tech;
  ret.item_name = std::format("{} Lv{}", table.techs[tech].name, level);
  ret.label = tier_label + " " + ret.item_name;
  return ret;
}

// 'Z' sub 6 of kind 4: the MIND the disk asks of the mag, then the answer with the item
void spire_reward_disk_pick(SpireRewardPick& pk) {
  auto& c = pk.c;
  auto& serial_number = pk.serial_number;
  auto& quest_number = pk.quest_number;
  auto& slot = pk.slot;
  auto& character = pk.character;
  auto& offer = pk.offer;
  auto& p = pk.p;
  auto& led = pk.led;
  auto& refusal = pk.refusal;
  auto& worn = pk.worn;
  auto& own_item = pk.own_item;
  using R = SpireRunMagRefusal;
  // part B, the disk: rule B's need keeps the weapon's POW and DEX, and the run's disks' MIND takes what they leave of
  // the mag's 200 levels (spire_reward_need_with_disk). So a disk never costs the weapon its requirement, and a later
  // reward weapon can be worn when it is given (the disks are learned by then).
  // never a disk newserv itself refuses when it is used (Items.cc throws on a level above the class's highest)
  uint8_t class_max = character ? c->require_server_state()->data->item_parameter_table(Version::GC_V3)->get_max_tech_level(
                                      character->disp.visual.sh.char_class, offer->tech)
                                : 0xFF;
  if ((class_max == 0xFF) || (offer->tech_level < 1) || (offer->tech_level > class_max + 1)) {
    own_item(std::format("disk {} is above the class's highest level", offer->label));
    return;
  }
  p.disk_mind = std::min<uint16_t>(spire_reward_disk_mind(offer->mst_required, p.mst), 200);
  p.need = spire_reward_need_with_disk(led ? led->need : SpireMagNeed{},
      std::max<uint16_t>(led ? led->disk_mind : 0, p.disk_mind));
  spire_reward_pick_rule_b(pk); // SpireFlagBridge.cc: p.mag_due, the pick in flight, the answer
  c->log.info_f("Spire reward: pick {} {} slot {} for serial {:010} in q{}: {} {}; the client asks MST {}, the run "
      "copy has {} (worn units +{}), so mag MIND {}; rule B needs mag POW {} DEX {} MIND {}; {}",
      spire_reward_kind_name(offer->kind), spire_reward_tier_name(offer->tier), slot, serial_number, quest_number,
      offer->label, spire_run_mag_bytes_hex(offer->item), offer->mst_required, p.mst, worn.bonus.mst, p.disk_mind,
      p.need.pow, p.need.dex, p.need.mind,
      !led ? std::format("no rule B ({})", (refusal == R::NONE) ? "no run or ledger" : spire_run_mag_refusal_name(refusal))
      : p.mag_due ? std::string("rule B's run mag follows in this room's pause B; the disk is the player's to use after it")
                  : std::string("the run mag already meets it"));
}

// 'Z' sub 7 of kind 4: the disk exists
void spire_reward_disk_result(SpireRewardResult& rs) {
  auto& c = rs.c;
  auto& serial_number = rs.serial_number;
  auto& quest_number = rs.quest_number;
  auto& p = rs.p;
  spire_reward_result_rule_b(rs); // SpireFlagBridge.cc: the ledger keeps the disk's MIND
  auto& led = rs.led;
  // nothing to equip: the player uses the disk
  c->log.info_f("Spire reward: RESULT 1 for serial {:010} in q{} slot {}: {} created{}", serial_number, quest_number,
      p.offer.slot, p.offer.label, led ? (p.mag_due ? "; rule B mag owed" : "; the mag meets it") : "; no run mag ledger");
}

[[maybe_unused]] const bool spire_reward_disk_registered = spire_mech_register_kind({SpireRewardKind::DISK, "force.schijf",
    false, spire_reward_disk_parse, spire_reward_disk_offer, spire_reward_disk_pick, spire_reward_disk_result});

} // namespace
