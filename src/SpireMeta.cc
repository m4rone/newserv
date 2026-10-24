#include "SpireMeta.hh"

#include <filesystem>
#include <format>
#include <map>
#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>

#include "CommandFormats.hh"
#include "SendCommands.hh"
#include "SpireFlagBridge.hh"
#include "StaticGameData.hh"

// Keyed by (difficulty, flag number); true = set, false = cleared. Same meaning as the bridge's flag entries.
using SpireMetaFlags = std::map<std::pair<uint8_t, uint16_t>, bool>;

static std::string spire_meta_filename(std::shared_ptr<Client> c, uint32_t serial_number) {
  auto character = c->character_file(false, false);
  if (!character) {
    return "";
  }
  return std::format("{}/{:010}.{}.meta.json", SPIRE_BRIDGE_DIRECTORY, serial_number,
      spire_guard_character_id(character->disp));
}

static SpireMetaFlags spire_meta_load(const std::string& filename) {
  SpireMetaFlags ret;
  if (!std::filesystem::is_regular_file(filename)) {
    return ret;
  }
  auto json = phosg::JSON::parse(phosg::load_file(filename));
  for (const auto& entry_json : json.get_list("Flags")) {
    uint8_t difficulty = entry_json->get_int("Difficulty");
    uint16_t flag_num = entry_json->get_int("Flag");
    if ((difficulty <= 3) && (flag_num >= SPIRE_BRIDGE_FIRST_FLAG) && (flag_num <= SPIRE_BRIDGE_LAST_FLAG)) {
      ret[std::make_pair(difficulty, flag_num)] = entry_json->get_bool("Set");
    }
  }
  return ret;
}

static void spire_meta_save(const std::string& filename, uint32_t serial_number, const SpireMetaFlags& flags) {
  phosg::JSON flags_json = phosg::JSON::list();
  for (const auto& [key, is_set] : flags) {
    flags_json.emplace_back(phosg::JSON::dict({{"Difficulty", key.first}, {"Flag", key.second}, {"Set", is_set}}));
  }
  auto json = phosg::JSON::dict({{"SerialNumber", serial_number}, {"Flags", std::move(flags_json)}});
  spire_bridge_write_durable(filename,
      json.serialize(phosg::JSON::SerializeOption::FORMAT | phosg::JSON::SerializeOption::HEX_INTEGERS));
}

bool spire_meta_record_flag(std::shared_ptr<Client> c, uint32_t serial_number, uint8_t difficulty, uint16_t flag_num,
    bool is_set) {
  std::string filename = spire_meta_filename(c, serial_number);
  if (filename.empty()) {
    return false;
  }
  auto flags = spire_meta_load(filename);
  flags[std::make_pair(difficulty, flag_num)] = is_set;
  spire_meta_save(filename, serial_number, flags);
  c->log.info_f("Spire meta: recorded {} quest flag {}:{:04X} in {} ({} stored)", is_set ? "set" : "cleared",
      name_for_difficulty(static_cast<Difficulty>(difficulty)), flag_num, filename, flags.size());
  return true;
}

void spire_meta_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number) {
  try {
    std::string filename = spire_meta_filename(c, serial_number);
    if (filename.empty()) {
      c->log.info_f("Spire meta: no character data for serial {:010}; no flags delivered", serial_number);
      return;
    }
    auto flags = spire_meta_load(filename);
    if (flags.empty()) {
      return;
    }
    // Same 6x75 form and 6D path as the bridge's delivery (send_game_flag_state_t); action 0 sets, 1 clears
    phosg::StringWriter w;
    for (const auto& [key, is_set] : flags) {
      w.put(G_UpdateQuestFlag_V3_BB_6x75{
          {{0x75, 0x03, 0x0000}, key.second, static_cast<uint16_t>(is_set ? 0 : 1)}, key.first, 0});
    }
    if (c->game_join_command_queue) {
      auto& cmd = c->game_join_command_queue->emplace_back();
      cmd.command = 0x006D;
      cmd.flag = c->lobby_client_id;
      cmd.data = std::move(w.str());
    } else {
      send_command(c, 0x6D, c->lobby_client_id, w.str());
    }
    c->log.info_f("Spire meta: delivered {} quest flag(s) from {} in {} game", flags.size(), filename,
        (l->mode == GameMode::BATTLE) ? "Battle" : "non-Battle");
  } catch (const std::exception& e) {
    c->log.warning_f("Spire meta: failed to deliver quest flags: {}", e.what());
  }
}

void spire_meta_set_flag(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint16_t flag_num) {
  if (spire_meta_record_flag(c, serial_number, static_cast<uint8_t>(l->difficulty), flag_num, true)) {
    spire_meta_deliver_flags(c, l, serial_number);
  }
}
