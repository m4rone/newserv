#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <memory>
#include <phosg/JSON.hh>
#include <string>
#include <vector>

#include "ItemData.hh"
#include "PlayerInventory.hh"
#include "PlayerSubordinates.hh"

class Client;
class Lobby;

// Inventory backups (regie pso-regie-10, Marck 2026-10-25: security in the server, after Wakiru lost 14 items to
// Sega's own penalty for a game that did not end cleanly: the q903 freeze of 13:35).
//
// Save: every time a client finishes joining a game (6F), the server writes the character's inventory, meseta, level
// and name to system/inventory-backups/<serial>.<character id>/<time>.json (character id: spire_guard_character_id, a
// hash of name, class and section ID) and keeps the newest INVENTORY_BACKUP_KEEP files of that character. It saves for
// every character, protected ones (SpireBlockedCharacters, e.g. Wakiru) included: saving only reads the character. The
// end of that game is written into the same file: a 98 (the client left the game and saves its card) sets
// LeftNormally; a disconnect while still in the game writes Exit, the server's own view of the inventory at that moment
// (it already applied every drop, trade, sale and use the server saw). The server never restores on its own.
//
// $restore (and "$restore check", which only lists): compares the newest backup of the typing player's own character
// made before the current game with his current inventory and drops what is missing at his feet, visible only to him.
// The rule (B-E10 decision log): only a backup whose game ended without a 98 (Exit present, or a backup imported from a
// guard snapshot) is restorable; a game that ended normally saved the card, so there is nothing Sega took. Refused
// outside a game, in a Spire run, for a backup from a Spire run (the guard restores those), and a second time for the
// same backup (Restored mark). Protected characters are allowed (regie: Marck must be able to restore Wakiru). What the
// server saw leave the inventory since the current game began (drops, trades, sales, bank, use, mag feed, shop meseta)
// is not counted as missing. Restored items get fresh item ids from the game (Lobby::generate_item_id).

constexpr const char* INVENTORY_BACKUP_DIRECTORY = "system/inventory-backups";
constexpr size_t INVENTORY_BACKUP_KEEP = 20;

struct InventoryBackup {
  uint32_t serial_number = 0;
  std::string character_id;
  uint64_t time_usecs = 0;
  uint32_t quest_number = 0;
  bool spire = false;
  bool left_normally = false;
  bool restored = false;
  uint64_t restored_time_usecs = 0;
  std::string imported_from; // the guard snapshot this backup was made from (spire-backup-import), else empty
  PlayerInventory inventory; // at the game join
  PlayerDispDataV4 disp;
  bool has_exit = false;
  PlayerInventory exit_inventory;
  uint32_t exit_meseta = 0;
};

phosg::JSON inventory_backup_to_json(const InventoryBackup& b);
InventoryBackup inventory_backup_from_json(const phosg::JSON& json);
std::string inventory_backup_character_directory(const std::string& root, uint32_t serial_number, const PlayerDispDataV4& disp);
// "20261025-130741-123456.json" (UTC): sorts by name in time order
std::string inventory_backup_basename_for_time(uint64_t usecs);
// The *.json files of dir, oldest first (by name); empty if dir does not exist
std::vector<std::string> inventory_backup_list(const std::string& dir);
// The names (from a list sorted oldest first) to delete so only the newest keep remain
std::vector<std::string> inventory_backup_files_to_delete(const std::vector<std::string>& sorted_names, size_t keep);
// Writes path atomically (temp file, fsync, rename)
void inventory_backup_write_file(const std::string& path, const InventoryBackup& b);
// Writes a new backup into dir, deletes all but the newest keep, and returns the path written
std::string inventory_backup_save(const std::string& dir, const InventoryBackup& b, size_t keep);
// The index of the newest name that is not current_basename, or -1
ssize_t inventory_backup_pick_previous(const std::vector<std::string>& sorted_names, const std::string& current_basename);

// What left the inventory in ways the server saw, since the current game began
struct InventoryRemovals {
  std::vector<ItemData> items;
  uint64_t meseta = 0;
};
void inventory_removals_add(InventoryRemovals& removals, const ItemData& item);

struct InventoryRestorePlan {
  std::vector<ItemData> items; // ids are 0xFFFFFFFF; the game assigns fresh ones when they are dropped
  uint32_t meseta = 0;
  bool empty() const {
    return this->items.empty() && (this->meseta == 0);
  }
};
// The items and meseta of source that are neither in current nor in removed. Items match exactly (all bytes but the id;
// tool stacks by count) first, then a weapon, armor or unit by its type (data1[0-2]) and a mag by data1[0-1], so a
// ground, tekked or fed item still counts as present.
InventoryRestorePlan inventory_restore_missing(const PlayerInventory& source, uint32_t source_meseta,
    const PlayerInventory& current, uint32_t current_meseta, const InventoryRemovals& removed);

enum class InventoryRestoreRefusal {
  NONE = 0,
  NOT_IN_GAME,
  SPIRE_GAME,
  NO_BACKUP,
  OTHER_CHARACTER,
  SPIRE_BACKUP,
  ALREADY_RESTORED,
  LEFT_NORMALLY,
  UNKNOWN_END,
};
// backup: the newest backup of this character made before the current game, or null. protected_character does not
// refuse (regie pso-regie-10: Wakiru may restore); it is a parameter so the static test pins that.
InventoryRestoreRefusal inventory_restore_refusal(bool in_game, bool spire_game, bool protected_character,
    uint32_t serial_number, const std::string& character_id, const InventoryBackup* backup);
const char* inventory_restore_refusal_text(InventoryRestoreRefusal r);
// After the refusals: in check mode returns nothing to drop and leaves the backup as it is; otherwise marks the backup
// restored (once per backup) and returns plan
InventoryRestorePlan inventory_restore_take(InventoryBackup& backup, const InventoryRestorePlan& plan, bool check_only,
    uint64_t now_usecs);
const char* inventory_restore_refusal_name(InventoryRestoreRefusal r);

// Server hooks
void inventory_backup_on_game_join(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l);
// normal: the client sent 98; otherwise it dropped out of the game (disconnect)
void inventory_backup_on_game_end(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, bool normal);
void inventory_backup_note_removed(std::shared_ptr<Client> c, const ItemData& item);
void inventory_backup_note_meseta_removed(std::shared_ptr<Client> c, uint64_t meseta);
// $restore / $restore check: returns the lines to show the player
std::vector<std::string> inventory_restore_command(std::shared_ptr<Client> c, bool check_only);

// CLI spire-backup-import: turns a SpireGuardSnapshot file into a restorable backup (Imported, time = now_usecs) in
// root/<serial>.<character id>/; returns the path written
std::string inventory_backup_import_snapshot(const std::string& snapshot_filename, const std::string& root, uint64_t now_usecs);
