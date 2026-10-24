#include "SpireFlagBridge.hh"

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <unordered_set>
#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <utility>

#include "AsyncUtils.hh"
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

// Spire guard (v6, v7). The snapshot file sits next to the bridge file and is never touched by spire_bridge_save, so
// the markers and the snapshot cannot be dropped by a flag or register write.

struct SpireGuardSnapshot {
  // Set at quest load; a 98 or the next login turns it into restore_pending (v8)
  bool run_in_progress = false;
  // Cleared only when the 61 requested right after the restore push shows the snapshot's level and EXP (v8)
  bool restore_pending = false;
  uint32_t quest_number = 0;
  uint64_t snapshot_time_usecs = 0;
  bool has_character = false;
  PlayerInventory inventory;
  PlayerDispDataV4 disp;
};

static std::string spire_guard_filename(uint32_t serial_number) {
  return std::format("{}/{:010}.snapshot.json", SPIRE_BRIDGE_DIRECTORY, serial_number);
}

static std::string spire_guard_hex_encode(const void* data, size_t size) {
  static const char* digits = "0123456789ABCDEF";
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  std::string ret;
  ret.reserve(size * 2);
  for (size_t z = 0; z < size; z++) {
    ret.push_back(digits[bytes[z] >> 4]);
    ret.push_back(digits[bytes[z] & 0x0F]);
  }
  return ret;
}

static uint8_t spire_guard_hex_digit(char ch) {
  if ((ch >= '0') && (ch <= '9')) {
    return ch - '0';
  }
  if ((ch >= 'A') && (ch <= 'F')) {
    return ch - 'A' + 10;
  }
  if ((ch >= 'a') && (ch <= 'f')) {
    return ch - 'a' + 10;
  }
  throw std::runtime_error("invalid hex digit in snapshot");
}

// Decodes a hex string into a structure of exactly the given size; throws if the length does not match.
static void spire_guard_hex_decode(const std::string& hex, void* data, size_t size) {
  if (hex.size() != size * 2) {
    throw std::runtime_error(std::format("snapshot field has {} hex digits; expected {}", hex.size(), size * 2));
  }
  uint8_t* bytes = reinterpret_cast<uint8_t*>(data);
  for (size_t z = 0; z < size; z++) {
    bytes[z] = (spire_guard_hex_digit(hex[z * 2]) << 4) | spire_guard_hex_digit(hex[z * 2 + 1]);
  }
}

static SpireGuardSnapshot spire_guard_load(const std::string& filename) {
  SpireGuardSnapshot ret;
  if (!std::filesystem::is_regular_file(filename)) {
    return ret;
  }
  auto json = phosg::JSON::parse(phosg::load_file(filename));
  ret.run_in_progress = json.get_bool("RunInProgress", false);
  ret.restore_pending = json.get_bool("RestorePending", false);
  ret.quest_number = json.get_int("QuestNumber", 0);
  ret.snapshot_time_usecs = json.get_int("SnapshotTime", 0);
  if (json.contains("Inventory") && json.contains("Disp")) {
    spire_guard_hex_decode(json.get_string("Inventory"), &ret.inventory, sizeof(ret.inventory));
    spire_guard_hex_decode(json.get_string("Disp"), &ret.disp, sizeof(ret.disp));
    ret.has_character = true;
  }
  return ret;
}

// Writes the snapshot file atomically, the same way as spire_bridge_save. Name, Level, Class and Meseta are only there
// for people reading the file; loading uses the hex fields.
static void spire_guard_save(const std::string& filename, uint32_t serial_number, const SpireGuardSnapshot& snapshot) {
  auto json = phosg::JSON::dict({
      {"SerialNumber", serial_number},
      {"RunInProgress", snapshot.run_in_progress},
      {"RestorePending", snapshot.restore_pending},
      {"QuestNumber", snapshot.quest_number},
      {"SnapshotTime", snapshot.snapshot_time_usecs},
  });
  if (snapshot.has_character) {
    Language language = snapshot.inventory.language;
    json.emplace("Name", snapshot.disp.visual.name.decode(language));
    json.emplace("Level", snapshot.disp.stats.level + 1);
    json.emplace("Class", snapshot.disp.visual.sh.char_class);
    json.emplace("Meseta", snapshot.disp.stats.meseta.load());
    json.emplace("Inventory", spire_guard_hex_encode(&snapshot.inventory, sizeof(snapshot.inventory)));
    json.emplace("Disp", spire_guard_hex_encode(&snapshot.disp, sizeof(snapshot.disp)));
  }
  std::string data = json.serialize(phosg::JSON::SerializeOption::FORMAT);

  std::filesystem::create_directories(SPIRE_BRIDGE_DIRECTORY);
  std::string temp_filename = filename + ".tmp";
  phosg::save_file(temp_filename, data);
  std::filesystem::rename(temp_filename, filename);
}

static std::string spire_guard_describe(const SpireGuardSnapshot& snapshot) {
  if (!snapshot.has_character) {
    return "no character";
  }
  return std::format("{} Lv{} meseta {}", snapshot.disp.visual.name.decode(snapshot.inventory.language),
      snapshot.disp.stats.level + 1, snapshot.disp.stats.meseta.load());
}

void spire_guard_begin_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  try {
    if (!l->is_game() || (l->mode != GameMode::BATTLE) || !l->quest) {
      return;
    }
    const auto& rules = l->quest->meta.battle_rules;
    if (!rules || !rules->replace_char) {
      return;
    }
    uint32_t quest_number = l->quest->meta.quest_number;
    if (!spire_bridge_quest_is_bridged(c, quest_number)) {
      return;
    }
    uint32_t serial_number = spire_bridge_serial_for_client(c);
    if (!serial_number) {
      return;
    }

    std::string filename = spire_guard_filename(serial_number);
    auto snapshot = spire_guard_load(filename);
    if (snapshot.run_in_progress || snapshot.restore_pending) {
      c->log.info_f("Spire guard: {} for serial {:010} (q{}); kept snapshot {} from q{}",
          snapshot.restore_pending ? "restore pending" : "run already in progress", serial_number, quest_number,
          spire_guard_describe(snapshot), snapshot.quest_number);
      return;
    }

    // allow_load=false: on GC, loading would create an empty character; allow_overlay=false: never the overlay
    auto character = c->character_file(false, false);
    if (!character) {
      c->log.warning_f("Spire guard: no character data for serial {:010} (q{}); marker not set", serial_number, quest_number);
      return;
    }

    snapshot.run_in_progress = true;
    snapshot.quest_number = quest_number;
    snapshot.snapshot_time_usecs = phosg::now();
    snapshot.has_character = true;
    snapshot.inventory = character->inventory;
    snapshot.disp = character->disp;
    spire_guard_save(filename, serial_number, snapshot);
    c->log.info_f("Spire guard: run started for serial {:010} in q{}; marker set, snapshot {}",
        serial_number, quest_number, spire_guard_describe(snapshot));

  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to start run: {}", e.what());
  }
}

void spire_guard_end_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  try {
    // v8: a 98 is never proof that the card holds the real character. Measured on the Wii: a normal end of a Spire run
    // (end, warp, Quit, "Wrote to Memory Card A") also saves the overlay character. So a 98 with RunInProgress set,
    // from whichever game, turns the run into a pending restore; only a verified restore at the next login closes it.
    uint32_t serial_number = spire_bridge_serial_for_client(c);
    if (!serial_number) {
      return;
    }
    uint32_t quest_number = (l && l->is_game() && l->quest) ? l->quest->meta.quest_number : 0;

    std::string filename = spire_guard_filename(serial_number);
    auto snapshot = spire_guard_load(filename);
    if (snapshot.restore_pending) {
      c->log.info_f("Spire guard: 98 in q{} for serial {:010} leaves the pending restore of {} open",
          quest_number, serial_number, spire_guard_describe(snapshot));
      return;
    }
    if (!snapshot.run_in_progress) {
      return;
    }
    snapshot.run_in_progress = false;
    snapshot.restore_pending = true;
    spire_guard_save(filename, serial_number, snapshot);
    c->log.info_f("Spire guard: run ended for serial {:010} (98 in q{}); restore pending for the next login, snapshot {} kept",
        serial_number, quest_number, spire_guard_describe(snapshot));

  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to end run: {}", e.what());
  }
}

// How long the guard waits for each client answer during a restore (30, the B3 of SetExtendedPlayerInfo, and the
// verifying 61). Each answer is at most about 10 KB on a LAN and normally arrives within a second; 15 seconds leaves
// room for a slow Wii without letting a stuck login sit forever.
static constexpr auto SPIRE_GUARD_ANSWER_TIMEOUT = std::chrono::seconds(15);

// Drops the client if *done is still false when the timeout expires. Dropping cancels the pending promises
// (Client::cancel_pending_promises), so the waiting restore throws and the caller never sends 97.
static asio::awaitable<void> spire_guard_watchdog(
    std::weak_ptr<Client> weak_c, std::shared_ptr<bool> done, uint32_t serial_number, std::string what) {
  co_await async_sleep(SPIRE_GUARD_ANSWER_TIMEOUT);
  if (*done) {
    co_return;
  }
  auto c = weak_c.lock();
  if (!c) {
    co_return;
  }
  c->log.warning_f("Spire guard: no {} within {} s for serial {:010}; dropping the connection, no 97 (marker kept)",
      what, SPIRE_GUARD_ANSWER_TIMEOUT.count(), serial_number);
  c->channel->disconnect();
  c->cancel_pending_promises();
}

// Arms the watchdog for one client answer; the answer counts as arrived when this goes out of scope, also when the
// wait throws.
struct SpireGuardWatchdog {
  std::shared_ptr<bool> done = std::make_shared<bool>(false);
  SpireGuardWatchdog(asio::any_io_executor executor, std::shared_ptr<Client> c, uint32_t serial_number, const char* what) {
    asio::co_spawn(executor, spire_guard_watchdog(c, this->done, serial_number, what), asio::detached);
  }
  SpireGuardWatchdog(const SpireGuardWatchdog&) = delete;
  SpireGuardWatchdog& operator=(const SpireGuardWatchdog&) = delete;
  ~SpireGuardWatchdog() {
    *this->done = true;
  }
};

// Verifies the 61 that the client sent right after the push: level and EXP must equal the snapshot. The inventory
// count is only logged. Clears RestorePending and returns true if verified.
static bool spire_guard_verify_restore(std::shared_ptr<Client> c, uint32_t serial_number, const std::string& filename,
    SpireGuardSnapshot& snapshot, std::shared_ptr<const PSOBBCharacterFile> character) {
  if (!character) {
    c->log.warning_f("Spire guard: restore not verified for serial {:010}: no character in the 61; marker kept", serial_number);
    return false;
  }
  uint32_t level = character->disp.stats.level;
  uint32_t exp = character->disp.stats.exp;
  uint32_t snapshot_level = snapshot.disp.stats.level;
  uint32_t snapshot_exp = snapshot.disp.stats.exp;
  size_t num_items = character->inventory.num_items;
  size_t snapshot_num_items = snapshot.inventory.num_items;
  std::string items_str = std::format("inventory {} items, snapshot {} ({})",
      num_items, snapshot_num_items, (num_items == snapshot_num_items) ? "equal" : "different");
  if ((level != snapshot_level) || (exp != snapshot_exp)) {
    c->log.warning_f("Spire guard: restore not verified for serial {:010}: 61 shows Lv{} EXP {}, snapshot Lv{} EXP {}; {}; marker kept",
        serial_number, level + 1, exp, snapshot_level + 1, snapshot_exp, items_str);
    return false;
  }
  snapshot.restore_pending = false;
  spire_guard_save(filename, serial_number, snapshot);
  c->log.info_f("Spire guard: restore verified for serial {:010}: 61 shows Lv{} EXP {}; {}; marker cleared",
      serial_number, level + 1, exp, items_str);
  return true;
}

// Clients for which a restore is running (v9). The login restore and the lobby restore share the client's promises
// (character_data_ready_promise, function_call_response_queue), so at most one restore runs per client at a time.
static std::unordered_set<const Client*> spire_guard_restoring_clients;

struct SpireGuardRestoreLock {
  const Client* client;
  bool locked;
  explicit SpireGuardRestoreLock(const Client* c) : client(c), locked(spire_guard_restoring_clients.emplace(c).second) {}
  SpireGuardRestoreLock(const SpireGuardRestoreLock&) = delete;
  SpireGuardRestoreLock& operator=(const SpireGuardRestoreLock&) = delete;
  ~SpireGuardRestoreLock() {
    if (this->locked) {
      spire_guard_restoring_clients.erase(this->client);
    }
  }
};

// The restore itself, shared by the login path and the lobby path: fetch the full character (30), put the snapshot's
// inventory and disp in, send it with SetExtendedPlayerInfo, and verify the 61 requested right after. Returns true
// only if the restore was verified (RestorePending is then cleared). where is "login" or "lobby" (for the log).
static asio::awaitable<bool> spire_guard_run_restore(std::shared_ptr<Client> c, uint32_t serial_number,
    const std::string& filename, SpireGuardSnapshot snapshot, const char* where) {
  c->log.info_f("Spire guard: restore pending for serial {:010} (q{}); restoring {} in the {}",
      serial_number, snapshot.quest_number, spire_guard_describe(snapshot), where);
  if (!snapshot.has_character) {
    c->log.warning_f("Spire guard: snapshot for serial {:010} has no character; nothing restored, marker kept",
        serial_number);
    co_return false;
  }
  if (!c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL) ||
      !c->check_flag(Client::Flag::SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE)) {
    c->log.warning_f("Spire guard: client cannot run functions (B2); nothing restored for serial {:010}, marker kept",
        serial_number);
    co_return false;
  }

  try {
    auto executor = co_await asio::this_coro::executor;

    // The 61 data the server has lacks the bank, quest flags and the rest of the save file, so the snapshot never
    // replaces the whole character: the client's current full character is fetched first and only the parts that the
    // overlay changes (inventory and disp) are put back.
    GetPlayerInfoResult info;
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "full character (30)");
      info = co_await send_get_player_info(c, true);
    }
    if (!info.is_full_info || !info.character) {
      c->log.warning_f("Spire guard: client sent no full character (no 30); nothing restored for serial {:010}, marker kept",
          serial_number);
      co_return false;
    }
    auto merged = info.character;
    uint8_t level_before = merged->disp.stats.level;
    auto visual = merged->disp.visual;
    merged->inventory = snapshot.inventory;
    merged->disp = snapshot.disp;
    merged->disp.visual = visual;

    // Same function call as $loadchar
    co_await prepare_client_for_patches(c);
    auto s = c->require_server_state();
    auto fn = s->data->client_functions->get("SetExtendedPlayerInfo", c->specific_version);
    PSOGCCharacterFile::Character gc_char = *merged;
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "answer to SetExtendedPlayerInfo (B3)");
      co_await send_function_call(c, fn, {}, &gc_char, sizeof(gc_char));
    }
    c->log.info_f("Spire guard: sent {} for serial {:010} (was Lv{}); asking for a 61 to verify",
        spire_guard_describe(snapshot), serial_number, level_before + 1);

    // Only this 61, requested after the push, can verify the restore (v8): no earlier 61 and no 98 counts. on_61_98
    // also puts it into the server's character data.
    GetPlayerInfoResult after;
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "verifying 61");
      after = co_await send_get_player_info(c, false);
    }
    if (!spire_guard_verify_restore(c, serial_number, filename, snapshot, after.character)) {
      co_return false;
    }
    co_return true;

  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to restore character for serial {:010}: {}; marker kept", serial_number, e.what());
    co_return false;
  }
}

asio::awaitable<bool> spire_guard_restore_if_needed(std::shared_ptr<Client> c) {
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  if (!serial_number) {
    co_return true;
  }

  std::string filename;
  SpireGuardSnapshot snapshot;
  try {
    filename = spire_guard_filename(serial_number);
    snapshot = spire_guard_load(filename);
    if (!snapshot.run_in_progress && !snapshot.restore_pending) {
      co_return true;
    }
    if (snapshot.run_in_progress) {
      // The run ended without a 98. From here on only a verified restore closes it (v7).
      snapshot.run_in_progress = false;
      snapshot.restore_pending = true;
      spire_guard_save(filename, serial_number, snapshot);
    }
  } catch (const std::exception& e) {
    // An unreadable snapshot file cannot tell whether a restore is open, so the login is not allowed to save
    c->log.warning_f("Spire guard: failed to read snapshot for serial {:010}: {}; no 97", serial_number, e.what());
    co_return false;
  }

  SpireGuardRestoreLock lock(c.get());
  if (!lock.locked) {
    // Cannot happen in the normal flow (the login restore runs before any game); a 97 now could save a Lv1
    c->log.warning_f("Spire guard: a restore is already running for serial {:010}; no 97", serial_number);
    co_return false;
  }
  c->log.info_f("Spire guard: holding 97 back for serial {:010}", serial_number);
  if (!co_await spire_guard_run_restore(c, serial_number, filename, snapshot, "login")) {
    c->log.warning_f("Spire guard: login restore for serial {:010} not verified; no 97, dropping the login (marker kept)",
        serial_number);
    co_return false;
  }
  c->log.info_f("Spire guard: restored {} for serial {:010} at login; 97 follows", spire_guard_describe(snapshot), serial_number);
  co_return true;
}

asio::awaitable<void> spire_guard_restore_in_lobby(std::shared_ptr<Client> c) {
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  if (!serial_number) {
    co_return;
  }

  std::string filename;
  SpireGuardSnapshot snapshot;
  try {
    filename = spire_guard_filename(serial_number);
    snapshot = spire_guard_load(filename);
  } catch (const std::exception& e) {
    // The login restore stays the safety net; dropping here would not tell more than the next login does
    c->log.warning_f("Spire guard: failed to read snapshot for serial {:010} after a game: {}", serial_number, e.what());
    co_return;
  }
  if (!snapshot.restore_pending) {
    co_return;
  }

  SpireGuardRestoreLock lock(c.get());
  if (!lock.locked) {
    c->log.info_f("Spire guard: a restore is already running for serial {:010}; lobby restore skipped", serial_number);
    co_return;
  }
  if (!co_await spire_guard_run_restore(c, serial_number, filename, snapshot, "lobby")) {
    c->log.warning_f("Spire guard: lobby restore for serial {:010} not verified; dropping the connection (marker kept, the login restore follows)",
        serial_number);
    c->channel->disconnect();
    co_return;
  }

  // Same as $loadchar: let the other players in the lobby see the restored character
  try {
    auto l = c->lobby.lock();
    if (l) {
      auto s = c->require_server_state();
      send_player_leave_notification(l, c->lobby_client_id);
      s->send_lobby_join_notifications(l, c);
    }
  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to refresh the lobby after the restore: {}", e.what());
  }
  c->log.info_f("Spire guard: restored {} for serial {:010} in the lobby; marker cleared", spire_guard_describe(snapshot),
      serial_number);
}
