#pragma once

#include <stdint.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "SpireMechanics.hh"
#include "SpireRewards.hh"

// The four rangers, RAmar, RAmarl, RAcast and RAcaseal (pso-roguelike
// new-features/roguelike/design/mechanics/ranger-types.md; integratie.md par. 3 H3, par. 5 and par. 7). What a ranger is
// offered leans to its type: "New line" gives a gun of the type's own, a technique disk gives a human ranger its
// support techniques first, and three of four units are of the type's own families. A type weighs, it shuts out only
// what a ranger cannot use: a saber or a partisan unless it holds one already, and for an android the units for
// techniques and against poison and paralysis (NeverUnits).
//
// The weights are system/spire-types-ra.json. This file registers on the seam's H3 (type_weight) and writes in no kind's
// file: it answers the weight of one candidate, 1 for every candidate of a class that is no ranger. With the table's
// Enabled false, without the file, for a type the table has not, and with the switch "ranger-types" off, every weight is
// 1: the draw of before this table.
//
// The trap numbers of the two android rangers are in the table too (Sega's rule, design par. 2); the code that hands
// traps out is the defence mechanic's (design/mechanics/decisions.md B-ONTWERP-5), which reads them with
// spire_ranger_trap_counts.

constexpr const char* SPIRE_RANGER_TYPES_FILE = "system/spire-types-ra.json";
constexpr const char* SPIRE_RANGER_TYPES_SWITCH = "ranger-types";
// The highest weight the table may name, and the highest WildcardOneIn: the seam caps a candidate's weight at 0x10000,
// and a unit's weight is at most (WildcardOneIn - 1) times the number of units in the tier
constexpr uint32_t SPIRE_RANGER_WEIGHT_MAX = 100;

struct SpireRangerTrapCounts {
  uint8_t damage = 0;
  uint8_t freeze = 0;
  uint8_t confuse = 0;
  bool operator==(const SpireRangerTrapCounts& other) const = default;
};

struct SpireRangerType {
  std::string name; // "RAmar"
  // NewLineWeights: the weight of a line the "New line" reward may give; a line the table does not name weighs 1
  std::map<std::string, uint32_t> line_weights;
  // TechWeights: the weight of a technique a disk may be, by technique number; one the table does not name weighs 1
  std::array<uint32_t, SPIRE_TECH_COUNT> tech_weights;
  // WildcardOneIn: one unit offer in this many is a unit outside the type's own
  uint32_t wildcard_one_in = 0;
  // Units: the type's own units per tier as the design lists them, code (0103xx) -> the name the table gives it. The
  // tier is the design's; a unit counts as the type's own in whatever tier the reward grid offers it (own_units).
  std::array<std::map<uint32_t, std::string>, 4> units;
  std::set<uint32_t> own_units;
  // NeverUnits (the two androids): units the type is never offered, code -> the name the table gives it
  std::map<uint32_t, std::string> never_units;
  // Traps (the two androids only): the levels per extra trap for damage, freeze and confuse
  std::optional<std::array<uint32_t, 3>> trap_levels_per;

  SpireRangerType() {
    this->tech_weights.fill(1);
  }
};

struct SpireRangerTypesTable {
  bool enabled = false;
  // The lines a ranger is handed by "New line" only when it holds one of them already
  std::set<std::string> melee_lines;
  // Sega's rule for an android's traps: trap_base + level / (the type's levels per trap), at most trap_max per kind
  uint32_t trap_base = 0;
  uint32_t trap_max = 0;
  std::map<uint8_t, SpireRangerType> types; // by the character class
};

// Strict: throws on a missing key, a type that is no ranger, a weight outside 1-SPIRE_RANGER_WEIGHT_MAX, a technique
// newserv does not know by that name, a code that is no unit or that a type names twice (within Units, or in Units and
// NeverUnits), and traps on a human
SpireRangerTypesTable spire_ranger_types_parse(const std::string& json_text);

// The weight of one line for "New line". held_line: the line of the weapon the player holds ("" for none or a line
// the class has not; SpireRewards.hh spire_reward_line_of). A melee line weighs 0 unless the player holds a melee line.
uint32_t spire_ranger_line_weight(const SpireRangerTypesTable& table, uint8_t char_class, const std::string& line,
    const std::string& held_line);
// The weight of one technique for a disk
uint32_t spire_ranger_tech_weight(const SpireRangerTypesTable& table, uint8_t char_class, uint32_t tech);
// The weight of one unit out of the candidates of its draw (the codes of the archetype's tier in the reward grid). The
// type's own units together weigh WildcardOneIn - 1 times what the others weigh together, so one offer in WildcardOneIn
// is another ranger unit, however many of each the tier holds. Every weight is 1 when the candidates are all the
// type's own or all another's. A unit of the type's NeverUnits weighs 0 and counts on neither side (when a draw holds
// nothing else, the seam draws plainly: SpireMechanics.hh spire_mech_type_weight_pick).
uint32_t spire_ranger_unit_weight(const SpireRangerTypesTable& table, uint8_t char_class, uint32_t code,
    const std::vector<uint32_t>& candidates);
// The weight of a candidate of the seam's H3, by the three functions above; 1 without the offer (candidate.in nullptr)
uint32_t spire_ranger_type_weight(const SpireRangerTypesTable& table, const SpireMechTypeWeight& candidate);

// The traps an android ranger starts with at a level (1-200), per kind; nullopt for a type without Traps, for a class
// the table has not, and with Enabled false. For the defence mechanic's H1.
std::optional<SpireRangerTrapCounts> spire_ranger_trap_counts(const SpireRangerTypesTable& table, uint8_t char_class,
    uint32_t level);

// The table of SPIRE_RANGER_TYPES_FILE, read again only when the file changes; nullptr when it is missing or unreadable
// (one line in the journal per state of the file)
std::shared_ptr<const SpireRangerTypesTable> spire_ranger_types_cached();
// Tests only: reads the table from another file ("" = SPIRE_RANGER_TYPES_FILE again) and forgets what was read
void spire_ranger_types_set_file(const std::string& path);
