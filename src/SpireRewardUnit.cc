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

// Reward kind 5 (UNIT: a unit for a slot of the worn frame). Owner: the defence mechanic; switch "verdediging.units"
// (pso-roguelike new-features/roguelike/design/mechanics/integratie.md par. 3, 5 and 7).
// Moved literally out of SpireRewards.cc and SpireFlagBridge.cc by the seam (docs/review-loop/goals/naad.md point 3):
// the text of every block below is what stood there, the indentation and the function around it aside. New are the
// lines that bind the old names to what the core hands over (const auto& table = in.table;) and the registration.

namespace {

// The section of kind 5: Units, the units by code and per archetype and tier the codes an offer draws from
void spire_reward_unit_parse(const phosg::JSON& json, SpireRewardTable& ret) {
  if (json.contains("Units")) { // part B; a part A file has none
    const auto& units = json.at("Units");
    for (const auto& [code, u_json] : units.get_dict("Items")) {
      SpireRewardUnit u;
      u.name = u_json->get_string("Name");
      u.stars = spire_get_u(*u_json, "Stars", 12);
      u.stat = spire_get_u(*u_json, "Stat", 0xFFFF);
      u.amount = spire_get_u(*u_json, "Amount", 0xFFFF);
      uint32_t c = spire_code_of(code);
      if ((c >> 8) != 0x0103) {
        throw std::runtime_error(std::format("Units Items {} is not a unit (0103xx)", code));
      }
      ret.units.emplace(c, std::move(u));
    }
    for (const auto& [arch, a_json] : units.get_dict("Archetypes")) {
      std::array<std::vector<uint32_t>, 4> tiers;
      for (size_t z = 0; z < 4; z++) {
        for (const auto& code : a_json->get_list(TIER_NAMES[z])) {
          uint32_t c = spire_code_of(code->as_string());
          if (!ret.units.count(c)) {
            throw std::runtime_error(std::format("Units {} {}: {:06X} not in Units Items", arch, TIER_NAMES[z], c));
          }
          tiers[z].emplace_back(c);
        }
        if (tiers[z].empty()) {
          throw std::runtime_error(std::format("Units {} {}: no unit", arch, TIER_NAMES[z]));
        }
      }
      ret.unit_tiers.emplace(arch, std::move(tiers));
    }
  }
}

// The offer of kind 5: one unit of the archetype's tier
std::optional<SpireRewardOffer> spire_reward_unit_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
  const auto& table = in.table;
  const auto& cls = in.cls;
  const auto& tier = in.tier;
  const auto& tier_label = in.tier_label;
  const auto& rnd = in.rnd;
  auto a_it = table.unit_tiers.find(cls.archetype);
  if (a_it == table.unit_tiers.end()) {
    throw std::runtime_error(std::format("archetype {} has no units in the reward grid", cls.archetype));
  }
  const auto& codes = a_it->second[static_cast<size_t>(tier) & 3];
  uint32_t code = codes[rnd(codes.size()) % codes.size()];
  ret.item[0] = (code >> 16) & 0xFF;
  ret.item[1] = (code >> 8) & 0xFF;
  ret.item[2] = code & 0xFF;
  ret.item_name = table.units.at(code).name;
  ret.label = tier_label + " " + ret.item_name;
  return ret;
}

// 'Z' sub 6 of kind 5 (part B): no requirement and no mag; newserv equips it after its RESULT
void spire_reward_unit_pick(SpireRewardPick& pk) {
  auto& c = pk.c;
  auto& serial_number = pk.serial_number;
  auto& quest_number = pk.quest_number;
  auto& slot = pk.slot;
  auto& offer = pk.offer;
  auto& offers = pk.offers;
  auto& p = pk.p;
  auto& worn = pk.worn;
  offers.pending = p;
  spire_reward_answer(c, &offer->item, false);
  c->log.info_f("Spire reward: pick {} {} slot {} for serial {:010} in q{}: {} {}; frame {} with {} slot(s), unit slots "
      "taken {:X}",
      spire_reward_kind_name(offer->kind), spire_reward_tier_name(offer->tier), slot, serial_number, quest_number,
      offer->label, spire_run_mag_bytes_hex(offer->item), worn.frame ? "worn" : "NOT worn", worn.frame_slots,
      worn.occupied);
  return;
}

// After the RESULT of a server reward unit (part B): SpireEquipItem puts it in the first free slot of the worn frame,
// once newserv's copy holds it. Without a worn frame or without a free slot it stays in the pack and the player reads
// why; review r1 B4: also on a client without the function (r143 = 0), whose kit frame has no slots, and there a free
// slot gets the units' "equip it" line in its turn (as the weapon's line at r143 = 0). Then the journal line of the pick.
static asio::awaitable<void> spire_reward_equip_unit(std::weak_ptr<Client> weak_c, std::weak_ptr<const Lobby> weak_l,
    SpireRewardPending p, uint32_t serial_number) {
  std::optional<uint32_t> item_id;
  SpireRewardWorn worn;
  for (size_t z = 0; (z < 16) && !item_id; z++) { // up to 8 s: the item in newserv's copy
    co_await async_sleep(std::chrono::milliseconds(500));
    auto c = weak_c.lock();
    auto l = weak_l.lock();
    if (!c || !l || (c->lobby.lock() != l)) {
      co_return;
    }
    auto overlay = c->has_overlay() ? c->character_file(false, true) : nullptr;
    if (!overlay) {
      co_return;
    }
    const auto& inv = overlay->inventory;
    for (size_t x = 0; (x < inv.num_items) && (x < inv.items.size()); x++) {
      const auto& it = inv.items[x];
      if (!it.data.empty() && !memcmp(it.data.data1.data(), p.offer.item.data(), 3) && !it.is_equipped()) {
        item_id = it.data.id.load();
      }
    }
    worn = spire_reward_worn(c->require_server_state()->data->item_parameter_table(Version::GC_V3), inv);
  }
  auto c = weak_c.lock();
  auto l = weak_l.lock();
  if (!c || !l || (c->lobby.lock() != l)) {
    co_return;
  }
  auto free_slot = worn.frame ? spire_reward_free_unit_slot(worn.occupied, worn.frame_slots) : std::nullopt;
  std::string outcome;
  if (!item_id) {
    outcome = "NOT FOUND in the run copy";
  } else if (!free_slot) {
    outcome = worn.frame ? "in the pack (no free slot)" : "in the pack (no frame worn)";
    spire_send_ticker(c, worn.frame ? SPIRE_REWARD_UNIT_NO_SLOT_TEXT : SPIRE_REWARD_UNIT_NO_FRAME_TEXT);
  } else if (!spire_equip_item_available_for(c)) {
    outcome = std::format("in the pack (r{} = 0, the player's own; free slot {})", SPIRE_EQUIP_REGISTER, *free_slot + 1);
    spire_equip_missing_line_in_turn(c, l, SPIRE_EQUIP_RESUME_UNIT, *item_id);
  } else {
    co_await spire_equip_item_send(c, l, *item_id, "reward unit", SPIRE_EQUIP_RESUME_UNIT,
        SPIRE_EQUIP_UNIT_SLOT_FIRST + *free_slot);
    c = weak_c.lock();
    l = weak_l.lock();
    if (!c || !l || (c->lobby.lock() != l)) {
      co_return;
    }
    auto overlay = c->has_overlay() ? c->character_file(false, true) : nullptr;
    if (!overlay) {
      co_return;
    }
    bool landed = false;
    for (size_t x = 0; (x < overlay->inventory.num_items) && (x < overlay->inventory.items.size()); x++) {
      const auto& it = overlay->inventory.items[x];
      if (it.data.id == *item_id) {
        landed = it.is_equipped();
      }
    }
    outcome = std::format("slot {} of {}: {}", *free_slot + 1, worn.frame_slots, landed ? "LANDED" : "NOT LANDED");
  }
  c->log.info_f("Spire reward: TAKEN serial {:010} class {} {} {} {} {}; frame {} with {} slot(s), unit slots taken {:X} "
      "before it; equip {}",
      serial_number, p.class_name.empty() ? "?" : p.class_name, spire_reward_kind_name(p.offer.kind),
      spire_reward_tier_name(p.offer.tier), p.offer.item_name, spire_run_mag_bytes_hex(p.offer.item),
      worn.frame ? "worn" : "NOT worn", worn.frame_slots, worn.occupied, outcome);
}

// 'Z' sub 7 of kind 5 (part B): no rule B need of its own; the weapon's need stays
void spire_reward_unit_result(SpireRewardResult& rs) {
  auto& c = rs.c;
  auto& l = rs.l;
  auto& serial_number = rs.serial_number;
  auto& quest_number = rs.quest_number;
  auto& p = rs.p;
  c->log.info_f("Spire reward: RESULT 1 for serial {:010} in q{} slot {}: {} created", serial_number, quest_number,
      p.offer.slot, p.offer.label);
  asio::co_spawn(*c->require_server_state()->io_context,
      spire_reward_equip_unit(c, l, std::move(p), serial_number), asio::detached);
  return;
}

[[maybe_unused]] const bool spire_reward_unit_registered = spire_mech_register_kind({SpireRewardKind::UNIT,
    "verdediging.units", false, spire_reward_unit_parse, spire_reward_unit_offer, spire_reward_unit_pick,
    spire_reward_unit_result});

} // namespace
