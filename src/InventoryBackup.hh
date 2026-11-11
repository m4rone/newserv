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
class ServerState;

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
// $restore (and "$restore check", which only lists): compares a backup of the typing player's own character made
// before the current game with his current inventory and drops what is missing at his feet, visible only to him. The
// backup is the newest imported one not yet restored, else the newest one before the current game
// (inventory_backup_pick_restore).
// The rule (B-E10 decision log): only a backup whose game ended without a 98 (Exit present, or a backup imported from a
// guard snapshot) is restorable; a game that ended normally saved the card, so there is nothing Sega took. Refused
// outside a game, in a Spire run, for a backup from a Spire run (the guard restores those), and a second time for the
// same backup (Restored mark). Protected characters are allowed (regie: Marck must be able to restore Wakiru). What the
// server saw leave the inventory since the current game began (drops, trades, sales, bank, use, mag feed, shop meseta)
// is not counted as missing. Restored items get fresh item ids from the game (Lobby::generate_item_id).
//
// Hard ends (register item karakter-verlies-battle, 2026-10-25):
// - Spire: a backup is a Spire backup (refused by $restore) only if the Spire guard took its snapshot for this character
//   in that game (spire_guard_begin_run, inventory_backup_note_guarded); a q925 or other Spire-listed game without the
//   guard's snapshot stays restorable, because nothing else would put it back.
// - Server stop: SIGTERM/SIGINT (systemctl restart, apply-on-pc.sh) first writes Exit for every client still in a game
//   (inventory_backup_on_server_stop), then the process ends as before. A crash or SIGKILL writes none: that game's
//   backup stays without an end and $restore refuses it (UNKNOWN_END).
// - Next login: the 61 shows what the client kept. If the backup $restore would use (inventory_backup_pick_restore) ended
//   without a 98 and the 61 misses items or meseta of it, the server logs it, writes a Loss mark into that backup and
//   tells the player in the lobby to type $restore in a normal game (inventory_loss_verdict). It never restores itself.
// - Retention: an unrestored backup with a Loss mark or an import is never rotated out, and $restore picks it before
//   later join backups, so a burst of joins (a reconnect loop) cannot push the pre-loss state away.
//
// Interim state (B-REGIE-20-4, 2026-11-11: a hard server stop in the middle of a game left only the join inventory on
// disk and $restore refused it):
// - While the game runs, the backup carries Interim: the server's view of the real character's inventory and meseta at
//   one moment, with the time and the server process (Boot) that wrote it. It is written at the join, at once whenever
//   the server sees something leave the inventory (every inventory_backup_note_removed and
//   inventory_backup_note_meseta_removed: drop, trade, sale, bank, use, mag feed, shop meseta) and when the Spire guard
//   takes its snapshot (the Spire mark must be on disk before the run copy exists), and every
//   INVENTORY_BACKUP_INTERIM_USECS for everything else that changed (pick-ups and other gains). Every write is the whole
//   file: temp file, fsync, rename, fsync of the directory.
// - An interim state is always a state the inventory really was in. What left the inventory is never in it any more
//   (so a hard stop cannot give it back); what came in less than one interval before the stop may be missing from it.
// - The end of the game (Exit or a 98) removes Interim again: a game that ended leaves the same file as before.
// - After a hard stop (no Exit, no 98, and Boot is not the running server process) the interim state counts as the end
//   state: $restore uses it like an Exit and says so; the next login treats it like an Exit. A backup without Interim
//   (written by an older server, or lost before its first write) is refused as before (UNKNOWN_END). Interim of the
//   running process is never used: that game is still open or lost its end inside this process, as before.

constexpr const char* INVENTORY_BACKUP_DIRECTORY = "system/inventory-backups";
constexpr size_t INVENTORY_BACKUP_KEEP = 20;
// How long a change that only adds to the inventory may wait for its interim write (what a hard stop can cost at most)
constexpr uint64_t INVENTORY_BACKUP_INTERIM_USECS = 2000000;
// A backup whose Spire mark could not be written is renamed to <name>.json + this: inventory_backup_list skips it
constexpr const char* INVENTORY_BACKUP_SET_ASIDE_SUFFIX = ".unmarked";
// What $restore says first when it works from the interim state of a game that ended in a hard server stop
constexpr const char* INVENTORY_RESTORE_INTERIM_TEXT = "Stand van de laatste tussenstand\nvoor de harde serverstop.";
constexpr const char* INVENTORY_RESTORE_NOTHING_INTERIM_TEXT = "Niets kwijt ten opzichte van\nde laatste tussenstand.";

struct InventoryBackup {
  uint32_t serial_number = 0;
  std::string character_id;
  uint64_t time_usecs = 0;
  uint32_t quest_number = 0;
  bool spire = false; // the Spire guard took its snapshot for this character in this game (it restores the run)
  bool left_normally = false;
  bool restored = false;
  uint64_t restored_time_usecs = 0;
  std::string imported_from; // the guard snapshot this backup was made from (spire-backup-import), else empty
  PlayerInventory inventory; // at the game join
  PlayerDispDataV4 disp;
  bool has_exit = false;
  PlayerInventory exit_inventory;
  uint32_t exit_meseta = 0;
  // Loss mark (next login after a hard end): when the 61 first showed items or meseta of this backup missing
  uint64_t loss_time_usecs = 0;
  uint32_t loss_items = 0;
  uint32_t loss_meseta = 0;
  // Interim state (see above): only while the game has no end in the file
  bool has_interim = false;
  PlayerInventory interim_inventory;
  uint32_t interim_meseta = 0;
  uint64_t interim_time_usecs = 0;
  uint64_t interim_boot = 0; // inventory_backup_boot_id of the server process that wrote it
  uint32_t interim_count = 0; // how many interim states this game wrote, this one included
};

phosg::JSON inventory_backup_to_json(const InventoryBackup& b);
InventoryBackup inventory_backup_from_json(const phosg::JSON& json);
std::string inventory_backup_character_directory(const std::string& root, uint32_t serial_number, const PlayerDispDataV4& disp);
// "20261025-130741-123456.json" (UTC): sorts by name in time order
std::string inventory_backup_basename_for_time(uint64_t usecs);
// The *.json files of dir, oldest first (by name); empty if dir does not exist
std::vector<std::string> inventory_backup_list(const std::string& dir);
struct InventoryBackupPickFlags {
  bool imported = false; // ImportedFrom set
  bool restored = false; // Restored mark set
  bool loss = false; // Loss mark set (the next login saw items missing after a hard end)
};
// Whether rotation must keep this backup and $restore prefers it: imported or loss-marked, and not yet restored
bool inventory_backup_pinned(const InventoryBackupPickFlags& f);
// The names (from a list sorted oldest first) to delete so only the newest keep remain; a name whose flags (flags[z] for
// sorted_names[z], if present) are pinned is never deleted and does not count against keep
std::vector<std::string> inventory_backup_files_to_delete(const std::vector<std::string>& sorted_names, size_t keep,
    const std::vector<InventoryBackupPickFlags>& flags = {});
// Writes path so that a crash or power loss leaves either the old file or the new one: temp file in the same
// directory, fsync, rename, fsync of the directory (the pattern of spire_bridge_write_durable). Throws if the temp file
// cannot be written or synced; the old file is then untouched
void inventory_backup_write_file(const std::string& path, const InventoryBackup& b);
// Writes a new backup into dir, deletes all but the newest keep, and returns the path written
std::string inventory_backup_save(const std::string& dir, const InventoryBackup& b, size_t keep);
// The index of the newest name that is not current_basename, or -1
ssize_t inventory_backup_pick_previous(const std::vector<std::string>& sorted_names, const std::string& current_basename);
// The backup $restore (and $restore check) uses, from names sorted oldest first and flags[z] for sorted_names[z]
// (regie pso-regie-10): the newest pinned backup (imported or loss-marked, not yet restored) always wins over join
// backups, also over later ones; otherwise the newest name that is not current_basename. Never current_basename; -1 if
// nothing is left
ssize_t inventory_backup_pick_restore(const std::vector<std::string>& sorted_names,
    const std::vector<InventoryBackupPickFlags>& flags, const std::string& current_basename);

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
// interim_is_end: inventory_backup_interim_is_end of backup (its interim state stands in for the missing end).
InventoryRestoreRefusal inventory_restore_refusal(bool in_game, bool spire_game, bool protected_character,
    uint32_t serial_number, const std::string& character_id, const InventoryBackup* backup, bool interim_is_end = false);
const char* inventory_restore_refusal_text(InventoryRestoreRefusal r);
// After the refusals: in check mode returns nothing to drop and leaves the backup as it is; otherwise marks the backup
// restored (once per backup) and returns plan
InventoryRestorePlan inventory_restore_take(InventoryBackup& backup, const InventoryRestorePlan& plan, bool check_only,
    uint64_t now_usecs);
const char* inventory_restore_refusal_name(InventoryRestoreRefusal r);

// The end of a game, written into its backup. normal: a 98. guard_covered: the Spire guard took its snapshot for this
// character in this game (it restores the run itself). Otherwise inventory (the server's view at the end, null if
// unknown) becomes Exit. A recorded end (98 or Exit) removes the interim state
void inventory_backup_record_end(InventoryBackup& b, bool normal, bool guard_covered, const PlayerInventory* inventory,
    uint32_t meseta);

// Interim state (see the top of this file)
// This server process, as written into Interim.Boot: its start time in microseconds, never 0
uint64_t inventory_backup_boot_id();
// Whether the interim state of b stands in for the end of its game: the file has no end (no Exit, no 98) and the
// process that wrote the interim state is not boot_id, so no end can come any more (a hard stop)
bool inventory_backup_interim_is_end(const InventoryBackup& b, uint64_t boot_id);
// The state $restore and the login check compare the character with: Exit, else the interim state after a hard stop,
// else the join inventory (an import, or a game whose older connection is still open)
struct InventoryBackupSource {
  enum class Kind {
    JOIN = 0,
    EXIT,
    INTERIM,
  };
  Kind kind = Kind::JOIN;
  const PlayerInventory* inventory = nullptr;
  uint32_t meseta = 0;
};
InventoryBackupSource inventory_backup_restore_source(const InventoryBackup& b, uint64_t boot_id);
// Puts inventory and meseta into b as its interim state and the guard mark into Spire (never back to false); count is
// the number of this write within its game
void inventory_backup_record_interim(InventoryBackup& b, const PlayerInventory& inventory, uint32_t meseta,
    bool guard_covered, uint64_t now_usecs, uint64_t boot_id, uint32_t count);
// What one open backup last put on disk as its interim state; the server keeps one per client, a static test one per
// simulated game
struct InventoryInterimState {
  bool written = false;
  PlayerInventory inventory;
  uint32_t meseta = 0;
  bool guard_covered = false;
  uint32_t count = 0;
  bool failed = false; // the last write failed (logged once per streak, retried at every later call)
};
// Whether inventory, meseta and the guard mark differ from what state last wrote (true if it wrote nothing yet)
bool inventory_backup_interim_changed(const InventoryInterimState& state, const PlayerInventory& inventory,
    uint32_t meseta, bool guard_covered);
// Writes the interim state into the backup at path if it changed (inventory_backup_interim_changed) and returns
// whether it wrote. It reads the file first, so the marks others wrote into it (Loss) stay, and never writes into a
// backup that already has its end. Throws if the file cannot be read or written; state and the file are then as before
bool inventory_backup_write_interim(const std::string& path, InventoryInterimState& state,
    const PlayerInventory& inventory, uint32_t meseta, bool guard_covered, uint64_t now_usecs, uint64_t boot_id);

// What the next login does with the backup $restore would use, given what is missing from it in the 61.
// session_alive: an older connection of this character is still in that game (its Exit is still to come).
enum class InventoryLossVerdict {
  NONE = 0, // nothing to say: nothing missing, a 98, already restored, or the guard restores it
  RESTORABLE, // tell the player to type $restore in a normal game
  SERVER_STOP, // lost, but the game ended without any end state (crash or SIGKILL): $restore refuses it
};
// interim_is_end: inventory_backup_interim_is_end of b (missing_* are then counted against the interim state).
InventoryLossVerdict inventory_loss_verdict(const InventoryBackup& b, bool session_alive, size_t missing_items,
    uint32_t missing_meseta, bool interim_is_end = false);
// The lobby text for a verdict (empty for NONE); uses %s for a literal $ (add_color)
std::string inventory_loss_text(InventoryLossVerdict v, size_t missing_items, uint32_t missing_meseta);

// Server hooks
void inventory_backup_on_game_join(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l);
// normal: the client sent 98; otherwise it dropped out of the game (disconnect)
void inventory_backup_on_game_end(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, bool normal);
// The Spire guard stored its snapshot for c in its current game (spire_guard_begin_run)
void inventory_backup_note_guarded(std::shared_ptr<Client> c);
// After the 61 of a login: the hard-end check above (logs, Loss mark, lobby text). Never throws
void inventory_backup_on_login(std::shared_ptr<Client> c);
// SIGTERM/SIGINT: writes Exit for every client still in a game; returns how many
size_t inventory_backup_on_server_stop(std::shared_ptr<ServerState> s);
// Both also write the interim state at once if the inventory already shows the removal, and mark the client so
// inventory_backup_after_command writes it if the removal follows later in the same command
void inventory_backup_note_removed(std::shared_ptr<Client> c, const ItemData& item);
void inventory_backup_note_meseta_removed(std::shared_ptr<Client> c, uint64_t meseta);
// Writes the interim state of c's current game now if it changed. Never throws (a failure is logged)
void inventory_backup_flush_interim(std::shared_ptr<Client> c);
// Renames the backup at path out of the *.json list (INVENTORY_BACKUP_SET_ASIDE_SUFFIX); returns whether that worked
bool inventory_backup_set_aside(const std::string& path);
// After every command of c (on_command): writes the interim state if something left the inventory in that command
void inventory_backup_after_command(std::shared_ptr<Client> c);
// $restore / $restore check: returns the lines to show the player
std::vector<std::string> inventory_restore_command(std::shared_ptr<Client> c, bool check_only);

// CLI spire-backup-import: turns a SpireGuardSnapshot file into a restorable backup (Imported, time = now_usecs) in
// root/<serial>.<character id>/; returns the path written
std::string inventory_backup_import_snapshot(const std::string& snapshot_filename, const std::string& root, uint64_t now_usecs);
