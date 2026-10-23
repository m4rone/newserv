#include "SpireFlagBridge.hh"

#include <filesystem>
#include <format>
#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <utility>

#include "CommandFormats.hh"
#include "SendCommands.hh"

// Stored entries, keyed by (difficulty, flag number). The value is true if the flag was set, false if it was cleared.
// The last write for a given key wins.
using SpireBridgeEntries = std::map<std::pair<uint8_t, uint16_t>, bool>;

static bool spire_bridge_flag_in_range(uint16_t flag_num) {
  return (flag_num >= SPIRE_BRIDGE_FIRST_FLAG) && (flag_num <= SPIRE_BRIDGE_LAST_FLAG);
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

static SpireBridgeEntries spire_bridge_load(const std::string& filename) {
  SpireBridgeEntries ret;
  if (!std::filesystem::is_regular_file(filename)) {
    return ret;
  }
  auto json = phosg::JSON::parse(phosg::load_file(filename));
  for (const auto& entry_json : json.get_list("Flags")) {
    uint8_t difficulty = entry_json->get_int("Difficulty");
    uint16_t flag_num = entry_json->get_int("Flag");
    bool is_set = entry_json->get_bool("Set");
    if ((difficulty <= 3) && spire_bridge_flag_in_range(flag_num)) {
      ret[std::make_pair(difficulty, flag_num)] = is_set;
    }
  }
  return ret;
}

// Writes the entries atomically: the data goes to a temporary file in the same directory, which is then renamed over
// the destination. If there are no entries, the file is removed instead.
static void spire_bridge_save(const std::string& filename, uint32_t serial_number, const SpireBridgeEntries& entries) {
  if (entries.empty()) {
    std::filesystem::remove(filename);
    return;
  }

  phosg::JSON flags_json = phosg::JSON::list();
  for (const auto& [key, is_set] : entries) {
    flags_json.emplace_back(phosg::JSON::dict({
        {"Difficulty", key.first},
        {"Flag", key.second},
        {"Set", is_set},
    }));
  }
  auto json = phosg::JSON::dict({
      {"SerialNumber", serial_number},
      {"Flags", std::move(flags_json)},
  });
  std::string data = json.serialize(phosg::JSON::SerializeOption::FORMAT | phosg::JSON::SerializeOption::HEX_INTEGERS);

  std::filesystem::create_directories(SPIRE_BRIDGE_DIRECTORY);
  std::string temp_filename = filename + ".tmp";
  phosg::save_file(temp_filename, data);
  std::filesystem::rename(temp_filename, filename);
}

void spire_bridge_record_flag(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, Difficulty difficulty, uint16_t flag_num, bool is_set) {
  try {
    if (!l->is_game() || (l->mode != GameMode::BATTLE) || !spire_bridge_flag_in_range(flag_num)) {
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
    auto entries = spire_bridge_load(filename);
    entries[std::make_pair(static_cast<uint8_t>(difficulty), flag_num)] = is_set;
    spire_bridge_save(filename, serial_number, entries);
    c->log.info_f("Spire bridge: recorded {} quest flag {}:{:04X} for serial {:010} ({} stored)",
        is_set ? "set" : "cleared", name_for_difficulty(difficulty), flag_num, serial_number, entries.size());

  } catch (const std::exception& e) {
    c->log.warning_f("Spire bridge: failed to record quest flag {:04X}: {}", flag_num, e.what());
  }
}

void spire_bridge_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  try {
    if (!l->is_game() || (l->mode == GameMode::BATTLE)) {
      return;
    }
    uint32_t serial_number = spire_bridge_serial_for_client(c);
    if (!serial_number) {
      return;
    }

    std::string filename = spire_bridge_filename(serial_number);
    auto entries = spire_bridge_load(filename);
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

    spire_bridge_save(filename, serial_number, SpireBridgeEntries());
    c->log.info_f("Spire bridge: delivered {} quest flag(s) for serial {:010}; stored entries erased",
        entries.size(), serial_number);

  } catch (const std::exception& e) {
    c->log.warning_f("Spire bridge: failed to deliver quest flags: {}", e.what());
  }
}
