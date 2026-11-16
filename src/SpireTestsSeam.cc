#include <phosg/Filesystem.hh>
#include <phosg/Hash.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <phosg/UnitTest.hh>

#include <stdlib.h>

#include <array>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ItemParameterTable.hh"
#include "SpireFlagBridge.hh"
#include "SpireMag.hh"
#include "SpireMechanics.hh"
#include "SpireRewards.hh"

// The seam's guard (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 9; this repo's
// docs/review-loop/goals/naad.md): the seam moves the reward kinds 1-5 and the run mag's savestate keys into files of
// their own and must not change what a player is offered, what a pick asks of the mag, or what a savestate reads and
// writes. Two measurements, both taken on the code from before the seam:
//
//   1. A snapshot of every offer of spire_reward_offer on the real grid (SPIRE_REWARDS_FILE), as one text dump with one
//      line per offer, and its fingerprint in tests/spire-seam/offers.fingerprint (SHA-256 and line count per section).
//      The test fails when a section's fingerprint differs.
//   2. Savestates as the code from before the seam wrote them (tests/spire-seam/savestate-*.json), with the run mag's
//      ledger keys in every state they have: each must load and be written back as exactly the same text.
//
// SPIRE_SEAM_DUMP=<path> writes what the code under test produces, to diff two commits: <path> (the dump),
// <path>.fingerprint and <path>.<fixture name> (each savestate as written now). Without the fingerprint file the
// comparison is skipped with one line on stderr: the file goes after the seam's review, the instrument stays.
//
// NOT covered here (they need a live client and game): the 'Q' and 'Z' handlers themselves and the coroutines that
// equip an item (SpireFlagBridge.cc spire_reward_make_offer, spire_reward_draw_offers, spire_reward_pick,
// spire_reward_answer, spire_reward_result, spire_reward_equip, spire_reward_equip_unit).

namespace {

constexpr const char* SEAM_FINGERPRINT_FILE = "tests/spire-seam/offers.fingerprint";
constexpr const char* SEAM_DIR = "tests/spire-seam/";
constexpr const char* SEAM_ENV = "SPIRE_SEAM_DUMP";

// Three fixed streams per row. s0 and s1 are uniform 0..n-1, as the server's own rnd is; s2 is NOT reduced to n, so the
// "% size" behind every rnd call of the offer is pinned too. The seed is the row's own key, so a row's stream does not
// depend on the rows before it.
constexpr std::array<uint64_t, 3> SEAM_SEEDS = {0x5EA3000000000001ULL, 0x5EA3000000000002ULL, 0x5EA3000000000003ULL};
constexpr size_t SEAM_RAW_MODE = 2;

struct SeamRnd {
  uint64_t state = 0;
  bool raw = false;
  std::string calls; // "n:value" per call, in call order: the number and order of the calls is part of the snapshot

  uint32_t next(uint32_t n) {
    this->state += 0x9E3779B97F4A7C15ULL; // splitmix64
    uint64_t z = this->state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;
    uint32_t v = this->raw ? static_cast<uint32_t>(z >> 32) : (n ? static_cast<uint32_t>(z % n) : 0);
    this->calls += std::format("{}{}:{}", this->calls.empty() ? "" : ",", n, v);
    return v;
  }
};

// Every field of an offer
std::string seam_offer_text(const SpireRewardOffer& o) {
  return std::format("kind={} tier={} w={} room={} slot={} entry={} line={} from={} item={} name=\"{}\" food={} "
                     "tech={:02X} level={} mst={} info=\"{}\",{},{},{},{},{},{} label=\"{}\"",
      static_cast<int>(o.kind), static_cast<int>(o.tier), o.w, o.room_index, o.slot, o.entry_id,
      o.line.empty() ? "-" : o.line, o.from_line.empty() ? "-" : o.from_line, spire_run_mag_bytes_hex(o.item), o.item_name,
      o.food_levels, o.tech, o.tech_level, o.mst_required, o.info.name, o.info.stars, o.info.atp, o.info.ata, o.info.mst,
      o.info.weapon_ata, o.info.max_grind, o.label);
}

// One line: the row's key, the rnd calls it made and the offer, or the exception it threw
void seam_row(std::string& out, const std::string& key, size_t mode,
    const std::function<SpireRewardOffer(const std::function<uint32_t(uint32_t)>&)>& make) {
  std::string full_key = std::format("{} s{}", key, mode);
  SeamRnd rnd{phosg::fnv1a64(full_key) ^ SEAM_SEEDS[mode], mode == SEAM_RAW_MODE, ""};
  std::function<uint32_t(uint32_t)> fn = [&rnd](uint32_t n) -> uint32_t { return rnd.next(n); };
  std::string result;
  try {
    result = seam_offer_text(make(fn));
  } catch (const std::exception& e) {
    result = std::format("THROW {}", e.what());
  }
  out += std::format("{} | rnd={} | {}\n", full_key, rnd.calls.empty() ? "-" : rnd.calls, result);
}

// The weapon a run copy holds (the offer's `held`). Which branch of the offer each variant reaches:
//   none          held == nullptr: the class's first line; for a force its first MST line (own_line)
//   kit:<line>    the kit weapon of each line of the class, found by its code (LineOf): the reward follows the weapon
//                 in use, also a force that holds a saber, slicer or gun
//   group:<line>  a code LineOf does not know, in the group (data1[1], LineGroups) of the class's last line
//   foreign:<line> a code in the group of a line the class has not: no line, so the first line again
//   lineof:<code> a code LineOf knows outside the plain groups (a rare weapon of a line), if the class has that line
//   unknown       a weapon in no group and not in LineOf: no line
//   nonweapon     an item that is no weapon (data1[0] = 01): no line
struct SeamHeld {
  std::string name;
  bool has = false;
  std::array<uint8_t, 12> data{};
  const uint8_t* ptr() const {
    return this->has ? this->data.data() : nullptr;
  }
};

SeamHeld seam_held(const std::string& name, uint8_t b0, uint8_t b1, uint8_t b2) {
  SeamHeld ret;
  ret.name = name;
  ret.has = true;
  ret.data[0] = b0;
  ret.data[1] = b1;
  ret.data[2] = b2;
  return ret;
}

std::vector<SeamHeld> seam_held_variants(const SpireRewardTable& table, const SpireRewardClass& cls) {
  std::vector<SeamHeld> ret;
  ret.emplace_back(SeamHeld{"none", false, {}});
  for (const auto& ln : cls.line_order) {
    uint32_t kit = cls.lines.at(ln).kit;
    ret.emplace_back(seam_held("kit:" + ln, (kit >> 16) & 0xFF, (kit >> 8) & 0xFF, kit & 0xFF));
  }
  bool own_group = false, foreign_group = false;
  for (const auto& [group, ln] : table.line_groups) {
    if (table.line_of.count((static_cast<uint32_t>(group) << 8) | 0x7F)) {
      continue;
    }
    if (!own_group && (ln == cls.line_order.back())) {
      ret.emplace_back(seam_held("group:" + ln, 0x00, group, 0x7F));
      own_group = true;
    } else if (!foreign_group && !cls.lines.count(ln)) {
      ret.emplace_back(seam_held("foreign:" + ln, 0x00, group, 0x7F));
      foreign_group = true;
    }
  }
  for (const auto& [code, ln] : table.line_of) {
    if (!table.line_groups.count((code >> 8) & 0xFF) && cls.lines.count(ln)) {
      ret.emplace_back(seam_held(std::format("lineof:{:04X}", code), 0x00, (code >> 8) & 0xFF, code & 0xFF));
      break;
    }
  }
  ret.emplace_back(seam_held("unknown", 0x00, 0xEE, 0x00));
  ret.emplace_back(seam_held("nonweapon", 0x01, 0x01, 0x00));
  return ret;
}

// What a DISK offer knows of the run copy (SpireRewardDiskState). class_max is always the class's own, from the item
// parameter table, as the bridge fills it: an android (every technique 0xFF) throws "no technique disk" in every
// variant. Which branch of the offer and of spire_reward_disk_level each variant reaches:
//   nostate   no state at all: the offer throws (a disk offer without the run copy's techniques)
//   fresh     nothing known, low MST, no cap, no mag: the budget meets little, so the floor "at least level 1", then
//             down while the MIND it takes is above the mag's room
//   clamped   mind_room below the mag's MIND (the budget's min) and mst_cap below the budget (the cap in the level, and
//             the cap in the way down)
//   planned   the mag's plan with the tier's tilt is above the worn mag at every tier
//   mixed     the worn mag is above the plan at Normal, Rare and Epic, the plan with its tilt above it at Legendary
//   ownline   the entry names Foie, the copy knows Foie Lv5 and a curse adds 2 levels: the line's own technique is
//             kept, at least two levels above what is known, the curse's levels on top; held is a kit weapon
//   ownnodrop the entry names Reverser, which Sega drops in few rooms: kept where the class learns it, else any
//             technique Sega drops there
//   owntop    the entry names Ryuker and the copy knows the class's highest of it: no level, so any technique Sega drops
//   top       every technique at the class's highest: nothing to give, the offer throws
//   neartop   every technique one below the class's highest: "one level when that is the class's highest", and the
//             curse's levels cut at the class's highest
//   tight     MST 10 with room for 20 MIND and a low cap, Foie Lv1 known: only a level 1 within reach, and never down
//             to what the copy knows
//   unreach   no MST, no room, a cap below every requirement: no level the copy could use, the offer throws
struct SeamDisk {
  std::string name;
  bool has = true;
  bool held_kit = false;
  SpireRewardDiskState st;
};

std::vector<SeamDisk> seam_disk_variants(const std::array<uint8_t, SPIRE_TECH_COUNT>& class_max) {
  std::vector<SeamDisk> ret;
  auto add = [&](const char* name) -> SeamDisk& {
    auto& d = ret.emplace_back();
    d.name = name;
    d.st.class_max = class_max;
    return d;
  };
  add("nostate").has = false;
  {
    auto& d = add("fresh");
    d.st.mst = 60;
  }
  {
    auto& d = add("clamped");
    d.st.mst = 200;
    d.st.mst_cap = 280;
    d.st.mind_now = 60;
    d.st.mind_plan = 10;
    d.st.mind_room = 50;
  }
  {
    auto& d = add("planned");
    d.st.mst = 300;
    d.st.mind_now = 10;
    d.st.mind_plan = 30;
  }
  {
    auto& d = add("mixed");
    d.st.mst = 150;
    d.st.mst_cap = 1500;
    d.st.mind_now = 60;
    d.st.mind_plan = 10;
  }
  {
    auto& d = add("ownline");
    d.held_kit = true;
    d.st.mst = 300;
    d.st.mind_plan = 30;
    d.st.line_tech = 0; // Foie
    d.st.known[0] = 4;
    d.st.extra_levels = 2;
  }
  {
    auto& d = add("ownnodrop");
    d.st.mst = 5000;
    d.st.line_tech = 17; // Reverser
  }
  {
    auto& d = add("owntop");
    d.st.mst = 5000;
    d.st.mind_room = 0;
    d.st.line_tech = 14; // Ryuker
    d.st.known[14] = class_max[14];
  }
  {
    auto& d = add("top");
    d.st.mst = 5000;
    d.st.known = class_max;
  }
  {
    auto& d = add("neartop");
    d.st.mst = 5000;
    d.st.extra_levels = 2;
    for (size_t t = 0; t < SPIRE_TECH_COUNT; t++) {
      d.st.known[t] = ((class_max[t] != 0xFF) && (class_max[t] >= 1)) ? (class_max[t] - 1) : 0xFF;
    }
  }
  {
    auto& d = add("tight");
    d.st.mst = 10;
    d.st.mst_cap = 100;
    d.st.mind_room = 20;
    d.st.known[0] = 0;
  }
  {
    auto& d = add("unreach");
    d.st.mst = 0;
    d.st.mst_cap = 30;
    d.st.mind_room = 0;
  }
  return ret;
}

struct SeamSection {
  std::string name;
  std::string text;
};

size_t seam_count_lines(const std::string& text) {
  size_t ret = 0;
  for (char ch : text) {
    ret += (ch == '\n');
  }
  return ret;
}

constexpr std::array<SpireRewardKind, 5> SEAM_KINDS = {SpireRewardKind::WEAPON, SpireRewardKind::NEW_LINE,
    SpireRewardKind::MAG_FOOD, SpireRewardKind::DISK, SpireRewardKind::UNIT};
constexpr size_t SEAM_CLASSES = 12;
constexpr uint32_t SEAM_ROOMS = 15; // k 1-15
constexpr size_t SEAM_TIERS = 4;
constexpr uint8_t SEAM_RUNGS = 4;

// The snapshot: one section per reward kind over class x room k x tier x rung x variant x stream, then the edges, the
// shared parts and the pure parts of the pick and its result
std::vector<SeamSection> seam_offer_sections(const SpireRewardTable& table,
    const std::array<std::array<uint8_t, SPIRE_TECH_COUNT>, SEAM_CLASSES>& class_max) {
  std::vector<SeamSection> sections;
  for (auto kind : SEAM_KINDS) {
    sections.emplace_back(SeamSection{std::format("kind{}", static_cast<int>(kind)), ""});
  }
  auto section_of = [&](SpireRewardKind kind) -> std::string& {
    return sections[static_cast<size_t>(kind) - 1].text;
  };

  for (uint8_t c = 0; c < SEAM_CLASSES; c++) {
    const auto& cls = table.classes.at(c);
    auto helds = seam_held_variants(table, cls);
    auto disks = seam_disk_variants(class_max[c]);
    const SeamHeld& no_held = helds[0];
    const SeamHeld& last_kit = helds[cls.line_order.size()]; // kit:<the class's last line>
    for (uint32_t k = 1; k <= SEAM_ROOMS; k++) {
      uint32_t room = spire_reward_room_index(k);
      for (size_t t = 0; t < SEAM_TIERS; t++) {
        auto tier = static_cast<SpireRewardTier>(t);
        for (uint8_t rung = 0; rung < SEAM_RUNGS; rung++) {
          std::string where = std::format("c{:02} k{:02} t{} r{}", c, k, t, rung);
          // kind 1 WEAPON and kind 2 NEW_LINE: every held variant (the line the offer follows, and for NEW_LINE the
          // other lines to choose from: for a force only its MST lines); rnd picks the new line, then the special
          for (auto kind : {SpireRewardKind::WEAPON, SpireRewardKind::NEW_LINE}) {
            for (const auto& held : helds) {
              for (size_t mode = 0; mode < SEAM_SEEDS.size(); mode++) {
                seam_row(section_of(kind), std::format("K{} {} h={}", static_cast<int>(kind), where, held.name), mode,
                    [&](const std::function<uint32_t(uint32_t)>& rnd) {
                      return spire_reward_offer(table, kind, c, held.ptr(), room, tier, rnd, nullptr, rung);
                    });
              }
            }
          }
          // kind 3 MAG_FOOD: the tier's levels; no rnd call (one stream says so); held only names from_line
          for (const SeamHeld* held : {&no_held, &last_kit}) {
            seam_row(section_of(SpireRewardKind::MAG_FOOD), std::format("K3 {} h={}", where, held->name), 0,
                [&](const std::function<uint32_t(uint32_t)>& rnd) {
                  return spire_reward_offer(table, SpireRewardKind::MAG_FOOD, c, held->ptr(), room, tier, rnd, nullptr, rung);
                });
          }
          // kind 4 DISK: every state of the run copy (seam_disk_variants); rnd picks among the techniques with a level
          for (const auto& disk : disks) {
            const SeamHeld& held = disk.held_kit ? last_kit : no_held;
            for (size_t mode = 0; mode < SEAM_SEEDS.size(); mode++) {
              seam_row(section_of(SpireRewardKind::DISK), std::format("K4 {} h={} d={}", where, held.name, disk.name), mode,
                  [&](const std::function<uint32_t(uint32_t)>& rnd) {
                    return spire_reward_offer(table, SpireRewardKind::DISK, c, held.ptr(), room, tier, rnd,
                        disk.has ? &disk.st : nullptr, rung);
                  });
            }
          }
          // kind 5 UNIT: the archetype's units of the tier; rnd picks one; held only names from_line
          for (const SeamHeld* held : {&no_held, &last_kit}) {
            for (size_t mode = 0; mode < SEAM_SEEDS.size(); mode++) {
              seam_row(section_of(SpireRewardKind::UNIT), std::format("K5 {} h={}", where, held->name), mode,
                  [&](const std::function<uint32_t(uint32_t)>& rnd) {
                    return spire_reward_offer(table, SpireRewardKind::UNIT, c, held->ptr(), room, tier, rnd, nullptr, rung);
                  });
            }
          }
        }
      }
    }
  }

  // The edges of every kind: a room index outside the grid's rooms (0, and past the last: the last row and the highest
  // W), a rung above 3 (read & 3), a class the grid has not (throws), a grid without Disks or Units (a part A file:
  // the DISK and UNIT offers throw, so the pick gives the entry's own item)
  {
    auto& out = sections.emplace_back(SeamSection{"edges", ""}).text;
    SpireRewardDiskState plain;
    plain.mst = 300;
    plain.mind_plan = 30;
    auto offer_on = [&](const SpireRewardTable& tb, const std::string& what, SpireRewardKind kind, uint8_t c, uint32_t room,
                        size_t t, uint8_t rung) {
      SpireRewardDiskState st = plain;
      if (c < SEAM_CLASSES) {
        st.class_max = class_max[c];
      }
      seam_row(out, std::format("E{} {} c{:02} room{} t{} r{}", static_cast<int>(kind), what, c, room, t, rung), 0,
          [&](const std::function<uint32_t(uint32_t)>& rnd) {
            return spire_reward_offer(tb, kind, c, nullptr, room, static_cast<SpireRewardTier>(t), rnd,
                (kind == SpireRewardKind::DISK) ? &st : nullptr, rung);
          });
    };
    SpireRewardTable part_a = table;
    part_a.disk_levels.clear();
    part_a.unit_tiers.clear();
    part_a.units.clear();
    for (auto kind : SEAM_KINDS) {
      for (size_t t = 0; t < SEAM_TIERS; t++) {
        for (uint8_t c = 0; c < SEAM_CLASSES; c++) {
          for (uint32_t room : {0, 16, 40}) {
            offer_on(table, "room", kind, c, room, t, 0);
          }
          for (uint8_t rung : {4, 5, 6, 7, 255}) {
            offer_on(table, "rung", kind, c, 5, t, rung);
          }
          offer_on(part_a, "part-a", kind, c, 5, t, 0);
        }
        for (uint8_t c : {12, 255}) {
          offer_on(table, "class", kind, c, 5, t, 0);
        }
      }
    }
  }

  // The shared part, which stays in SpireRewards.cc: the room index, W, the trede, the odds and the tier of a roll, the
  // names. spire_reward_kind_known is left out on purpose: the seam widens it to the kinds 6-8.
  {
    auto& out = sections.emplace_back(SeamSection{"shared", ""}).text;
    out += std::format("table enabled={} ata_model={} source=\"{}\"\n", table.enabled, spire_ata_model_name(table.ata_model),
        table.source);
    for (uint32_t k = 0; k <= 31; k++) {
      out += std::format("room_index k{} = {}\n", k, spire_reward_room_index(k));
    }
    for (uint32_t room = 0; room <= 17; room++) {
      for (uint8_t rung : {0, 1, 2, 3, 4, 7}) {
        out += std::format("w room{} r{} = {}\n", room, rung, spire_reward_w(table, room, rung));
      }
    }
    using S = std::set<uint8_t>;
    size_t trede_case = 0;
    for (const auto& [beaten, run] : std::initializer_list<std::pair<S, S>>{{{}, {}}, {{table.trede1_room}, {}},
             {{}, {table.trede1_room}}, {{table.trede2_room}, {}}, {{}, {table.trede2_room}},
             {{table.trede1_room}, {table.trede2_room}}, {{1, 2, 3}, {5, 6, 7}}, {{15}, {12}}}) {
      out += std::format("trede case{} = {}\n", trede_case++, spire_reward_trede(table, beaten, run));
    }
    for (uint32_t k = 0; k <= 16; k++) {
      for (uint32_t trede = 0; trede <= 3; trede++) {
        for (bool elite : {false, true}) {
          for (uint32_t rare_bonus : {0, 10, 255}) {
            auto odds = spire_reward_odds(table, k, trede, elite, rare_bonus);
            std::string rolls;
            for (uint32_t roll = 0; roll < 100; roll++) {
              rolls += "NREL"[static_cast<size_t>(spire_reward_tier_for_roll(odds, roll)) & 3];
            }
            out += std::format("odds k{} trede{} elite{} bonus{} = L<{} E<{} R<{} rolls={}\n", k, trede, elite ? 1 : 0,
                rare_bonus, odds.legendary, odds.epic, odds.rare, rolls);
          }
        }
      }
    }
    for (size_t t = 0; t < SEAM_TIERS; t++) {
      auto tier = static_cast<SpireRewardTier>(t);
      out += std::format("tier {} name=\"{}\" label=\"{}\" color=\"{}\" food={} disk_mind={}\n", t,
          spire_reward_tier_name(tier), spire_reward_tier_label(tier), spire_reward_tier_color(tier), table.mag_food[t],
          table.tier_disk_mind[t]);
    }
    for (int kind = 0; kind <= 5; kind++) {
      out += std::format("kind {} name=\"{}\"\n", kind, spire_reward_kind_name(static_cast<SpireRewardKind>(kind)));
    }
    for (uint32_t curse = 0; curse <= 3; curse++) {
      out += std::format("curse {} disk_levels={}\n", curse, spire_reward_curse_disk_levels(curse));
    }
  }

  // The pure parts of the pick ('Z' sub 6) and of its result ('Z' sub 7), as far as they can be called without a
  // client: what a weapon asks of rule B's mag, what a disk asks, how the two share the mag's 200 levels, the answer
  // word, a unit's bonus and slot, and the gate of the weapon's equip
  {
    auto& out = sections.emplace_back(SeamSection{"pick", ""}).text;
    constexpr std::array<SpireAtaModel, 4> models = {SpireAtaModel::TABLE, SpireAtaModel::SCREEN_WEAPON,
        SpireAtaModel::SCREEN_BARE, SpireAtaModel::SAFE};
    auto need_text = [](const SpireMagNeed& need) -> std::string {
      return std::format("{},{},{}", need.pow, need.dex, need.mind);
    };
    // a weapon: every cell of the grid, held against the grid's own run copy of that weapon level
    for (uint8_t c = 0; c < SEAM_CLASSES; c++) {
      const auto& cls = table.classes.at(c);
      for (const auto& ln : cls.line_order) {
        for (const auto& [w, level] : cls.lines.at(ln).w) {
          for (size_t t = 0; t < SEAM_TIERS; t++) {
            const auto& info = table.items.at(level.tiers[t].code);
            std::string line = std::format("weapon c{:02} {} W{} t{} code={:06X} run={},{},{}", c, ln, w, t,
                level.tiers[t].code, level.run_atp, level.run_ata, level.run_mst);
            for (auto model : models) {
              line += std::format(" {}={}/{}", spire_ata_model_name(model),
                  need_text(spire_reward_mag_need(info, level.run_atp, level.run_ata, level.run_mst, model)),
                  spire_reward_meets(info, level.run_atp, level.run_ata, level.run_mst, nullptr, model) ? "meets" : "short");
            }
            auto need = spire_reward_mag_need(info, level.run_atp, level.run_ata, level.run_mst, table.ata_model);
            for (uint16_t disk_mind : {0, 60, 200}) {
              line += std::format(" disk{}={}", disk_mind, need_text(spire_reward_need_with_disk(need, disk_mind)));
            }
            out += line + "\n";
          }
        }
      }
    }
    // a disk: the MST of every technique and level, the MIND it asks of the mag for three copies, and that MIND next to
    // a weapon's need (one with room in the 200 levels, one without)
    for (size_t tech = 0; tech < SPIRE_TECH_COUNT; tech++) {
      for (uint8_t level = 1; level <= SPIRE_TECH_LEVELS; level++) {
        uint32_t req = spire_reward_disk_mst(table.techs[tech], level);
        std::string line = std::format("disk tech{:02} \"{}\" Lv{} mst={}", tech, table.techs[tech].name, level, req);
        for (uint32_t mst : {40, 200, 600}) {
          uint16_t mind = std::min<uint16_t>(spire_reward_disk_mind(req, mst), 200);
          line += std::format(" copy{}: mind={} alone={} weapon={} full={}", mst, mind,
              need_text(spire_reward_need_with_disk(SpireMagNeed{}, mind)),
              need_text(spire_reward_need_with_disk(SpireMagNeed{30, 20, 0}, mind)),
              need_text(spire_reward_need_with_disk(SpireMagNeed{120, 90, 5}, mind)));
        }
        out += line + "\n";
      }
    }
    // a unit: what it adds to the stats a weapon's requirement is held against, and the slot it goes into
    for (const auto& [code, unit] : table.units) {
      SpireUnitBonus bonus;
      spire_reward_unit_bonus_add(bonus, unit.stat, unit.amount);
      out += std::format("unit {:06X} \"{}\" stars={} stat={} amount={} bonus atp={} mst={}\n", code, unit.name, unit.stars,
          unit.stat, unit.amount, bonus.atp, bonus.mst);
    }
    for (const auto& [archetype, tiers] : table.unit_tiers) {
      for (size_t t = 0; t < SEAM_TIERS; t++) {
        std::string line = std::format("unit_tier {} t{}", archetype, t);
        for (uint32_t code : tiers[t]) {
          line += std::format(" {:06X}", code);
        }
        out += line + "\n";
      }
    }
    for (uint8_t frame_slots = 0; frame_slots <= 5; frame_slots++) {
      std::string line = std::format("unit_slot frame{}", frame_slots);
      for (uint8_t occupied = 0; occupied < 16; occupied++) {
        auto slot = spire_reward_free_unit_slot(occupied, frame_slots);
        line += slot ? std::format(" {}", *slot) : std::string(" -");
      }
      out += line + "\n";
    }
    // the answer word (r41) and the lines a player reads
    for (uint32_t flags : {0u, SPIRE_REWARD_ITEM, SPIRE_REWARD_MAG_FOLLOWS, SPIRE_REWARD_ITEM | SPIRE_REWARD_MAG_FOLLOWS}) {
      out += std::format("answer flags{} = {:08X}\n", flags, SPIRE_REWARD_ANSWER_TAG | flags);
    }
    out += std::format("sub reward={} result={}\n", SPIRE_RUN_MAG_REWARD, SPIRE_RUN_MAG_REWARD_RESULT);
    out += std::format("text no_slot=\"{}\"\ntext no_frame=\"{}\"\n", SPIRE_REWARD_UNIT_NO_SLOT_TEXT,
        SPIRE_REWARD_UNIT_NO_FRAME_TEXT);
    // the gate of a reward weapon's equip after its RESULT
    for (bool mag_due : {false, true}) {
      for (bool has_ledger : {false, true}) {
        for (bool boost_due : {false, true}) {
          for (bool mag_equipped : {false, true}) {
            std::string line = std::format("equip_gate due{} ledger{} boost{} equipped{}", mag_due ? 1 : 0,
                has_ledger ? 1 : 0, boost_due ? 1 : 0, mag_equipped ? 1 : 0);
            for (uint32_t polls : {0u, SPIRE_REWARD_MAG_WAIT_POLLS - 1, SPIRE_REWARD_MAG_WAIT_POLLS}) {
              for (uint32_t landed : {0u, SPIRE_REWARD_MAG_EQUIP_POLLS - 1, SPIRE_REWARD_MAG_EQUIP_POLLS}) {
                line += std::format(" {}/{}={}", polls, landed,
                    static_cast<int>(spire_reward_equip_gate(mag_due, has_ledger, boost_due, mag_equipped, polls, landed)));
              }
            }
            out += line + "\n";
          }
        }
      }
    }
  }
  return sections;
}

// "<section> <lines> <sha256>" per section and for the whole dump
std::vector<std::string> seam_fingerprint_lines(const std::vector<SeamSection>& sections) {
  std::vector<std::string> ret;
  std::string all;
  for (const auto& section : sections) {
    ret.emplace_back(std::format("{} {} {}", section.name, seam_count_lines(section.text), phosg::SHA256(section.text).hex()));
    all += section.text;
  }
  ret.emplace_back(std::format("total {} {}", seam_count_lines(all), phosg::SHA256(all).hex()));
  return ret;
}

void seam_test_offers(const char* dump_path) {
  auto table = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
  expect(table.enabled);
  expect_eq(SEAM_CLASSES, table.classes.size());
  auto ipt = ItemParameterTable::from_json(
      phosg::JSON::parse(phosg::load_file("system/tables/item-parameter-table-gc-v3.json")));
  std::array<std::array<uint8_t, SPIRE_TECH_COUNT>, SEAM_CLASSES> class_max;
  for (uint8_t c = 0; c < SEAM_CLASSES; c++) {
    for (size_t t = 0; t < SPIRE_TECH_COUNT; t++) {
      class_max[c][t] = ipt->get_max_tech_level(c, t);
    }
  }

  auto sections = seam_offer_sections(table, class_max);
  auto now = seam_fingerprint_lines(sections);
  if (dump_path) {
    std::string all, fingerprint;
    for (const auto& section : sections) {
      all += section.text;
    }
    for (const auto& line : now) {
      fingerprint += line + "\n";
    }
    phosg::save_file(dump_path, all);
    phosg::save_file(std::string(dump_path) + ".fingerprint", fingerprint);
    phosg::log_info_f("Spire seam: wrote the offer dump to {} and its fingerprint to {}.fingerprint", dump_path, dump_path);
  }

  if (!std::filesystem::exists(SEAM_FINGERPRINT_FILE)) {
    phosg::fwrite_fmt(stderr, "Spire seam: {} is missing; the offer snapshot is NOT compared\n", SEAM_FINGERPRINT_FILE);
    return;
  }
  std::vector<std::string> pinned;
  for (const auto& line : phosg::split(phosg::load_file(SEAM_FINGERPRINT_FILE), '\n')) {
    if (!line.empty() && (line[0] != '#')) {
      pinned.emplace_back(line);
    }
  }
  for (size_t z = 0; z < std::max(now.size(), pinned.size()); z++) {
    std::string have = (z < now.size()) ? now[z] : "(no such section)";
    std::string want = (z < pinned.size()) ? pinned[z] : "(no such section)";
    if (have != want) {
      throw std::runtime_error(std::format(
          "Spire seam: the reward offers differ from before the seam ({}).\n  pinned: {}\n  now:    {}\n  Write the "
          "dump on both commits with {}=<path> and diff the two files.",
          SEAM_FINGERPRINT_FILE, want, have, SEAM_ENV));
    }
  }
}

// The savestates of the fixtures, as structs. The run mag's ledger keys ("RunMag") in every state they have:
//   no-run-mag        no "RunMag" at all (a run without a ledger); none of the optional top-level keys either
//   run-mag-fresh     a ledger that issued nothing: IssuedStep -1, no boss rooms, Lean 0, and none of LastMirrored,
//                     Need, DiskMind, BoostDue, FeedLevels
//   run-mag-full      every key: a step issued, boss rooms, Lean 1 (Fang/Mind), LastMirrored, Need with all three,
//                     DiskMind, BoostDue, FeedLevels; and every optional top-level key
//   run-mag-shell     Lean 2 (Shell), Need with MIND alone at its cap, FeedLevels at its cap, no BoostDue, no DiskMind
//   run-mag-need-pow  Need with POW alone at its cap, DiskMind at its cap, BoostDue without FeedLevels, no LastMirrored
//   run-mag-need-dex  Need with DEX alone, the lowest DiskMind and FeedLevels that are written, no BoostDue
struct SeamSavestate {
  std::string name;
  uint32_t serial_number = 0;
  SpireSavestate ss;
};

ItemData seam_item(const std::string& hex) {
  ItemData ret;
  spire_guard_hex_decode(hex, &ret, sizeof(ret));
  return ret;
}

std::vector<SeamSavestate> seam_savestates() {
  const std::string saber = "0001000000000000000000000001000100000000";
  const std::string frame = "0101000000040000000000000001000200000000";
  const std::string mag_hex = "02050D00E8030A00C8000000";
  const std::string mag = mag_hex + "0001000300000000";
  const std::string monomate = "0300000000030000000000000001000400000000";
  auto base = [](const char* name, uint32_t serial_number, uint32_t run_id) {
    SeamSavestate ret;
    ret.name = name;
    ret.serial_number = serial_number;
    ret.ss.exists = true;
    ret.ss.run_id = run_id;
    ret.ss.episode = 1;
    ret.ss.quest_number = 2921;
    ret.ss.start_room = 1;
    ret.ss.values.assign(SPIRE_SAVESTATE_NUM_VALUES, 0);
    ret.ss.values[SPIRE_SS_K] = 3;
    ret.ss.values[SPIRE_SS_LIVES] = 2;
    ret.ss.time_usecs = 1794405147991074ULL;
    return ret;
  };
  std::vector<SeamSavestate> ret;
  {
    auto s = base("savestate-no-run-mag.json", 5200, 0x1001);
    s.ss.items = {seam_item(saber), seam_item(monomate)};
    ret.emplace_back(std::move(s));
  }
  {
    auto s = base("savestate-run-mag-fresh.json", 5200, 0x1002);
    s.ss.level = 0;
    s.ss.equipped = std::vector<uint32_t>();
    SpireRunMagLedger led;
    led.run_id = 0x1002;
    s.ss.run_mag = led;
    ret.emplace_back(std::move(s));
  }
  {
    auto s = base("savestate-run-mag-full.json", 42, 0x853F1A91);
    s.ss.episode = 2;
    s.ss.quest_number = 2956;
    s.ss.start_room = 16;
    s.ss.practice = true;
    for (size_t z = 0; z < SPIRE_SAVESTATE_NUM_VALUES; z++) {
      s.ss.values[z] = 0x01010101u * static_cast<uint32_t>(z + 1);
    }
    s.ss.values[SPIRE_SS_K] = 28;
    s.ss.values[SPIRE_SS_VARIANT] = 2;
    s.ss.items = {seam_item(saber), seam_item(monomate), seam_item(frame), seam_item(mag)};
    s.ss.glass_index = 1;
    s.ss.level = 17;
    s.ss.start_bonus = 6;
    s.ss.equipped = std::vector<uint32_t>{0, 2, 3};
    s.ss.rest_rooms = 5;
    SpireRunMagLedger led;
    led.run_id = 0x853F1A91;
    led.issued_step = 3;
    led.start_step = 1;
    led.boss_rooms = {19, 23, 27};
    led.lean = 1;
    led.last_mirrored = spire_run_mag_bytes_from_hex(mag_hex);
    led.need = SpireMagNeed{40, 25, 10};
    led.disk_mind = 60;
    led.boost_due = true;
    led.feed_levels = 30;
    s.ss.run_mag = led;
    ret.emplace_back(std::move(s));
  }
  {
    auto s = base("savestate-run-mag-shell.json", 5200, 0x1004);
    s.ss.items = {seam_item(mag)};
    s.ss.equipped = std::vector<uint32_t>{0};
    SpireRunMagLedger led;
    led.run_id = 0x1004;
    led.issued_step = 0;
    led.boss_rooms = {4};
    led.lean = 2;
    led.last_mirrored = spire_run_mag_bytes_from_hex(mag_hex);
    led.need = SpireMagNeed{0, 0, 200};
    led.feed_levels = 200;
    s.ss.run_mag = led;
    ret.emplace_back(std::move(s));
  }
  {
    auto s = base("savestate-run-mag-need-pow.json", 5200, 0x1005);
    s.ss.start_bonus = 0;
    SpireRunMagLedger led;
    led.run_id = 0x1005;
    led.issued_step = 1;
    led.start_step = 1;
    led.boss_rooms = {4, 8};
    led.need = SpireMagNeed{200, 0, 0};
    led.disk_mind = 200;
    led.boost_due = true;
    s.ss.run_mag = led;
    ret.emplace_back(std::move(s));
  }
  {
    auto s = base("savestate-run-mag-need-dex.json", 5200, 0x1006);
    s.ss.rest_rooms = 15;
    SpireRunMagLedger led;
    led.run_id = 0x1006;
    led.issued_step = 2;
    led.need = SpireMagNeed{0, 1, 0};
    led.disk_mind = 1;
    led.feed_levels = 1;
    s.ss.run_mag = led;
    ret.emplace_back(std::move(s));
  }
  return ret;
}

// A savestate file loads and is written back as the same text. The serial number is not in SpireSavestate (the writer
// takes it apart), so it is read from the file.
void seam_expect_round_trip(const std::string& file, const std::string& text) {
  auto ss = spire_savestate_from_json(text);
  uint32_t serial_number = phosg::JSON::parse(text).get_int("SerialNumber");
  std::string again = spire_savestate_to_json(ss, serial_number);
  if (again != text) {
    throw std::runtime_error(std::format(
        "Spire seam: the savestate {} is not written back as it was read.\n--- the file:\n{}\n--- written back:\n{}", file,
        text, again));
  }
}

void seam_test_savestates(const char* dump_path) {
  auto savestates = seam_savestates();
  if (dump_path) { // all of them first: a fixture that fails below does not keep the later ones from being written
    for (const auto& s : savestates) {
      phosg::save_file(std::format("{}.{}", dump_path, s.name), spire_savestate_to_json(s.ss, s.serial_number));
    }
  }
  for (const auto& s : savestates) {
    std::string now = spire_savestate_to_json(s.ss, s.serial_number);
    std::string file = SEAM_DIR + s.name;
    std::string text = phosg::load_file(file); // a fixture stays: a missing one fails
    seam_expect_round_trip(file, text);
    // and the struct of this state is still written as the code from before the seam wrote it
    if (now != text) {
      throw std::runtime_error(std::format(
          "Spire seam: the savestate {} is written differently than before the seam.\n--- the file:\n{}\n--- written "
          "now:\n{}",
          file, text, now));
    }
    // what the file says of the run mag is what the reader gives (the writer leaves out a key at its default, so the
    // text alone cannot tell a lost key from a default one)
    auto read = spire_savestate_from_json(text);
    expect_eq(s.ss.run_mag.has_value(), read.run_mag.has_value());
    if (read.run_mag) {
      const auto& want = *s.ss.run_mag;
      const auto& have = *read.run_mag;
      expect_eq(want.run_id, have.run_id);
      expect_eq(want.issued_step, have.issued_step);
      expect_eq(want.start_step, have.start_step);
      expect(want.boss_rooms == have.boss_rooms);
      expect_eq(want.lean, have.lean);
      expect(want.last_mirrored == have.last_mirrored);
      expect(want.need == have.need);
      expect_eq(want.disk_mind, have.disk_mind);
      expect_eq(want.boost_due, have.boost_due);
      expect_eq(want.feed_levels, have.feed_levels);
    }
  }
  // a savestate of a real run from before (newserv a5fa1dc), read by the same reader: no run mag, Episode 2
  const std::string old_file = "tests/spire-savestate-before-all-exams.json";
  seam_expect_round_trip(old_file, phosg::load_file(old_file));
}

} // namespace

void spire_seam_run_tests() {
  const char* dump_path = getenv(SEAM_ENV);
  if (dump_path && !dump_path[0]) {
    dump_path = nullptr;
  }
  phosg::log_info_f("-- Spire seam: the reward offers are as before the seam");
  seam_test_offers(dump_path);
  phosg::log_info_f("-- Spire seam: a savestate with the run mag loads and is written back the same");
  seam_test_savestates(dump_path);
}

[[maybe_unused]] static const bool spire_seam_registered = spire_mech_register_tests("seam", spire_seam_run_tests);
