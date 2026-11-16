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

// Reward kind 3 (MAG_FOOD: levels on the run mag's main stat). Owner: the mags mechanic; switch "mags" (pso-roguelike
// new-features/roguelike/design/mechanics/integratie.md par. 3, 5 and 7).
// Moved literally out of SpireRewards.cc and SpireFlagBridge.cc by the seam (docs/review-loop/goals/naad.md point 3):
// the text of every block below is what stood there, the indentation and the function around it aside. New are the
// lines that bind the old names to what the core hands over (const auto& table = in.table;) and the registration.

namespace {

// The section of kind 3: MagFood, the levels per tier
void spire_reward_mag_food_parse(const phosg::JSON& json, SpireRewardTable& ret) {
  for (size_t z = 0; z < 4; z++) {
    ret.mag_food[z] = spire_get_u(json.at("MagFood"), TIER_NAMES[z], 200);
  }
}

// The offer of kind 3: the tier's levels of mag food
std::optional<SpireRewardOffer> spire_reward_mag_food_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
  const auto& table = in.table;
  const auto& tier = in.tier;
  const auto& tier_label = in.tier_label;
  ret.food_levels = table.mag_food[static_cast<size_t>(tier)];
  ret.item_name = std::format("Mag food +{}", ret.food_levels);
  ret.label = tier_label + " " + ret.item_name;
  return ret;
}

// 'Z' sub 6 of kind 3: the ledger owes the food; no item, the run mag follows (rule B). No RESULT follows.
void spire_reward_mag_food_pick(SpireRewardPick& pk) {
  auto& c = pk.c;
  auto& serial_number = pk.serial_number;
  auto& quest_number = pk.quest_number;
  auto& slot = pk.slot;
  auto& offer = pk.offer;
  auto& offers = pk.offers;
  auto& p = pk.p;
  auto& led = pk.led;
  auto& refusal = pk.refusal;
  auto& main_stat = pk.main_stat;
  auto& own_item = pk.own_item;
  using R = SpireRunMagRefusal;
  if (!led) {
    own_item(std::format("mag food without a run mag ({})", (refusal == R::NONE) ? "no ledger" : spire_run_mag_refusal_name(refusal)));
    return;
  }
  led->feed_levels = std::min<uint32_t>(led->feed_levels + offer->food_levels, 200);
  led->boost_due = true;
  offers.by_slot.erase(slot);
  spire_reward_answer(c, nullptr, true);
  c->log.info_f("Spire reward: pick {} {} slot {} for serial {:010} in q{}: {} on {}; run copy Lv{} ATP {} ATA {} MST {}; "
      "mag now {}; owed food {} levels; the run mag follows in this room's pause B (rule B)",
      spire_reward_kind_name(offer->kind), spire_reward_tier_name(offer->tier), slot, serial_number, quest_number,
      offer->label, (main_stat == 0) ? "DEF" : (main_stat == 3) ? "MIND" : "POW", p.level + 1, p.atp, p.ata, p.mst,
      spire_reward_mag_text(p.mag_before), led->feed_levels);
  return;
}

[[maybe_unused]] const bool spire_reward_mag_food_registered = spire_mech_register_kind({SpireRewardKind::MAG_FOOD, "mags",
    false, spire_reward_mag_food_parse, spire_reward_mag_food_offer, spire_reward_mag_food_pick, nullptr});

} // namespace
