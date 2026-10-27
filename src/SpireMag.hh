#pragma once

#include <stdint.h>

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Route B F6 (server half): the run mag ("Spire Mag"). The server decides everything; the quest asks, creates, swaps
// and acks. Plan: pso-roguelike new-features/roguelike/output/plan-route-b-2026-10-26.md ("1. Run mag", the run mag
// numbers, F6, invariants 2-8, and the amendments after the mag measurement, which override it).
//
// Request in r248: 'Z' << 24 | sub << 16 | lean << 8 | k (k: the room the quest is in; lean: bits 8-15, ASK and EXAM only).
//   sub 1 ASK (kit point; a non-boss room clear while a mag is pending), sub 4 EXAM (exam_reward at a boss clear: room k counts as
//   a beaten boss), sub 2 RESULT (low byte 1: the create succeeded, 0: item_create2 gave FFFFFFFF), sub 3 NO_MAG (the
//   equip reminder at a boss room start), sub 5 LEAN (low byte = lean; no quest sends it, the lean travels in the ASK/EXAM).
// F10 lean (routeb2-f10-randgevallen.md, "Besluit na de critic" 1-2): 0 no choice (default), 1 Fang/Mind, 2 Shell. The
//   server stores a lean only when it is not 0 and the ledger has issued no step above 0 (issued_step <= 0), before it
//   decides the step; otherwise, and for any value above 2, it logs and ignores it. Lean 2 picks the class's ShellSteps
//   column (spire-mag.json), lean 0 and 1 its Steps. A server from before F10 ignores bits 8-15: the run gets the default.
// Answer to ASK and EXAM: r20-r31 the new mag's data1 (12 registers, one byte each), r41 = SPIRE_RUN_MAG_TAG |
// old_type << 8 | result (result 1: create this mag, 0: no change; old_type: data1[1] of the run mag the run copy holds
// now, 0xFF if none), then r248 = 'DONE'. RESULT, NO_MAG and LEAN get no answer (the quest waits for none).
// Shared contract with the quest: pso-roguelike quests/spire/ROUTEB-F6-PROTOCOL.md (quest 18ba60c).
//
// The pure parts live here, for the static tests; the handler, the ledger store and the texts in SpireFlagBridge.cc.

constexpr uint8_t SPIRE_BRIDGE_RUN_MAG = 'Z';
constexpr uint8_t SPIRE_RUN_MAG_ASK = 1;
constexpr uint8_t SPIRE_RUN_MAG_RESULT = 2;
constexpr uint8_t SPIRE_RUN_MAG_NO_MAG = 3;
constexpr uint8_t SPIRE_RUN_MAG_EXAM = 4;
constexpr uint8_t SPIRE_RUN_MAG_LEAN = 5;
constexpr uint8_t SPIRE_RUN_MAG_LEAN_NONE = 0; // no choice: the default column (Steps)
constexpr uint8_t SPIRE_RUN_MAG_LEAN_FANG = 1; // Fang (HU/RA) or Mind (FO): Steps
constexpr uint8_t SPIRE_RUN_MAG_LEAN_SHELL = 2; // Shell: ShellSteps (DEF swapped with POW for HU/RA, with MIND for FO)
constexpr uint8_t SPIRE_RUN_MAG_ANSWER_REGISTER = 41; // shared with 'O' and 'I'; the quest sets it to 0 before asking
constexpr uint8_t SPIRE_RUN_MAG_ITEM_FIRST_REGISTER = 20; // r20-r31, the RS_ITEM registers of the 'J' item loop
constexpr uint8_t SPIRE_RUN_MAG_NO_OLD = 0xFF;
// The top byte of every answer in r41 ('Z' << 24): r41 is shared with 'J', 'I', 'M' and 'O', each tagged by its letter
constexpr uint32_t SPIRE_RUN_MAG_TAG = static_cast<uint32_t>(SPIRE_BRIDGE_RUN_MAG) << 24; // 0x5A000000
constexpr const char* SPIRE_RUN_MAG_FILE = "system/spire-mag.json";

using SpireMagBytes = std::array<uint8_t, 12>;

// One row of the table: the mag kind (data1[1]) and the four stats in levels.
struct SpireMagRow {
  uint8_t id = 0;
  uint16_t def = 0;
  uint16_t pow = 0;
  uint16_t dex = 0;
  uint16_t mind = 0;
};
// A start-level floor: from this start_level bonus on (SpireMeta's spire_start_level_bonus: +5, +10, +14), the run mag
// starts at least at this Sega challenge template mag (PlayerSubordinates.cc v3 templates), merged per stat.
struct SpireMagFloor {
  uint32_t min_bonus = 0;
  SpireMagRow row;
};
struct SpireMagLine {
  std::vector<SpireMagRow> steps; // step 0 = the kit, step n = after the n-th boss
  std::vector<SpireMagFloor> floors; // ascending min_bonus
  // F10: the Shell column (lean 2). Optional: missing, unreadable or not as long as steps leaves it empty and says why
  // in shell_note; a Shell run then gets steps. It never makes the table unreadable (NO_TABLE).
  std::vector<SpireMagRow> shell_steps;
  std::string shell_note; // "" when shell_steps is usable
};
struct SpireMagTable {
  bool enabled = false; // false: every 'Z' answers 0 (the data-only kill switch)
  std::map<std::string, SpireMagLine> classes; // "HU", "RA", "FO"
  std::set<uint8_t> boss_rooms; // the boss rooms k (Episode 1 k4-k15, Episode 2 k18-k27: one room counter)
};
// Strict: throws on a missing key, an empty line, a stat above 200, a row whose levels sum above 200. ShellSteps is the
// one exception: a bad ShellSteps only empties that class's shell_steps (SpireMagLine::shell_note).
SpireMagTable spire_mag_parse(const std::string& json_text);

// F10: the column a lean picks. Lean 2 with a usable ShellSteps (as long as Steps): the line with steps = shell_steps
// (shell true). Lean 2 without one: the line as it is (fell_back true, the caller logs it). Any other lean: the line.
// Floors are the class's one list for both columns.
struct SpireMagColumn {
  SpireMagLine line;
  bool shell = false;
  bool fell_back = false;
};
SpireMagColumn spire_mag_column(const SpireMagLine& line, uint8_t lean);

// "HU", "RA" or "FO" for a character class (0-11); "" for any other value.
const char* spire_mag_archetype(uint8_t char_class);

// The mag level: the sum of the four stat levels (data1 words / 100).
uint32_t spire_mag_level_of(const SpireMagBytes& bytes);
// The 12 data1 bytes of a row, built as ItemData::assign_mag_stats does (ItemData.cc:281-294): 02 ZZ LL 00, then
// DEF, POW, DEX, MIND as little-endian words in hundredths, each & 0x7FFE. Photon blasts 0 (data2 cannot be set by
// item_create2, so no photon blast flags either).
SpireMagBytes spire_mag_row_bytes(const SpireMagRow& row);

// The step the run's mag should be at: min(num_steps - 1, start_step + bosses + (exam of a room not counted yet)).
uint32_t spire_mag_expected_step(size_t num_steps, uint32_t start_step, size_t stored_bosses, bool exam_new_boss);
// The start step of a run that first asks in room k: the boss rooms before k (a practice run that starts
// after a boss gets the mag of that point; a run from its first room starts at 0).
uint32_t spire_mag_start_step(const SpireMagTable& table, uint8_t k);

// The bytes of step `step` for a line: the step row, merged per stat with the highest floor the start bonus reaches and
// with the mag the ledger last mirrored (so fed progress is never lost). The kind is the step row's, or the floor's
// when the floor's level is above the step's. LL is recomputed as the sum; if the merge would pass 200, the mirrored
// mag's stats stay (it is a valid mag already) under the step's kind.
SpireMagBytes spire_mag_step_bytes(const SpireMagLine& line, uint32_t step, uint32_t start_bonus,
    const std::optional<SpireMagBytes>& last_mirrored);

// The ledger of one run mag: per (serial, character id, run id), kept by run id and step, never by item id (item ids
// change per game). Persisted in the savestate file ("RunMag").
struct SpireRunMagLedger {
  uint32_t run_id = 0;
  // -1: no mag issued yet. B-REGIE-14-10: a step counts as issued when the server answers it with result 1; a RESULT 0
  // gives it back (step_before_offer), a RESULT 1 only confirms, a lost RESULT leaves it issued (the client holds it).
  int32_t issued_step = -1;
  int32_t offered_step = -1; // the step of the last answer with result 1, until its RESULT
  int32_t step_before_offer = -1; // issued_step before that answer: what a RESULT 0 restores (in memory only)
  uint32_t start_step = 0;
  std::set<uint8_t> boss_rooms; // the boss rooms k this run counted (EXAM, and the server's own boss clears)
  std::optional<SpireMagBytes> last_mirrored; // the run mag's data1 as last mirrored (overlay) or created
  std::optional<SpireMagBytes> offered; // the mag of the last answer with result 1, until its RESULT
  bool offered_shell = false; // F10: the offered mag came from the Shell column (the evolve text says so; in memory only)
  uint8_t lean = 0; // 0 none (default Fang/Mind), 1 Fang/Mind, 2 Shell
};

// F10: the lean bits of an ASK or EXAM word (bits 8-15)
uint8_t spire_run_mag_lean_bits(uint32_t value);
// F10: the lean of a ledger read from a savestate file: 0, 1 or 2 as written, anything else 0 (no choice), so an
// edited file never wraps to Shell (258 as a byte is 2)
uint8_t spire_run_mag_saved_lean(int64_t value);
// F10: storing a lean (from the ASK/EXAM bits, or a sub 5) in the ledger. Stored only when it is 1 or 2 and the ledger
// has issued no step above 0; the column of a run never changes once a step above the kit's is out.
enum class SpireRunMagLeanStore {
  NONE = 0, // lean 0: nothing chosen, nothing stored
  STORED,
  AFTER_STEP, // issued_step > 0: ignored
  INVALID, // above 2: ignored
};
SpireRunMagLeanStore spire_run_mag_lean_store(SpireRunMagLedger& ledger, uint8_t lean);
const char* spire_run_mag_lean_store_name(SpireRunMagLeanStore r);

// Why a 'Z' that would create or count a mag is refused (the answer is then result 0, old type 0xFF). The order of
// the checks is the order below.
enum class SpireRunMagRefusal {
  NONE = 0,
  NOT_GC_V3,
  NO_SPIRE_GAME, // not a guarded Spire game (Battle, replace_char, SpireGuardQuests)
  NO_OVERLAY, // no run copy: the real character (q925 base game, or after the run)
  RESTORE_PENDING, // the snapshot's restore is pending
  MARKER, // the snapshot's marker is not RunInProgress
  BASE_RETURNED, // 'O' OK in this game: the client holds the real character
  RUN_AGAIN, // an 'I' (run again) happened in this game since the quest load: the live player is the earlier run copy
  REFRESHED_BY_61, // an in-game 61 refreshed the overlay: it may hold the real mag (the save buffer after a push)
  DISABLED, // spire-mag.json "Enabled" false
  NO_TABLE, // spire-mag.json missing or unreadable, or no line for the class
  TWO_MAGS, // the overlay holds more than one type-02 item: the quest's find_inventory_item could take the wrong one
  NO_LEDGER, // set by the 'Z' handler after the checks above: no ledger for the run (run id 0, no character key)
};
struct SpireRunMagState {
  bool gc_v3 = false;
  bool spire_game = false;
  bool has_overlay = false;
  bool restore_pending = false;
  bool run_in_progress = false;
  bool base_returned = false;
  bool run_again = false;
  bool refreshed_by_61 = false;
  bool enabled = false;
  bool has_table = false;
  size_t overlay_mags = 0; // type-02 items in the overlay
};
SpireRunMagRefusal spire_run_mag_refusal(const SpireRunMagState& st);
const char* spire_run_mag_refusal_name(SpireRunMagRefusal r);

// r41 of an answer
uint32_t spire_run_mag_answer(uint32_t tag, uint8_t old_type, bool result);

// The decision of an ASK or EXAM, with the ledger updated: whether a new mag is answered (then bytes holds it). An
// EXAM adds k to the ledger's boss rooms. An answer with result 1 issues its step (issued_step moves here, B-REGIE-14-10),
// so the same EXAM twice, an ASK after a resume, a floor reload or a lost RESULT never issue a step twice.
// server_boss_rooms: the boss rooms the server counted in this game for this client.
struct SpireRunMagDecision {
  bool result = false;
  uint32_t expected_step = 0;
  SpireMagBytes bytes{};
};
SpireRunMagDecision spire_run_mag_decide(SpireRunMagLedger& ledger, const SpireMagLine& line, uint8_t sub, uint8_t k,
    const std::set<uint8_t>& server_boss_rooms, uint32_t start_bonus);
// The ledger step of an ASK or EXAM that no refusal stopped (the 'Z' handler in SpireFlagBridge.cc): the start step
// from the asking room (only while the ledger has issued, counted and offered nothing, so a later ASK never moves it),
// the ledger following the fed mag (spire_run_mag_follow_mirror), the old type the quest deletes (data1[1] of the one
// run mag in the overlay; SPIRE_RUN_MAG_NO_OLD with none or more than one), the decision and the r41 answer.
// F10: lean is the word's bits 8-15 (spire_run_mag_lean_bits). It is stored (spire_run_mag_lean_store) after the start
// step and before the decision, so the first ASK of a practice run from k >= 5 already gets its column; the column
// follows the ledger's lean (spire_mag_column), never the bits alone, so a resumed ASK without bits keeps it. The lean
// changes only which row is answered: never the result, the old type or the r41 answer.
struct SpireRunMagHandled {
  SpireRunMagDecision decision;
  uint8_t old_type = SPIRE_RUN_MAG_NO_OLD;
  bool followed_mirror = false;
  uint32_t answer = SPIRE_RUN_MAG_TAG | (static_cast<uint32_t>(SPIRE_RUN_MAG_NO_OLD) << 8); // a refusal's answer
  SpireRunMagLeanStore lean_store = SpireRunMagLeanStore::NONE;
  bool shell = false; // the Shell column answered
  bool shell_fell_back = false; // the ledger's lean is Shell, but the line has no usable ShellSteps
};
SpireRunMagHandled spire_run_mag_handle(SpireRunMagLedger& ledger, const SpireMagTable& table, const SpireMagLine& line,
    uint8_t sub, uint8_t k, const std::vector<SpireMagBytes>& overlay_mags, const std::set<uint8_t>& server_boss_rooms,
    uint32_t start_bonus, uint8_t lean_bits);
// The ledger step of a 'V' (savestate write): the ledger follows the overlay's fed mag (spire_run_mag_follow_mirror)
// unless an in-game 61 refreshed the overlay in this game (amendment 3: it may hold the real mag then). False without
// a ledger or when it did not follow.
bool spire_run_mag_savestate_follow(
    SpireRunMagLedger* ledger, bool refreshed_by_61, const std::vector<SpireMagBytes>& overlay_mags);
// RESULT: 1 confirms the offered mag (last_mirrored = it; the step was issued at the answer); 0 gives the step back
// (issued_step = step_before_offer: the step is owed again, the quest asks at the next non-boss room clear). Returns
// false if nothing was offered.
bool spire_run_mag_result(SpireRunMagLedger& ledger, bool created);
// Hotfix 1: the pack-full notice. A RESULT 0 that settled an offer (known) tells the player once per given-back step
// per run; a second RESULT 0 for the same step in the same run stays silent, a later step or a new run tells again.
// Only this record changes: never the ledger, the r41 answer or the item handling.
struct SpireRunMagPackFullNotices {
  uint32_t run_id = 0;
  std::set<int32_t> steps; // the steps of run_id whose RESULT 0 was told
};
bool spire_run_mag_pack_full_notice(
    SpireRunMagPackFullNotices& told, uint32_t run_id, bool known, bool created, int32_t offered_step);
// The 'Z' handler's whole decision at a RESULT (critic N3): the record per client key (spire_run_mag_key: serial and
// character id, so two players of one run each get their own notice), no notice without a key or a run id, the wipe of
// every record when a new key would make it more than SPIRE_RUN_MAG_PACK_FULL_KEYS_MAX (as the ledger store: anyone can
// be told once more after it, never less), and the text: SPIRE_RUN_MAG_PACK_FULL_FIRST_TEXT when nothing was issued
// before this offer (step_before_offer < 0: the kit or a practice run's first ASK), else SPIRE_RUN_MAG_PACK_FULL_TEXT.
constexpr size_t SPIRE_RUN_MAG_PACK_FULL_KEYS_MAX = 1024;
using SpireRunMagPackFullTold = std::map<std::string, SpireRunMagPackFullNotices>;
struct SpireRunMagPackFullDecision {
  bool tell = false;
  const char* text = nullptr; // set only with tell
  bool wiped = false;
  const char* note = ""; // for the log line
};
SpireRunMagPackFullDecision spire_run_mag_pack_full_decide(SpireRunMagPackFullTold& told, const std::string& key,
    uint32_t run_id, bool known, bool created, int32_t offered_step, int32_t step_before_offer);

// The run mag in an overlay's items (data1 of every type-02 item): the ledger follows the mirror (feeding) only when
// exactly one type-02 item is there and the ledger has issued a mag; its level must not be below the ledger's (feeding
// never lowers it). An item equal to the offered mag confirms it as a RESULT 1 would (the RESULT was lost after the
// create). Returns true if last_mirrored changed.
bool spire_run_mag_follow_mirror(SpireRunMagLedger& ledger, const std::vector<SpireMagBytes>& overlay_mags);

// The ledger in the savestate file
std::string spire_run_mag_bytes_hex(const SpireMagBytes& bytes);
SpireMagBytes spire_run_mag_bytes_from_hex(const std::string& hex); // throws on a bad string

// Ticker texts (C4_SRVMSG path, spire_send_ticker); the evolve text carries the Sega name
// No promise of a retry: the quest asks again at the next clear while the run goes on, but the server cannot tell
// whether one comes (the RESULT word has no k, no config names a quest's last room, and time up, a death or 'O' end
// a run too); the retry needs free slots after that clear's own rewards, which come first (the exam rare, up to five
// room clear rewards). FIRST: no mag was issued before this offer, so there is no mag that could grow.
constexpr const char* SPIRE_RUN_MAG_PACK_FULL_TEXT = "Pack full: your mag did not grow. Keep slots free";
constexpr const char* SPIRE_RUN_MAG_PACK_FULL_FIRST_TEXT = "Pack full: no room for your mag. Keep slots free";
constexpr const char* SPIRE_RUN_MAG_REMINDER_TEXT = "Your mag is in your pack: equip it (Item Pack > Mag)";
// v62: unchanged in bundle 8. The grind on the kit weapon moved to bundle 9 (B-REGIE-16-15: equip_item cannot set a
// grind), so the kit still holds Monogrinders to use next to the HP Materials
constexpr const char* SPIRE_KIT_USE_TEXT = "Use your materials and grinders now (Item Pack)";
// Route B bundle 9 (ROUTEB9-PROTOCOL.md § 3): the kit ticker of a client with r15 = 1 (SpireEquipItem.hh). The quest
// then gives no Monogrinders but makes the kit weapon with grind G, so the text names the grind and only the HP
// Materials: "Your kit weapon is ground +<G>. Use your HP Materials now (Item Pack)." Without HP Materials (hp_mat 0)
// only the grind sentence; G 0 (atp_grind 0) only the HP Materials sentence. server_equips false: SPIRE_KIT_USE_TEXT.
std::string spire_kit_use_text(bool server_equips, uint32_t grind, uint32_t hp_mat_level);
// F10: shell names the choice ("Your mag evolves: Varuna (Shell). ..."); Fang/Mind is the text as before. Bundle 9:
// server_equips (r15 = 1, the server equips the new mag at 'C' 14) drops " Equip it: Item Pack > Mag."
std::string spire_run_mag_evolve_text(const std::string& sega_name, bool shell = false, bool server_equips = false);
// The text at a run's first mag (RESULT 1 with nothing issued before: the kit, or a practice run's first ASK). Fix
// round (F10S critic N4): a Shell mag names the choice as the evolve text does; otherwise SPIRE_RUN_MAG_REMINDER_TEXT.
constexpr const char* SPIRE_RUN_MAG_REMINDER_SHELL_TEXT = "Your mag (Shell) is in your pack: equip it (Item Pack > Mag)";
std::string spire_run_mag_reminder_text(bool shell);
// The longest ticker line of the run mag (measured 2026-10-27: "Your mag evolves: Kabanda (Shell). Equip it: Item
// Pack > Mag.", 61 characters); the static tests keep every run mag line at or below it
constexpr size_t SPIRE_RUN_MAG_TICKER_MAX = 61;

// Route B bundle 2 S: the kit text ('M' START, SpireFlagBridge.cc spire_shop_handle). Due once per client and run when
// the START answer carries an hp_mat level above 0 (the kit creates 2 x level HP Materials, rewards.py kit_meta) and the
// client's start answer ('J') did not resume it (a resumed run gets no kit: its items come from the savestate).
// already_sent: this client had the text in this run. v62 (B-REGIE-16-13): atp_grind no longer makes it due (the
// grinders are a grind on the kit weapon); the level stays an argument for the callers' log lines.
bool spire_kit_use_text_due(bool start, uint32_t hp_mat_level, uint32_t atp_grind_level, bool resumed, bool already_sent);

// Route B bundle 2 S: the boss burst text. The quest (F7, make_spire.py boss_burst) casts Shifta, and Deband unless
// the boss modifier is Furious or Elite, on each player at the door of every boss room from k8 on (not the Dragon at
// k4). The server sees that door as the client's 'V' (savestate write) of that room, which the quest sends right
// before the casts, in the leader's room body and in the follower's mirror phase 4. The text is true in both cases and
// names no mag (a run has no mag before it is issued, and a disabled mag table issues none).
constexpr const char* SPIRE_BOSS_BURST_TEXT = "Boss ahead: you are buffed";
constexpr uint8_t SPIRE_BOSS_BURST_FROM_K = 8; // the boss burst (F7) starts at De Rol Le, not at the Dragon
// Due when room k is a boss room (boss_rooms: spire-mag.json BossRooms) at or above SPIRE_BOSS_BURST_FROM_K and this
// client has not had the text for k in this run (sent_rooms, per client and run); marks k in sent_rooms when due.
bool spire_boss_burst_text_due(uint8_t room_k, const std::set<uint8_t>& boss_rooms, std::set<uint8_t>& sent_rooms);

// Route B bundle 2 S: whether a player data command (61 or 98, ReceiveCommands.cc on_61_98) refreshes the run copy in
// game as the run mag ledger must know (amendment 3, spire_run_mag_note_in_game_61): only the 61.
bool spire_run_mag_command_refreshes(uint16_t command);
