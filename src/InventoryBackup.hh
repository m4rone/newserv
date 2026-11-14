#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <memory>
#include <phosg/JSON.hh>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ItemData.hh"
#include "Map.hh"
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
// - That preference ends once the player played on (critic B2, 2026-11-11): a later game, other than the current
//   one, that ended with a 98 or whose end state differs from its join; a guarded Spire game counts like any other
//   (round 2, B1: in the base after the run he banks, sells and hands over as the real character). What he sold, gave
//   away or used there would come back. $restore then says so (SUPERSEDED) instead of silently working from that old state, and the login
//   check no longer tells him to type $restore for it.
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
// - An interim state is always a state the inventory of that one player really was in. What left his inventory is
//   never in it any more (so a hard stop cannot give it back to him); what came in less than one interval before the
//   stop may be missing from it. Across players that is not enough: what one player gave away is in the other's
//   interim state, and the giver's memory card may still hold it. Hence provenance, below.
// - The end of the game (Exit or a 98) removes Interim again: a game that ended leaves the same file as before.
// - After a hard stop (no Exit, no 98, and Boot is not the running server process) the interim state counts as the end
//   state: $restore uses it like an Exit and says so; the next login treats it like an Exit. A backup without Interim
//   (written by an older server, or lost before its first write) is refused as before (UNKNOWN_END). Interim of the
//   running process is never used: that game is still open or lost its end inside this process, as before.
// - Only a server process that died with the game open leaves a backup without an end (critic M2). Whenever a living
//   process gives a backup up without writing its end, it sets it aside as LOST instead: the end could not be read or
//   written, the client's next join came while the backup was still open, the client left without ever being in the
//   game's lobby again, or the server stops (SIGTERM) while the backup belongs to no client in a game. So a later
//   process never reads a game that in truth ended (perhaps with a 98, card saved) as a hard stop. If that rename
//   fails as well it is an error line; that backup then does read as a hard stop after a restart. A join backup that
//   was written is always tracked, also when rotating out older ones fails right after (critic round 2, B2).
// - A write that fails (full disk, I/O error) after something left the inventory leaves an interim state on disk that
//   still holds it; and it stays that way for as long as the disk refuses, whatever he uses, sells or gives away
//   (critic B3). The backup is then set aside as LOST at once (a rename, see InventoryBackupSetAside) and this game
//   has no backup any more: $restore refuses it (NOT_UPDATED), as it refused every hard stop before interim states
//   existed. If the rename fails too, that is logged as an error and every later change tries both again. A write
//   that fails with nothing having left the inventory only delays what came in: the state on disk is older, not wrong.
//
// Provenance (kwaliteit-3 critic B1, 2026-11-11: A drops an item he had at the join, B picks it up, hard stop; A's card
// still holds it, B's interim state holds it too, and $restore gave it to B a second time):
// - After a hard stop $restore does not work from the interim state itself but from the part of it that is proven the
//   player's own (inventory_backup_own_state): what he had at the join and never lost, and what came in during the
//   game from a source the server itself vouches for. Everything else that came in is left out: from another player
//   (a floor item a player dropped, a trade) or of unknown origin (6x2B: bank, tekker, quest; a shop purchase, paid
//   with meseta whose origin is not proven; anything the server did not see arrive).
// - What the server vouches for: a floor item it made itself for an enemy or box (server drop modes), a floor item the
//   game's leader announced (6x5F) for an enemy or box whose drop request the server had seen and marked, once per
//   enemy or box, a $restore drop, and a floor item this same player dropped himself (6x2A) and picks up again.
// - The server keeps this per client while the game runs (InventoryProvenance): per item id how many units are not
//   proven. It compares the inventory with the last one it saw after every subcommand; a gain nobody vouched for is
//   not proven. Units that leave a stack are counted as the proven ones first.
// - On disk the interim state carries Own: the item ids whose proven units differ from the default rule, with their
//   proven units, and OwnMeseta. The default rule is what a reader without any provenance may assume: an item is the
//   player's own if the join inventory holds the same id with the same type, up to the units it had there; meseta up
//   to the join meseta. An interim state without Own (written before provenance existed) is not an end state:
//   refused as before (UNKNOWN_END).
// - The same rule holds for an Exit (critic M4: after a dropped connection the Exit of the receiver held what the
//   giver's card still holds). An Exit carries Own and OwnMeseta only where they differ from the default rule, so the
//   file of a game without proven gains is byte for byte what it was; an Exit of before provenance reads by the
//   default rule, which gives back what he joined with and still had, and nothing he got in that game.

constexpr const char* INVENTORY_BACKUP_DIRECTORY = "system/inventory-backups";
constexpr size_t INVENTORY_BACKUP_KEEP = 20;
// How long a change that only adds to the inventory may wait for its interim write (what a hard stop can cost at most)
constexpr uint64_t INVENTORY_BACKUP_INTERIM_USECS = 2000000;
// What $restore says first when it works from the interim state of a game that ended in a hard server stop
constexpr const char* INVENTORY_RESTORE_INTERIM_TEXT = "Stand van de laatste tussenstand\nvoor de harde serverstop.";
// Critic round 2, M3: after a hard stop or an Exit only what is proven his own comes back, so "nothing lost" is not
// true when the state held something of another player; the texts say what is left out on purpose
constexpr const char* INVENTORY_RESTORE_NOTHING_INTERIM_TEXT = "Niets terug te leggen uit\nde laatste tussenstand.";
constexpr const char* INVENTORY_RESTORE_NOTHING_EXIT_TEXT = "Niets terug te leggen uit\nde back-up.";
constexpr const char* INVENTORY_RESTORE_OWN_ONLY_TEXT =
    "Wat je van een ander kreeg,\nkocht of uit de bank haalde,\nen Meseta komen niet terug.";

// A backup that must never be restored from is set aside by renaming it, not by rewriting it (a rename in the same
// directory needs no room, so it also works when the content can no longer be written): <time>.json becomes
// <time><suffix>. The name still ends in .json, so the backup stays in inventory_backup_list at its place in time:
// $restore sees it as that game and refuses it, and never silently works from an older game instead.
// LOST (critic B3): something left the inventory and the interim state could not be written after it, so what is on
// disk still holds it. Also (critic M2): the game ended inside a living server process and its end is not in the file.
// UNMARKED (critic M1): the Spire guard took its snapshot in this game and the Spire mark could not be written into
// the backup. The name is the mark now: the backup reads as a Spire backup (the guard restores that game), also if
// its content cannot be read
enum class InventoryBackupSetAside {
  NONE = 0,
  LOST,
  UNMARKED,
};
constexpr const char* INVENTORY_BACKUP_LOST_SUFFIX = ".lost.json";
constexpr const char* INVENTORY_BACKUP_UNMARKED_SUFFIX = ".unmarked.json";
// The kind a backup's file name says
InventoryBackupSetAside inventory_backup_set_aside_kind(const std::string& name);
// Renames the backup at path (<...>.json) to <...><suffix>; returns the new path, or "" if the rename failed
std::string inventory_backup_set_aside_as(const std::string& path, const char* suffix);

// What of an end state (interim state or Exit) is proven the player's own, as differences from the default rule (see
// Provenance above)
struct InventoryOwnership {
  bool present = false; // false: written without provenance
  std::vector<std::pair<uint32_t, uint32_t>> units; // item id and its proven units, sorted by id
  bool has_meseta = false;
  uint32_t meseta = 0; // the proven meseta, if has_meseta
};

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
  InventoryOwnership exit_own; // what of the Exit is proven the player's own (see Provenance above)
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
  InventoryOwnership interim_own; // what of the interim state is proven the player's own (see Provenance above)
  // From the file name, never in the file: inventory_backup_read_all sets it
  InventoryBackupSetAside set_aside = InventoryBackupSetAside::NONE;
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
  // The player played on in a later game that is not the current one (inventory_backup_played_on): a hard-end backup
  // is then no longer what he lost (critic B2). Set by inventory_backup_read_all
  bool superseded = false;
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
// Writes a new backup into dir, deletes all but the newest keep, and returns the path written. Throws only if the
// backup itself was not written; a rotation that fails afterwards is reported in rotate_error (critic round 2, B2)
std::string inventory_backup_save(const std::string& dir, const InventoryBackup& b, size_t keep,
    std::string* rotate_error = nullptr);
// Deletes all but the newest keep backups in dir (inventory_backup_files_to_delete). Never throws: the error, or ""
std::string inventory_backup_rotate(const std::string& dir, size_t keep) noexcept;
// The index of the newest name that is not current_basename, or -1
ssize_t inventory_backup_pick_previous(const std::vector<std::string>& sorted_names, const std::string& current_basename);
// The backup $restore (and $restore check) uses, from names sorted oldest first and flags[z] for sorted_names[z]
// (regie pso-regie-10): the newest pinned backup (imported or loss-marked, not yet restored) always wins over join
// backups, also over later ones; otherwise the newest name that is not current_basename. Never current_basename; -1 if
// nothing is left. A loss-marked backup that is superseded is not pinned any more (an imported one stays: importing is
// the operator's own decision)
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
  SUPERSEDED, // a hard-end backup with a Loss mark exists, but the player played on after it (critic B2)
  NOT_UPDATED, // set aside as LOST: what is on disk misses a removal (critic B3)
};
// backup: the newest backup of this character made before the current game, or null. protected_character does not
// refuse (regie pso-regie-10: Wakiru may restore); it is a parameter so the static test pins that.
// interim_is_end: inventory_backup_interim_is_end of backup (its interim state stands in for the missing end).
InventoryRestoreRefusal inventory_restore_refusal(bool in_game, bool spire_game, bool protected_character,
    uint32_t serial_number, const std::string& character_id, const InventoryBackup* backup, bool interim_is_end = false);
const char* inventory_restore_refusal_text(InventoryRestoreRefusal r);
// What $restore says for a source of this kind (InventoryBackupSource::Kind as int): when nothing is missing, and the
// line on what it leaves out (null for an import, which gives back its own state)
struct InventoryRestoreTexts {
  const char* nothing = "";
  const char* own_only = nullptr;
};
InventoryRestoreTexts inventory_restore_texts(int source_kind);
// Whether the player played on in the game of b, so that an older hard-end backup no longer describes what he lost: it
// ended with a 98, or its end state (Exit, else its interim state) differs from its join state in items or meseta, or
// it has no end state at all, or it cannot be read (null), or it was set aside (LOST, UNMARKED: nothing was written
// into it after that). A join that changed nothing (a reconnect loop) does not count. A game the Spire guard covered is
// no exception (critic round 2, B1): the real character plays in that game before the run and after the return to the
// base, so its 98 counts; only a guarded game that ended without a 98 and in which the server saw the real character
// end as it joined (a run that never changed it) does not count, by the same rule as every other game
bool inventory_backup_played_on(const InventoryBackup* b);
// Every backup of one character as $restore and the login check see it: names oldest first, backups[z] (null if
// unreadable) and flags[z] for names[z]. superseded is set for every backup with a later one, other than
// current_basename, that the player played on in
struct InventoryBackupListing {
  std::vector<std::string> names;
  std::vector<std::unique_ptr<InventoryBackup>> backups;
  std::vector<InventoryBackupPickFlags> flags;
  std::vector<std::string> unreadable; // "<name>: <why>"
};
InventoryBackupListing inventory_backup_read_all(const std::string& dir, const std::string& current_basename);
// What $restore does for a character whose backups are in dir, in the server process boot_id, in the game whose backup
// is current_basename: the backup it is about, the refusal, and what is missing from it in current (the same steps
// for $restore, $restore check and the static tests)
struct InventoryRestoreDecision {
  InventoryRestoreRefusal refusal = InventoryRestoreRefusal::NONE;
  std::string path; // empty if there is no backup
  std::unique_ptr<InventoryBackup> backup;
  bool interim_is_end = false;
  int source_kind = 0; // InventoryBackupSource::Kind
  InventoryRestorePlan plan; // empty if refused
  std::vector<std::string> unreadable;
};
InventoryRestoreDecision inventory_restore_decide(const std::string& dir, const std::string& current_basename,
    uint64_t boot_id, bool in_game, bool spire_game, bool protected_character, uint32_t serial_number,
    const std::string& character_id, const PlayerInventory& current, uint32_t current_meseta,
    const InventoryRemovals& removed);
// After the refusals: in check mode returns nothing to drop and leaves the backup as it is; otherwise marks the backup
// restored (once per backup) and returns plan
InventoryRestorePlan inventory_restore_take(InventoryBackup& backup, const InventoryRestorePlan& plan, bool check_only,
    uint64_t now_usecs);
const char* inventory_restore_refusal_name(InventoryRestoreRefusal r);

// The end of a game, written into its backup. normal: a 98. guard_covered: the Spire guard took its snapshot for this
// character in this game (it restores the run itself). Otherwise inventory (the server's view at the end, null if
// unknown) becomes Exit. A recorded end (98 or Exit) removes the interim state. own: what of the Exit is proven the
// player's own; null proves nothing beyond the default rule
void inventory_backup_record_end(InventoryBackup& b, bool normal, bool guard_covered, const PlayerInventory* inventory,
    uint32_t meseta, const InventoryOwnership* own = nullptr);

// Interim state (see the top of this file)
// This server process, as written into Interim.Boot: its start time in microseconds, never 0
uint64_t inventory_backup_boot_id();
// Whether the interim state of b stands in for the end of its game: the file has no end (no Exit, no 98), the
// process that wrote the interim state is not boot_id, so no end can come any more (a hard stop), and the interim
// state says what of it is proven the player's own
bool inventory_backup_interim_is_end(const InventoryBackup& b, uint64_t boot_id);

// Provenance (see the top of this file). What the server knows, while a game runs, about where the units in one
// client's inventory came from
struct InventoryProvenance {
  std::unordered_map<uint32_t, uint32_t> seen; // item id -> units at the last reconcile
  std::unordered_map<uint32_t, uint32_t> foreign; // item id -> units of it that are not proven the player's own
  std::unordered_map<uint32_t, uint32_t> credit; // item id -> proven units that came in since the last reconcile
  uint32_t seen_meseta = 0;
  uint32_t foreign_meseta = 0;
  uint32_t credit_meseta = 0;
};
// The units of one inventory slot: the stack size of a tool stack, the amount of a meseta item, else 1
uint32_t inventory_provenance_units(const ItemData& item);
// The join: everything in the inventory is the player's own
void inventory_provenance_begin(InventoryProvenance& prov, const PlayerInventory& inventory, uint32_t meseta);
// The server vouches for units of item (meseta if it is a meseta item) that inventory holds now: under the item's own
// id, or for a tool that joined a stack, under that stack's. Call inventory_provenance_reconcile right after
void inventory_provenance_credit(InventoryProvenance& prov, const PlayerInventory& inventory, const ItemData& item,
    uint32_t units);
// Compares the inventory with the one of the last reconcile: every unit that came in without credit is foreign, and
// foreign units never exceed what the slot still holds (units that leave are counted as the proven ones first)
void inventory_provenance_reconcile(InventoryProvenance& prov, const PlayerInventory& inventory, uint32_t meseta);
// How many of the units of item, as the last reconcile saw it in the inventory, are proven
uint32_t inventory_provenance_own_units(const InventoryProvenance& prov, const ItemData& item);
// The proven units of a partial stack drop (6x5D, before its 6x29): Meseta up to what of it is proven, a tool up to the
// proven units of the stack it comes from (critic round 2, M2)
uint32_t inventory_provenance_split_own_units(const InventoryProvenance& prov, const PlayerInventory& inventory,
    const ItemData& item);
// prov (reconciled with end) as it goes to disk: only what differs from the default rule against join
InventoryOwnership inventory_provenance_ownership(const InventoryProvenance& prov, const PlayerInventory& join,
    uint32_t join_meseta, const PlayerInventory& end, uint32_t end_meseta);
// The part of end that is proven the player's own: per item id the units own names, else the default rule (the join
// inventory holds the same id with the same type: up to its units there). own_meseta: own's meseta, else
// min(join_meseta, end_meseta); never more than end_meseta
PlayerInventory inventory_backup_own_state(const PlayerInventory& join, uint32_t join_meseta, const PlayerInventory& end,
    uint32_t end_meseta, const InventoryOwnership& own, uint32_t& own_meseta);

// What the server itself knows about where a floor item came from; every Lobby::FloorItem carries one. The default is
// "nothing": whoever picks that item up has an item of unknown origin
struct InventoryFloorOrigin {
  bool world = false; // the server vouches for it (see Provenance at the top of this file)
  std::weak_ptr<Client> dropper; // the player whose own drop (6x2A) put it on the floor
  uint32_t dropper_own = 0; // how many of its units were proven the dropper's own at that moment
};

// The state $restore and the login check compare the character with: the proven part (inventory_backup_own_state) of
// the Exit, else of the interim state after a hard stop, else the join inventory (an import, or a game whose older
// connection is still open)
struct InventoryBackupSource {
  enum class Kind {
    JOIN = 0,
    EXIT,
    INTERIM,
  };
  Kind kind = Kind::JOIN;
  PlayerInventory inventory;
  uint32_t meseta = 0;
};
InventoryBackupSource inventory_backup_restore_source(const InventoryBackup& b, uint64_t boot_id);
// The Meseta $restore and the login check work from (critic round 2, M1): none after a hard stop or an Exit. On the
// GameCube a Meseta deposit in the bank reaches the server by no command the source knows of (6xBB/6xBD are BB and
// Ep3 only), so the server's Meseta may already sit in the bank on the card; giving it back could make it twice. An
// import keeps its Meseta. from.meseta itself stays the proven own Meseta
uint32_t inventory_backup_restore_meseta(const InventoryBackupSource& from);
// Puts inventory and meseta into b as its interim state and the guard mark into Spire (never back to false); count is
// the number of this write within its game. own: what of it is proven the player's own
void inventory_backup_record_interim(InventoryBackup& b, const PlayerInventory& inventory, uint32_t meseta,
    bool guard_covered, uint64_t now_usecs, uint64_t boot_id, uint32_t count, const InventoryOwnership& own);
// What one open backup last put on disk as its interim state; the server keeps one per client, a static test one per
// simulated game
struct InventoryInterimState {
  bool written = false;
  PlayerInventory inventory;
  uint32_t meseta = 0;
  bool guard_covered = false;
  uint32_t count = 0;
  bool failed = false; // the last write failed (logged once per streak, retried at every later call)
  // A write failed after something left the inventory and none succeeded since: what is on disk still holds it
  bool removal_unwritten = false;
  bool set_aside_failed = false; // and the backup could not be set aside either (logged once per streak)
};
// Whether inventory, meseta and the guard mark differ from what state last wrote (true if it wrote nothing yet)
bool inventory_backup_interim_changed(const InventoryInterimState& state, const PlayerInventory& inventory,
    uint32_t meseta, bool guard_covered);
// Writes the interim state into the backup at path if it changed (inventory_backup_interim_changed) and returns
// whether it wrote. It reads the file first, so the marks others wrote into it (Loss) stay, and never writes into a
// backup that already has its end. Throws if the file cannot be read or written; state and the file are then as before.
// prov: the provenance of inventory, reconciled with it; null proves nothing beyond the default rule
bool inventory_backup_write_interim(const std::string& path, InventoryInterimState& state,
    const PlayerInventory& inventory, uint32_t meseta, bool guard_covered, uint64_t now_usecs, uint64_t boot_id,
    const InventoryProvenance* prov = nullptr);

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
// normal: the client sent 98; otherwise it dropped out of the game (disconnect). l: the lobby it is in, may be null.
// A backup that is open and gets no end here (no game lobby and no 98, or the end cannot be read or written) is set
// aside as LOST
void inventory_backup_on_game_end(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, bool normal);
// The Spire guard stored its snapshot for c in its current game (spire_guard_begin_run)
void inventory_backup_note_guarded(std::shared_ptr<Client> c);
// After the 61 of a login: the hard-end check above (logs, Loss mark, lobby text). Never throws
void inventory_backup_on_login(std::shared_ptr<Client> c);
// SIGTERM/SIGINT: writes Exit for every client still in a game and returns how many; every other backup that is
// still open is set aside as LOST
size_t inventory_backup_on_server_stop(std::shared_ptr<ServerState> s);
// Both also write the interim state at once if the inventory already shows the removal, and mark the client so
// inventory_backup_after_command writes it if the removal follows later in the same command
void inventory_backup_note_removed(std::shared_ptr<Client> c, const ItemData& item);
void inventory_backup_note_meseta_removed(std::shared_ptr<Client> c, uint64_t meseta);
// Writes the interim state of c's current game now if it changed. Never throws (a failure is logged)
void inventory_backup_flush_interim(std::shared_ptr<Client> c);
// After every command of c (on_command): writes the interim state if something left the inventory in that command
void inventory_backup_after_command(std::shared_ptr<Client> c);
// Provenance hooks (see the top of this file). After every subcommand of c: reconciles c's provenance with its
// inventory (no write)
void inventory_backup_after_subcommand(std::shared_ptr<Client> c);
// The floor item item_id on floor is one the server vouches for: it made it itself for an enemy or box, or $restore
// dropped it
void inventory_backup_note_floor_world(std::shared_ptr<Lobby> l, uint8_t floor, uint32_t item_id);
// The game's leader announced the floor item item_id (6x5F) for the enemy or box entity. The server vouches for it
// only if it had seen and marked the drop request for that entity (request_seen) and this is the first item for it
void inventory_backup_note_leader_drop(std::shared_ptr<Lobby> l, uint8_t floor, uint32_t item_id, const void* entity,
    bool request_seen);
// c dropped item out of its inventory onto floor (6x2A; the server moved it itself). Call before
// inventory_backup_note_removed: it reads how many units of it were proven c's own
void inventory_backup_note_dropped(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, const ItemData& item);
// 6x5D, a partial stack drop (also Meseta): the floor item remembers whose own drop it is, with the proven units of
// the stack it was split from (critic round 2, M2)
void inventory_backup_note_dropped_partial(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor,
    const ItemData& item);
// 6x5F: the leader's floor item for ene_st or obj_st; a compound enemy is checked and counted as its alias target
// (critic round 2, M6)
void inventory_backup_note_leader_drop(std::shared_ptr<Lobby> l, uint8_t floor, uint32_t item_id, bool client_drop_mode,
    std::shared_ptr<const MapState::EnemyState> ene_st, std::shared_ptr<const MapState::ObjectState> obj_st);
// c picked up a floor item (item, with the origin the floor item carried) and its inventory holds it now
void inventory_backup_note_picked_up(std::shared_ptr<Client> c, const ItemData& item, const InventoryFloorOrigin& origin);
// $restore / $restore check: returns the lines to show the player
std::vector<std::string> inventory_restore_command(std::shared_ptr<Client> c, bool check_only);

// CLI spire-backup-import: turns a SpireGuardSnapshot file into a restorable backup (Imported, time = now_usecs) in
// root/<serial>.<character id>/; returns the path written
std::string inventory_backup_import_snapshot(const std::string& snapshot_filename, const std::string& root, uint64_t now_usecs);
