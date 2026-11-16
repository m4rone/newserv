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

#include "SpireMag.hh"
#include "SpireMechanics.hh"
#include "SpireRewards.hh"
#include "SpireTestSupport.hh"

// The tests of reward kind 1 (a weapon in the line held) and kind 2 (a weapon in a new line).
// Moved literally out of run_static_tests() in StaticTests.cc (the seam, docs/review-loop/goals/naad.md point 1): the
// text of every test below is what stood there, the indentation and this function around it aside. The owner of this
// file is the attack mechanic (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 5).

namespace {

void spire_tests_attack() {
  auto rewards = spire_rewards_parse(phosg::load_file(SPIRE_REWARDS_FILE));
  auto mags = spire_mag_parse(phosg::load_file(SPIRE_RUN_MAG_FILE));
  auto first_room_of_w = spire_test_first_room_of_w(rewards);

  // Every cell: the item the server answers is the grid's (code, grind at most MaxGrind, a special of the cell's
  // list, Hit), for the line the player holds, at the W of the room, in the tier rolled; its rule B need at the grid's
  // run copy equals the grid's mag (POW, DEX, MIND); rule B's mag meets it, stays at most 200, and never sinks
  size_t cells = 0, need_mismatches = 0, rule_b_checked = 0;
  for (const auto& [char_class, cls] : rewards.classes) {
    const char* arch = spire_mag_archetype(char_class);
    const auto& mag_line = mags.classes.at(arch);
    for (const auto& [line_name, line] : cls.lines) {
      uint8_t held[12] = {static_cast<uint8_t>(line.kit >> 16), static_cast<uint8_t>(line.kit >> 8),
          static_cast<uint8_t>(line.kit)};
      expect_eq(line_name, spire_reward_line_of(rewards, cls, held));
      for (const auto& [w, wl] : line.w) {
        uint32_t room = first_room_of_w.at(w);
        for (size_t t = 0; t < 4; t++) {
          auto tier = static_cast<SpireRewardTier>(t);
          const auto& cell = wl.tiers[t];
          for (size_t pick = 0; pick < std::max<size_t>(cell.specials.size(), 1); pick++) {
            auto offer = spire_reward_offer(rewards, SpireRewardKind::WEAPON, char_class, held, room, tier,
                [pick](uint32_t n) -> uint32_t { return n ? (pick % n) : 0; });
            cells += (pick == 0);
            expect_eq(line_name, offer.line);
            expect_eq(line_name, offer.from_line);
            expect_eq(w, offer.w);
            expect_eq(cell.code, static_cast<uint32_t>((offer.item[0] << 16) | (offer.item[1] << 8) | offer.item[2]));
            expect(offer.item[3] <= offer.info.max_grind);
            expect_eq(std::min<uint32_t>(cell.grind, offer.info.max_grind), offer.item[3]);
            if (cell.specials.empty()) {
              expect_eq(0, offer.item[4]);
            } else {
              expect_eq(cell.specials[pick].id, offer.item[4]);
              expect(offer.item_name.ends_with(cell.specials[pick].name));
            }
            expect_eq(cell.hit ? 0x05 : 0x00, offer.item[6]);
            expect_eq(cell.hit, offer.item[7]);
            expect(offer.label.starts_with(std::string(spire_reward_tier_label(tier)) + " "));
          }
          auto offer = spire_reward_offer(rewards, SpireRewardKind::WEAPON, char_class, held, room, tier,
              [](uint32_t) -> uint32_t { return 0; });
          // review r4: the reward held keeps the line, up to a Legendary (Marck: "krijg je gewoon van die lijn. tot
          // aan legendaries"): the next Wapen is of the same line, also for a rare with a group of its own (Musashi 89)
          expect_eq(line_name, spire_reward_line_of(rewards, cls, offer.item.data()));
          expect_eq(line_name, spire_reward_offer(rewards, SpireRewardKind::WEAPON, char_class, offer.item.data(), room,
              SpireRewardTier::NORMAL, [](uint32_t) -> uint32_t { return 0; }).line);
          auto need = spire_reward_mag_need(offer.info, wl.run_atp, wl.run_ata, wl.run_mst, SpireAtaModel::SAFE);
          if ((need.pow != cell.grid_mag[0]) || (need.dex != cell.grid_mag[1]) || (need.mind != cell.grid_mag[2])) {
            need_mismatches++;
            phosg::log_error_f("   {} {} W{} {}: need {}/{}/{}, grid {}/{}/{}", cls.name, line_name, w,
                spire_reward_tier_name(tier), need.pow, need.dex, need.mind, cell.grid_mag[0], cell.grid_mag[1],
                cell.grid_mag[2]);
          }
          // rule B against every step of the class's mag line, from no mag and from the last step's mag
          for (uint32_t step = 0; step < mag_line.steps.size(); step++) {
            for (bool from_mag : {false, true}) {
              std::optional<SpireMagBytes> current;
              if (from_mag) {
                current = spire_mag_step_bytes(mag_line, mag_line.steps.size() - 1, 0, std::nullopt);
              }
              for (size_t main_stat : {size_t(0), size_t(1), size_t(3)}) {
                auto b = spire_mag_rule_b(mag_line, step, 0, current, need, 0, main_stat);
                uint32_t sum = 0;
                for (size_t z = 0; z < 4; z++) {
                  sum += spire_mag_stat_word(b, z) / 100;
                }
                expect(sum <= 200);
                expect(spire_mag_level_of(b) <= 200);
                if (current) {
                  expect(spire_mag_level_of(b) >= spire_mag_level_of(*current));
                }
                if (need.pow + need.dex + need.mind <= 200) {
                  expect(spire_reward_meets(offer.info, wl.run_atp, wl.run_ata, wl.run_mst, &b, SpireAtaModel::SAFE));
                }
                rule_b_checked++;
              }
            }
          }
        }
      }
      // the new-line reward: another line of the class, at the same W
      if (cls.lines.size() > 1) {
        for (uint32_t r = 0; r < cls.lines.size() + 2; r++) {
          auto offer = spire_reward_offer(rewards, SpireRewardKind::NEW_LINE, char_class, held, 5,
              SpireRewardTier::RARE, [r](uint32_t n) -> uint32_t { return n ? (r % n) : 0; });
          expect(offer.line != line_name);
          expect(cls.lines.count(offer.line));
          expect_eq(line_name, offer.from_line);
          expect_eq(spire_reward_w(rewards, 5), offer.w);
        }
      }
    }
  }
  phosg::log_info_f("   rewards: {} cells checked, {} need mismatches, {} rule B mags checked", cells, need_mismatches,
      rule_b_checked);
  expect(cells >= 1000);
  expect_eq(0u, need_mismatches);

  // a force's own lines (Marck, 16 Nov 2026: a force's strength is its spells and MST): without a weapon of a known
  // line the reward follows its first MST line (Cane), and a new line is another MST line, never a saber, slicer or
  // gun; a force that holds a saber still gets the saber line. Hunters and rangers keep Saber and all lines.
  {
    const uint8_t saber[12] = {0x00, 0x01, 0x00};
    auto first = [](uint32_t) -> uint32_t { return 0; };
    size_t forces = 0;
    for (const auto& [char_class, cls] : rewards.classes) {
      auto mst_line = [&](const std::string& ln) { return rewards.items.at(cls.lines.at(ln).kit).mst > 0; };
      bool fo = cls.archetype == "FO";
      forces += fo ? 1 : 0;
      auto bare = spire_reward_offer(rewards, SpireRewardKind::WEAPON, char_class, nullptr, 5, SpireRewardTier::NORMAL, first);
      expect_eq(std::string(fo ? "Cane" : "Saber"), bare.line);
      size_t other_lines = 0;
      for (const auto& ln : cls.line_order) {
        other_lines += (!fo && (ln != "Saber")) ? 1 : 0;
      }
      std::set<std::string> seen;
      for (uint32_t r = 0; r < cls.lines.size() + 2; r++) {
        auto pick = [r](uint32_t n) -> uint32_t { return n ? (r % n) : 0; };
        for (const uint8_t* held : {static_cast<const uint8_t*>(nullptr), static_cast<const uint8_t*>(saber)}) {
          auto offer = spire_reward_offer(rewards, SpireRewardKind::NEW_LINE, char_class, held, 5, SpireRewardTier::RARE, pick);
          expect(offer.line != offer.from_line);
          expect(!fo || mst_line(offer.line));
          seen.emplace(offer.line);
        }
      }
      if (fo) {
        expect((seen == std::set<std::string>{"Cane", "Rod", "Wand"}));
      } else {
        expect_eq(other_lines, seen.size()); // every other line of a hunter or ranger is still offered
      }
      expect_eq(std::string("Saber"), spire_reward_offer(rewards, SpireRewardKind::WEAPON, char_class, saber, 5, SpireRewardTier::NORMAL, first).line);
    }
    expect_eq(4u, forces);
  }

  // review r4: without a weapon of a known line (none held, or one outside the grid) every class gets the Saber line,
  // the one every class carries (design par. 1), never the line a hash order put first
  for (const auto& [char_class, cls] : rewards.classes) {
    expect_eq(std::string("Saber"), cls.line_order.front());
    const uint8_t unknown[12] = {0x00, 0xEE, 0x00};
    for (const uint8_t* held : {static_cast<const uint8_t*>(nullptr), unknown}) {
      auto bare = spire_reward_offer(rewards, SpireRewardKind::WEAPON, char_class, held, 1, SpireRewardTier::NORMAL,
          [](uint32_t) -> uint32_t { return 0; });
      // a force falls back on its first MST line (Cane), every other class on Saber
      std::string fallback = (cls.archetype == "FO") ? "Cane" : "Saber";
      expect_eq(fallback, bare.line);
      expect_eq(fallback, bare.from_line);
    }
  }
}

[[maybe_unused]] const bool registered = spire_mech_register_tests("attack", spire_tests_attack);

} // namespace
