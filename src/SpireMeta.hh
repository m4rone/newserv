#pragma once

#include <memory>
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

// Called from spire_bridge_record_flag for a bridged quest flag write in range. Stores it for the writing character.
// Returns false if the client has no character data (the caller then does nothing). Throws on a file error.
bool spire_meta_record_flag(std::shared_ptr<Client> c, uint32_t serial_number, uint8_t difficulty, uint16_t flag_num,
    bool is_set);

// Called from spire_bridge_deliver_flags. Sends the character's stored flags (6x75 in one 6D). Never throws.
void spire_meta_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number);

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
