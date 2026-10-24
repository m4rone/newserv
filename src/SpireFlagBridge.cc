#include "SpireFlagBridge.hh"

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <utility>

#include "CommandFormats.hh"
#include "SendCommands.hh"
#include "ServerState.hh"

// Stored entries, keyed by (difficulty, flag number). The value is true if the flag was set, false if it was cleared.
// The last write for a given key wins.
using SpireBridgeEntries = std::map<std::pair<uint8_t, uint16_t>, bool>;

// Stored meta registers, keyed by register number (only registers that spire_bridge_register_is_stored accepts).
using SpireBridgeRegisters = std::map<uint8_t, uint32_t>;

// Everything stored in one per-serial file. Flag writes and register writes both load and save the whole state, so
// neither one drops the other's data.
struct SpireBridgeState {
  SpireBridgeEntries flags;
  SpireBridgeRegisters registers;
};

static bool spire_bridge_flag_in_range(uint16_t flag_num) {
  return (flag_num >= SPIRE_BRIDGE_FIRST_FLAG) && (flag_num <= SPIRE_BRIDGE_LAST_FLAG);
}

// Registers 232-239 and 243-247 are stored. 240-242 are left alone (Sega's q101 uses them, and Schtserv commands use
// 240 and 241); 248 is the request register and is never stored.
static bool spire_bridge_register_is_stored(uint8_t register_number) {
  return ((register_number >= SPIRE_BRIDGE_FIRST_REGISTER) && (register_number <= 239)) ||
      ((register_number >= 243) && (register_number <= SPIRE_BRIDGE_LAST_REGISTER));
}

// Returns the GC license serial number for the client, or 0 if the bridge does not apply to this client.
static uint32_t spire_bridge_serial_for_client(std::shared_ptr<const Client> c) {
  if (c->version() != Version::GC_V3) {
    return 0;
  }
  if (!c->login || !c->login->gc_license) {
    return 0;
  }
  return c->login->gc_license->serial_number;
}

static std::string spire_bridge_filename(uint32_t serial_number) {
  // Same decimal format newserv uses when printing GC license serial numbers (see Account::str)
  return std::format("{}/{:010}.json", SPIRE_BRIDGE_DIRECTORY, serial_number);
}

static SpireBridgeState spire_bridge_load(const std::string& filename) {
  SpireBridgeState ret;
  if (!std::filesystem::is_regular_file(filename)) {
    return ret;
  }
  auto json = phosg::JSON::parse(phosg::load_file(filename));
  // v2 files have no Registers key; a file first written by a register write has an empty Flags list
  if (json.contains("Flags")) {
    for (const auto& entry_json : json.get_list("Flags")) {
      uint8_t difficulty = entry_json->get_int("Difficulty");
      uint16_t flag_num = entry_json->get_int("Flag");
      bool is_set = entry_json->get_bool("Set");
      if ((difficulty <= 3) && spire_bridge_flag_in_range(flag_num)) {
        ret.flags[std::make_pair(difficulty, flag_num)] = is_set;
      }
    }
  }
  if (json.contains("Registers")) {
    for (const auto& entry_json : json.get_list("Registers")) {
      int64_t register_number = entry_json->get_int("Register");
      uint32_t value = entry_json->get_int("Value");
      if ((register_number >= 0) && (register_number < 0x100) && spire_bridge_register_is_stored(register_number)) {
        ret.registers[register_number] = value;
      }
    }
  }
  return ret;
}

// Writes the state atomically: the data goes to a temporary file in the same directory, which is then renamed over
// the destination.
static void spire_bridge_save(const std::string& filename, uint32_t serial_number, const SpireBridgeState& state) {
  phosg::JSON flags_json = phosg::JSON::list();
  for (const auto& [key, is_set] : state.flags) {
    flags_json.emplace_back(phosg::JSON::dict({
        {"Difficulty", key.first},
        {"Flag", key.second},
        {"Set", is_set},
    }));
  }
  phosg::JSON registers_json = phosg::JSON::list();
  for (const auto& [register_number, value] : state.registers) {
    registers_json.emplace_back(phosg::JSON::dict({
        {"Register", register_number},
        {"Value", value},
    }));
  }
  auto json = phosg::JSON::dict({
      {"SerialNumber", serial_number},
      {"Flags", std::move(flags_json)},
      {"Registers", std::move(registers_json)},
  });
  std::string data = json.serialize(phosg::JSON::SerializeOption::FORMAT | phosg::JSON::SerializeOption::HEX_INTEGERS);

  std::filesystem::create_directories(SPIRE_BRIDGE_DIRECTORY);
  std::string temp_filename = filename + ".tmp";
  phosg::save_file(temp_filename, data);
  std::filesystem::rename(temp_filename, filename);
}

static bool spire_bridge_quest_is_bridged(std::shared_ptr<const Client> c, uint32_t quest_number);

void spire_bridge_record_flag(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, Difficulty difficulty, uint16_t flag_num, bool is_set) {
  try {
    if (!l->is_game() || !spire_bridge_flag_in_range(flag_num)) {
      return;
    }
    // Only flag writes made while a SpireBridgeQuests quest runs are stored (v5); delivery on join stays unconditional
    if (!l->quest || !spire_bridge_quest_is_bridged(c, l->quest->meta.quest_number)) {
      return;
    }
    if (static_cast<size_t>(difficulty) > 3) {
      return;
    }
    uint32_t serial_number = spire_bridge_serial_for_client(c);
    if (!serial_number) {
      return;
    }

    std::string filename = spire_bridge_filename(serial_number);
    auto state = spire_bridge_load(filename);
    state.flags[std::make_pair(static_cast<uint8_t>(difficulty), flag_num)] = is_set;
    spire_bridge_save(filename, serial_number, state);
    c->log.info_f("Spire bridge: recorded {} quest flag {}:{:04X} for serial {:010} in {} game ({} stored)",
        is_set ? "set" : "cleared", name_for_difficulty(difficulty), flag_num, serial_number,
        (l->mode == GameMode::BATTLE) ? "Battle" : "non-Battle", state.flags.size());

  } catch (const std::exception& e) {
    c->log.warning_f("Spire bridge: failed to record quest flag {:04X}: {}", flag_num, e.what());
  }
}

void spire_bridge_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  try {
    if (!l->is_game()) {
      return;
    }
    uint32_t serial_number = spire_bridge_serial_for_client(c);
    if (!serial_number) {
      return;
    }

    std::string filename = spire_bridge_filename(serial_number);
    const auto entries = spire_bridge_load(filename).flags;
    if (entries.empty()) {
      return;
    }

    // Same 6x75 format and delivery path as send_game_flag_state_t (SendCommands.cc). Action 0 sets the flag; 1 clears
    // it.
    phosg::StringWriter w;
    for (const auto& [key, is_set] : entries) {
      w.put(G_UpdateQuestFlag_V3_BB_6x75{
          {{0x75, 0x03, 0x0000}, key.second, static_cast<uint16_t>(is_set ? 0 : 1)}, key.first, 0});
      c->log.info_f("Spire bridge: sending {} quest flag {}:{:04X}",
          is_set ? "set" : "clear", name_for_difficulty(static_cast<Difficulty>(key.first)), key.second);
    }
    if (c->game_join_command_queue) {
      c->log.info_f("Client not ready to receive join commands; adding to queue");
      auto& cmd = c->game_join_command_queue->emplace_back();
      cmd.command = 0x006D;
      cmd.flag = c->lobby_client_id;
      cmd.data = std::move(w.str());
    } else {
      send_command(c, 0x6D, c->lobby_client_id, w.str());
    }

    // The stored state is permanent: it is not erased after delivery, since the client does not save delivered flags
    // to its memory card.
    c->log.info_f("Spire bridge: delivered {} quest flag(s) for serial {:010} in {} game",
        entries.size(), serial_number, (l->mode == GameMode::BATTLE) ? "Battle" : "non-Battle");

  } catch (const std::exception& e) {
    c->log.warning_f("Spire bridge: failed to deliver quest flags: {}", e.what());
  }
}

// Merges a newly written value into the stored one. Returns the new stored value and sets how to the name of the rule
// that was applied (for the log).
static uint32_t spire_bridge_merge_register(
    uint8_t register_number, uint32_t old_value, uint32_t new_value, const char** how) {
  switch (register_number) {
    case 232: // runs started (low 16 bits) and won (high 16 bits); max on the whole value
    case 233: // best score
      *how = "max";
      return std::max(old_value, new_value);
    case 234: // best time in seconds; 0 means none, so a 0 on either side never wins
      *how = "min, 0 ignored";
      if (old_value == 0) {
        return new_value;
      }
      if (new_value == 0) {
        return old_value;
      }
      return std::min(old_value, new_value);
    case 235:
    case 236:
    case 237:
    case 238:
    case 239:
    case 245:
      *how = "or";
      return old_value | new_value;
    default: // 243, 244, 246, 247
      *how = "last";
      return new_value;
  }
}

// Only quests listed in SpireBridgeQuests are bridged; a missing or empty list means no quest is.
static bool spire_bridge_quest_is_bridged(std::shared_ptr<const Client> c, uint32_t quest_number) {
  try {
    return c->require_server_state()->data->spire_bridge_quest_numbers.count(quest_number) != 0;
  } catch (const std::exception&) {
    return false;
  }
}

bool spire_bridge_handle_register(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint16_t register_number, uint32_t value) {
  if (!l->is_game() || !l->quest) {
    return false;
  }
  uint32_t quest_number = l->quest->meta.quest_number;
  if (!spire_bridge_quest_is_bridged(c, quest_number)) {
    return false;
  }
  bool is_request = (register_number == SPIRE_BRIDGE_REQUEST_REGISTER);
  if (!is_request && ((register_number >= 0x100) || !spire_bridge_register_is_stored(register_number))) {
    return false;
  }
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  if (!serial_number) {
    return false;
  }

  // From here on the write belongs to the bridge: it is never forwarded, even if storing or answering fails, so a
  // co-op partner never takes over this client's meta registers.
  try {
    std::string filename = spire_bridge_filename(serial_number);

    if (is_request) {
      if (value != SPIRE_BRIDGE_REQUEST_VALUE) {
        c->log.info_f("Spire bridge: ignored register {} value {:08X} for serial {:010} in q{}",
            register_number, value, serial_number, quest_number);
        return true;
      }

      // Same 6x77 form and delivery path as the Schtserv PVER response in on_sync_quest_register
      auto state = spire_bridge_load(filename);
      std::string values_str;
      for (size_t z = SPIRE_BRIDGE_FIRST_REGISTER; z <= SPIRE_BRIDGE_LAST_REGISTER; z++) {
        if (!spire_bridge_register_is_stored(z)) {
          continue;
        }
        auto it = state.registers.find(z);
        uint32_t stored_value = (it == state.registers.end()) ? 0 : it->second;
        G_SyncQuestRegister_6x77 ret_cmd;
        ret_cmd.header.subcommand = 0x77;
        ret_cmd.header.size = sizeof(ret_cmd) / 4;
        ret_cmd.header.unused = 0;
        ret_cmd.register_number = z;
        ret_cmd.value.as_int = stored_value;
        send_command_t(c, 0x60, 0x00, ret_cmd);
        values_str += std::format(" r{}={:08X}", z, stored_value);
      }
      G_SyncQuestRegister_6x77 done_cmd;
      done_cmd.header.subcommand = 0x77;
      done_cmd.header.size = sizeof(done_cmd) / 4;
      done_cmd.header.unused = 0;
      done_cmd.register_number = SPIRE_BRIDGE_REQUEST_REGISTER;
      done_cmd.value.as_int = SPIRE_BRIDGE_DONE_VALUE;
      send_command_t(c, 0x60, 0x00, done_cmd);
      c->log.info_f("Spire bridge: answered SPIR for serial {:010} in q{}:{} r{}=DONE",
          serial_number, quest_number, values_str, SPIRE_BRIDGE_REQUEST_REGISTER);
      return true;
    }

    auto state = spire_bridge_load(filename);
    // A register with no stored entry reads as 0, which is also what a SPIR answer sends for it
    auto it = state.registers.find(register_number);
    uint32_t old_value = (it == state.registers.end()) ? 0 : it->second;
    const char* how = "";
    uint32_t new_value = spire_bridge_merge_register(register_number, old_value, value, &how);
    bool changed = (new_value != old_value);
    if (changed) {
      state.registers[register_number] = new_value;
      spire_bridge_save(filename, serial_number, state);
    }
    c->log.info_f("Spire bridge: register {} for serial {:010} in q{}: wrote {:08X}, stored {:08X} -> {:08X} ({}{})",
        register_number, serial_number, quest_number, value, old_value, new_value, how,
        changed ? "" : ", unchanged");

  } catch (const std::exception& e) {
    c->log.warning_f("Spire bridge: failed to handle register {} value {:08X}: {}", register_number, value, e.what());
  }
  return true;
}
