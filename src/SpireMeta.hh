#pragma once

#include <map>
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
  // v48 (B-BASIS-13): the rung the base opened per ladder run (1 Run I, 2 Run II; 0-3, see spire_meta_open_rung), in
  // the file as "BaseRung": {"1": n, "2": n}; written only if a run has one, so older files stay as they are
  std::map<uint8_t, uint8_t> base_rung;
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
uint16_t spire_meta_ladder_flag(uint8_t run); // 0x38B, 0x38C, 0 for neither
// The highest difficulty won open for a run (0 Normal ... 3 Ultimate): Normal always, then one past the highest won.
uint8_t spire_meta_ladder_rung(const SpireMetaFlags& flags, uint8_t run);
// v48 (B-BASIS-13, Marck via the regie): a rung is open if it was won (spire_meta_ladder_rung) or the base opened it
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
// The open rung of a run for a client's real character (its meta file, v48 won or BaseRung; 0 without one or on a read
// error). Never throws.
uint8_t spire_meta_ladder_rung_for_client(std::shared_ptr<const Client> c, uint8_t run);

// Called from the 'F' record code after the records are saved. Sets the run's unlock and payout flags for the
// character on the game's difficulty, delivers them and returns the text lines to add to the record text (empty if
// nothing is new). Throws on a file error.
std::string spire_meta_on_finish(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t finish_value, bool new_best_time);

// Pure part of spire_meta_on_finish, for the offline test: the flags a finished run sets, for a class (0-11).
struct SpireMetaFinish {
  bool won = false;
  uint8_t exams = 0;
  bool deathless = false;
};
SpireMetaFinish spire_meta_decode_finish(uint32_t finish_value);
std::vector<uint16_t> spire_meta_flags_for_finish(const SpireMetaFinish& f, uint8_t char_class, bool new_best_time);
const char* spire_meta_flag_name(uint16_t flag_num); // nullptr for a flag that is not announced
