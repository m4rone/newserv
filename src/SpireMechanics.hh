#pragma once

#include <optional>
#include <string>
#include <vector>

#include <phosg/JSON.hh>

#include "SpireRewards.hh"

// SpireMechanics: the seam between the Spire core and its mechanics (pso-roguelike
// new-features/roguelike/design/mechanics/integratie.md par. 3; this repo's docs/review-loop/goals/naad.md). A
// mechanic builds in files of its own (src/Spire*.cc, taken by a glob in CMakeLists.txt) and registers here; the core
// calls what was registered and names no mechanic. Everything in this file carries the prefix spire_mech_.
//
// Every list of registrations is a static inside a function: a file registers from a static initialiser of its own,
// and the order in which the translation units start does not matter.

// --- Tests ---
//
// A test file (src/SpireTests*.cc) registers one function under a name of its own, with one line:
//   [[maybe_unused]] const bool registered = spire_mech_register_tests("mags", spire_tests_mags);
// run_static_tests() (StaticTests.cc) calls spire_mech_run_tests() once and names no test file.

using SpireMechTestFunction = void (*)();

// Registers a test file's function under its name. Always returns true (so it can initialise a static). A name
// registered twice keeps the first function and makes spire_mech_run_tests throw: no test runs twice, none is hidden.
bool spire_mech_register_tests(const char* name, SpireMechTestFunction fn);

// Runs every registered function exactly once, in the order of the names, and logs one line per test file on stderr
// before it runs. Throws std::logic_error when nothing was registered or a name was registered twice.
void spire_mech_run_tests();

// The names registered, in the order spire_mech_run_tests runs them
std::vector<std::string> spire_mech_test_names();

// --- Switches ---
//
// system/spire-schakelaars.json: one JSON object, key -> true or false (pso-roguelike integratie.md par. 7; the quest
// repo's quests/spire/data/schakelaars.json, installed by apply-on-pc.sh). Read again whenever the file changes. A
// missing file or a missing key is ON, unless the asker names its key "default off". An unreadable file (no JSON, no
// object, a value that is no boolean) leaves the last good state standing and logs one line.

constexpr const char* SPIRE_SWITCHES_FILE = "system/spire-schakelaars.json";

// Whether the mechanic behind this key is on. For a reward kind, off means: no offer, so the entry's own item. For a
// hook: not registered. A mechanic also needs the Enabled of its own table.
bool spire_mech_enabled(const std::string& key, bool default_off = false);

// Tests only: reads the switches from another file ("" = SPIRE_SWITCHES_FILE again) and forgets what was read
void spire_mech_set_switches_file(const std::string& path);

// --- Reward kinds ---
//
// A reward kind (SpireRewardKind, the srv value of a draw entry) lives in a file of its own (src/SpireReward*.cc) and
// registers there with its number, its switch key and four functions:
//
//   namespace {
//   void my_parse(const phosg::JSON& json, SpireRewardTable& ret);  // its own section of the grid -> ret.sections[key]
//   std::optional<SpireRewardOffer> my_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret);
//   void my_pick(SpireRewardPick& pk);      // 'Z' sub 6 (SpireRewardKinds.hh)
//   void my_result(SpireRewardResult& rs);  // 'Z' sub 7
//   [[maybe_unused]] const bool registered = spire_mech_register_kind(
//       {SpireRewardKind::FOCUS, "force.focus", false, my_parse, my_offer, my_pick, my_result});
//   } // namespace
//
// The core (SpireRewards.cc spire_rewards_parse and spire_reward_offer; SpireFlagBridge.cc spire_reward_pick and
// spire_reward_result) asks this table and names no kind. A kind reads its input itself from what the core hands it:
// the grid's JSON at the parse, SpireRewardOfferInput (with the client at the bridge in in.live), SpireRewardPick and
// SpireRewardResult. A kind that is not registered, or whose switch is off, gives no offer: the pick answers the
// entry's own item.

struct SpireRewardPick; // SpireRewardKinds.hh
struct SpireRewardResult;

struct SpireMechKind {
  SpireRewardKind kind = SpireRewardKind::NONE;
  const char* switch_key = ""; // spire_mech_enabled; also the key of its section in SpireRewardTable::sections
  bool default_off = false; // off until its key is true in the switches file (a kind that waits for a measurement)
  // Its own section of the grid; nullptr: none. Called once per parse of the grid, after the shared part, whatever its
  // switch says. Throws on a section it cannot read (the grid is then refused as a whole, as before the seam).
  void (*parse)(const phosg::JSON& json, SpireRewardTable& ret) = nullptr;
  // The offer. ret comes with what every kind shares filled in (kind, tier, room_index, w, from_line); nullopt: none
  // from this registration. A throw is "no offer" with its reason.
  std::optional<SpireRewardOffer> (*offer)(const SpireRewardOfferInput& in, SpireRewardOffer ret) = nullptr;
  void (*pick)(SpireRewardPick& pk) = nullptr;
  void (*result)(SpireRewardResult& rs) = nullptr; // nullptr: its pick never leaves a pick in flight
};

// Registers a kind. Always returns true (so it can initialise a static). A kind registered twice keeps the first
// registration and is named by spire_mech_kind_conflicts.
bool spire_mech_register_kind(const SpireMechKind& reg);
// Registers a route that is asked BEFORE the kind itself: its offer function answers nullopt when the offer is not its
// own, and the kind itself is asked then. An offer it gives carries first_route, so its pick and result come back to
// this registration. Two points wait for one, nobody registers there yet: kind 4 (DISK) for the android route before
// the disk, kind 5 (UNIT) for "power first" before the unit.
bool spire_mech_register_kind_first(const SpireMechKind& reg);
// The registration of a kind (first: the route before it), whatever its switch says; nullptr when nobody registered
const SpireMechKind* spire_mech_kind(SpireRewardKind kind, bool first = false);
// Every registration, by kind number, a kind's first route before the kind itself
std::vector<const SpireMechKind*> spire_mech_kinds();
// "kind 4" or "kind 4 (first route)" for every registration that came second; empty when all is well
std::vector<std::string> spire_mech_kind_conflicts();
// Tests only: takes a registration out again
void spire_mech_forget_kind(SpireRewardKind kind, bool first = false);
