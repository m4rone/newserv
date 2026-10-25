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
SpireMetaFlags spire_meta_load(const std::string& filename); // empty if the file does not exist

// What a delivery sends: every stored entry, plus a clear of each server-owned flag (0x380-0x3A2) the character has no
// entry for on the game's difficulty. Pure, for the offline test.
struct SpireMetaDelivery {
  uint8_t difficulty = 0;
  uint16_t flag = 0;
  bool set = false;
};
std::vector<SpireMetaDelivery> spire_meta_delivery_entries(const SpireMetaFlags& stored, uint8_t difficulty);

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

// Called from the 'F' record code after the records are saved. Sets the run's unlock and payout flags for the
// character on the game's difficulty, delivers them and returns the text lines to add to the record text (empty if
// nothing is new). Throws on a file error.
std::string spire_meta_on_finish(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t finish_value, bool new_best_time);

// Pure part of spire_meta_on_finish, for the offline test: the flags a finished run sets, for a class (0-11).
struct SpireMetaFinish {
  bool won = false;
  uint8_t exams = 0;
  bool deathless = false;
};
SpireMetaFinish spire_meta_decode_finish(uint32_t finish_value);
std::vector<uint16_t> spire_meta_flags_for_finish(const SpireMetaFinish& f, uint8_t char_class, bool new_best_time);
const char* spire_meta_flag_name(uint16_t flag_num); // nullptr for a flag that is not announced
