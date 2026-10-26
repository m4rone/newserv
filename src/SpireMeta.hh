#pragma once

#include <array>
#include <map>
#include <optional>
#include <memory>
#include <string>
#include <vector>
#include <stdint.h>

#include "Client.hh"
#include "Lobby.hh"

// Spire meta (v22 flags per character, v24 the run's unlocks decided on the server).
//
// The Spire quest flags 0x380-0x3AF (E04 milestones, pacts, second kits, relics; E06 payout) belong to a character,
// not to a memory card: system/spire-bridge/<serial>.<character id>.meta.json. Before v22 the bridge stored them per
// serial, so a second character on the same card got the first one's unlocks and could collect its payout in q925.
//
// The run's unlocks are decided here, not in the quest: the 'F' record code carries the exams passed (bits 16-17:
// 1 = Dragon, 2 = De Rol Le) and a deathless team (bit 18) next to the won bit (23) and the seconds (low 16 bits).
// From that the server sets 0x380 (first run), 0x381/0x384/0x385 (Dragon: Iron man, Haste), 0x382/0x386/0x387/0x3A0
// (De Rol Le: Bare, Hungry, payout win), 0x383/0x3A1 + the second kit of the class's archetype (0x388 HU, 0x389 RA,
// 0x38A FO) for a deathless win, and 0x3A2 on a new best time; it delivers them at once and names what is new.

constexpr uint16_t SPIRE_META_FLAG_FIRST_RUN = 0x0380;
constexpr uint16_t SPIRE_META_FLAG_DRAGON = 0x0381;
constexpr uint16_t SPIRE_META_FLAG_DEROLLE = 0x0382;
constexpr uint16_t SPIRE_META_FLAG_DEATHLESS_WIN = 0x0383;
constexpr uint16_t SPIRE_META_FLAG_PACT_FIRST = 0x0384; // Iron man, Haste, Bare, Hungry
constexpr uint16_t SPIRE_META_FLAG_KIT2_FIRST = 0x0388; // HU, RA, FO
constexpr uint16_t SPIRE_META_FLAG_RUN1_WON = 0x038B; // ladder (plan-unlockables §2): Spire Run I won on that difficulty
constexpr uint16_t SPIRE_META_FLAG_RUN2_WON = 0x038C; // the same for Spire Run II
constexpr uint16_t SPIRE_META_FLAG_PAY_WIN = 0x03A0;
constexpr uint16_t SPIRE_META_FLAG_PAY_DEATHLESS = 0x03A1;
constexpr uint16_t SPIRE_META_FLAG_PAY_RECORD = 0x03A2;

constexpr uint32_t SPIRE_META_FINISH_EXAMS_SHIFT = 16; // 2 bits
constexpr uint32_t SPIRE_META_FINISH_DEATHLESS = 1 << 18;
// Route B F2: the pacts the run carried, bits 19-21 of 'F' (the quest adds pacts << 19 before its score_keep jump, so a
// lost run carries them too); more than SPIRE_META_FINISH_PACTS_MAX counts as the maximum. Bit 22 is unused and bit 23
// is the won bit; a quest that does not send pacts sends 0.
constexpr uint32_t SPIRE_META_FINISH_PACTS_SHIFT = 19; // 3 bits
constexpr uint8_t SPIRE_META_FINISH_PACTS_MAX = 4;

// Keyed by (difficulty, flag number); true = set, false = cleared. Same meaning as the bridge's flag entries.
using SpireMetaFlags = std::map<std::pair<uint8_t, uint16_t>, bool>;

// Audit S-6: the flags the server owns, 0x380 to the last payout flag (unlocks, relics seen, payout). 0x3A3-0x3AF
// (q925's 0x3A8 S-rank received, 0x3A9 ever paid) are set by the client in a normal game and stay on its card.
constexpr uint16_t SPIRE_META_SERVER_LAST_FLAG = SPIRE_META_FLAG_PAY_RECORD;

// The meta file of one character: system/spire-bridge/<serial>.<character id>.meta.json ("" without character data).
// The v5-v21 file <serial>.json (one per card) is never read, merged or moved: it cannot tell which character wrote a
// flag, so it would hand the first character's unlocks to every character on the card.
std::string spire_meta_filename_for(uint32_t serial_number, const PlayerDispDataV4* disp);

// Regie B-REGIE-10-2: a character without a meta file loses nothing at its first delivery. The file is adopted then:
// with the 0x380-0x3A2 flags the character has (KNOWN, "Adopted": "known") if the server knows them, else with none and
// the marker "Adopted": "unknown". The server does not know them on GC v3: the 61/98 player data has no quest flags
// (C_CharacterData_V3_61_98, CommandFormats.hh; on_61_98 copies inventory, disp and records only), the server's own
// character quest_flags follow a 6x75 on BB only (on_set_quest_flag), and a 6x6F join sync carries the leader's
// game, not the joiner's card. So the delivery adopts UNKNOWN, and an UNKNOWN file never clears a flag it has no entry
// for: an entry is a flag the character wrote in a Spire quest or the server set for it ('F' record code).
enum class SpireMetaAdopted : uint8_t {
  NO_MARKER = 0, // a file from before B-REGIE-10-2: cleared as before (audit S-6)
  KNOWN,
  UNKNOWN,
};
struct SpireMetaFile {
  bool exists = false;
  SpireMetaAdopted adopted = SpireMetaAdopted::NO_MARKER;
  SpireMetaFlags flags;
  // v49 (B-BASIS-13): the rung the base opened per ladder run (1 Run I, 2 Run II; 0-3, see spire_meta_open_rung), in
  // the file as "BaseRung": {"1": n, "2": n}; written only if a run has one, so older files stay as they are
  std::map<uint8_t, uint8_t> base_rung;
  // v50 (B-BASIS-2): the base shop's upgrades, name -> level (SPIRE_SHOP_ITEM_NAMES), in the file as
  // "Upgrades": {"hp_mat": 2, ...}. One block for every difficulty (a flag is per difficulty, an upgrade is not);
  // written only if there is one, kept through every flag write like BaseRung
  std::map<std::string, uint32_t> upgrades;
};
SpireMetaFile spire_meta_load_file(const std::string& filename); // exists false (and no flags) if there is no file
SpireMetaFlags spire_meta_load(const std::string& filename); // spire_meta_load_file(...).flags
// The file adopted for a character without one; current = the character's quest flags, nullptr if the server does not
// know them. Pure, for the offline test.
SpireMetaFile spire_meta_adopt(const QuestFlags* current);

// What a delivery sends: every stored entry, plus a clear of each server-owned flag (0x380-0x3A2) the character has no
// entry for on the game's difficulty (audit S-6). No such clears in the delivery that adopted the file (just_adopted)
// and none ever for an UNKNOWN file. Pure, for the offline test.
struct SpireMetaDelivery {
  uint8_t difficulty = 0;
  uint16_t flag = 0;
  bool set = false;
};
std::vector<SpireMetaDelivery> spire_meta_delivery_entries(const SpireMetaFile& file, uint8_t difficulty,
    bool just_adopted);

// Audit S-6: in a game where the meta flags apply (spire_meta_delivery_applies), a quest flag write in 0x380-0x3AF
// belongs to the writer: it is not forwarded to the other clients and not kept in the game's flag state (which a
// joining client receives). Elsewhere every flag is shared as before. Pure, for the offline test.
bool spire_meta_flag_is_shared(bool meta_game, uint16_t flag_num);

// Called from spire_bridge_record_flag for a bridged quest flag write in range. Stores it for the writing character.
// Returns false if the client has no character data (the caller then does nothing). Throws on a file error.
bool spire_meta_record_flag(std::shared_ptr<Client> c, uint32_t serial_number, uint8_t difficulty, uint16_t flag_num,
    bool is_set);

// Called from spire_bridge_deliver_flags. Sends the character's stored flags (6x75 in one 6D). Never throws.
void spire_meta_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number);

// Regie pso-regie-9: the stored flags go only into a game that runs a Spire quest (SpireBridgeQuests or
// SpireGuardQuests, spire_game_is_spire) or a quest listed in SpireMetaQuests (the E06 payout q925). A normal game
// (Normal, Battle or Solo, with another quest or none) gets nothing, so 0x380-0x3AF never reach a character outside
// those quests. Pure, for the offline test.
bool spire_meta_delivery_applies(bool is_game, bool has_quest, bool spire_quest, bool meta_quest);

// Whether a quest flag write is stored (spire_bridge_record_flag). In a SpireBridgeQuests quest every write in range
// (v5). In a SpireMetaQuests quest only a clear of a payout flag (0x3A0-0x3A2): q925 clears what it paid, so the
// delivery at the next quest load does not set it again; its other writes stay the client's own. Pure, for the test.
bool spire_meta_record_applies(bool bridged_quest, bool meta_quest, uint16_t flag_num, bool is_set);

// The difficulty ladder (plan-unlockables-2026-10-25 §2): per character and per run, a win on a difficulty opens the
// next one. Run I is q921 (and the retired q920) on Normal plus q981-q983 (Hard, Very Hard, Ultimate); Run II is q922
// plus q984-q986. 0 = a quest of neither run (no ladder flag).
uint8_t spire_meta_ladder_run(uint32_t quest_number);
// v54: the rung a ladder quest plays (1 Hard q981/q984, 2 Very Hard q982/q985, 3 Ultimate q983/q986); 0 for q920-q922
// and every other quest. The heat table of that rung applies (SpireShopCatalogue::heat).
uint8_t spire_meta_ladder_quest_rung(uint32_t quest_number);
uint16_t spire_meta_ladder_flag(uint8_t run); // 0x38B, 0x38C, 0 for neither
// The highest difficulty won open for a run (0 Normal ... 3 Ultimate): Normal always, then one past the highest won.
uint8_t spire_meta_ladder_rung(const SpireMetaFlags& flags, uint8_t run);
// v49 (B-BASIS-13, Marck via the regie): a rung is open if it was won (spire_meta_ladder_rung) or the base opened it
// (BaseRung in meta.json, per run). The open rung is the larger of the two, at most 3 (Ultimate). BaseRung never counts
// as a win: records "Won", the ladder flags 0x38B/0x38C and the "Unlocked" text come from wins only. Every open rung
// the server computes (SPIRE_RUN1/2 in AvailableIf, the difficulty gate, the quest menu note) goes through
// spire_meta_ladder_rung_for_client, which combines them. Nothing sets BaseRung yet (the base shop, slice 4).
uint8_t spire_meta_base_rung(const SpireMetaFile& file, uint8_t run); // 0 without one, at most 3
uint8_t spire_meta_open_rung(uint8_t won_rung, uint8_t base_rung);
uint8_t spire_meta_open_rung(const SpireMetaFile& file, uint8_t run);
// The difficulty a finish flag is stored on: the payout flags (0x3A0-0x3A2) on Normal (q925 pays there, plan §5), every
// other flag on the game's difficulty.
uint8_t spire_meta_flag_difficulty(uint16_t flag_num, uint8_t game_difficulty);
// The ladder in a game the client creates or a quest menu it opens (pure, for the offline test).
enum class SpireLadderGate : uint8_t {
  LEVEL_OK = 0, // the character's own level allows the difficulty: Sega's check passes
  RUNG_OPEN, // too low, but a run's rung covers the difficulty: the minimum-level check is skipped (the run is an overlay)
  REFUSED, // too low and no rung: refused with spire_meta_ladder_refuse_text
};
SpireLadderGate spire_meta_ladder_gate(bool battle_game, bool spire_character, uint8_t rung, uint8_t difficulty,
    uint32_t char_level, uint32_t min_level);
// One line (a lobby message box) for a refused difficulty: the level Sega asks and the Spire win that opens it.
std::string spire_meta_ladder_refuse_text(uint8_t difficulty, uint32_t min_level);
// The info line in a Battle quest menu on a difficulty above Normal for a run whose rung does not reach it ("" if both
// runs are open there), so a hidden run quest is never a silent nothing.
std::string spire_meta_ladder_menu_note(uint8_t rung1, uint8_t rung2, uint8_t difficulty);
// The open rung of a run for a client's real character (its meta file, v49 won or BaseRung; 0 without one or on a read
// error). Never throws.
uint8_t spire_meta_ladder_rung_for_client(std::shared_ptr<const Client> c, uint8_t run);

// Called from the 'F' record code after the records are saved. Sets the run's unlock and payout flags for the
// character on the game's difficulty, delivers them and returns the text lines to add to the record text (empty if
// nothing is new). Throws on a file error.
// open_payout: whether this 'F' may open the payout flags (0x3A0-0x3A2); see spire_meta_apply_finish.
std::string spire_meta_on_finish(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t finish_value, bool new_best_time, bool open_payout);

// Pure part of spire_meta_on_finish, for the offline test: the flags a finished run sets, for a class (0-11).
struct SpireMetaFinish {
  bool won = false;
  uint8_t exams = 0;
  bool deathless = false;
  uint8_t pacts = 0; // route B F2: 0-SPIRE_META_FINISH_PACTS_MAX
};
SpireMetaFinish spire_meta_decode_finish(uint32_t finish_value);
// Fix round 2: the flags of a finish (spire_meta_flags_for_finish, plus the ladder flag) set in a character's flags on
// the game's difficulty (pure part of spire_meta_on_finish). A payout flag (0x3A0-0x3A2) is set only when open_payout is
// true: the first won 'F' of a run (spire_records_finish_stats won_counted), so a repeated 'F' of the same run never
// reopens a payout q925 cleared. Every other flag is set again (a no-op when it is set; nothing is named twice).
struct SpireMetaFinishApplied {
  std::string unlocked; // the newly set unlocks, ", "-separated
  bool payout = false; // a payout flag was opened by this 'F'
  std::string set_str; // " 0380* 0381 ..." for the log (* = new)
};
SpireMetaFinishApplied spire_meta_apply_finish(SpireMetaFlags& flags, const std::vector<uint16_t>& finish_flags,
    uint8_t difficulty, uint8_t ladder_run, bool open_payout);

// v50 (plan-basis-metawinkel 2026-10-26 slice 4, B-BASIS-2/-3/-13): the base shop. The quartermaster on Pioneer 2 sells
// upgrades for photon splinters (records.json Splinters); the server checks the balance, debits it and keeps the levels
// in meta.json (Upgrades). The catalogue (prices, caps, the strength P per level, the rung thresholds and the heat table)
// is data: SPIRE_SHOP_CATALOGUE_FILE, read at every request (a missing or broken file closes the shop). The item order
// below is the protocol's: item index i is SPIRE_SHOP_ITEM_NAMES[i] in the 'M' code and in the answer registers.
// - start_level, kit_tier, hp_mat, atp_grind: strength. P = the sum over them of the catalogue's P at the owned level
//   (level equivalents, B-BASIS-3). The kit hook applies them at the run start ('M' START).
// - open_hard, open_vh, open_ult (B-BASIS-13): BaseRung 1, 2, 3 for Spire Run I and II. Buyable only with P at least
//   the item's threshold (PNeeded) and the rung below open for one of the runs (won or BaseRung); refused while both
//   runs have the rung open already. They count 0 toward P. BaseRung is never a win.
// - H = H_auto(P, class) (curve_spire.economy() meta_heat): the heat the run gets from its strength; P past the table
//   takes the last column, a cell without a heat (null) the last heat of the class at a lower P. The chosen heat
//   (H_keuze) is not known on the server yet and is not added (v55: the pacts are chosen in the quest after its .bin
//   is sent, so the heat patch cannot carry them; SpireFlagBridge.hh SpireHeatSent). v54: the table of the quest's ladder rung
//   (spire_meta_ladder_quest_rung; Normal for every quest outside q981-q986).
constexpr const char* SPIRE_SHOP_CATALOGUE_FILE = "system/spire-shop.json";
constexpr size_t SPIRE_SHOP_NUM_ITEMS = 7;
constexpr const char* SPIRE_SHOP_ITEM_NAMES[SPIRE_SHOP_NUM_ITEMS] = {
    "start_level", "kit_tier", "hp_mat", "atp_grind", "open_hard", "open_vh", "open_ult"};
struct SpireShopItem {
  uint32_t max = 0; // the level cap (design)
  uint32_t on_sale = 0; // the highest level the shop sells now (<= max); a level without its mechanism is not sold
  std::vector<uint32_t> price; // price[n - 1]: splinters for level n
  std::vector<uint32_t> strength; // strength[n]: P at level n (strength[0] = 0); empty for a rung item
  // route B F3: levels[n]: what level n gives (start_level: the start levels, "Levels" in the catalogue); empty for a
  // rung item. Without "Levels" level n gives n (one start level per owned level, the meaning before route B).
  std::vector<uint32_t> levels;
  uint8_t rung = 0; // 1-3 for open_hard/open_vh/open_ult, 0 otherwise
  uint32_t p_needed = 0; // a rung item: the P the character needs to buy it
};
// v54: MetaHeat has one H_auto table per ladder rung, HAuto (Normal) and HAutoHard, HAutoVHard, HAutoUltimate (each
// rung's quests carry their own heat rows, q981-q986, curve_spire.economy() on sim/plan-spire-<rung>.json). HAuto is
// required; a rung without its table plays H 0 there.
constexpr const char* SPIRE_SHOP_HEAT_KEYS[4] = {"HAuto", "HAutoHard", "HAutoVHard", "HAutoUltimate"};
// Route B F2: the splinter payout, the catalogue's "Payout" block {"PerRoom": 3, "PerBoss": 10, "PactTenths": 1}. A room k
// of a run earns PerRoom when its checkpoint is stored, a boss room PerBoss on top (a boss room counts as a room), once
// per (run, k) and durable in records.json; at 'F' the run adds floor(credited * pacts * PactTenths / 10). Without the
// block the server pays the old (rooms + SPIRE_SPLINTERS_PER_BOSS * bosses) at 'F' and logs a warning.
constexpr uint32_t SPIRE_SHOP_PAYOUT_PER_ROOM_MAX = 1000;
constexpr uint32_t SPIRE_SHOP_PAYOUT_PACT_TENTHS_MAX = 10;
struct SpireShopPayout {
  uint32_t per_room = 0;
  uint32_t per_boss = 0;
  uint32_t pact_tenths = 0;
};
struct SpireShopCatalogue {
  std::vector<SpireShopItem> items; // SPIRE_SHOP_NUM_ITEMS, in SPIRE_SHOP_ITEM_NAMES order
  // per rung (0 Normal ... 3 Ultimate, SPIRE_SHOP_HEAT_KEYS): class name (name_for_char_class) -> H_auto by P, -1 = null
  std::array<std::map<std::string, std::vector<int32_t>>, 4> heat;
  std::optional<SpireShopPayout> payout; // route B F2: absent without a "Payout" block
};
// Throws on a malformed catalogue (a missing item, a price list shorter than on_sale, on_sale above max, a P or Levels
// list that does not have max + 1 entries; a Payout block that is no dict, lacks PerRoom, PerBoss or PactTenths, has
// another key not starting with '_', or a value that is no integer in 0-SPIRE_SHOP_PAYOUT_PER_ROOM_MAX, PactTenths
// 0-SPIRE_SHOP_PAYOUT_PACT_TENTHS_MAX).
SpireShopCatalogue spire_shop_parse(const std::string& json_text);
// The level of item i (0 if none).
uint32_t spire_shop_level(const std::map<std::string, uint32_t>& upgrades, size_t index);
uint32_t spire_shop_strength(const SpireShopCatalogue& cat, const std::map<std::string, uint32_t>& upgrades);
// rung: the ladder rung of the quest the heat is for (spire_meta_ladder_quest_rung), 0 = Normal's table
uint32_t spire_shop_heat(const SpireShopCatalogue& cat, uint8_t char_class, uint32_t strength, uint8_t rung = 0);
enum class SpireShopRefusal : uint8_t {
  NONE = 0,
  NO_CHARACTER, // no serial, not GC V3 or no character data
  NO_CATALOGUE, // SPIRE_SHOP_CATALOGUE_FILE missing or malformed
  UNKNOWN_ITEM, // an index past the catalogue, or a sub-request that is no LIST, BUY or START
  MAX_LEVEL, // the item is at its cap
  NOT_ON_SALE, // the next level has no mechanism yet (on_sale)
  STRENGTH, // a rung item: P below its threshold
  RUNG_CLOSED, // a rung item: the rung below is open for neither run
  ALREADY_OPEN, // a rung item: both runs have the rung open already
  BALANCE, // not enough splinters
  FILE_ERROR, // meta.json or records.json could not be written
};
const char* spire_shop_refusal_name(SpireShopRefusal r);
// The decision of a BUY of item index, free of state for the static tests. open_rung[r - 1] = the open rung of run r
// (spire_meta_open_rung). Route B F3: kit2_owned = the character already has its archetype's second kit
// (spire_meta_kit2_owned); then kit_tier is refused with ALREADY_OPEN (code 8), so it never takes splinters twice.
SpireShopRefusal spire_shop_buy_refusal(const SpireShopCatalogue& cat, size_t index,
    const std::map<std::string, uint32_t>& upgrades, uint32_t balance, const uint8_t open_rung[2],
    bool kit2_owned = false);
// The price of the next level of item index, or 0 if it cannot be bought now for any reason but the balance.
uint32_t spire_shop_next_price(const SpireShopCatalogue& cat, size_t index,
    const std::map<std::string, uint32_t>& upgrades, const uint8_t open_rung[2], bool kit2_owned = false);
// Route B F3: whether the character has the second-kit flag of its class's archetype (0x388 HU, 0x389 RA, 0x38A FO,
// spire_meta_flags_for_finish) set on Normal. The flag is per difficulty and kit_tier is not: Normal is where every run
// of a character starts, so a flag earned on a higher difficulty only does not make kit_tier a double buy.
bool spire_meta_kit2_owned(const SpireMetaFile& file, uint8_t char_class);
// A BUY without a refusal on the meta file: the item one level up, and for a rung item BaseRung of both runs at least
// the rung. Returns the price to debit.
uint32_t spire_shop_apply_buy(const SpireShopCatalogue& cat, size_t index, SpireMetaFile& file);
// The character's meta file for the shop: loaded, or adopted (and written) if it has none; *filename gets its name
// ("" without character data: then nothing is loaded). Throws on a file error.
SpireMetaFile spire_meta_load_for_client(std::shared_ptr<Client> c, uint32_t serial_number, std::string* filename);
void spire_meta_save_file(const std::string& filename, uint32_t serial_number, const SpireMetaFile& file);
std::vector<uint16_t> spire_meta_flags_for_finish(const SpireMetaFinish& f, uint8_t char_class, bool new_best_time);
const char* spire_meta_flag_name(uint16_t flag_num); // nullptr for a flag that is not announced

// v52 (B-BASIS-2, gap of B-BASIS-32): the start_level upgrade raises the run copy's level by one per owned level. The
// run copy comes from the quest's rule block: the GC client applies its ba_set_char_level (F871) itself at ba_start
// on Pioneer 2 (research battle-mode-gc.md §1, make_spire.py battle_rules), and newserv mirrors it in the battle
// overlay (Client::create_battle_overlay, level and stats from the class's level table). So the server raises both:
// at the quest load it sends each client its own quest .bin with the rule block's level raised (the client builds the
// run copy from it, as from any Sega Battle quest; no push), and it builds that client's overlay with the same level.
// The run again ('I') pushes the overlay, so there the raised overlay is the run copy. A bin whose rule block it cannot
// find exactly once is sent as it is, and that client's overlay then keeps the quest's level (never a mismatch).
// The bonus (route B F3): the catalogue's start_level Levels[min(owned, Max)] (SPIRE_START_LEVEL_FALLBACK_LEVELS without
// a readable catalogue, the shipped table, so a broken catalogue never takes bought levels away); the level at most
// SPIRE_START_LEVEL_LEVEL_CAP (create_battle_overlay's clamp, 0-based). Bought start levels count 0 toward P
// (B-BASIS-67): the catalogue's start_level P is all 0, so they add no heat.
constexpr std::array<uint32_t, 4> SPIRE_START_LEVEL_FALLBACK_LEVELS = {0, 5, 10, 14};
constexpr uint32_t SPIRE_START_LEVEL_FALLBACK_MAX = SPIRE_START_LEVEL_FALLBACK_LEVELS.size() - 1; // owned level cap
constexpr uint32_t SPIRE_START_LEVEL_LEVEL_CAP = 199;
uint32_t spire_start_level_bonus(const SpireShopCatalogue* cat, const std::map<std::string, uint32_t>& upgrades);
uint32_t spire_start_level_char_level(uint32_t base_char_level, uint32_t bonus); // 0-based, as ba_set_char_level
// In a decompressed GC/v3 quest .bin: the rule block's "arg_pushl base_char_level; ba_set_char_level" (49 <u32 LE>
// F8 71). Returns how many there are and replaces the level with new_char_level only if there is exactly one.
size_t spire_start_level_patch_bin(std::string& decompressed_bin, uint32_t base_char_level, uint32_t new_char_level);

// v53 (B-BASIS-54): the heat steps server-side. A Spire quest built with the heat steps carries the override rows of H 0
// in a layout that is the same for every step H 0..HeatMax, and a sidecar SPIRE_HEAT_PATCH_DIR/q<N>.json (pso-roguelike
// quests/spire make_spire.py heat_patch_sidecar) that lists every byte range that differs over H: its offset in the
// DECOMPRESSED .bin, its width and its bytes per step. The server writes the bytes of one step into the .bin it sends
// (spire_heat_quest_bin, SpireFlagBridge.hh): the client then plays the rows of that H with no heat switch of its own.
//   {"Quest": 921, "BinSize": 160848, "HeatMax": 7, "Fields": [{"Offset": 1234, "Width": 36, "Values": ["3A00..", ...]}]}
constexpr const char* SPIRE_HEAT_PATCH_DIR = "system/spire-heat";
struct SpireHeatPatch {
  struct Field {
    size_t offset = 0;
    std::vector<std::string> values; // values[h]: the bytes of step h, all of one width
  };
  uint32_t quest_number = 0;
  size_t bin_size = 0;
  uint32_t heat_max = 0;
  std::vector<Field> fields; // by offset, never overlapping
};
// Throws on a malformed sidecar: a missing key, a value that is no even-length hex, a field without HeatMax + 1
// values or with values of different widths (or width 0), a field past BinSize, overlapping fields.
SpireHeatPatch spire_heat_patch_parse(const std::string& json_text);
// Writes the bytes of step min(heat, HeatMax) into a decompressed .bin and returns that step. Throws, with the .bin
// unchanged, unless the .bin has BinSize bytes and every field holds its H 0 bytes (the sidecar of another build).
uint32_t spire_heat_patch_bin(std::string& decompressed_bin, const SpireHeatPatch& patch, uint32_t heat);
