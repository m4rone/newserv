#pragma once

#include <stdint.h>

#include <memory>

#include "Client.hh"
#include "Lobby.hh"
#include "StaticGameData.hh"

// Spire flag bridge (GC V3 only).
//
// Battle quests with replace_char enabled run on a temporary overlay character on the client, so any quest flag the
// quest script sets during the battle is lost once the battle ends. Measured on the lab: a 6x75 sent by the server
// sets the flag in the running game, but the GC client does not save it to the memory card as character state. So the
// server is the only source of truth for the bridged range:
// - Every quest flag write in the bridged range by a GC V3 client, in any game (Battle or not) that runs a quest listed
//   in SpireBridgeQuests (v5; a missing or empty list records nothing), is recorded per GC license serial number in
//   system/spire-bridge/<serial>.json. Both set and clear are recorded; the last write wins.
// - Every time the same client finishes loading into any game (Battle or not), all stored flags are sent to it as
//   6x75 commands (set or clear), so quest scripts in that game read the server's state. Stored entries are never
//   erased by delivery.
// - Spire meta registers (v4): in the quests listed in the server config key SpireBridgeQuests (a JSON list of quest
//   numbers; any game mode), 6x77 writes to registers 232-239 and 243-247 by a GC V3 client are merged into the same
//   per-serial file (232/233 max, 234 min with 0 ignored, 235-239 and 245 OR, 243/244/246/247 last write wins) and
//   are not forwarded, so a co-op partner never takes them over. A write of 'SPIR' to register 248 is answered to that
//   client only: one 6x77 per stored register (0 if none), then 248 = 'DONE'. Registers 240-242 are left alone (Sega's
//   q101 and Schtserv commands use them). If SpireBridgeQuests is missing or empty, the register bridge takes nothing:
//   every register write is handled exactly as without the bridge. The flag part above has no quest limit.

// The range of quest flags that is bridged (inclusive on both ends). Flags outside this range are never recorded.
constexpr uint16_t SPIRE_BRIDGE_FIRST_FLAG = 0x0380;
constexpr uint16_t SPIRE_BRIDGE_LAST_FLAG = 0x03AF;

// The meta registers that are stored (inclusive; 240-242 in between are not), and the request register and its values.
// The quest numbers in which the register bridge applies come from the config (DataIndex::spire_bridge_quest_numbers).
constexpr uint8_t SPIRE_BRIDGE_FIRST_REGISTER = 232;
constexpr uint8_t SPIRE_BRIDGE_LAST_REGISTER = 247;
constexpr uint8_t SPIRE_BRIDGE_REQUEST_REGISTER = 248;
constexpr uint32_t SPIRE_BRIDGE_REQUEST_VALUE = 0x53504952; // 'SPIR'
constexpr uint32_t SPIRE_BRIDGE_DONE_VALUE = 0x444F4E45; // 'DONE'

// Directory (relative to the server's working directory) where the per-serial JSON files are stored.
constexpr const char* SPIRE_BRIDGE_DIRECTORY = "system/spire-bridge";

// Called from the 6x75 handler for every valid quest flag write. Does nothing unless the client is GC V3, the lobby is
// a game running a quest listed in SpireBridgeQuests, the client has a GC license, and the flag is in the bridged
// range. Never throws.
void spire_bridge_record_flag(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, Difficulty difficulty, uint16_t flag_num, bool is_set);

// Called when a client has finished loading into a game (6F). Does nothing unless the client is GC V3, the lobby is a
// game, and there are stored entries for the client's GC license serial. Never throws.
void spire_bridge_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);

// Called from the 6x77 handler for every valid quest register write. Returns true if the bridge took the write (the
// caller must then not forward it), false if the caller should handle it as before. Takes nothing unless the client
// is GC V3 with a GC license, the lobby is a game running a quest listed in SpireBridgeQuests (a missing or empty list
// means never), and the register is 232-239, 243-247 or 248. Never throws.
bool spire_bridge_handle_register(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint16_t register_number, uint32_t value);
