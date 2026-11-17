#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <phosg/UnitTest.hh>

#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ItemParameterTable.hh"
#include "SpireMechanics.hh"
#include "SpireRangerTypes.hh"
#include "SpireRewards.hh"
#include "SpireTestSupport.hh"
#include "StaticGameData.hh"

// The tests of the four rangers (SpireRangerTypes.hh): the table against Sega's own tables and the reward grid, the
// pure weights, the traps of the two androids, and the weights through the seam's H3 and the reward kinds that choose
// (another line, a technique disk, a unit), with the switch "ranger-types" on, off and missing. The owner of this file
// is the ranger-types mechanic (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 5).

namespace {

constexpr uint8_t RAMAR = 3, RACAST = 4, RACASEAL = 5, RAMARL = 11;
constexpr std::array<uint8_t, 4> RANGERS = {RAMAR, RACAST, RACASEAL, RAMARL};

using Rnd = std::function<uint32_t(uint32_t)>;

// Every draw of one choice of an offer. The first rnd call of the offer is the choice (the kinds ask it first); its n
// is what the kind drew from (the number of candidates, or the sum of their weights). Answers that n and, per key of
// the offer, how many of the values 0..n-1 gave it. Later rnd calls of the offer (a weapon's special) answer 0.
template <typename KeyT>
std::pair<uint32_t, std::map<KeyT, uint32_t>> draws_of(const std::function<SpireRewardOffer(const Rnd&)>& make,
    const std::function<KeyT(const SpireRewardOffer&)>& key_of) {
  uint32_t n_first = 0;
  bool first = true;
  Rnd probe = [&](uint32_t n) -> uint32_t {
    if (first) {
      n_first = n;
      first = false;
    }
    return 0;
  };
  make(probe);
  std::map<KeyT, uint32_t> counts;
  for (uint32_t r = 0; r < n_first; r++) {
    first = true;
    Rnd rnd = [&](uint32_t n) -> uint32_t {
      if (first) {
        first = false;
        expect_eq(n_first, n);
        return r;
      }
      return 0;
    };
    counts[key_of(make(rnd))]++;
  }
  return {n_first, counts};
}

uint32_t unit_code(const SpireRewardOffer& offer) {
  return (static_cast<uint32_t>(offer.item[0]) << 16) | (static_cast<uint32_t>(offer.item[1]) << 8) | offer.item[2];
}

void spire_tests_ranger() {
  auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
  const std::string text = phosg::load_file(SPIRE_RANGER_TYPES_FILE);
  auto table = spire_ranger_types_parse(text);
  auto ipt = ItemParameterTable::from_json(phosg::JSON::parse(phosg::load_file("system/tables/item-parameter-table-gc-v3.json")));
  auto names = phosg::JSON::parse(phosg::load_file("system/tables/names-v3.json"));

  // the table against Sega's own tables and the grid: the four rangers and no other class; every line it names is a
  // line of the class in the grid; a technique it weighs is one the class can learn (the item parameter table's
  // MaxTechLevels: an android none, RAmar no Jellen and no Zalure); every unit is a unit by the name the item names
  // give its code, and one the item parameter table knows; traps for the two androids only
  expect(table.enabled);
  expect_eq(4u, table.types.size());
  expect(!table.melee_lines.empty());
  size_t units_checked = 0;
  for (uint8_t c = 0; c < 12; c++) {
    expect_eq(char_class_is_ranger(c), table.types.count(c) == 1);
  }
  for (const auto& [char_class, type] : table.types) {
    const auto& cls = rewards.classes.at(char_class);
    expect_eq(std::string(name_for_char_class(char_class)), type.name);
    expect_eq(type.name, cls.name);
    expect_eq(std::string("RA"), cls.archetype);
    for (const auto& [line, weight] : type.line_weights) {
      expect(cls.lines.count(line));
      expect(!table.melee_lines.count(line)); // a melee line has no weight of its own: 0, or 1 when one is held
      expect(weight >= 1);
    }
    for (const auto& line : table.melee_lines) {
      expect(cls.lines.count(line));
    }
    bool weighs_a_tech = false;
    for (size_t t = 0; t < SPIRE_TECH_COUNT; t++) {
      if (type.tech_weights[t] != 1) {
        expect(ipt->get_max_tech_level(char_class, t) != 0xFF);
        weighs_a_tech = true;
      }
    }
    expect_eq(!char_class_is_android(char_class), weighs_a_tech);
    expect_eq(char_class_is_android(char_class), type.trap_levels_per.has_value());
    // an android is never offered a unit for techniques, nor one against poison or paralysis (design par. 1 and 4)
    expect_eq(char_class_is_android(char_class), !type.never_units.empty());
    for (const auto& [code, name] : type.never_units) {
      expect_eq(names.get_string(std::format("{:06X}", code)), name);
      ipt->get_unit(code & 0xFF);
      expect(!type.own_units.count(code));
      units_checked++;
    }
    for (const char* never : {"God/Mind", "God/TP", "TP/Revival", "God/Technique", "Cure/Poison", "Cure/Paralysis"}) {
      bool named = false;
      for (const auto& [code, name] : type.never_units) {
        named |= (name == never);
      }
      expect_eq(char_class_is_android(char_class), named);
    }
    // the attack speed units are no type's own: they exist only as a power (design/mechanics/decisions.md B-ONTWERP-6)
    for (uint32_t battle : {0x01033Fu, 0x010340u, 0x010341u}) {
      expect(!type.own_units.count(battle));
    }
    expect_eq(4u, type.wildcard_one_in);
    for (size_t z = 0; z < 4; z++) {
      expect(!type.units[z].empty());
      for (const auto& [code, name] : type.units[z]) {
        expect_eq(names.get_string(std::format("{:06X}", code)), name);
        ipt->get_unit(code & 0xFF); // throws on a unit the table has not
        expect(type.own_units.count(code));
        units_checked++;
      }
    }
  }
  phosg::log_info_f("   ranger types: 4 types, {} units checked against the item names", units_checked);

  // what the design gives each type (ranger-types.md par. 1 and par. 6): a human ranger's support techniques weigh more
  // than an attack technique, RAmarl's Jellen and Zalure too, and every type leans to a gun line of its own
  const uint8_t foie = technique_for_name("Foie");
  for (uint8_t c : {RAMAR, RAMARL}) {
    for (const char* tech : {"Shifta", "Deband", "Resta"}) {
      expect(spire_ranger_tech_weight(table, c, technique_for_name(tech)) > spire_ranger_tech_weight(table, c, foie));
    }
  }
  for (const char* tech : {"Jellen", "Zalure"}) {
    expect(spire_ranger_tech_weight(table, RAMARL, technique_for_name(tech)) > 1);
    expect_eq(1u, spire_ranger_tech_weight(table, RAMAR, technique_for_name(tech)));
  }
  expect_eq(1u, spire_ranger_tech_weight(table, RAMAR, foie));
  expect_eq(1u, spire_ranger_tech_weight(table, RAMAR, SPIRE_TECH_COUNT)); // no technique: 1
  expect(spire_ranger_line_weight(table, RAMAR, "Rifle", "Handgun") > 1);
  expect(spire_ranger_line_weight(table, RAMARL, "Rifle", "Handgun") > 1);
  expect(spire_ranger_line_weight(table, RACAST, "Shot", "Handgun") > 1);
  expect(spire_ranger_line_weight(table, RACASEAL, "Mechgun", "Handgun") > 1);
  // a saber or a partisan only for a ranger that holds a melee weapon itself; every other line always
  for (uint8_t c : RANGERS) {
    for (const auto& line : rewards.classes.at(c).line_order) {
      bool melee = table.melee_lines.count(line);
      for (const char* held : {"", "Handgun", "Rifle", "Mechgun", "Shot", "Slicer"}) {
        expect_eq(melee, spire_ranger_line_weight(table, c, line, held) == 0);
      }
      for (const auto& held : table.melee_lines) {
        expect(spire_ranger_line_weight(table, c, line, held) >= 1);
      }
    }
  }
  // a class that is no ranger, and a class number that is none: 1 for everything
  for (uint8_t c : std::initializer_list<uint8_t>{0, 1, 2, 6, 7, 8, 9, 10, 12, 0xFF}) {
    expect_eq(1u, spire_ranger_line_weight(table, c, "Saber", "Handgun"));
    expect_eq(1u, spire_ranger_line_weight(table, c, "Rifle", ""));
    expect_eq(1u, spire_ranger_tech_weight(table, c, technique_for_name("Shifta")));
    expect_eq(1u, spire_ranger_unit_weight(table, c, 0x010308, {0x010308, 0x010300}));
    expect(!spire_ranger_trap_counts(table, c, 40));
  }
  // the hook's candidate without its offer (a test of the hook alone): 1
  expect_eq(1u, spire_ranger_type_weight(table, SpireMechTypeWeight{nullptr, SpireMechCandidate::UNIT, nullptr, 0x010308, 0}));

  // units: the type's own together weigh three times the others together, however many of each the draw holds; a draw
  // with only own units, or with none, is the plain draw
  {
    const auto& own = table.types.at(RAMAR).own_units;
    uint32_t a = *own.begin(), b = *own.rbegin();
    expect(a != b);
    const uint32_t x = 0x010310, y = 0x010318, z = 0x010330; // Digger/HP, Warrior/Body, All/Resist: not RAmar's
    expect(!own.count(x) && !own.count(y) && !own.count(z));
    auto share = [&](const std::vector<uint32_t>& candidates) {
      uint32_t own_total = 0, other_total = 0;
      for (uint32_t code : candidates) {
        uint32_t w = spire_ranger_unit_weight(table, RAMAR, code, candidates);
        expect(w >= 1); // a type weighs, it never shuts out
        (own.count(code) ? own_total : other_total) += w;
      }
      return std::make_pair(own_total, other_total);
    };
    for (const auto& candidates : std::vector<std::vector<uint32_t>>{{a, x}, {a, b, x}, {a, x, y, z}, {a, b, x, y, z}}) {
      auto [own_total, other_total] = share(candidates);
      expect_eq(own_total, 3 * other_total); // three of four
    }
    auto all_own = share({a, b});
    expect((all_own.first == 2) && (all_own.second == 0));
    auto none_own = share({x, y, z});
    expect((none_own.first == 0) && (none_own.second == 3));
    expect_eq(1u, spire_ranger_unit_weight(table, RAMAR, a, {}));
    // an android's NeverUnits weigh 0 and count on neither side: three of four of the rest are still its own
    const auto& cast = table.types.at(RACAST);
    const uint32_t power = 0x010300, hp = 0x010310, arm = 0x010308, mind = 0x010304, tp = 0x010314, cure = 0x010342;
    expect(cast.own_units.count(power) && cast.own_units.count(hp) && !cast.own_units.count(arm));
    const std::vector<uint32_t> with_never = {power, mind, hp, tp, arm, cure};
    expect_eq(0u, spire_ranger_unit_weight(table, RACAST, mind, with_never));
    expect_eq(0u, spire_ranger_unit_weight(table, RACAST, tp, with_never));
    expect_eq(0u, spire_ranger_unit_weight(table, RACAST, cure, with_never));
    expect_eq(3u, spire_ranger_unit_weight(table, RACAST, power, with_never)); // 3 * one other
    expect_eq(3u, spire_ranger_unit_weight(table, RACAST, hp, with_never));
    expect_eq(2u, spire_ranger_unit_weight(table, RACAST, arm, with_never)); // two own
    expect_eq(0u, spire_ranger_unit_weight(table, RACAST, mind, {mind, tp})); // nothing else: the seam draws plainly
    expect_eq(1u, spire_ranger_unit_weight(table, RACAST, power, {power, mind})); // the rest is all own
    // a human ranger has no NeverUnits: a Mind unit is another ranger's, not shut out
    expect(spire_ranger_unit_weight(table, RAMAR, mind, {a, mind}) >= 1);
    expect(spire_ranger_unit_weight(table, RAMARL, mind, {arm, mind}) > spire_ranger_unit_weight(table, RAMARL, arm, {arm, mind}));
  }

  // traps of the two androids: Sega's rule 2 + level / x per kind, at most 20 (design par. 2); the humans have none
  using T = SpireRangerTrapCounts;
  expect(spire_ranger_trap_counts(table, RACAST, 1) == std::optional<T>(T{2, 2, 2}));
  expect(spire_ranger_trap_counts(table, RACAST, 20) == std::optional<T>(T{4, 4, 3}));
  expect(spire_ranger_trap_counts(table, RACAST, 40) == std::optional<T>(T{7, 6, 5}));
  expect(spire_ranger_trap_counts(table, RACAST, 80) == std::optional<T>(T{13, 10, 9}));
  expect(spire_ranger_trap_counts(table, RACASEAL, 1) == std::optional<T>(T{2, 2, 2}));
  expect(spire_ranger_trap_counts(table, RACASEAL, 20) == std::optional<T>(T{4, 3, 4}));
  expect(spire_ranger_trap_counts(table, RACASEAL, 40) == std::optional<T>(T{7, 5, 6}));
  expect(spire_ranger_trap_counts(table, RACASEAL, 80) == std::optional<T>(T{13, 9, 10}));
  expect(spire_ranger_trap_counts(table, RACAST, 200) == std::optional<T>(T{20, 20, 20}));
  expect(!spire_ranger_trap_counts(table, RAMAR, 40));
  expect(!spire_ranger_trap_counts(table, RAMARL, 40));

  // the parse refuses what it cannot stand for, on the table's own text with one thing changed
  const std::string compact = spire_test_compact_json(text);
  auto edited = [&](const std::string& from, const std::string& to) {
    size_t at = compact.find(from);
    expect(at != std::string::npos);
    std::string ret = compact;
    ret.replace(at, from.size(), to);
    return ret;
  };
  auto refused = [&](const std::string& from, const std::string& to) {
    const std::string bad = edited(from, to);
    expect_raises(std::runtime_error, [&]() { spire_ranger_types_parse(bad); });
  };
  spire_ranger_types_parse(compact); // the compact form itself reads
  refused("\"RAmar\":{", "\"HUmar\":{"); // a type that is no ranger
  refused("\"RAmar\":{", "\"RAmarr\":{"); // a class newserv does not know
  refused("\"Shifta\":10", "\"Shifta\":0"); // a weight shuts nothing out
  refused("\"Shifta\":10", std::format("\"Shifta\":{}", SPIRE_RANGER_WEIGHT_MAX + 1));
  refused("\"Shifta\":10", "\"shifta\":10"); // a technique by newserv's own name only
  refused("\"Shifta\":10", "\"13\":10");
  refused("\"010308\"", "\"010108\""); // a frame is no unit
  refused("\"010308\"", "\"01030B\""); // God/Arm is RAmar's already: named twice
  refused("\"WildcardOneIn\":4", "\"WildcardOneIn\":1");
  refused("\"Cure/Poison\":\"010342\"", "\"Cure/Poison\":\"010142\""); // NeverUnits: no unit
  refused("\"Cure/Poison\":\"010342\"", "\"Cure/Poison\":\"010310\""); // Digger/HP is the android's own
  refused("\"Cure/Poison\":\"010342\"", "\"Cure/Poison\":\"010343\""); // Cure/Paralysis twice
  refused("\"RAmar\":{", "\"RAmar\":{\"Traps\":{\"Damage\":7,\"Freeze\":9,\"Confuse\":11},"); // traps on a human
  refused("\"Damage\":7", "\"Damage\":0");
  refused("\"Base\":2", "\"Base\":30"); // above Max
  expect_raises(std::exception, [&]() { spire_ranger_types_parse(edited("\"Enabled\":true", "\"Enabeld\":true")); });
  // Enabled false: the table reads, and every weight is 1
  {
    auto off = spire_ranger_types_parse(edited("\"Enabled\":true", "\"Enabled\":false"));
    expect(!off.enabled);
    expect_eq(1u, spire_ranger_line_weight(off, RAMAR, "Rifle", "Handgun"));
    expect_eq(1u, spire_ranger_line_weight(off, RAMAR, "Saber", "Handgun"));
    expect_eq(1u, spire_ranger_tech_weight(off, RAMAR, technique_for_name("Shifta")));
    const auto& own = table.types.at(RAMAR).own_units;
    expect_eq(1u, spire_ranger_unit_weight(off, RAMAR, *own.begin(), {*own.begin(), 0x010310}));
    expect(!spire_ranger_trap_counts(off, RACAST, 40));
  }

  // --- through the seam's H3 and the kinds that choose, with the registration of SpireRangerTypes.cc itself ---
  auto hook_names = spire_mech_hook_names(SpireMechHookId::TYPE_WEIGHT);
  expect(std::count(hook_names.begin(), hook_names.end(), std::string(SPIRE_RANGER_TYPES_SWITCH)) == 1);

  auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-ranger-{}", getpid());
  std::filesystem::create_directories(dir);
  auto switches = [&](const char* name, const std::string& json_text) {
    std::string file = (dir / name).string();
    phosg::save_file(file, json_text);
    spire_mech_set_switches_file(file);
  };
  const std::string switch_on = std::format("{{\"{}\": true}}", SPIRE_RANGER_TYPES_SWITCH);
  const std::string switch_off = std::format("{{\"{}\": false}}", SPIRE_RANGER_TYPES_SWITCH);

  const uint8_t rifle[12] = {0x00, 0x07, 0x00};
  const uint8_t saber[12] = {0x00, 0x01, 0x00};
  const uint8_t partisan[12] = {0x00, 0x04, 0x00};
  // The run copy for a disk offer, as the force's own test hands it: the copy's MST reaches every level
  auto disk_state = [&](uint8_t char_class) {
    SpireRewardDiskState st;
    st.mst = 5000;
    st.mind_room = 0;
    for (size_t x = 0; x < SPIRE_TECH_COUNT; x++) {
      st.class_max[x] = ipt->get_max_tech_level(char_class, x);
    }
    return st;
  };
  auto line_draws = [&](uint8_t c, const uint8_t* held) {
    return draws_of<std::string>(
        [&](const Rnd& rnd) { return spire_reward_offer(rewards, SpireRewardKind::NEW_LINE, c, held, 5, SpireRewardTier::RARE, rnd); },
        [](const SpireRewardOffer& o) { return o.line; });
  };
  auto unit_draws = [&](uint8_t c, size_t tier) {
    return draws_of<uint32_t>(
        [&](const Rnd& rnd) { return spire_reward_offer(rewards, SpireRewardKind::UNIT, c, nullptr, 3, static_cast<SpireRewardTier>(tier), rnd); },
        unit_code);
  };
  auto disk_draws = [&](uint8_t c, uint32_t room, size_t tier) {
    auto st = disk_state(c);
    return draws_of<uint32_t>(
        [&](const Rnd& rnd) { return spire_reward_offer(rewards, SpireRewardKind::DISK, c, nullptr, room, static_cast<SpireRewardTier>(tier), rnd, &st); },
        [](const SpireRewardOffer& o) -> uint32_t { return o.tech; });
  };
  // The lines "New line" chooses from: every line of the class but the one the offer follows
  auto other_lines = [&](uint8_t c, const uint8_t* held) {
    const auto& cls = rewards.classes.at(c);
    std::string held_line = spire_reward_line_of(rewards, cls, held);
    std::string from = held_line.empty() ? cls.line_order.front() : held_line;
    std::vector<std::string> ret;
    for (const auto& line : cls.line_order) {
      if (line != from) {
        ret.emplace_back(line);
      }
    }
    return std::make_pair(held_line, ret);
  };
  // The techniques a disk chooses from for a copy that reaches every level: what Sega drops in the room and tier and
  // the class can learn
  auto disk_techs = [&](uint8_t c, uint32_t room, size_t tier) {
    std::vector<uint32_t> ret;
    const auto& levels = rewards.disk_levels.at(room);
    for (size_t x = 0; x < SPIRE_TECH_COUNT; x++) {
      if (levels[x][tier] && (ipt->get_max_tech_level(c, x) != 0xFF)) {
        ret.emplace_back(x);
      }
    }
    return ret;
  };

  // The plain draw, as before this table: one of n candidates, each as likely
  auto expect_plain = [&]() {
    for (uint8_t c : RANGERS) {
      for (const uint8_t* held : {static_cast<const uint8_t*>(nullptr), static_cast<const uint8_t*>(rifle), static_cast<const uint8_t*>(saber)}) {
        auto [held_line, others] = other_lines(c, held);
        auto [n, got] = line_draws(c, held);
        expect_eq(others.size(), n);
        expect_eq(others.size(), got.size());
      }
      for (size_t tier = 0; tier < 4; tier++) {
        const auto& codes = rewards.unit_tiers.at("RA")[tier];
        auto [n, got] = unit_draws(c, tier);
        expect_eq(codes.size(), n);
        expect_eq(codes.size(), got.size());
      }
    }
    for (uint8_t c : {RAMAR, RAMARL}) {
      for (size_t tier = 1; tier < 4; tier++) {
        auto techs = disk_techs(c, 5, tier);
        auto [n, got] = disk_draws(c, 5, tier);
        expect_eq(techs.size(), n);
        expect_eq(techs.size(), got.size());
      }
    }
  };
  // What every class that is no ranger is offered, with every rnd call it made
  auto others_text = [&]() {
    std::string out;
    for (uint8_t c : std::initializer_list<uint8_t>{0, 1, 2, 6, 7, 8, 9, 10}) {
      for (SpireRewardKind kind : {SpireRewardKind::NEW_LINE, SpireRewardKind::UNIT, SpireRewardKind::DISK}) {
        for (size_t tier = 0; tier < 4; tier++) {
          for (uint32_t r : {0u, 1u, 5u}) {
            auto st = disk_state(c);
            Rnd rnd = [&](uint32_t n) -> uint32_t {
              out += std::format("[{}]", n);
              return n ? (r % n) : 0;
            };
            try {
              out += spire_reward_offer(rewards, kind, c, nullptr, 5, static_cast<SpireRewardTier>(tier), rnd, &st).label + "\n";
            } catch (const std::runtime_error& e) {
              out += std::string("THROW ") + e.what() + "\n";
            }
          }
        }
      }
    }
    return out;
  };

  // a RAmar with a rifle in hand is offered no saber: the table is at work
  auto weighted = [&]() { return line_draws(RAMAR, rifle).second.count("Saber") == 0; };

  // in the silence of the static tests (SpireMechanics.hh) the registration is not called, whatever its switch says:
  // the tests of the other owners see the plain draw
  expect(spire_mech_tests_silence());
  switches("aan.json", switch_on);
  expect_plain();
  spire_mech_test_only({SPIRE_RANGER_TYPES_SWITCH}); // from here to the end of this test file the switch decides
  expect(weighted());
  // without a switches file, and with one that does not name the switch: on (integratie.md par. 7)
  spire_mech_set_switches_file("");
  expect(spire_mech_enabled(SPIRE_RANGER_TYPES_SWITCH));
  expect(weighted());
  switches("geen.json", "{}");
  expect(weighted());
  // the switch off: the plain draw, as before this table
  switches("uit.json", switch_off);
  expect_plain();
  const std::string others_off = others_text();
  expect(others_off.find("[") != std::string::npos); // the offers do draw

  // the switch on: every draw of a ranger follows the table, exactly
  switches("aan.json", switch_on);
  size_t leaning = 0; // draws in which the table moved something
  for (uint8_t c : RANGERS) {
    // "New line": a line as often as its weight; no saber and no partisan unless one is held
    for (const uint8_t* held : {static_cast<const uint8_t*>(nullptr), static_cast<const uint8_t*>(rifle), static_cast<const uint8_t*>(saber), static_cast<const uint8_t*>(partisan)}) {
      auto [held_line, others] = other_lines(c, held);
      std::map<std::string, uint32_t> want;
      uint32_t total = 0;
      bool leans = false;
      for (const auto& line : others) {
        uint32_t w = spire_ranger_line_weight(table, c, line, held_line);
        total += w;
        leans |= (w != 1);
        if (w) {
          want.emplace(line, w);
        }
      }
      auto [n, got] = line_draws(c, held);
      expect_eq(total, n);
      expect(got == want);
      bool melee_held = (held == saber) || (held == partisan);
      expect_eq(melee_held, got.count("Saber") || got.count("Partisan"));
      for (const char* gun : {"Handgun", "Rifle", "Mechgun", "Shot"}) {
        expect_eq(held_line != gun, got.count(gun) == 1); // every gun line but the one held stays on offer
      }
      leaning += leans ? 1 : 0;
    }
    // a unit: as often as its weight; where the tier holds own units and others, three of four are the type's own;
    // a unit of the type's NeverUnits never comes
    const auto& type = table.types.at(c);
    const auto& own = type.own_units;
    for (size_t tier = 0; tier < 4; tier++) {
      const auto& codes = rewards.unit_tiers.at("RA")[tier];
      std::map<uint32_t, uint32_t> want;
      uint32_t total = 0, own_total = 0, own_count = 0, other_count = 0;
      for (uint32_t code : codes) {
        uint32_t w = spire_ranger_unit_weight(table, c, code, codes);
        if (w) {
          want.emplace(code, w);
        }
        total += w;
        own_total += own.count(code) ? w : 0;
        own_count += own.count(code) ? 1 : 0;
        other_count += (!own.count(code) && !type.never_units.count(code)) ? 1 : 0;
      }
      auto [n, got] = unit_draws(c, tier);
      expect_eq(total, n);
      expect(got == want);
      if (own_count && other_count) {
        expect_eq(3 * (total - own_total), own_total);
        leaning++;
      } else {
        expect_eq(own_count + other_count, total); // all own or none own: the plain draw
      }
    }
  }
  // a disk for the two humans, in every room and tier: a technique as often as its weight, and wherever a support
  // technique and another one are both on offer, the type's support techniques are more than half and never all
  size_t disk_draws_checked = 0;
  for (uint8_t c : {RAMAR, RAMARL}) {
    for (const auto& [room, levels] : rewards.disk_levels) {
      for (size_t tier = 0; tier < 4; tier++) {
        auto techs = disk_techs(c, room, tier);
        if (techs.empty()) {
          continue;
        }
        std::map<uint32_t, uint32_t> want;
        uint32_t total = 0, support = 0, support_count = 0;
        for (uint32_t tech : techs) {
          uint32_t w = spire_ranger_tech_weight(table, c, tech);
          want.emplace(tech, w);
          total += w;
          support += (w > 1) ? w : 0;
          support_count += (w > 1) ? 1 : 0;
        }
        auto [n, got] = disk_draws(c, room, tier);
        expect_eq(total, n);
        expect(got == want);
        if (support_count && (support_count < techs.size())) {
          expect((2 * support > total) && (support < total));
          leaning++;
        }
        disk_draws_checked++;
      }
    }
  }
  expect(disk_draws_checked >= 2 * 15 * 3);
  // an android ranger learns no technique: no disk offer, with or without the table (the entry's own item)
  for (uint8_t c : {RACAST, RACASEAL}) {
    auto st = disk_state(c);
    expect_raises(std::runtime_error, [&]() {
      spire_reward_offer(rewards, SpireRewardKind::DISK, c, nullptr, 5, SpireRewardTier::RARE, [](uint32_t) -> uint32_t { return 0; }, &st);
    });
  }
  phosg::log_info_f("   ranger types: {} draws leaned to the type, {} disk draws checked", leaning, disk_draws_checked);
  expect(leaning >= 4 * 4 + 2 * 15 * 3); // every ranger's four held weapons, and the humans' disks past Normal
  // every class that is no ranger: the same offers and the same rnd calls as with the switch off
  expect_eq(others_off, others_text());

  // the table's own Enabled, a missing table and an unreadable one: the plain draw again, with the switch still on
  expect(weighted());
  const std::string table_file = (dir / "spire-types-ra.json").string();
  uint32_t writes = 0;
  auto write_table = [&](const std::string& json_text) {
    phosg::save_file(table_file, json_text);
    std::filesystem::last_write_time(table_file, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++writes));
  };
  write_table(edited("\"Enabled\":true", "\"Enabled\":false"));
  spire_ranger_types_set_file(table_file);
  expect(spire_ranger_types_cached() && !spire_ranger_types_cached()->enabled);
  expect_plain();
  write_table("{"); // unreadable, at the same path: read again because the file changed
  expect(!spire_ranger_types_cached());
  expect_plain();
  write_table(compact); // and good again
  expect(spire_ranger_types_cached() && spire_ranger_types_cached()->enabled);
  expect(weighted());
  std::filesystem::remove(table_file);
  expect(!spire_ranger_types_cached());
  expect_plain();
  spire_ranger_types_set_file("");
  expect(spire_ranger_types_cached() && spire_ranger_types_cached()->enabled); // the server's own table again
  expect(weighted());

  // and off again with the switch
  switches("uit.json", switch_off);
  expect_plain();

  spire_mech_set_switches_file("");
  std::filesystem::remove_all(dir);
}

[[maybe_unused]] const bool registered = spire_mech_register_tests("ranger", spire_tests_ranger);

} // namespace
