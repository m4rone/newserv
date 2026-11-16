#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <phosg/UnitTest.hh>

#include <stdint.h>

#include <algorithm>
#include <array>
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
#include "PlayerInventory.hh"
#include "SpireEquipItem.hh"
#include "SpireFlagBridge.hh"
#include "SpireMechanics.hh"
#include "SpireRewards.hh"

// The tests of reward kind 5 (a unit) and of the frame with four slots it goes into.
// Moved literally out of run_static_tests() in StaticTests.cc (the seam, docs/review-loop/goals/naad.md point 1): the
// text of every test below is what stood there, the indentation and this function around it aside. The owner of this
// file is the defence mechanic (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 5).

namespace {

void spire_tests_defence() {
  auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));

  // part B, Kracht: for every class and tier a unit of its archetype's list, as data1 01 03 xx and nothing else, with
  // the tier word in front; every unit of the grid is one the item parameter table knows with that stat and amount
  expect(!rewards.units.empty());
  auto ipt = ItemParameterTable::from_json(phosg::JSON::parse(phosg::load_file("system/tables/item-parameter-table-gc-v3.json")));
  for (const auto& [code, unit] : rewards.units) {
    expect_eq(0x0103u, code >> 8);
    const auto& def = ipt->get_unit(code & 0xFF);
    expect_eq(unit.stat, static_cast<uint16_t>(def.stat));
    expect_eq(unit.amount, static_cast<uint16_t>(def.amount));
  }
  size_t unit_offers = 0;
  for (const auto& [char_class, cls] : rewards.classes) {
    const auto& tiers = rewards.unit_tiers.at(cls.archetype);
    for (size_t t = 0; t < 4; t++) {
      auto tier = static_cast<SpireRewardTier>(t);
      for (uint32_t r = 0; r < tiers[t].size(); r++) {
        auto offer = spire_reward_offer(rewards, SpireRewardKind::UNIT, char_class, nullptr, 3, tier,
            [r](uint32_t n) -> uint32_t { return n ? (r % n) : 0; });
        uint32_t code = (offer.item[0] << 16) | (offer.item[1] << 8) | offer.item[2];
        expect_eq(tiers[t][r], code);
        expect_eq(rewards.units.at(code).name, offer.item_name);
        expect_eq(std::string(spire_reward_tier_label(tier)) + " " + offer.item_name, offer.label);
        for (size_t z = 3; z < 12; z++) {
          expect_eq(0, offer.item[z]);
        }
        unit_offers++;
      }
    }
  }
  phosg::log_info_f("   rewards: {} units, {} unit offers checked", rewards.units.size(), unit_offers);
  expect(unit_offers >= 12 * 4);

  // review r1 B3: the run copy's worn frame and units as a pick (a weapon's requirement) and a reward unit's equip
  // (its slot) read them: only equipped units count, by the table's stat and amount, in the slot their data1[4]
  // names; the frame's slots are its data1[5], at most 4; without a table the slots still count, the stats not
  PlayerInventory inv;
  auto put = [&](size_t z, std::initializer_list<uint8_t> data1, uint32_t flags) {
    auto& it = inv.items[z];
    it = PlayerInventoryItem();
    it.state = 1;
    size_t x = 0;
    for (uint8_t b : data1) {
      it.data.data1[x++] = b;
    }
    it.data.id = 0x00810100 + z;
    it.flags = flags;
    inv.num_items = std::max<size_t>(inv.num_items, z + 1);
  };
  put(0, {0x01, 0x01, 0x00, 0x00, 0x00, 0x04}, 0x0000000C); // the kit frame with four slots, worn
  put(1, {0x01, 0x03, 0x03, 0x00, 0x00}, 0x0000000C); // God/Power in slot 0
  put(2, {0x01, 0x03, 0x07, 0x00, 0x02}, 0x0000000C); // God/Mind in slot 2
  put(3, {0x01, 0x03, 0x00}, 0x00000004); // Knight/Power in the pack: no slot, no stats
  put(4, {0x00, 0x01, 0x00}, 0x0000000C); // a weapon
  auto worn = spire_reward_worn(ipt, inv);
  expect(worn.frame);
  expect_eq(4, worn.frame_slots);
  expect_eq(0x05, worn.occupied);
  expect_eq(static_cast<uint32_t>(ipt->get_unit(0x03).amount), worn.bonus.atp);
  expect_eq(static_cast<uint32_t>(ipt->get_unit(0x07).amount), worn.bonus.mst);
  expect(worn.bonus.atp > 0 && worn.bonus.mst > 0);
  expect(spire_reward_free_unit_slot(worn.occupied, worn.frame_slots) == std::optional<uint8_t>(1));
  auto bare = spire_reward_worn(nullptr, inv);
  expect(bare.frame && (bare.occupied == 0x05) && !bare.bonus.atp && !bare.bonus.mst);
  inv.items[0].data.data1[5] = 9; // a slot byte past 4 gives no fifth slot
  expect_eq(4, spire_reward_worn(ipt, inv).frame_slots);
  inv.items[0].flags = 0x00000004; // the frame in the pack: no frame worn
  inv.items[1].flags = inv.items[2].flags = 0x00000004;
  auto none = spire_reward_worn(ipt, inv);
  expect(!none.frame && !none.frame_slots && !none.occupied && !none.bonus.atp && !none.bonus.mst);

  // review r1 B2: the kit frame 'C' 13 equips (part B): the newest unequipped, unwrapped frame with slots; none when
  // it has no slots or is wrapped, or when a frame is worn already (a quest that equips its frame itself)
  inv = PlayerInventory();
  put(0, {0x00, 0x01, 0x00}, 0x00000004); // the kit weapon, made with its grind, not equipped
  put(1, {0x01, 0x01, 0x00, 0x00, 0x00, 0x04}, 0x00000004);
  expect(spire_equip_find_kit_frame(inv) == std::optional<uint32_t>(0x00810101));
  put(2, {0x01, 0x01, 0x00, 0x00, 0x00, 0x04}, 0x00000004); // a second one: the newest
  expect(spire_equip_find_kit_frame(inv) == std::optional<uint32_t>(0x00810102));
  inv.items[2].flags = 0x00000404; // wrapped: the one before it
  expect(spire_equip_find_kit_frame(inv) == std::optional<uint32_t>(0x00810101));
  inv.items[1].data.data1[5] = 0; // no slots: equip_item's frame, not the one item_create2 made for its slots
  expect(spire_equip_find_kit_frame(inv) == std::nullopt);
  inv.items[1].data.data1[5] = 4;
  inv.items[0] = inv.items[2];
  inv.items[0].data.data1[5] = 0;
  inv.items[0].flags = 0x0000000C; // a frame worn before the one with slots
  inv.items[2].flags = 0x00000004;
  expect(spire_equip_find_kit_frame(inv) == std::nullopt);

  // what worn units add to a weapon's requirement: ATP, MST, both from God/Ability (stat 8); nothing from the rest
  {
    SpireUnitBonus b;
    spire_reward_unit_bonus_add(b, 0, 50); // God/Power
    spire_reward_unit_bonus_add(b, 1, 40); // God/Mind
    spire_reward_unit_bonus_add(b, 8, 20); // God/Ability
    spire_reward_unit_bonus_add(b, 2, 15); // God/Arm: ATA is not counted
    spire_reward_unit_bonus_add(b, 0x13, 20); // God/Battle
    expect_eq(70u, b.atp);
    expect_eq(60u, b.mst);
  }
  // the slot of a new unit: the first free one of the frame's slots, none on a frame without slots or a full one
  expect(spire_reward_free_unit_slot(0x00, 4) == std::optional<uint8_t>(0));
  expect(spire_reward_free_unit_slot(0x01, 4) == std::optional<uint8_t>(1));
  expect(spire_reward_free_unit_slot(0x0B, 4) == std::optional<uint8_t>(2));
  expect(spire_reward_free_unit_slot(0x0F, 4) == std::nullopt);
  expect(spire_reward_free_unit_slot(0x00, 0) == std::nullopt);
  expect(spire_reward_free_unit_slot(0x01, 1) == std::nullopt);
  expect(spire_reward_free_unit_slot(0x00, 9) == std::optional<uint8_t>(0)); // a slot byte past 4 gives no fifth slot
  expect(spire_reward_free_unit_slot(0x0F, 9) == std::nullopt);
}

[[maybe_unused]] const bool registered = spire_mech_register_tests("defence", spire_tests_defence);

} // namespace
