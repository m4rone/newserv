#pragma once

#include <stdint.h>

#include <chrono>
#include <optional>
#include <string>

#include "PlayerInventory.hh"

// Route B bundle 9 (B-REGIE-16-15; contract: pso-roguelike quests/spire/ROUTEB9-PROTOCOL.md § 1 and § 3): server equip.
// The client function SpireEquipItem (GC EU v1.0 only) gets one item id, finds that item in the player's own live
// inventory and equips it through the game's own Item Pack "Equip" routine, so the client sends its own 6x25 and
// newserv's copy of the inventory stays the client's. newserv tells the quest in r143 (SPIRE_EQUIP_REGISTER, every 'M'
// answer) whether it can do that for this client; with r143 = 1 the quest makes the kit weapon with its grind and sends
// 'C' 13 (low byte = grind G), and after a run mag evolution 'C' 14; newserv answers each by sending the function with
// the item id of the kit weapon or of the one run mag in its own copy of the inventory.
//
// Bundle 9 state (2026-10-27, ROUTEB9-NEWSERV-F3-REPORT.md): system/client-functions/SpireEquipItemGC.s exists for
// 3OP0, built only from the addresses proven statically in ROUTEB9-NEWSERV-F2-REPORT.md. With the switch on, r143 is 1
// for a 3OP0 client that runs function calls. Whether the client handles the call correctly at run time is decided by
// the lab check, not by this tree.

constexpr uint32_t SPIRE_EQUIP_ITEM_SPECIFIC_VERSION = 0x334F5030; // "3OP0"
// The function's .meta key, in the form pso-roguelike's quests/spire/pc-build.sh reads: it puts every
// SPIRE_<X>_FUNCTION_NAME = "<name>" into client-functions/required.txt and refuses the build when no Spire*.s carries
// that key (contract ROUTEB9-PROTOCOL.md § 3).
constexpr const char* SPIRE_EQUIP_FUNCTION_NAME = "SpireEquipItem";
// The label of the .s that receives the item id (send_function_call label_writes)
constexpr const char* SPIRE_EQUIP_ITEM_ID_LABEL = "item_id";
// r143, not r15: every quest version (also the live bundle 8.1) keeps its resume "asked" flag in r15, so an r15 in
// the 'M' answer would make an older quest skip its resume question. r143 is used by no quest version (bundle 8.1,
// bundle 9, the rewards track), by no fixed-register opcode in them, and is outside every register newserv writes or
// stores (r1-r14, r20-r58, r105-r107, r181, the replay registers, r232-r239/r243-r248, r251-r253; Schtserv r240/r241).
constexpr uint8_t SPIRE_EQUIP_REGISTER = 143; // r143: 1 = this client can have an item equipped by the server, else 0
constexpr uint8_t SPIRE_PROMPT_KIT_GROUND = 13; // 'C' 13: the kit weapon was made with grind G (low byte), not equipped
constexpr uint8_t SPIRE_PROMPT_RUN_MAG_REPLACED = 14; // 'C' 14: the run mag was replaced after an evolution

constexpr bool spire_equip_item_applies(uint32_t specific_version) {
  return specific_version == SPIRE_EQUIP_ITEM_SPECIFIC_VERSION;
}

// r143 for a client: its version is 3OP0, the switch SpireEquipItemPatch is on, it can run client functions (B2 runs
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

// kwaliteit-2 r2 (critic K3): a mag reminder ("... equip it") when it goes, a gap or more after the NO_MAG or RESULT
// that asked for it: "" when newserv's copy shows a mag that is equipped by then, otherwise the text as planned.
inline std::string spire_run_mag_reminder_text_now(const std::string& text, const PlayerInventory& inventory) {
  for (size_t z = 0; (z < inventory.num_items) && (z < inventory.items.size()); z++) {
    const auto& item = inventory.items[z];
    if (!item.data.empty() && (item.data.data1[0] == 0x02) && item.is_equipped()) {
      return "";
    }
  }
  return text;
}

// Route B bundle 9 critic N-4: SpireEquipItem returns 1 also when the game refuses the 6x25 quietly (F2 § 6). newserv
// waits SPIRE_EQUIP_CHECK_DELAY after the send for that client's 6x25 (on_equip_item marks the item equipped in its
// copy); when the item is still there and not equipped, one line tells the player to equip it by hand.
constexpr auto SPIRE_EQUIP_CHECK_DELAY = std::chrono::milliseconds(1000);
constexpr const char* SPIRE_EQUIP_KIT_MISSING_TEXT = "Equip your kit weapon: Item Pack, Equip.";
constexpr const char* SPIRE_EQUIP_MAG_MISSING_TEXT = "Equip your mag: Item Pack, Mag.";
// kwaliteit-1 (observaties 209/216): newserv's own point for the weapon it equips again on a resume (not a 'C' code;
// the mag of a resume uses SPIRE_PROMPT_RUN_MAG_REPLACED's line), and that weapon's line when the 6x25 does not come
constexpr uint8_t SPIRE_EQUIP_RESUME_WEAPON = 0x80;
constexpr const char* SPIRE_EQUIP_WEAPON_MISSING_TEXT = "Equip your weapon: Item Pack, Equip.";
// The line for 'C' point after SpireEquipItem for item_id, or nullopt: the 6x25 came (the item is equipped), the item
// is no longer in newserv's copy, or another point.
inline std::optional<std::string> spire_equip_missing_text(uint8_t point, uint32_t item_id, const PlayerInventory& inventory) {
  if ((point != SPIRE_PROMPT_KIT_GROUND) && (point != SPIRE_PROMPT_RUN_MAG_REPLACED) &&
      (point != SPIRE_EQUIP_RESUME_WEAPON)) {
    return std::nullopt;
  }
  for (size_t z = 0; (z < inventory.num_items) && (z < inventory.items.size()); z++) {
    const auto& item = inventory.items[z];
    if (!item.data.empty() && (item.data.id == item_id)) {
      if (item.is_equipped()) {
        return std::nullopt;
      }
      return std::string((point == SPIRE_PROMPT_KIT_GROUND)     ? SPIRE_EQUIP_KIT_MISSING_TEXT
              : (point == SPIRE_EQUIP_RESUME_WEAPON) ? SPIRE_EQUIP_WEAPON_MISSING_TEXT
                                                     : SPIRE_EQUIP_MAG_MISSING_TEXT);
    }
  }
  return std::nullopt;
}

// The item id to send SpireEquipItem for at 'C' point (13 or 14; low = the code's low byte), or nullopt: another
// point, r143 = 0 for this client (available false), or no such item in newserv's copy of its inventory.
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
