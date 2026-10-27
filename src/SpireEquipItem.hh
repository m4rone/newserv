#pragma once

#include <stdint.h>

#include <optional>

#include "PlayerInventory.hh"

// Route B bundle 9 (B-REGIE-16-15; contract: pso-roguelike quests/spire/ROUTEB9-PROTOCOL.md § 1 and § 3): server equip.
// The client function SpireEquipItem (GC EU v1.0 only) gets one item id, finds that item in the player's own live
// inventory and equips it through the game's own Item Pack "Equip" routine, so the client sends its own 6x25 and
// newserv's copy of the inventory stays the client's. newserv tells the quest in r15 (SPIRE_EQUIP_REGISTER, every 'M'
// answer) whether it can do that for this client; with r15 = 1 the quest makes the kit weapon with its grind and sends
// 'C' 13 (low byte = grind G), and after a run mag evolution 'C' 14; newserv answers each by sending the function with
// the item id of the kit weapon or of the one run mag in its own copy of the inventory.
//
// Bundle 9 state (2026-10-27, ROUTEB9-NEWSERV-F3-REPORT.md): system/client-functions/SpireEquipItemGC.s exists for
// 3OP0, built only from the addresses proven statically in ROUTEB9-NEWSERV-F2-REPORT.md. With the switch on, r15 is 1
// for a 3OP0 client that runs function calls. Whether the client handles the call correctly at run time is decided by
// the lab check, not by this tree.

constexpr uint32_t SPIRE_EQUIP_ITEM_SPECIFIC_VERSION = 0x334F5030; // "3OP0"
// The function's .meta key, in the form pso-roguelike's quests/spire/pc-build.sh reads: it puts every
// SPIRE_<X>_FUNCTION_NAME = "<name>" into client-functions/required.txt and refuses the build when no Spire*.s carries
// that key (contract ROUTEB9-PROTOCOL.md § 3).
constexpr const char* SPIRE_EQUIP_FUNCTION_NAME = "SpireEquipItem";
// The label of the .s that receives the item id (send_function_call label_writes)
constexpr const char* SPIRE_EQUIP_ITEM_ID_LABEL = "item_id";
constexpr uint8_t SPIRE_EQUIP_REGISTER = 15; // r15: 1 = this client can have an item equipped by the server, else 0
constexpr uint8_t SPIRE_PROMPT_KIT_GROUND = 13; // 'C' 13: the kit weapon was made with grind G (low byte), not equipped
constexpr uint8_t SPIRE_PROMPT_RUN_MAG_REPLACED = 14; // 'C' 14: the run mag was replaced after an evolution

constexpr bool spire_equip_item_applies(uint32_t specific_version) {
  return specific_version == SPIRE_EQUIP_ITEM_SPECIFIC_VERSION;
}

// r15 for a client: its version is 3OP0, the switch SpireEquipItemPatch is on, it can run client functions (B2 runs
// code), and the function index has SpireEquipItem for its version.
constexpr bool spire_equip_item_available(
    uint32_t specific_version, bool patch_on, bool can_run_functions, bool function_loaded) {
  return spire_equip_item_applies(specific_version) && patch_on && can_run_functions && function_loaded;
}

// 'C' 13: the newest (last in inventory order: a 6x2B appends) unequipped, unwrapped weapon with grind G. newserv does
// not carry the quest's kit table, so the weapon's data1[0-2] are not compared: right after the kit the run copy holds
// one new unequipped weapon, the kit weapon. nullopt: none.
inline std::optional<uint32_t> spire_equip_find_kit_weapon(const PlayerInventory& inventory, uint8_t grind) {
  std::optional<uint32_t> ret;
  for (size_t z = 0; (z < inventory.num_items) && (z < inventory.items.size()); z++) {
    const auto& item = inventory.items[z];
    if (!item.data.empty() && (item.data.data1[0] == 0x00) && (item.data.data1[3] == grind) && !item.is_equipped() &&
        !(item.flags & 0x400)) {
      ret = item.data.id.load();
    }
  }
  return ret;
}

// 'C' 14: the one run mag (type 02) of the run copy, when it is not equipped yet. nullopt: no mag, more than one, or
// the one mag is already equipped (nothing to do).
inline std::optional<uint32_t> spire_equip_find_run_mag(const PlayerInventory& inventory) {
  std::optional<uint32_t> ret;
  size_t mags = 0;
  for (size_t z = 0; (z < inventory.num_items) && (z < inventory.items.size()); z++) {
    const auto& item = inventory.items[z];
    if (!item.data.empty() && (item.data.data1[0] == 0x02)) {
      mags++;
      ret = item.is_equipped() ? std::nullopt : std::optional<uint32_t>(item.data.id.load());
    }
  }
  return (mags == 1) ? ret : std::nullopt;
}

// The item id to send SpireEquipItem for at 'C' point (13 or 14; low = the code's low byte), or nullopt: another
// point, r15 = 0 for this client (available false), or no such item in newserv's copy of its inventory.
inline std::optional<uint32_t> spire_equip_target(
    uint8_t point, uint8_t low, bool available, const PlayerInventory& inventory) {
  if (!available) {
    return std::nullopt;
  }
  if (point == SPIRE_PROMPT_KIT_GROUND) {
    return spire_equip_find_kit_weapon(inventory, low);
  }
  if (point == SPIRE_PROMPT_RUN_MAG_REPLACED) {
    return spire_equip_find_run_mag(inventory);
  }
  return std::nullopt;
}
