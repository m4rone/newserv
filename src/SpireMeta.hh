#pragma once

#include <memory>
#include <stdint.h>

#include "Client.hh"
#include "Lobby.hh"

// Spire meta (v22): the Spire quest flags 0x380-0x3AF (E04 milestones, pacts, second kits, relics; E06 payout) belong
// to a character, not to a memory card: system/spire-bridge/<serial>.<character id>.meta.json. Before v22 the bridge
// stored them per serial, so a second character on the same card got the first one's unlocks and could collect its
// payout in q925.

// Called from spire_bridge_record_flag for a bridged quest flag write in range. Stores it for the writing character.
// Returns false if the client has no character data (the caller then does nothing). Throws on a file error.
bool spire_meta_record_flag(std::shared_ptr<Client> c, uint32_t serial_number, uint8_t difficulty, uint16_t flag_num,
    bool is_set);

// Called from spire_bridge_deliver_flags. Sends the character's stored flags (6x75 in one 6D). Never throws.
void spire_meta_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number);

// Sets one flag for the character on the game's difficulty and delivers the flags (the 'F' record code: payout flag
// 0x3A2 on a new best time). Throws on a file error.
void spire_meta_set_flag(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint16_t flag_num);
