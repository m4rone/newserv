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

// Reward kind 1 (WEAPON: a weapon of the line the player holds) and kind 2 (NEW_LINE: a weapon of another line of
// the class). Owner: the attack mechanic; switch "aanval" (pso-roguelike new-features/roguelike/design/mechanics/
// integratie.md par. 3, 5 and 7).
// Moved literally out of SpireRewards.cc and SpireFlagBridge.cc by the seam (docs/review-loop/goals/naad.md point 3):
// the text of every block below is what stood there, the indentation and the function around it aside. New are the
// lines that bind the old names to what the core hands over (const auto& table = in.table;) and the registration.

SpireMagBytes spire_reward_weapon_bytes(const SpireRewardTable& table, const SpireRewardCell& cell, uint8_t special) {
  SpireMagBytes ret{};
  ret[0] = (cell.code >> 16) & 0xFF;
  ret[1] = (cell.code >> 8) & 0xFF;
  ret[2] = cell.code & 0xFF;
  uint32_t max_grind = table.items.at(cell.code).max_grind;
  ret[3] = std::min<uint32_t>(cell.grind, max_grind);
  ret[4] = special;
  if (cell.hit) {
    ret[6] = 0x05; // attribute 5: Hit
    ret[7] = cell.hit;
  }
  return ret;
}

static std::string spire_reward_weapon_name(const SpireRewardCell& cell, const SpireMagBytes& bytes, const std::string& special) {
  std::string ret = cell.name;
  if (bytes[3]) {
    ret += std::format(" +{}", bytes[3]);
  }
  if (!special.empty()) {
    ret += " " + special;
  }
  return ret;
}

namespace {

// The offer of kind 1 and kind 2: the weapon of the cell (line, W, tier), with one of the cell's specials
std::optional<SpireRewardOffer> spire_reward_weapon_offer(const SpireRewardOfferInput& in, SpireRewardOffer ret) {
  const auto& table = in.table;
  const auto& kind = in.kind;
  const auto& cls = in.cls;
  const auto& tier = in.tier;
  const auto& tier_label = in.tier_label;
  const auto& rnd = in.rnd;
  const auto& own_line = in.own_line;
  ret.line = ret.from_line;
  if (kind == SpireRewardKind::NEW_LINE) {
    std::vector<std::string> others;
    for (const auto& ln : cls.line_order) {
      if ((ln != ret.from_line) && own_line(ln)) { // a force: another MST line, never a saber, slicer or gun
        others.emplace_back(ln);
      }
    }
    if (!others.empty()) {
      ret.line = others[spire_mech_type_weight_pick(others.size(), rnd, [&](size_t z) { // the seam's H3
        return SpireMechTypeWeight{&in, SpireMechCandidate::LINE, &others[z], 0, 0};
      })];
    }
  }
  const auto& line = cls.lines.at(ret.line);
  auto w_it = line.w.find(ret.w);
  if (w_it == line.w.end()) {
    throw std::runtime_error(std::format("{} {} has no W{}", cls.name, ret.line, ret.w));
  }
  const auto& cell = w_it->second.tiers[static_cast<size_t>(tier)];
  std::string special_name;
  uint8_t special = 0;
  if (!cell.specials.empty()) {
    const auto& sp = cell.specials[rnd(cell.specials.size()) % cell.specials.size()];
    special = sp.id;
    special_name = sp.name;
  }
  ret.item = spire_reward_weapon_bytes(table, cell, special);
  ret.info = table.items.at(cell.code);
  ret.item_name = spire_reward_weapon_name(cell, ret.item, special_name);
  ret.label = tier_label + " " + ret.item_name;
  return ret;
}

// 'Z' sub 6 of kind 1 and kind 2: rule B's need of the weapon, then the answer with the item
void spire_reward_weapon_pick(SpireRewardPick& pk) {
  auto& c = pk.c;
  auto& serial_number = pk.serial_number;
  auto& quest_number = pk.quest_number;
  auto& slot = pk.slot;
  auto& table = pk.table;
  auto& offer = pk.offer;
  auto& p = pk.p;
  auto& led = pk.led;
  auto& refusal = pk.refusal;
  auto& worn = pk.worn;
  using R = SpireRunMagRefusal;
  p.need = spire_reward_need_with_disk(spire_reward_mag_need(offer->info, p.atp, p.ata, p.mst, table->ata_model),
      led ? led->disk_mind : 0);
  spire_reward_pick_rule_b(pk); // SpireFlagBridge.cc: p.mag_due, the pick in flight, the answer
  c->log.info_f("Spire reward: pick {} {} slot {} for serial {:010} in q{}: {} {} (line {}{}); needs mag POW {} DEX {} "
      "MIND {} ({} ATA model; worn units count ATP +{} MST +{}); {}",
      spire_reward_kind_name(offer->kind), spire_reward_tier_name(offer->tier), slot, serial_number, quest_number,
      offer->label, spire_run_mag_bytes_hex(offer->item), offer->line,
      (offer->line != offer->from_line) ? std::format(", held {}", offer->from_line) : "", p.need.pow, p.need.dex,
      p.need.mind, spire_ata_model_name(table->ata_model), worn.bonus.atp, worn.bonus.mst,
      !led ? std::format("no rule B ({})", (refusal == R::NONE) ? "no run or ledger" : spire_run_mag_refusal_name(refusal))
      : p.mag_due ? std::string("rule B's run mag follows in this room's pause B, the weapon's equip after it")
                  : std::string("the run mag already meets it"));
}

// After the RESULT of a server reward weapon: SpireEquipItem for it once newserv's copy holds it and, when rule B owes a
// mag for it (p.mag_due), once that mag is on the client and equipped (spire_reward_equip_gate): the game holds the
// weapon's requirement also when the server equips it (Marck, 16 Nov 2026), so the stats go first. The quest asks the
// mag at the start of this room's pause B (never in the pick's thread); until then the player keeps the weapon held
// before. A later server weapon (equip_seq) ends this one's wait. Then the journal line of the pick.
static asio::awaitable<void> spire_reward_equip(std::weak_ptr<Client> weak_c, std::weak_ptr<const Lobby> weak_l,
    SpireRewardPending p, uint32_t serial_number, uint32_t run_id, uint32_t equip_seq) {
  std::optional<uint32_t> item_id;
  for (size_t z = 0; z < 16; z++) { // up to 8 s: the item in newserv's copy
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
    item_id.reset();
    const auto& inv = overlay->inventory;
    for (size_t x = 0; (x < inv.num_items) && (x < inv.items.size()); x++) {
      const auto& it = inv.items[x];
      if (!it.data.empty() && !memcmp(it.data.data1.data(), p.offer.item.data(), 5) && !it.is_equipped()) {
        item_id = it.data.id.load();
      }
    }
    if (item_id) {
      break;
    }
  }
  const char* mag_wait = "";
  uint32_t polls = 0;
  if (item_id && p.mag_due) {
    for (uint32_t since_landed = 0;; polls++) {
      auto c = weak_c.lock();
      auto l = weak_l.lock();
      if (!c || !l || (c->lobby.lock() != l)) {
        co_return;
      }
      auto overlay = c->has_overlay() ? c->character_file(false, true) : nullptr;
      if (!overlay) {
        co_return;
      }
      if (spire_reward_offers_for(c, l->lobby_id).equip_seq != equip_seq) {
        c->log.info_f("Spire reward: the equip of {} {:08X} ends its wait for rule B's mag after {} s: a later reward "
            "weapon was taken", p.offer.item_name, *item_id, polls);
        co_return;
      }
      auto* led = spire_run_mag_ledger_for(c, run_id, false);
      bool mag_equipped = false;
      spire_reward_run_mag(overlay->inventory, &mag_equipped);
      auto gate = spire_reward_equip_gate(true, led != nullptr, led && led->boost_due, mag_equipped, polls, since_landed);
      if (gate == SpireRewardEquipGate::WAIT) {
        if (!polls) {
          c->log.info_f("Spire reward: the equip of {} {:08X} waits for rule B's mag", p.offer.item_name, *item_id);
        }
        since_landed += (led && !led->boost_due) ? 1 : 0;
        c.reset(); // the wait holds neither the client nor its game
        l.reset();
        co_await async_sleep(SPIRE_REWARD_EQUIP_POLL_DELAY);
        continue;
      }
      mag_wait = (gate == SpireRewardEquipGate::GO) ? "; rule B mag on" : "; rule B mag NOT on, the wait ran out";
      break;
    }
  }
  auto c = weak_c.lock();
  auto l = weak_l.lock();
  if (!c || !l || (c->lobby.lock() != l)) {
    co_return;
  }
  bool available = spire_equip_item_available_for(c);
  if (item_id && available) {
    co_await spire_equip_item_send(c, l, *item_id, "reward weapon", SPIRE_EQUIP_RESUME_WEAPON);
    c = weak_c.lock();
    l = weak_l.lock();
    if (!c || !l || (c->lobby.lock() != l)) {
      co_return;
    }
  } else if (item_id) {
    spire_send_ticker(c, SPIRE_EQUIP_WEAPON_MISSING_TEXT);
  }
  auto overlay = c->has_overlay() ? c->character_file(false, true) : nullptr;
  if (!overlay) {
    co_return;
  }
  bool landed = false;
  for (size_t x = 0; item_id && (x < overlay->inventory.num_items) && (x < overlay->inventory.items.size()); x++) {
    const auto& it = overlay->inventory.items[x];
    if (it.data.id == *item_id) {
      landed = it.is_equipped();
    }
  }
  bool mag_equipped = false;
  auto mag = spire_reward_run_mag(overlay->inventory, &mag_equipped);
  const SpireMagBytes* mag_p = mag ? &*mag : nullptr;
  auto meets = [&](SpireAtaModel m, const SpireMagBytes* with) {
    return spire_reward_meets(p.offer.info, p.atp, p.ata, p.mst, with, m) ? "yes" : "no";
  };
  c->log.info_f("Spire reward: TAKEN serial {:010} run {:08X} class {} {} line {} W{} {} {} {} (req ATP {} ATA {} MST {}, "
      "weapon ATA {}); run copy Lv{} ATP {} ATA {} MST {}; mag before {}; mag now {} ({}); rule B need POW {} DEX {} "
      "MIND {}{}{}; equip {} ({}); meets without mag table/screen_weapon/screen_bare {}/{}/{}, with mag {}/{}/{}",
      serial_number, run_id, p.class_name.empty() ? "?" : p.class_name,
      spire_reward_kind_name(p.offer.kind), p.offer.line, p.offer.w, spire_reward_tier_name(p.offer.tier),
      p.offer.item_name, spire_run_mag_bytes_hex(p.offer.item), p.offer.info.atp, p.offer.info.ata, p.offer.info.mst,
      p.offer.info.weapon_ata, p.level + 1, p.atp, p.ata, p.mst, spire_reward_mag_text(p.mag_before),
      spire_reward_mag_text(mag), mag_equipped ? "equipped" : "not equipped", p.need.pow, p.need.dex, p.need.mind,
      p.mag_due ? std::format(" (rule B mag owed; the equip waited {} s)", polls) : std::string(), mag_wait,
      !item_id ? "NOT FOUND in the run copy" : landed ? "LANDED" : "NOT LANDED",
      !item_id ? "no item" : available ? "SpireEquipItem sent" : std::format("r{} = 0, the player's own", SPIRE_EQUIP_REGISTER),
      meets(SpireAtaModel::TABLE, nullptr), meets(SpireAtaModel::SCREEN_WEAPON, nullptr),
      meets(SpireAtaModel::SCREEN_BARE, nullptr), meets(SpireAtaModel::TABLE, mag_p),
      meets(SpireAtaModel::SCREEN_WEAPON, mag_p), meets(SpireAtaModel::SCREEN_BARE, mag_p));
}

// 'Z' sub 7 of kind 1 and kind 2: the weapon exists; the equip follows (spire_reward_equip)
void spire_reward_weapon_result(SpireRewardResult& rs) {
  auto& c = rs.c;
  auto& l = rs.l;
  auto& serial_number = rs.serial_number;
  auto& quest_number = rs.quest_number;
  auto& offers = rs.offers;
  auto& p = rs.p;
  spire_reward_result_rule_b(rs); // SpireFlagBridge.cc: the ledger keeps the weapon's need
  auto& run = rs.run;
  auto& led = rs.led;
  uint32_t equip_seq = ++offers.equip_seq;
  c->log.info_f("Spire reward: RESULT 1 for serial {:010} in q{} slot {}: {} created{}", serial_number, quest_number,
      p.offer.slot, p.offer.label, led ? (p.mag_due ? "; rule B mag owed" : "; the mag meets it") : "; no run mag ledger");
  asio::co_spawn(*c->require_server_state()->io_context,
      spire_reward_equip(c, l, std::move(p), serial_number, run ? run->run_id : 0, equip_seq), asio::detached);
}

// Kind 1 and kind 2: one file, one switch, no section of their own in the grid (classes, lines and items are shared)
[[maybe_unused]] const bool spire_reward_weapon_registered =
    spire_mech_register_kind({SpireRewardKind::WEAPON, "aanval", false, nullptr, spire_reward_weapon_offer,
        spire_reward_weapon_pick, spire_reward_weapon_result}) &&
    spire_mech_register_kind({SpireRewardKind::NEW_LINE, "aanval", false, nullptr, spire_reward_weapon_offer,
        spire_reward_weapon_pick, spire_reward_weapon_result});

} // namespace
