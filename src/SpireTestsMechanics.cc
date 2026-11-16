#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <phosg/UnitTest.hh>

#include <stdint.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "SpireMechanics.hh"
#include "SpireRewards.hh"

// The tests of the seam's own rules (SpireMechanics.hh; docs/review-loop/goals/naad.md point 2): the table of reward
// kinds and the switches. What a kind gives is tested in its owner's file (SpireTestsAttack.cc and the others) and
// pinned by the seam's fingerprint (SpireTestsSeam.cc).
//
// "No offer" is a throw of spire_reward_offer here. At the bridge every throw of an offer ends in the entry's own item
// (SpireFlagBridge.cc spire_reward_make_offer catches it; the 'Q' leaves the slot to the quest and the pick answers
// own_item): that path needs a client and is not played here.

namespace {

// A kind of this file only, registered while the test runs: a new kind reads its own section, keeps it, and makes its
// offer from what the core hands it, without a line in the core
constexpr SpireRewardKind TEST_KIND = static_cast<SpireRewardKind>(250);
constexpr const char* TEST_KEY = "test.naad";
constexpr const char* TEST_FIRST_KEY = "test.naad.eerst";
struct TestSection {
  int64_t levels = 0;
};

void test_parse(const phosg::JSON& json, SpireRewardTable& ret) {
  auto section = std::make_shared<TestSection>();
  section->levels = json.at("MagFood").get_int("Rare"); // any key of the grid: the kind gets the grid's JSON itself
  ret.sections[TEST_KEY] = section;
}

std::optional<SpireRewardOffer> test_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
  auto section = std::static_pointer_cast<const TestSection>(in.table.sections.at(TEST_KEY));
  ret.food_levels = section->levels;
  ret.label = in.tier_label + " test";
  return ret;
}

// The route asked before the kind: only class 2 is its own (as an android would be the android route's)
std::optional<SpireRewardOffer> test_first_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
  if (in.char_class != 2) {
    return std::nullopt;
  }
  ret.label = in.tier_label + " first";
  return ret;
}

std::string offer_text(const SpireRewardOffer& o) {
  return std::format("{} {} {} {} {} {}", static_cast<int>(o.kind), o.label, spire_run_mag_bytes_hex(o.item), o.line, o.w,
      o.food_levels);
}

// The reason an offer is not made, "" when it is
template <typename FnT>
std::string no_offer(FnT&& fn) {
  try {
    fn();
  } catch (const std::runtime_error& e) {
    return e.what();
  }
  return "";
}

void spire_tests_mechanics() {
  auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
  auto first = [](uint32_t) -> uint32_t { return 0; };
  auto offer_of = [&](SpireRewardKind kind) {
    return spire_reward_offer(rewards, kind, 0, nullptr, 3, SpireRewardTier::RARE, first);
  };
  using K = SpireRewardKind;

  // The table: the kinds 1-5 are registered, each under its owner's switch; 6-8 are known numbers nobody registered;
  // the two points before a kind (4: the android route, 5: power first) are empty
  expect(spire_mech_kind_conflicts().empty());
  const std::vector<std::pair<K, std::string>> keys = {{K::WEAPON, "aanval"}, {K::NEW_LINE, "aanval"}, {K::MAG_FOOD, "mags"},
      {K::DISK, "force.schijf"}, {K::UNIT, "verdediging.units"}};
  for (const auto& [kind, key] : keys) {
    const auto* reg = spire_mech_kind(kind);
    expect(reg != nullptr);
    expect_eq(key, std::string(reg->switch_key));
    expect(!reg->default_off);
    expect(reg->offer && reg->pick);
    expect_eq(kind != K::MAG_FOOD, reg->result != nullptr); // mag food leaves no pick in flight
  }
  expect_eq(5u, spire_mech_kinds().size());
  for (int64_t k = 1; k <= 8; k++) {
    expect(spire_reward_kind_known(k));
    expect(!spire_mech_kind(static_cast<K>(k), true));
  }
  expect(!spire_reward_kind_known(0) && !spire_reward_kind_known(9));
  for (K kind : {K::FOCUS, K::MANA, K::ARMOR}) {
    expect(!spire_mech_kind(kind));
    // a known kind nobody registered gives no offer: the entry's own item
    expect_eq(std::format("reward kind {} is not registered", static_cast<uint32_t>(kind)), no_offer([&]() { offer_of(kind); }));
  }

  // The switches, read from a file of this test
  auto dir = std::filesystem::temp_directory_path() / std::format("newserv-static-spire-mechanics-{}", getpid());
  std::filesystem::create_directories(dir);
  std::string file = (dir / "spire-schakelaars.json").string();
  uint32_t writes = 0;
  auto write = [&](const std::string& text) {
    phosg::save_file(file, text);
    // a time of its own for every write: two writes within one tick of the file system's clock would read as one
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++writes));
  };
  spire_mech_set_switches_file(file);

  // without a switches file everything is on, except what its asker names "default off"
  expect(spire_mech_enabled("aanval") && spire_mech_enabled("mags") && spire_mech_enabled("no such key"));
  expect(!spire_mech_enabled("verdediging.vallen", true));
  const std::vector<K> plain = {K::WEAPON, K::NEW_LINE, K::MAG_FOOD, K::UNIT}; // DISK needs the run copy's state
  std::vector<std::string> before;
  for (K kind : plain) {
    before.emplace_back(offer_text(offer_of(kind)));
  }
  auto expect_as_before = [&](size_t z) { expect_eq(before[z], offer_text(offer_of(plain[z]))); };
  const std::string disk_on = "a disk offer without the run copy's techniques"; // the disk's own code was reached
  expect_eq(disk_on, no_offer([&]() { offer_of(K::DISK); }));

  // a kind with its switch off gives no offer (the entry's own item); the others give what they gave
  write(R"({"aanval": false, "verdediging.vallen": true})");
  expect(!spire_mech_enabled("aanval") && spire_mech_enabled("mags"));
  expect(spire_mech_enabled("verdediging.vallen", true));
  expect_eq(std::string("reward kind 1 is switched off (aanval)"), no_offer([&]() { offer_of(K::WEAPON); }));
  expect_eq(std::string("reward kind 2 is switched off (aanval)"), no_offer([&]() { offer_of(K::NEW_LINE); }));
  expect_as_before(2);
  expect_as_before(3);
  expect_eq(disk_on, no_offer([&]() { offer_of(K::DISK); }));

  // the file is read again when it changes: a key that is gone is on again, and every kind has its own key
  write(R"({"mags": false, "force.schijf": false, "verdediging.units": false})");
  expect(spire_mech_enabled("aanval") && !spire_mech_enabled("mags"));
  expect_as_before(0); // with its switch on again a kind gives the offer it gave before
  expect_as_before(1);
  expect_eq(std::string("reward kind 3 is switched off (mags)"), no_offer([&]() { offer_of(K::MAG_FOOD); }));
  expect_eq(std::string("reward kind 4 is switched off (force.schijf)"), no_offer([&]() { offer_of(K::DISK); }));
  expect_eq(std::string("reward kind 5 is switched off (verdediging.units)"), no_offer([&]() { offer_of(K::UNIT); }));

  // an unreadable file leaves the last good state standing: no JSON, no object, a value that is no boolean
  for (const char* bad : {"{ this is no JSON", R"(["mags"])", R"({"aanval": false, "mags": "nee"})"}) {
    write(bad);
    expect(spire_mech_enabled("aanval") && !spire_mech_enabled("mags") && !spire_mech_enabled("verdediging.units"));
    expect_as_before(0);
    expect_eq(std::string("reward kind 3 is switched off (mags)"), no_offer([&]() { offer_of(K::MAG_FOOD); }));
  }

  // readable again: the new state
  write(R"({"mags": true})");
  expect(spire_mech_enabled("mags") && spire_mech_enabled("verdediging.units"));
  for (size_t z = 0; z < plain.size(); z++) {
    expect_as_before(z);
  }
  expect_eq(disk_on, no_offer([&]() { offer_of(K::DISK); }));

  // the file gone: everything on again
  write(R"({"aanval": false})");
  expect(!spire_mech_enabled("aanval"));
  std::filesystem::remove(file);
  expect(spire_mech_enabled("aanval"));
  for (size_t z = 0; z < plain.size(); z++) {
    expect_as_before(z);
  }

  // A new kind, from this file alone: its own section in the table, its offer from what the core hands it, "default
  // off" until its key is true, and a route that is asked before it
  spire_mech_register_kind({TEST_KIND, TEST_KEY, true, test_parse, test_offer, nullptr, nullptr});
  spire_mech_register_kind_first({TEST_KIND, TEST_FIRST_KEY, false, nullptr, test_first_offer, nullptr, nullptr});
  expect(spire_mech_kind_conflicts().empty());
  auto grid = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
  expect_eq(1u, grid.sections.count(TEST_KEY));
  auto test_offer_of = [&](uint8_t char_class) {
    return spire_reward_offer(grid, TEST_KIND, char_class, nullptr, 3, SpireRewardTier::RARE, first);
  };
  expect_eq(std::string("reward kind 250 is switched off (test.naad)"), no_offer([&]() { test_offer_of(0); }));
  auto o = test_offer_of(2); // the route before the kind has its own switch, and it is on
  expect(o.first_route);
  expect_eq(std::string("RARE first"), o.label);
  write(R"({"test.naad": true})");
  o = test_offer_of(0); // not the route's: the kind itself
  expect(!o.first_route);
  expect_eq(std::string("RARE test"), o.label);
  expect_eq(rewards.mag_food[1], o.food_levels);
  expect(test_offer_of(2).first_route);
  write(R"({"test.naad": true, "test.naad.eerst": false})");
  o = test_offer_of(2); // the route off: the kind itself
  expect(!o.first_route);
  expect_eq(std::string("RARE test"), o.label);
  spire_mech_register_kind({TEST_KIND, TEST_KEY, false, nullptr, nullptr, nullptr, nullptr});
  expect_eq(1u, spire_mech_kind_conflicts().size()); // a second registration of a kind is named, the first stays
  expect(spire_mech_kind(TEST_KIND)->offer == test_offer);

  // leave nothing behind: the tests after this one see the table and the switches as the server has them
  spire_mech_forget_kind(TEST_KIND);
  spire_mech_forget_kind(TEST_KIND, true);
  expect(!spire_mech_kind(TEST_KIND) && !spire_mech_kind(TEST_KIND, true));
  expect_eq(5u, spire_mech_kinds().size());
  expect(spire_mech_kind_conflicts().empty());
  expect_eq(0u, spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE)).sections.count(TEST_KEY));
  spire_mech_set_switches_file("");
  std::filesystem::remove_all(dir);
  expect(spire_mech_enabled("aanval"));
}

[[maybe_unused]] const bool registered = spire_mech_register_tests("mechanics", spire_tests_mechanics);

} // namespace
