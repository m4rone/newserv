#pragma once

#include <stdint.h>

#include <memory>

#include "Client.hh"
#include "Lobby.hh"
#include "StaticGameData.hh"

// Spire flag bridge (GC V3 only).
//
// Battle quests with replace_char enabled run on a temporary overlay character on the client, so any quest flag the
// quest script sets during the battle is lost once the battle ends. The bridge works around this on the server side:
// while a GC V3 client is in a Battle game, quest flag writes in a small configured range are recorded per GC license
// serial number in system/spire-bridge/<serial>.json. When the same client next finishes loading into a non-Battle
// game, the recorded flags are sent back to it as 6x75 commands and the stored entries are erased.

// The range of quest flags that is bridged (inclusive on both ends). Flags outside this range are never recorded.
constexpr uint16_t SPIRE_BRIDGE_FIRST_FLAG = 0x0380;
constexpr uint16_t SPIRE_BRIDGE_LAST_FLAG = 0x03AF;

// Directory (relative to the server's working directory) where the per-serial JSON files are stored.
constexpr const char* SPIRE_BRIDGE_DIRECTORY = "system/spire-bridge";

// Called from the 6x75 handler for every valid quest flag write. Does nothing unless the client is GC V3, the lobby is
// a Battle game, the client has a GC license, and the flag is in the bridged range. Never throws.
void spire_bridge_record_flag(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, Difficulty difficulty, uint16_t flag_num, bool is_set);

// Called when a client has finished loading into a game (6F). Does nothing unless the client is GC V3, the lobby is a
// non-Battle game, and there are stored entries for the client's GC license serial. Never throws.
void spire_bridge_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);
