#include "SpireRangerTypes.hh"

#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>

#include <stdint.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <utility>

#include "SpireRewardKinds.hh"
#include "StaticGameData.hh"

// The four rangers: the table's parse, the pure weights and the registration on the seam's H3 (SpireRangerTypes.hh).
// Owner: the ranger-types mechanic; switch "ranger-types" (pso-roguelike
// new-features/roguelike/design/mechanics/integratie.md par. 3, 5 and 7).

namespace {

constexpr const char* TRAP_KEYS[3] = {"Damage", "Freeze", "Confuse"};

// A weight the table names: 1-SPIRE_RANGER_WEIGHT_MAX. A type weighs (design par. 3), so 0 is not a weight the table
// can give; the only zeroes are the melee line a ranger does not hold and a unit of NeverUnits.
uint32_t named_weight(const phosg::JSON& json, const std::string& where) {
  int64_t v = json.as_int();
  if ((v < 1) || (v > SPIRE_RANGER_WEIGHT_MAX)) {
    throw std::runtime_error(std::format("{}: weight {} outside 1-{}", where, v, SPIRE_RANGER_WEIGHT_MAX));
  }
  return static_cast<uint32_t>(v);
}

// The type of a class, or nullptr: Enabled false and a class the table has not are both "every weight is 1"
const SpireRangerType* type_of(const SpireRangerTypesTable& table, uint8_t char_class) {
  if (!table.enabled) {
    return nullptr;
  }
  auto it = table.types.find(char_class);
  return (it != table.types.end()) ? &it->second : nullptr;
}

struct TableCache {
  std::string path = SPIRE_RANGER_TYPES_FILE;
  bool looked = false; // the file of looked_time was read (well or not), or found missing
  bool missing = false;
  std::filesystem::file_time_type looked_time{};
  std::shared_ptr<const SpireRangerTypesTable> table; // nullptr: missing or unreadable
};

TableCache& table_cache() {
  static TableCache cache;
  return cache;
}

} // namespace

SpireRangerTypesTable spire_ranger_types_parse(const std::string& json_text) {
  auto json = phosg::JSON::parse(json_text);
  SpireRangerTypesTable ret;
  ret.enabled = json.get_bool("Enabled");
  for (const auto& line : json.get_list("MeleeLines")) {
    ret.melee_lines.emplace(line->as_string());
  }
  const auto& rule = json.at("TrapRule");
  ret.trap_base = spire_get_u(rule, "Base", 255);
  ret.trap_max = spire_get_u(rule, "Max", 255);
  if (ret.trap_base > ret.trap_max) {
    throw std::runtime_error(std::format("TrapRule: Base {} above Max {}", ret.trap_base, ret.trap_max));
  }
  for (const auto& [name, t_json] : json.get_dict("Types")) {
    uint8_t char_class = 0xFF;
    for (uint8_t c = 0; c < 12; c++) {
      if (name == name_for_char_class(c)) {
        char_class = c;
      }
    }
    if ((char_class == 0xFF) || !char_class_is_ranger(char_class)) {
      throw std::runtime_error(std::format("Types {}: not a ranger", name));
    }
    SpireRangerType type;
    type.name = name;
    for (const auto& [line, w_json] : t_json->get_dict("NewLineWeights")) {
      type.line_weights.emplace(line, named_weight(*w_json, std::format("Types {} NewLineWeights {}", name, line)));
    }
    for (const auto& [tech_name, w_json] : t_json->get_dict("TechWeights")) {
      uint8_t tech = technique_for_name(tech_name);
      if ((tech >= SPIRE_TECH_COUNT) || (name_for_technique(tech) != tech_name)) {
        throw std::runtime_error(std::format("Types {} TechWeights {}: not a technique", name, tech_name));
      }
      type.tech_weights[tech] = named_weight(*w_json, std::format("Types {} TechWeights {}", name, tech_name));
    }
    type.wildcard_one_in = spire_get_u(*t_json, "WildcardOneIn", SPIRE_RANGER_WEIGHT_MAX);
    if (type.wildcard_one_in < 2) {
      throw std::runtime_error(std::format("Types {} WildcardOneIn {} below 2", name, type.wildcard_one_in));
    }
    const auto& units = t_json->at("Units");
    for (size_t z = 0; z < 4; z++) {
      for (const auto& [unit_name, code_json] : units.get_dict(TIER_NAMES[z])) {
        uint32_t code = spire_code_of(code_json->as_string());
        if ((code >> 8) != 0x0103) {
          throw std::runtime_error(std::format("Types {} Units {} {}: {:06X} is not a unit (0103xx)", name, TIER_NAMES[z],
              unit_name, code));
        }
        if (!type.own_units.emplace(code).second) {
          throw std::runtime_error(std::format("Types {} Units: {:06X} is named twice", name, code));
        }
        type.units[z].emplace(code, unit_name);
      }
    }
    if (t_json->contains("NeverUnits")) {
      for (const auto& [unit_name, code_json] : t_json->get_dict("NeverUnits")) {
        uint32_t code = spire_code_of(code_json->as_string());
        if ((code >> 8) != 0x0103) {
          throw std::runtime_error(std::format("Types {} NeverUnits {}: {:06X} is not a unit (0103xx)", name, unit_name, code));
        }
        if (type.own_units.count(code) || !type.never_units.emplace(code, unit_name).second) {
          throw std::runtime_error(std::format("Types {} NeverUnits: {:06X} is named twice", name, code));
        }
      }
    }
    if (t_json->contains("Traps")) {
      if (!char_class_is_android(char_class)) {
        throw std::runtime_error(std::format("Types {} Traps: not an android", name));
      }
      const auto& traps = t_json->at("Traps");
      std::array<uint32_t, 3> levels_per{};
      for (size_t z = 0; z < 3; z++) {
        levels_per[z] = spire_get_u(traps, TRAP_KEYS[z], 255);
        if (!levels_per[z]) {
          throw std::runtime_error(std::format("Types {} Traps {}: 0 levels per trap", name, TRAP_KEYS[z]));
        }
      }
      type.trap_levels_per = levels_per;
    }
    ret.types.emplace(char_class, std::move(type));
  }
  return ret;
}

uint32_t spire_ranger_line_weight(const SpireRangerTypesTable& table, uint8_t char_class, const std::string& line,
    const std::string& held_line) {
  const auto* type = type_of(table, char_class);
  if (!type) {
    return 1;
  }
  // a saber or a partisan only for a ranger that holds a melee weapon itself (design par. 1 and par. 8 point 2)
  if (table.melee_lines.count(line) && !table.melee_lines.count(held_line)) {
    return 0;
  }
  auto it = type->line_weights.find(line);
  return (it != type->line_weights.end()) ? it->second : 1;
}

uint32_t spire_ranger_tech_weight(const SpireRangerTypesTable& table, uint8_t char_class, uint32_t tech) {
  const auto* type = type_of(table, char_class);
  return (type && (tech < SPIRE_TECH_COUNT)) ? type->tech_weights[tech] : 1;
}

uint32_t spire_ranger_unit_weight(const SpireRangerTypesTable& table, uint8_t char_class, uint32_t code,
    const std::vector<uint32_t>& candidates) {
  const auto* type = type_of(table, char_class);
  if (!type) {
    return 1;
  }
  if (type->never_units.count(code)) {
    return 0; // a unit the type cannot use
  }
  uint32_t own = 0, other = 0;
  for (uint32_t c : candidates) {
    if (!type->never_units.count(c)) {
      (type->own_units.count(c) ? own : other)++;
    }
  }
  if (!own || !other) {
    return 1; // nothing to lean to, or nothing to lean away from
  }
  // own units: own * (one_in - 1) * other together; the others: other * own together
  return type->own_units.count(code) ? ((type->wildcard_one_in - 1) * other) : own;
}

uint32_t spire_ranger_type_weight(const SpireRangerTypesTable& table, const SpireMechTypeWeight& candidate) {
  if (!candidate.in) {
    return 1;
  }
  const auto& in = *candidate.in;
  switch (candidate.what) {
    case SpireMechCandidate::LINE:
      if (!candidate.line || (in.kind != SpireRewardKind::NEW_LINE)) {
        return 1;
      }
      return spire_ranger_line_weight(table, in.char_class, *candidate.line,
          spire_reward_line_of(in.table, in.cls, in.held));
    case SpireMechCandidate::TECH:
      return spire_ranger_tech_weight(table, in.char_class, candidate.value);
    case SpireMechCandidate::UNIT: {
      // the candidates of the unit kind's draw (SpireRewardUnit.cc): the archetype's codes of the offer's tier
      auto a_it = in.table.unit_tiers.find(in.cls.archetype);
      if (a_it == in.table.unit_tiers.end()) {
        return 1;
      }
      return spire_ranger_unit_weight(table, in.char_class, candidate.value,
          a_it->second[static_cast<size_t>(in.tier) & 3]);
    }
  }
  return 1;
}

std::optional<SpireRangerTrapCounts> spire_ranger_trap_counts(const SpireRangerTypesTable& table, uint8_t char_class,
    uint32_t level) {
  const auto* type = type_of(table, char_class);
  if (!type || !type->trap_levels_per) {
    return std::nullopt;
  }
  auto count = [&](size_t z) -> uint8_t {
    return static_cast<uint8_t>(std::min<uint32_t>(table.trap_base + (level / (*type->trap_levels_per)[z]), table.trap_max));
  };
  return SpireRangerTrapCounts{count(0), count(1), count(2)};
}

std::shared_ptr<const SpireRangerTypesTable> spire_ranger_types_cached() {
  auto& cache = table_cache();
  bool present = false;
  std::filesystem::file_time_type t{};
  try {
    present = std::filesystem::is_regular_file(cache.path);
    if (present) {
      t = std::filesystem::last_write_time(cache.path);
    }
  } catch (const std::exception&) {
    return cache.table; // the file cannot be asked about now: what was read last stands
  }
  if (cache.looked && (cache.missing == !present) && (!present || (t == cache.looked_time))) {
    return cache.table;
  }
  cache.looked = true;
  cache.missing = !present;
  cache.looked_time = t;
  cache.table.reset();
  if (!present) {
    phosg::log_info_f("Spire ranger types: no {}; every weight is 1", cache.path);
    return nullptr;
  }
  try {
    cache.table = std::make_shared<const SpireRangerTypesTable>(spire_ranger_types_parse(phosg::load_file(cache.path)));
  } catch (const std::exception& e) {
    phosg::log_warning_f("Spire ranger types: {} is unreadable ({}); every weight is 1 until the file changes",
        cache.path, e.what());
  }
  return cache.table;
}

void spire_ranger_types_set_file(const std::string& path) {
  auto& cache = table_cache();
  cache = TableCache();
  cache.path = path.empty() ? SPIRE_RANGER_TYPES_FILE : path;
}

namespace {

// H3: the weight of one candidate. 1 for every candidate of a class that is no ranger, and without the table.
uint32_t spire_ranger_types_hook(const SpireMechTypeWeight& candidate) {
  if (!candidate.in || (candidate.in->char_class >= 12) || !char_class_is_ranger(candidate.in->char_class)) {
    return 1;
  }
  auto table = spire_ranger_types_cached();
  return table ? spire_ranger_type_weight(*table, candidate) : 1;
}

[[maybe_unused]] const bool spire_ranger_types_registered = spire_mech_register_type_weight(
    {SPIRE_RANGER_TYPES_SWITCH, SPIRE_RANGER_TYPES_SWITCH, SPIRE_MECH_ORDER_RANGER_TYPES, spire_ranger_types_hook});

} // namespace
