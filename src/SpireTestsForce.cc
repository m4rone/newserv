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
#include "LevelTable.hh"
#include "SpireMag.hh"
#include "SpireMechanics.hh"
#include "SpireRewards.hh"

// The tests of reward kind 4 (a technique disk) and of the level the disk teaches.
// Moved literally out of run_static_tests() in StaticTests.cc (the seam, docs/review-loop/goals/naad.md point 1): the
// text of every test below is what stood there, the indentation and this function around it aside. The owner of this
// file is the force mechanic (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 5).

namespace {

void spire_tests_force() {
  auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
  auto ipt = ItemParameterTable::from_json(phosg::JSON::parse(phosg::load_file("system/tables/item-parameter-table-gc-v3.json")));

  // part B, the disk (Schijf). The MST a disk asks per level: the stricter of the client image and Blue Burst
  expect_eq(15u, rewards.disk_levels.size());
  expect_eq("Foie", rewards.techs[0].name);
  expect_eq(40u, spire_reward_disk_mst(rewards.techs[0], 1));
  expect_eq(180u, spire_reward_disk_mst(rewards.techs[0], 8));
  expect_eq(620u, spire_reward_disk_mst(rewards.techs[0], 30));
  expect_eq(44u, spire_reward_disk_mst(rewards.techs[3], 1)); // Barta: Blue Burst asks more at Lv1 (the image 35)
  expect_eq(760u, spire_reward_disk_mst(rewards.techs[3], 30)); // and the image more at Lv30 (Blue Burst 740)
  expect_eq(150u, spire_reward_disk_mst(rewards.techs[14], 1)); // Ryuker: flat
  expect_eq(150u, spire_reward_disk_mst(rewards.techs[17], 30)); // Reverser: flat
  expect_eq(972u, spire_reward_disk_mst(rewards.techs[18], 30)); // Megid
  expect_eq(40, spire_reward_disk_mind(180, 100));
  expect_eq(41, spire_reward_disk_mind(181, 100)); // rounded up: a MIND level gives 2 MST
  expect_eq(0, spire_reward_disk_mind(100, 180));
  // the tilt per tier and the grid column per rung (pso-roguelike design/mechanics/force-spells.md par. 2)
  expect((rewards.tier_disk_mind == std::array<uint16_t, 4>{0, 15, 40, 80}));
  expect((rewards.rung_disk_columns == std::array<uint8_t, 4>{0, 1, 2, 3}));
  {
    // the budget: the copy's MST and two per MIND level of the worn mag, or of the mag's plan with the tier's tilt
    // when that is more, within the room the reward weapon leaves in the mag
    SpireRewardDiskState st;
    st.mst = 76;
    expect_eq(76u, spire_reward_disk_budget(st, 0));
    expect_eq(236u, spire_reward_disk_budget(st, 80));
    st.mind_plan = 18;
    expect_eq(76u + 2 * 33, spire_reward_disk_budget(st, 15));
    st.mind_now = 60; // fed above the plan and its tilt
    expect_eq(76u + 2 * 60, spire_reward_disk_budget(st, 15));
    st.mind_room = 20;
    expect_eq(76u + 2 * 20, spire_reward_disk_budget(st, 80));
  }
  // the level of one technique: the highest the budget meets, at most the class's highest, two above what the copy
  // knows when the budget gives less, and down to what the copy's MST and its mag can reach
  {
    const auto& foie = rewards.techs[0];
    SpireRewardDiskState st;
    st.mst = 1000;
    expect_eq(8, spire_reward_disk_level(foie, 180, 0xFF, 29, st));
    expect_eq(8, spire_reward_disk_level(foie, 199, 0xFF, 29, st)); // Lv9 asks 200
    expect_eq(30, spire_reward_disk_level(foie, 5000, 0xFF, 29, st));
    expect_eq(5, spire_reward_disk_level(foie, 180, 0xFF, 4, st)); // the class learns Lv5 at most
    expect_eq(1, spire_reward_disk_level(foie, 0, 0xFF, 29, st)); // a budget below Lv1: Lv1, the mag carries it
    expect_eq(8, spire_reward_disk_level(foie, 180, 2, 29, st)); // the copy knows Lv3
    expect_eq(12, spire_reward_disk_level(foie, 180, 9, 29, st)); // it knows Lv10 already: two above
    expect_eq(11, spire_reward_disk_level(foie, 180, 9, 10, st)); // one, when that is the class's highest
    expect_eq(0, spire_reward_disk_level(foie, 180, 9, 9, st)); // and that is the class's highest: nothing to teach
    expect_eq(0, spire_reward_disk_level(foie, 180, 0xFF, 0xFF, st)); // not the class's
    st.extra_levels = 2; // a drawn curse's levels
    expect_eq(10, spire_reward_disk_level(foie, 180, 0xFF, 29, st));
    st.extra_levels = 0;
    st.mst = 100;
    st.mind_room = 20; // the mag can add 40 MST: 140 holds Foie Lv6 (40 + 5 x 20)
    expect_eq(6, spire_reward_disk_level(foie, 140, 0xFF, 29, st));
    expect_eq(6, spire_reward_disk_level(foie, 140, 4, 29, st)); // it knows Lv5: Lv7 is out of reach, Lv6 is not
    expect_eq(0, spire_reward_disk_level(foie, 140, 5, 29, st)); // it knows Lv6: Lv7 is out of reach
    st.mst = 1000;
    st.mind_room = 200;
    st.mst_cap = 100; // the class's highest MST: Foie Lv4 asks 100
    expect_eq(4, spire_reward_disk_level(foie, 5000, 0xFF, 29, st));
  }
  {
    // the two full tables of force-spells.md par. 2: Foie for a FOnewearl (class 8) and a FOmar (10) per room,
    // rung and tier, on the copy's MST at the room's level (pso-roguelike sim/plan-spire.json level_x4, a rung adds
    // 19, 39 or 79 levels) with the run mag's step row. want[room - 1][rung][tier].
    auto level_table = std::make_shared<JSONLevelTable>(
        phosg::JSON::parse(phosg::load_file("system/tables/level-table-v3.json")));
    static constexpr uint8_t room_level[15] = {4, 6, 8, 10, 11, 12, 13, 15, 16, 17, 19, 20, 21, 22, 24};
    static constexpr uint8_t room_mind[15] = {0, 0, 0, 18, 18, 18, 18, 30, 30, 30, 42, 42, 42, 42, 46};
    static constexpr uint8_t rung_levels[4] = {0, 19, 39, 79};
    static constexpr uint8_t want_fonewearl[15][4][4] = {
        {{2, 4, 6, 10}, {8, 9, 12, 16}, {14, 15, 18, 22}, {26, 27, 30, 30}},
        {{3, 4, 7, 11}, {9, 10, 13, 17}, {15, 16, 19, 23}, {27, 28, 30, 30}},
        {{3, 5, 7, 11}, {9, 11, 13, 17}, {15, 17, 19, 23}, {27, 29, 30, 30}},
        {{6, 7, 10, 14}, {12, 13, 16, 20}, {18, 19, 22, 26}, {30, 30, 30, 30}},
        {{6, 8, 10, 14}, {12, 13, 16, 20}, {18, 19, 22, 26}, {30, 30, 30, 30}},
        {{6, 8, 10, 14}, {12, 14, 16, 20}, {18, 20, 22, 26}, {30, 30, 30, 30}},
        {{7, 8, 11, 15}, {12, 14, 16, 20}, {18, 20, 22, 26}, {30, 30, 30, 30}},
        {{9, 10, 13, 17}, {14, 16, 18, 22}, {20, 22, 24, 28}, {30, 30, 30, 30}},
        {{9, 10, 13, 17}, {15, 16, 19, 23}, {21, 22, 25, 29}, {30, 30, 30, 30}},
        {{9, 11, 13, 17}, {15, 16, 19, 23}, {21, 22, 25, 29}, {30, 30, 30, 30}},
        {{11, 13, 15, 19}, {17, 18, 21, 25}, {23, 24, 27, 30}, {30, 30, 30, 30}},
        {{11, 13, 15, 19}, {17, 18, 21, 25}, {23, 24, 27, 30}, {30, 30, 30, 30}},
        {{12, 13, 16, 20}, {17, 19, 21, 25}, {23, 25, 27, 30}, {30, 30, 30, 30}},
        {{12, 13, 16, 20}, {18, 19, 22, 26}, {24, 25, 28, 30}, {30, 30, 30, 30}},
        {{13, 14, 17, 21}, {19, 20, 23, 27}, {25, 26, 29, 30}, {30, 30, 30, 30}},
    };
    static constexpr uint8_t want_fomar[15][4][4] = {
        {{2, 3, 6, 10}, {6, 7, 10, 14}, {10, 12, 14, 18}, {20, 21, 24, 28}},
        {{2, 4, 6, 10}, {6, 8, 10, 14}, {11, 12, 15, 19}, {20, 22, 24, 28}},
        {{3, 4, 7, 11}, {7, 8, 11, 15}, {11, 13, 15, 19}, {21, 22, 25, 29}},
        {{5, 6, 9, 13}, {9, 11, 13, 17}, {14, 15, 18, 22}, {23, 24, 27, 30}},
        {{5, 7, 9, 13}, {9, 11, 13, 17}, {14, 15, 18, 22}, {23, 25, 27, 30}},
        {{5, 7, 9, 13}, {9, 11, 13, 17}, {14, 16, 18, 22}, {23, 25, 27, 30}},
        {{6, 7, 10, 14}, {10, 11, 14, 18}, {14, 16, 18, 22}, {23, 25, 27, 30}},
        {{7, 9, 11, 15}, {11, 13, 15, 19}, {16, 18, 20, 24}, {25, 26, 29, 30}},
        {{7, 9, 11, 15}, {12, 13, 16, 20}, {17, 18, 21, 25}, {25, 27, 29, 30}},
        {{8, 9, 12, 16}, {12, 13, 16, 20}, {17, 18, 21, 25}, {25, 27, 29, 30}},
        {{9, 11, 13, 17}, {13, 15, 17, 21}, {19, 20, 23, 27}, {27, 28, 30, 30}},
        {{10, 11, 14, 18}, {14, 15, 18, 22}, {19, 20, 23, 27}, {27, 29, 30, 30}},
        {{10, 11, 14, 18}, {14, 15, 18, 22}, {19, 21, 23, 27}, {27, 29, 30, 30}},
        {{10, 11, 14, 18}, {14, 16, 18, 22}, {19, 21, 23, 27}, {27, 29, 30, 30}},
        {{11, 12, 15, 19}, {15, 16, 19, 23}, {20, 22, 24, 28}, {28, 30, 30, 30}},
    };
    size_t cells = 0;
    for (const auto& [cls, want] : {std::make_pair(uint8_t(8), want_fonewearl), std::make_pair(uint8_t(10), want_fomar)}) {
      for (size_t k = 0; k < 15; k++) {
        for (size_t rung = 0; rung < 4; rung++) {
          PlayerStats stats;
          level_table->reset_to_base(stats, cls);
          level_table->advance_to_level(stats, room_level[k] + rung_levels[rung] - 1, cls);
          SpireRewardDiskState st;
          st.mst = stats.char_stats.mst;
          st.mst_cap = level_table->max_stats_for_class(cls).char_stats.mst;
          st.mind_now = room_mind[k];
          st.mind_plan = room_mind[k];
          for (size_t t = 0; t < 4; t++) {
            expect_eq(want[k][rung][t], spire_reward_disk_level(rewards.techs[0],
                                            spire_reward_disk_budget(st, rewards.tier_disk_mind[t]), 0xFF, 29, st));
            cells++;
          }
        }
      }
    }
    expect_eq(480u, cells);
  }
  // an offer for every class that learns techniques, in every room and tier: a disk (03 02 level-1 00 technique)
  // of a technique Sega drops there, never above the class's highest (newserv refuses such a disk when it is
  // used; here the copy's MST reaches every level), with its name and tier word; an android gets none (the
  // entry's own item)
  size_t disk_offers = 0, disk_classes = 0;
  for (const auto& [char_class, cls] : rewards.classes) {
    SpireRewardDiskState st;
    st.mst = 5000;
    st.mind_room = 0;
    bool learns = false;
    for (size_t x = 0; x < SPIRE_TECH_COUNT; x++) {
      st.class_max[x] = ipt->get_max_tech_level(char_class, x);
      learns |= (st.class_max[x] != 0xFF);
    }
    disk_classes += learns;
    for (const auto& [room, levels] : rewards.disk_levels) {
      for (size_t t = 0; t < 4; t++) {
        auto tier = static_cast<SpireRewardTier>(t);
        std::vector<size_t> want;
        for (size_t x = 0; x < SPIRE_TECH_COUNT; x++) {
          if (levels[x][t] && (st.class_max[x] != 0xFF)) {
            want.emplace_back(x);
          }
        }
        if (want.empty()) {
          expect(!learns || (t == 0)); // a class that learns always has a Rare, Epic and Legendary disk
          expect_raises(std::runtime_error, [&]() {
            spire_reward_offer(rewards, SpireRewardKind::DISK, char_class, nullptr, room, tier,
                [](uint32_t) -> uint32_t { return 0; }, &st);
          });
          continue;
        }
        for (uint32_t r = 0; r < want.size(); r++) {
          auto offer = spire_reward_offer(rewards, SpireRewardKind::DISK, char_class, nullptr, room, tier,
              [&](uint32_t n) -> uint32_t {
                expect_eq(want.size(), n);
                return r;
              },
              &st);
          size_t x = want[r];
          uint8_t level = std::min<uint8_t>(st.class_max[x], 29) + 1;
          expect_eq(x, offer.tech);
          expect_eq(level, offer.tech_level);
          expect_eq(spire_reward_disk_mst(rewards.techs[x], level), offer.mst_required);
          expect((offer.item == SpireMagBytes{0x03, 0x02, static_cast<uint8_t>(level - 1), 0x00, static_cast<uint8_t>(x)}));
          expect_eq(std::format("{} {} Lv{}", spire_reward_tier_label(tier), rewards.techs[x].name, level), offer.label);
          disk_offers++;
        }
      }
    }
  }
  phosg::log_info_f("   rewards: {} disk offers checked for {} classes that learn techniques", disk_offers, disk_classes);
  expect(disk_classes >= 4); // the four forces at least
  expect(disk_offers >= 4 * 15 * 4);
  {
    // a copy that knows the class's highest of every technique gets no disk; an offer without the copy is refused
    SpireRewardDiskState st;
    st.mst = 5000;
    for (size_t x = 0; x < SPIRE_TECH_COUNT; x++) {
      st.class_max[x] = ipt->get_max_tech_level(6, x); // FOmarl
      st.known[x] = st.class_max[x];
    }
    auto first = [](uint32_t) -> uint32_t { return 0; };
    expect_raises(std::runtime_error, [&]() {
      spire_reward_offer(rewards, SpireRewardKind::DISK, 6, nullptr, 15, SpireRewardTier::LEGENDARY, first, &st);
    });
    expect_raises(std::runtime_error, [&]() {
      spire_reward_offer(rewards, SpireRewardKind::DISK, 6, nullptr, 15, SpireRewardTier::LEGENDARY, first);
    });
    // with one technique a level short, that one, a level above what the copy knows
    st.known[3] = st.class_max[3] - 1; // Barta
    auto offer = spire_reward_offer(rewards, SpireRewardKind::DISK, 6, nullptr, 15, SpireRewardTier::LEGENDARY, first, &st);
    expect_eq(3, offer.tech);
    expect_eq(st.class_max[3] + 1, offer.tech_level);
  }
  {
    // a line that names a disk of its own (the FO lines that were a fixed disk, "Foie"): its technique, at the
    // level the copy's MST and the tier's tilt give, two above what the copy knows, a drawn curse's two levels on
    // top; another technique only when the copy cannot grow in the line's own
    SpireRewardDiskState st;
    st.mst = 100;
    for (size_t x = 0; x < SPIRE_TECH_COUNT; x++) {
      st.class_max[x] = ipt->get_max_tech_level(6, x); // FOmarl
    }
    const auto& row = rewards.disk_levels.at(1);
    expect(row[0][0] && row[0][1]); // Sega drops Foie in room 1, on Normal and on Hard
    expect_eq(0, row[9][0]); // no Normal Grants there
    auto only = [&](uint32_t n) -> uint32_t {
      expect_eq(1u, n); // the line's technique is the one candidate
      return 0;
    };
    auto line_offer = [&](SpireRewardTier tier) {
      return spire_reward_offer(rewards, SpireRewardKind::DISK, 6, nullptr, 1, tier, only, &st);
    };
    st.line_tech = 0;
    auto offer = line_offer(SpireRewardTier::NORMAL);
    expect_eq(0, offer.tech);
    expect_eq(4, offer.tech_level); // MST 100 holds Foie Lv4
    expect_eq(std::string("NORMAL Foie Lv4"), offer.label);
    expect((offer.item == SpireMagBytes{0x03, 0x02, 0x03, 0x00, 0x00}));
    expect_eq(5, line_offer(SpireRewardTier::RARE).tech_level); // 15 MIND on top: 130 holds Lv5
    expect_eq(8, line_offer(SpireRewardTier::EPIC).tech_level); // 40 MIND: 180
    expect_eq(12, line_offer(SpireRewardTier::LEGENDARY).tech_level); // 80 MIND: 260
    st.mind_plan = 18; // after the first boss the mag's plan carries 36 MST more
    expect_eq(5, line_offer(SpireRewardTier::NORMAL).tech_level);
    st.mind_plan = 0;
    st.mst = 400; // holds Foie Lv19
    expect_eq(19, line_offer(SpireRewardTier::NORMAL).tech_level);
    st.known[0] = 20; // the copy knows Foie Lv21: two above
    expect_eq(23, line_offer(SpireRewardTier::NORMAL).tech_level);
    st.extra_levels = spire_reward_curse_disk_levels(3); // Glass on the pick: two more
    expect_eq(25, line_offer(SpireRewardTier::NORMAL).tech_level);
    st.known[0] = 27; // Lv28: the class's highest holds the curse's levels too
    expect_eq(30, line_offer(SpireRewardTier::NORMAL).tech_level);
    st.known[0] = 0xFF;
    st.mst = 100;
    st.mind_room = 20; // the copy reaches MST 140: Foie Lv6, whatever the tier's tilt and with or without the curse
    expect_eq(6, line_offer(SpireRewardTier::LEGENDARY).tech_level);
    st.extra_levels = 0;
    expect_eq(6, line_offer(SpireRewardTier::LEGENDARY).tech_level);
    st.mst = 400;
    st.mind_room = 200;
    st.line_tech = 9; // a line of a technique Sega drops none of in this room and tier still gives its own
    offer = line_offer(SpireRewardTier::NORMAL);
    expect_eq(9, offer.tech);
    expect_eq(9, offer.tech_level); // Grants Lv9 asks 400
    // the copy knows the class's highest Foie: the Foie line gives another technique Sega drops here, as "Disk" does
    st.line_tech = 0;
    st.known[0] = st.class_max[0];
    size_t others = 0;
    for (size_t x = 1; x < SPIRE_TECH_COUNT; x++) {
      others += (row[x][1] && (st.class_max[x] != 0xFF));
    }
    expect(others >= 2);
    offer = spire_reward_offer(rewards, SpireRewardKind::DISK, 6, nullptr, 1, SpireRewardTier::RARE,
        [&](uint32_t n) -> uint32_t {
          expect_eq(others, n);
          return n - 1;
        },
        &st);
    expect(offer.tech != 0);
    expect(offer.tech_level >= 1);
    // a line of a technique the class cannot learn (never in a pool the quest builds) gives another one as well
    st.known[0] = 0xFF;
    st.line_tech = 0;
    st.class_max[0] = 0xFF;
    expect(spire_reward_offer(rewards, SpireRewardKind::DISK, 6, nullptr, 1, SpireRewardTier::RARE,
               [](uint32_t) -> uint32_t { return 0; }, &st)
               .tech != 0);
    // a difficulty rung reads which techniques may come from the grid's later column: a Normal "Disk" on Hard
    // draws from Sega's Hard disks of the room, on Ultimate from its Ultimate ones
    st.class_max[0] = ipt->get_max_tech_level(6, 0);
    st.line_tech = 0xFF;
    for (uint8_t rung = 0; rung < 4; rung++) {
      size_t present = 0;
      for (size_t x = 0; x < SPIRE_TECH_COUNT; x++) {
        present += (row[x][rung] && (st.class_max[x] != 0xFF));
      }
      expect(present >= 2);
      spire_reward_offer(rewards, SpireRewardKind::DISK, 6, nullptr, 1, SpireRewardTier::NORMAL,
          [&](uint32_t n) -> uint32_t {
            expect_eq(present, n);
            return 0;
          },
          &st, rung);
    }
    size_t normal_techs = 0, hard_techs = 0;
    for (size_t x = 0; x < SPIRE_TECH_COUNT; x++) {
      normal_techs += !!row[x][0];
      hard_techs += !!row[x][1];
    }
    expect(hard_techs > normal_techs);
  }
  // what a drawn curse pays on a disk: Cursed Chest and Glass two levels, Jellen Mark and no curse none
  expect_eq(0, spire_reward_curse_disk_levels(0));
  expect_eq(2, spire_reward_curse_disk_levels(1));
  expect_eq(0, spire_reward_curse_disk_levels(2));
  expect_eq(2, spire_reward_curse_disk_levels(3));
  // rule B's need with a disk in the run: per stat the highest, so the weapon's POW and DEX stay
  expect((spire_reward_need_with_disk(SpireMagNeed{40, 30, 10}, 25) == SpireMagNeed{40, 30, 25}));
  expect((spire_reward_need_with_disk(SpireMagNeed{40, 30, 50}, 25) == SpireMagNeed{40, 30, 50}));
  // a reward weapon can be worn when it is given, also after a disk: the weapon's POW and DEX stand and the disks'
  // MIND takes what they leave of 200; the weapon's own MIND (a cane) stands
  expect((spire_reward_need_with_disk(SpireMagNeed{160, 0, 0}, 50) == SpireMagNeed{160, 0, 40}));
  expect((spire_reward_need_with_disk(SpireMagNeed{150, 60, 0}, 50) == SpireMagNeed{150, 60, 0}));
  expect((spire_reward_need_with_disk(SpireMagNeed{0, 0, 180}, 50) == SpireMagNeed{0, 0, 180}));
}

[[maybe_unused]] const bool registered = spire_mech_register_tests("force", spire_tests_force);

} // namespace
