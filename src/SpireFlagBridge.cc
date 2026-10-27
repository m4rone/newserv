#include "SpireFlagBridge.hh"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <phosg/Filesystem.hh>
#include <phosg/Hash.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <utility>
#include <fcntl.h>
#include <unistd.h>

#include "AsyncUtils.hh"
#include "Compression.hh"
#include "InventoryBackup.hh"
#include "Loggers.hh"
#include "ReceiveCommands.hh"
#include "CommandFormats.hh"
#include "SendCommands.hh"
#include "SpireBBATxGuard.hh"
#include "SpireEquipItem.hh"
#include "SpireDraw.hh"
#include "SpireDrops.hh"
#include "SpireMeta.hh"
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

// v12: writes data to filename so that a crash or power loss leaves either the old file or the new one, never a
// partial or empty one: write a temp file, fsync it, rename it over the target, then fsync the directory.
void spire_bridge_write_durable(const std::string& filename, const std::string& data) {
  std::filesystem::create_directories(SPIRE_BRIDGE_DIRECTORY);
  std::string temp_filename = filename + ".tmp";
  int fd = ::open(temp_filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) {
    throw std::runtime_error(std::format("cannot open {}: errno {}", temp_filename, errno));
  }
  size_t offset = 0;
  while (offset < data.size()) {
    ssize_t written = ::write(fd, data.data() + offset, data.size() - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      int error = errno;
      ::close(fd);
      throw std::runtime_error(std::format("cannot write {}: errno {}", temp_filename, error));
    }
    offset += written;
  }
  if (::fsync(fd) != 0) {
    int error = errno;
    ::close(fd);
    throw std::runtime_error(std::format("cannot fsync {}: errno {}", temp_filename, error));
  }
  ::close(fd);
  std::filesystem::rename(temp_filename, filename);
  int dir_fd = ::open(SPIRE_BRIDGE_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dir_fd >= 0) {
    ::fsync(dir_fd);
    ::close(dir_fd);
  }
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

// Audit S-6: the meta registers belong to a character, like its flags (SpireMeta) and records, so a second character on
// the same card never reads the first one's registers. The file before this, <serial>.json, is no longer read. Returns
// "" if the client has no character data: the registers then read as 0 and a write is not stored.
static std::string spire_bridge_filename(std::shared_ptr<Client> c, uint32_t serial_number) {
  auto character = c->character_file(false, false);
  return spire_bridge_registers_filename_for(serial_number, character ? &character->disp : nullptr);
}

std::string spire_bridge_registers_filename_for(uint32_t serial_number, const PlayerDispDataV4* disp) {
  return disp ? spire_bridge_registers_filename(serial_number, spire_guard_character_id(*disp)) : "";
}

std::string spire_bridge_registers_filename(uint32_t serial_number, const std::string& character_id) {
  // Same decimal format newserv uses when printing GC license serial numbers (see Account::str)
  return std::format("{}/{:010}.{}.registers.json", SPIRE_BRIDGE_DIRECTORY, serial_number, character_id);
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

  spire_bridge_write_durable(filename, data);
}

static bool spire_bridge_quest_is_bridged(std::shared_ptr<const Client> c, uint32_t quest_number);

// Regie pso-regie-9: SpireMetaQuests (quests outside the run that read the meta flags); a missing list means none
static bool spire_meta_quest_is_listed(std::shared_ptr<const Client> c, uint32_t quest_number) {
  try {
    return c->require_server_state()->data->spire_meta_quest_numbers.count(quest_number) != 0;
  } catch (const std::exception&) {
    return false;
  }
}

static bool spire_client_run_is_practice(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l);

void spire_bridge_record_flag(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, Difficulty difficulty, uint16_t flag_num, bool is_set) {
  try {
    if (!l->is_game() || !spire_bridge_flag_in_range(flag_num)) {
      return;
    }
    // Only flag writes made while a SpireBridgeQuests quest runs are stored (v5), plus a payout clear in a
    // SpireMetaQuests quest (regie pso-regie-9); delivery too is limited to those quests
    if (!l->quest || !spire_meta_record_applies(spire_bridge_quest_is_bridged(c, l->quest->meta.quest_number),
                         spire_meta_quest_is_listed(c, l->quest->meta.quest_number), flag_num, is_set)) {
      return;
    }
    if (static_cast<size_t>(difficulty) > 3) {
      return;
    }
    uint32_t serial_number = spire_bridge_serial_for_client(c);
    if (!serial_number) {
      return;
    }
    // v44: a practice run unlocks nothing, so its flag writes (relics seen) are not stored
    if (spire_practice_blocks_flag(spire_client_run_is_practice(c, l), flag_num)) {
      c->log.info_f("Spire bridge: quest flag {:04X} in a practice run; not recorded", flag_num);
      return;
    }

    // v23: the flags belong to the character (SpireMeta), not to the card
    if (!spire_meta_record_flag(c, serial_number, static_cast<uint8_t>(difficulty), flag_num, is_set)) {
      c->log.info_f("Spire bridge: no character data for serial {:010}; quest flag {:04X} not recorded",
          serial_number, flag_num);
    }

  } catch (const std::exception& e) {
    c->log.warning_f("Spire bridge: failed to record quest flag {:04X}: {}", flag_num, e.what());
  }
}

void spire_bridge_deliver_flags(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  // Regie pso-regie-9: only a game running a Spire quest or a SpireMetaQuests quest gets the flags; the join of a game
  // without one (the town start's Battle game before its quest loads, any normal game) delivers nothing,
  // set_lobby_quest delivers at load
  if (!l || !spire_meta_delivery_applies(l->is_game(), !!l->quest, spire_game_is_spire(c, l),
                l->quest && spire_meta_quest_is_listed(c, l->quest->meta.quest_number))) {
    return;
  }
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  if (!serial_number) {
    return;
  }
  // v23: the character's flags (SpireMeta). The stored state is permanent: the client does not save delivered flags to
  // its memory card. Flags in a v5-v21 <serial>.json are no longer delivered: they belonged to the whole card.
  spire_meta_deliver_flags(c, l, serial_number);
}

bool spire_bridge_flag_is_shared(std::shared_ptr<const Client> c, std::shared_ptr<const Lobby> l, uint16_t flag_num) {
  try {
    bool meta_game = l && spire_meta_delivery_applies(l->is_game(), !!l->quest, spire_game_is_spire(c, l),
                              l->quest && spire_meta_quest_is_listed(c, l->quest->meta.quest_number));
    return spire_meta_flag_is_shared(meta_game, flag_num);
  } catch (const std::exception&) {
    return true;
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

// v15: the guard has its own list (SpireGuardQuests), so a probe quest can be protected without its flags and
// registers being stored under the player's serial. Without the key the guard follows SpireBridgeQuests, as before.
static bool spire_guard_quest_is_guarded(std::shared_ptr<const Client> c, uint32_t quest_number) {
  try {
    const auto& data = c->require_server_state()->data;
    if (data->spire_guard_quest_numbers) {
      return data->spire_guard_quest_numbers->count(quest_number) != 0;
    }
    return data->spire_bridge_quest_numbers.count(quest_number) != 0;
  } catch (const std::exception&) {
    return false;
  }
}

// Sends one 6x7D (what = 0: set battle phase) to every client in the game. Same delivery path as the 6x77 answers.
static void spire_bridge_send_battle_phase(std::shared_ptr<const Lobby> l, uint8_t phase) {
  G_SetBattleModeData_6x7D cmd;
  cmd.header.subcommand = 0x7D;
  cmd.header.size = sizeof(cmd) / 4;
  cmd.header.unused = 0;
  cmd.what = 0;
  cmd.phase = phase;
  for (const auto& lc : l->clients) {
    if (lc) {
      send_command_t(lc, 0x60, 0x00, cmd);
    }
  }
}

static asio::awaitable<void> spire_bridge_send_battle_phases(
    std::weak_ptr<const Lobby> weak_l, std::vector<uint8_t> phases, std::string log_prefix) {
  for (size_t z = 0; z < phases.size(); z++) {
    if (z > 0) {
      co_await async_sleep(SPIRE_BRIDGE_BATTLE_PHASE_INTERVAL);
    }
    auto l = weak_l.lock();
    if (!l || !l->is_game()) {
      // The game is gone (everyone left), so there is no battle left to end
      co_return;
    }
    spire_bridge_send_battle_phase(l, phases[z]);
    l->log.info_f("{}: sent 6x7D phase {} to {} client(s)", log_prefix, phases[z], l->count_clients());
  }
}

// v10b: the quest writes 'ENDB' to register 248 at its end. If SpireBattleEndPhases lists phases (default: none), the
// server sends them as 6x7D to the whole game, SPIRE_BRIDGE_BATTLE_PHASE_INTERVAL apart, so the clients' battle leaves
// phase 2 the way a Sega battle does when its time limit runs out. Without the key the write is only logged.
static void spire_bridge_end_battle(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number, uint32_t quest_number) {
  const auto& phases = c->require_server_state()->data->spire_battle_end_phases;
  if (phases.empty()) {
    c->log.info_f("Spire bridge: ENDB for serial {:010} in q{}; SpireBattleEndPhases is empty, no 6x7D sent",
        serial_number, quest_number);
    return;
  }
  if (l->mode != GameMode::BATTLE) {
    c->log.info_f("Spire bridge: ENDB for serial {:010} in q{} outside a Battle game; no 6x7D sent", serial_number, quest_number);
    return;
  }
  std::string prefix = std::format("Spire bridge: ENDB for serial {:010} in q{}", serial_number, quest_number);
  auto s = c->require_server_state();
  asio::co_spawn(*s->io_context, spire_bridge_send_battle_phases(l, phases, prefix), asio::detached);
}

// v35: no server window during a Spire run (Marck, pc 2026-10-25: a multi-line server text stood on the screen as a
// window he had to press away, and his character could not move meanwhile). Every text the bridge sends in a run goes
// out the way "Waiting for your partner..." does: one B0 line, no line break. Texts that are not about the moment
// (the records and unlocks at the run end) wait for the lobby instead.
std::string spire_ticker_line(const std::string& text) {
  std::string ret;
  ret.reserve(text.size());
  for (char ch : text) {
    if ((ch == '\n') || (ch == '\r')) {
      if (!ret.empty() && (ret.back() != ' ')) {
        ret.push_back(' ');
      }
    } else {
      ret.push_back(ch);
    }
  }
  while (!ret.empty() && (ret.back() == ' ')) {
    ret.pop_back();
  }
  return ret;
}

static void spire_send_ticker(std::shared_ptr<Client> c, const std::string& text) {
  send_text_message(c, spire_ticker_line(text));
}

// v36: the same, after delay, if the client is still in that game
static asio::awaitable<void> spire_send_ticker_later_co(
    std::weak_ptr<Client> weak_c, std::weak_ptr<const Lobby> weak_l, std::string text, std::chrono::milliseconds delay) {
  co_await async_sleep(delay);
  auto c = weak_c.lock();
  auto l = weak_l.lock();
  if (c && l && (c->lobby.lock() == l)) {
    spire_send_ticker(c, text);
  }
}

static void spire_send_ticker_later(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, const std::string& text, std::chrono::milliseconds delay) {
  asio::co_spawn(*c->require_server_state()->io_context, spire_send_ticker_later_co(c, l, text, delay), asio::detached);
}

// Texts held for the lobby, per client. The weak_ptr tells a reused Client address from the client that earned them.
struct SpireLobbyTexts {
  std::weak_ptr<Client> client;
  std::vector<std::string> texts;
};
static std::unordered_map<const Client*, SpireLobbyTexts> spire_lobby_texts;

static void spire_defer_to_lobby(std::shared_ptr<Client> c, const std::string& text) {
  auto& e = spire_lobby_texts[c.get()];
  if (e.client.lock() != c) {
    e = SpireLobbyTexts{.client = c, .texts = {}};
  }
  if (e.texts.size() < 8) {
    e.texts.emplace_back(text);
  }
  if (spire_lobby_texts.size() > 256) {
    std::erase_if(spire_lobby_texts, [](const auto& it) { return it.second.client.expired(); });
  }
  c->log.info_f("Spire bridge: text held for the lobby ({} waiting): \"{}\"", e.texts.size(), spire_ticker_line(text));
}

static asio::awaitable<void> spire_lobby_texts_send(std::weak_ptr<Client> weak_c) {
  co_await async_sleep(SPIRE_LOBBY_TEXT_DELAY);
  auto c = weak_c.lock();
  if (!c) {
    co_return;
  }
  auto l = c->lobby.lock();
  if (!l || l->is_game()) {
    co_return; // already in a game again: the texts wait for the next lobby join
  }
  auto it = spire_lobby_texts.find(c.get());
  if ((it == spire_lobby_texts.end()) || (it->second.client.lock() != c)) {
    co_return;
  }
  auto texts = std::move(it->second.texts);
  spire_lobby_texts.erase(it);
  for (const auto& text : texts) {
    send_text_message(c, text);
  }
  c->log.info_f("Spire bridge: {} held text(s) sent in the lobby", texts.size());
}

void spire_deliver_lobby_texts(std::shared_ptr<Client> c) {
  try {
    auto it = spire_lobby_texts.find(c.get());
    if ((it == spire_lobby_texts.end()) || (it->second.client.lock() != c) || it->second.texts.empty()) {
      return;
    }
    asio::co_spawn(*c->require_server_state()->io_context, spire_lobby_texts_send(c), asio::detached);
  } catch (const std::exception& e) {
    c->log.warning_f("Spire bridge: failed to schedule the held texts: {}", e.what());
  }
}

// ---- v44 savestate (SpireFlagBridge.hh). The pure parts first, then the run per game and per client, then the files.

std::vector<ItemData> spire_savestate_items(const PlayerInventory& inventory, uint32_t glass_item_id,
    int32_t* glass_index, const SpireRunMagBytes& run_mag) {
  std::vector<ItemData> ret;
  *glass_index = -1;
  bool run_mag_kept = false;
  for (size_t z = 0; (z < inventory.num_items) && (z < inventory.items.size()); z++) {
    const auto& item = inventory.items[z].data;
    if (item.empty() || spire_drop_is_forbidden(item)) { // never a Scape Doll (B-E01-96)
      continue;
    }
    // F6: only the ledger's run mag comes back, matched on all 12 data1 bytes and kept once. A type-02 item here is
    // never the real mag: forbid_mags 0 (FORBID_ALL) strips it from the server's run copy (Client.cc:696-697), and the
    // GC client strips it from its live run copy too (lab memory dumps, no live TItemMag in any run; pc journal
    // 2026-10-26 20:47:39-41; new-features/roguelike/output/routeb-mag-vraag.md). Any other type-02 item (a partner's
    // run mag of the same class, the run mag before an 'I') is dropped.
    if (item.data1[0] == 0x02) {
      if (run_mag_kept || !run_mag || memcmp(item.data1.data(), run_mag->data(), 12)) {
        continue;
      }
      run_mag_kept = true;
    }
    if (glass_item_id && (item.id == glass_item_id)) {
      *glass_index = ret.size();
    }
    ret.emplace_back(item);
  }
  return ret;
}

bool spire_savestate_write_allowed(bool run_ended, bool has_overlay, bool began_run, size_t num_values, uint32_t count) {
  return !run_ended && has_overlay && began_run && (count == SPIRE_SAVESTATE_NUM_VALUES) && (num_values == count);
}

bool spire_savestate_end_deletes(bool player_left_end) {
  return !player_left_end;
}

std::vector<std::string> spire_savestate_end_files(const std::set<std::string>& run_files,
    const std::vector<std::string>& present_files) {
  std::set<std::string> all = run_files;
  all.insert(present_files.begin(), present_files.end());
  all.erase("");
  return std::vector<std::string>(all.begin(), all.end());
}

bool spire_savestate_is_of_run(const SpireSavestate& ss, uint32_t run_id) {
  return ss.exists && run_id && (ss.run_id == run_id);
}

bool spire_savestate_matches(const SpireSavestate& ss, uint8_t episode, uint8_t k, uint8_t variant) {
  return ss.exists && (ss.values.size() == SPIRE_SAVESTATE_NUM_VALUES) && (ss.episode == episode) &&
      (ss.values[SPIRE_SS_K] == k) && (ss.values[SPIRE_SS_VARIANT] == variant);
}

uint32_t spire_resume_item_ack(bool run_ended, bool answered_resume, uint32_t index, size_t num_items, int32_t glass_index) {
  if (run_ended || !answered_resume || (index >= num_items)) {
    return 0;
  }
  return (index + 1) | ((static_cast<int32_t>(index) == glass_index) ? SPIRE_RESUME_ITEM_GLASS : 0);
}

bool spire_resume_client(bool run_ended, bool game_resumed, uint32_t game_run_id, bool own_matches, uint32_t own_run_id) {
  return !run_ended && game_resumed && game_run_id && own_matches && (own_run_id == game_run_id);
}

std::string spire_savestate_to_json(const SpireSavestate& ss, uint32_t serial_number) {
  phosg::JSON values_json = phosg::JSON::list();
  for (uint32_t v : ss.values) {
    values_json.emplace_back(v);
  }
  phosg::JSON items_json = phosg::JSON::list();
  for (const auto& item : ss.items) {
    items_json.emplace_back(spire_guard_hex_encode(&item, sizeof(item)));
  }
  auto json = phosg::JSON::dict({
      {"SerialNumber", serial_number},
      {"RunId", ss.run_id},
      {"Episode", ss.episode},
      {"Quest", ss.quest_number},
      {"StartRoom", ss.start_room},
      {"Practice", ss.practice},
      {"Room", ss.values.empty() ? 0 : ss.values[SPIRE_SS_K]},
      {"Values", std::move(values_json)},
      {"Items", std::move(items_json)},
      {"GlassIndex", ss.glass_index},
      {"Time", ss.time_usecs},
  });
  if (ss.run_mag) { // F6: the run mag ledger, keyed by run id and step, never by item id
    const auto& led = *ss.run_mag;
    phosg::JSON bosses = phosg::JSON::list();
    for (uint8_t k : led.boss_rooms) {
      bosses.emplace_back(k);
    }
    auto led_json = phosg::JSON::dict({
        {"RunId", led.run_id},
        {"IssuedStep", led.issued_step},
        {"StartStep", led.start_step},
        {"BossRooms", std::move(bosses)},
        {"Lean", led.lean},
    });
    if (led.last_mirrored) {
      led_json.emplace("LastMirrored", spire_run_mag_bytes_hex(*led.last_mirrored));
    }
    json.emplace("RunMag", std::move(led_json));
  }
  return json.serialize(phosg::JSON::SerializeOption::FORMAT);
}

SpireSavestate spire_savestate_from_json(const std::string& data) {
  auto json = phosg::JSON::parse(data);
  SpireSavestate ret;
  ret.run_id = json.get_int("RunId");
  if (!ret.run_id) { // fix round (F10S critic N1): no run has id 0 (spire_new_run_id), so a run built from it has no ledger
    throw std::runtime_error("savestate has run id 0");
  }
  ret.episode = json.get_int("Episode");
  ret.quest_number = json.get_int("Quest");
  ret.start_room = json.get_int("StartRoom");
  ret.practice = json.get_bool("Practice");
  for (const auto& v : json.get_list("Values")) {
    ret.values.emplace_back(v->as_int());
  }
  if (ret.values.size() != SPIRE_SAVESTATE_NUM_VALUES) {
    throw std::runtime_error(std::format("savestate has {} values; expected {}", ret.values.size(), static_cast<size_t>(SPIRE_SAVESTATE_NUM_VALUES)));
  }
  for (const auto& h : json.get_list("Items")) {
    ItemData item;
    spire_guard_hex_decode(h->as_string(), &item, sizeof(item));
    ret.items.emplace_back(item);
  }
  if (ret.items.size() > 30) {
    throw std::runtime_error("savestate has more than 30 items");
  }
  ret.glass_index = json.get_int("GlassIndex", -1);
  if ((ret.glass_index < -1) || (ret.glass_index >= static_cast<int32_t>(ret.items.size()))) {
    ret.glass_index = -1;
  }
  ret.time_usecs = json.get_int("Time", 0);
  if (json.contains("RunMag")) {
    const auto& led_json = json.at("RunMag");
    SpireRunMagLedger led;
    led.run_id = led_json.get_int("RunId");
    led.issued_step = led_json.get_int("IssuedStep");
    led.start_step = led_json.get_int("StartStep");
    for (const auto& k : led_json.get_list("BossRooms")) {
      led.boss_rooms.emplace(k->as_int());
    }
    int64_t lean = led_json.get_int("Lean", 0);
    led.lean = spire_run_mag_saved_lean(lean);
    if (led.lean != lean) {
      player_data_log.warning_f("Spire savestate: run mag ledger of run {:08X} has lean {}; read as 0 (no choice)",
          led.run_id, lean);
    }
    if (led_json.contains("LastMirrored")) {
      led.last_mirrored = spire_run_mag_bytes_from_hex(led_json.get_string("LastMirrored"));
    }
    ret.run_mag = led;
  }
  ret.exists = true;
  return ret;
}

bool spire_run_is_practice(uint8_t start_k, uint8_t first_k, bool resumed, bool savestate_practice) {
  if (!start_k) {
    return false; // a 'B' from before v44: counted as before
  }
  return resumed ? savestate_practice : (start_k != first_k);
}

bool spire_run_counts_as_new(uint8_t start_k, bool resumed, bool practice) {
  return !start_k || (!resumed && !practice);
}

bool spire_practice_blocks_flag(bool practice, uint16_t flag_num) {
  return practice && (flag_num >= SPIRE_BRIDGE_FIRST_FLAG) && (flag_num <= SPIRE_BRIDGE_LAST_FLAG);
}

std::string spire_run_start_ticker(bool resumed, bool practice, const SpireSavestate* ss) {
  if (resumed && ss && (ss->values.size() == SPIRE_SAVESTATE_NUM_VALUES)) {
    return std::format("{} resumed at room {}: {} lives, {} min. Equip your gear.", practice ? "Practice run" : "Run",
        ss->values[SPIRE_SS_K], ss->values[SPIRE_SS_LIVES], ss->values[SPIRE_SS_ELAPSED] / 60);
  }
  return practice ? "Practice run: records and unlocks do not count." : "";
}

std::string spire_run_resume_mag_ticker(bool resumed, bool has_mag) {
  return (resumed && has_mag) ? SPIRE_RESUME_MAG_TEXT : "";
}

uint64_t spire_draw_seed(uint32_t run_id, uint32_t k, uint32_t nth, const std::vector<uint32_t>& values) {
  // FNV-1a over the fields, then splitmix64's finalizer
  uint64_t h = 0xCBF29CE484222325ULL;
  auto add = [&](uint32_t v) {
    for (size_t z = 0; z < 4; z++) {
      h = (h ^ ((v >> (z * 8)) & 0xFF)) * 0x100000001B3ULL;
    }
  };
  add(run_id);
  add(k);
  add(nth);
  for (uint32_t v : values) {
    add(v);
  }
  h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
  h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
  return h ^ (h >> 31);
}

std::string spire_savestate_continue_text(const SpireSavestate& ss) {
  const auto& v = ss.values;
  static const char* variants[] = {"", " elite", " rest"};
  uint32_t variant = v[SPIRE_SS_VARIANT];
  return std::format("Continue at Spire room {}{}: {} lives, {} min played{}", v[SPIRE_SS_K],
      (variant < 3) ? variants[variant] : "", v[SPIRE_SS_LIVES], v[SPIRE_SS_ELAPSED] / 60,
      ss.practice ? " (practice run)" : "");
}

std::string spire_savestate_room_quest_name(const SpireSavestate& ss) {
  static const char* variants[] = {"", " elite", " rest"};
  const auto& v = ss.values;
  if ((v.size() <= SPIRE_SS_VARIANT) || (v[SPIRE_SS_K] == 0) || (v[SPIRE_SS_VARIANT] >= 3)) {
    return "";
  }
  return std::format("Spire room {}{}", v[SPIRE_SS_K], variants[v[SPIRE_SS_VARIANT]]);
}

std::string spire_savestate_notice_saved_line(const SpireSavestate& ss) {
  static const char* variants[] = {"", " elite", " rest"};
  const auto& v = ss.values;
  std::vector<std::string> parts;
  if ((v.size() > SPIRE_SS_K) && (v[SPIRE_SS_K] != 0)) {
    bool has_variant = (v.size() > SPIRE_SS_VARIANT) && (v[SPIRE_SS_VARIANT] < 3);
    parts.emplace_back(std::format("Room {}{}", v[SPIRE_SS_K], has_variant ? variants[v[SPIRE_SS_VARIANT]] : ""));
  }
  if (v.size() > SPIRE_SS_LIVES) {
    parts.emplace_back(std::format("{} lives", v[SPIRE_SS_LIVES]));
  }
  if (v.size() > SPIRE_SS_ELAPSED) {
    uint32_t t = v[SPIRE_SS_ELAPSED];
    parts.emplace_back((t >= 3600) ? std::format("{}:{:02}:{:02}", t / 3600, (t / 60) % 60, t % 60)
                                   : std::format("{}:{:02}", t / 60, t % 60));
  }
  std::string ret = "Saved run";
  for (size_t z = 0; z < parts.size(); z++) {
    ret += (z ? ", " : ": ") + parts[z];
  }
  return ret + ".";
}

std::string spire_savestate_notice_continue_line(const std::string& category_name, const std::string& quest_name) {
  if (category_name.empty() || quest_name.empty()) {
    return SPIRE_SAVESTATE_NOTICE_NO_QUEST_TEXT;
  }
  std::string ret = spire_ticker_line(std::format("To continue: Quest > {} > {}.", category_name, quest_name));
  return (ret.size() <= SPIRE_BRIDGE_TEXT_MAX) ? ret : SPIRE_SAVESTATE_NOTICE_NO_QUEST_TEXT;
}

std::vector<std::string> spire_savestate_notice_take(
    bool* told, const SpireSavestate& ss, const std::string& category_name, const std::string& quest_name) {
  if (*told || !ss.exists) {
    return {};
  }
  *told = true;
  return {spire_savestate_notice_saved_line(ss), spire_savestate_notice_continue_line(category_name, quest_name)};
}

// The run of a game (lobby id; a game's id is never reused while the server runs): set by the first 'J' or 'B' in the
// game, forgotten at its next quest start.
struct SpireGameRun {
  uint32_t run_id = 0;
  bool resumed = false; // the leader resumed from its savestate
  uint32_t elapsed = 0; // the leader's saved seconds (resumed only)
  size_t max_clients = 0; // the most clients the game had during the run
  std::set<std::string> savestate_files; // written ('V') or answered from ('J') in this run, also of a client gone since
};
static std::unordered_map<uint32_t, SpireGameRun> spire_game_runs;

static SpireGameRun* spire_game_run_for(std::shared_ptr<const Lobby> l) {
  auto it = spire_game_runs.find(l->lobby_id);
  if (it == spire_game_runs.end()) {
    return nullptr;
  }
  it->second.max_clients = std::max(it->second.max_clients, l->count_clients());
  return &it->second;
}

static uint32_t spire_new_run_id() {
  uint32_t ret;
  do {
    ret = phosg::random_object<uint32_t>();
  } while (!ret);
  return ret;
}

static SpireGameRun& spire_game_run_set(std::shared_ptr<const Lobby> l, const SpireGameRun& run) {
  if (spire_game_runs.size() > 4096) {
    spire_game_runs.clear();
  }
  auto& ret = spire_game_runs[l->lobby_id] = run;
  ret.max_clients = std::max(ret.max_clients, l->count_clients());
  l->log.info_f("Spire savestate: run {:08X} in this game ({})", ret.run_id,
      ret.resumed ? std::format("resumed from the leader's savestate, {} s played", ret.elapsed) : std::string("new"));
  return ret;
}

// The run of one client in its current game: what its 'J' and 'B' decided. The weak_ptr and the lobby id tell a reused
// Client address or a later game from the one that wrote it.
struct SpireClientRun {
  std::weak_ptr<Client> client;
  uint32_t lobby_id = 0;
  bool answered_resume = false; // the start answer resumed this client (its items are in savestate)
  SpireSavestate savestate;
  bool began = false; // its 'B' came
  bool resumed = false; // the 'B' confirmed the resume
  bool practice = false;
  uint8_t start_room = 0;
  std::map<uint32_t, uint32_t> draws_per_room; // n-th draw per room (seed)
  bool kit_text_sent = false; // route B bundle 2 S: SPIRE_KIT_USE_TEXT went to this client in this run
  int kit_grind = -1; // route B bundle 9: the grind G of this client's 'C' 13 in this run, -1 = none came
  std::set<uint8_t> boss_burst_texts; // route B bundle 2 S: the rooms whose SPIRE_BOSS_BURST_TEXT went to this client
};
static std::unordered_map<const Client*, SpireClientRun> spire_client_runs;

// Route B bundle 9 (SpireEquipItem.hh): r143 for this client, the same answer in every 'M' answer, at 'C' 13/14 and for
// the kit and evolution texts
static bool spire_equip_item_available_for(std::shared_ptr<Client> c) {
  auto s = c->require_server_state();
  bool loaded = false;
  if (spire_equip_item_applies(c->specific_version) && s->data->client_functions) {
    try {
      s->data->client_functions->get(SPIRE_EQUIP_FUNCTION_NAME, c->specific_version);
      loaded = true;
    } catch (const std::out_of_range&) {
    }
  }
  return spire_equip_item_available(c->specific_version, s->data->spire_equip_item_patch,
      c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL) && c->check_flag(Client::Flag::SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE),
      loaded);
}

static SpireClientRun* spire_client_run_for(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, bool create) {
  auto it = spire_client_runs.find(c.get());
  if ((it != spire_client_runs.end()) && ((it->second.client.lock() != c) || (it->second.lobby_id != l->lobby_id))) {
    spire_client_runs.erase(it);
    it = spire_client_runs.end();
  }
  if (it == spire_client_runs.end()) {
    if (!create) {
      return nullptr;
    }
    if (spire_client_runs.size() > 256) {
      std::erase_if(spire_client_runs, [](const auto& e) { return e.second.client.expired(); });
    }
    it = spire_client_runs.emplace(c.get(), SpireClientRun{}).first;
    it->second.client = c;
    it->second.lobby_id = l->lobby_id;
  }
  return &it->second;
}

static bool spire_client_run_is_practice(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  auto* cr = spire_client_run_for(c, l, false);
  return cr && cr->began && cr->practice;
}

// The savestate file of the client's character (the server's character data, never the overlay), "" without one
static std::string spire_savestate_filename(std::shared_ptr<const Client> c) {
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  auto character = c->character_file(false, false);
  if (!character || !serial_number) {
    return "";
  }
  return std::format("{}/{:010}.{}.savestate.json", SPIRE_BRIDGE_DIRECTORY, serial_number,
      spire_guard_character_id(character->disp));
}

static SpireSavestate spire_savestate_load(const std::string& filename) {
  if (filename.empty() || !std::filesystem::is_regular_file(filename)) {
    return SpireSavestate();
  }
  return spire_savestate_from_json(phosg::load_file(filename));
}

static SpireSavestate spire_savestate_load_logged(std::shared_ptr<const Client> c) {
  try {
    return spire_savestate_load(spire_savestate_filename(c));
  } catch (const std::exception& e) {
    c->log.warning_f("Spire savestate: unreadable file: {}; ignored", e.what());
    return SpireSavestate();
  }
}

static std::string spire_savestate_describe(const SpireSavestate& ss) {
  if (!ss.exists) {
    return "none";
  }
  return std::format("run {:08X} room {} variant {} ({} items, {} lives, {} s, start room {}{})", ss.run_id,
      ss.values[SPIRE_SS_K], ss.values[SPIRE_SS_VARIANT], ss.items.size(), ss.values[SPIRE_SS_LIVES],
      ss.values[SPIRE_SS_ELAPSED], ss.start_room, ss.practice ? ", practice" : "");
}

// ---- Route B F6: the run mag ledger (SpireMag.hh) per (serial, character id, run id). In memory, and in the savestate
// file ("RunMag", written at every 'V'); a resume takes the savestate's ledger (it matches the items the resume gives
// back), and after a server restart the savestate file is read when the run has no ledger in memory.
struct SpireRunMagStore {
  std::map<uint32_t, SpireRunMagLedger> runs; // run id -> ledger
  uint32_t latest_run_id = 0;
};
static std::unordered_map<std::string, SpireRunMagStore> spire_run_mag_store; // "<serial>.<character id>"
// Hotfix 1: the pack-full notices told, per spire_run_mag_key (in memory only; apart from the ledger, which they never
// change)
static SpireRunMagPackFullTold spire_run_mag_pack_full_told; // spire_run_mag_pack_full_decide

static std::string spire_run_mag_key(std::shared_ptr<const Client> c) {
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  auto character = c->character_file(false, false); // the server's character data, never the overlay
  if (!character || !serial_number) {
    return "";
  }
  return std::format("{:010}.{}", serial_number, spire_guard_character_id(character->disp));
}

static SpireRunMagLedger* spire_run_mag_ledger_for(std::shared_ptr<const Client> c, uint32_t run_id, bool create) {
  std::string key = spire_run_mag_key(c);
  if (!run_id || key.empty()) {
    return nullptr;
  }
  auto store_it = spire_run_mag_store.find(key);
  if (store_it == spire_run_mag_store.end()) {
    if (spire_run_mag_store.size() > 1024) {
      spire_run_mag_store.clear();
    }
    store_it = spire_run_mag_store.emplace(key, SpireRunMagStore{}).first;
  }
  auto& store = store_it->second;
  auto it = store.runs.find(run_id);
  if (it == store.runs.end()) {
    SpireSavestate ss = spire_savestate_load_logged(c);
    if (ss.exists && ss.run_mag && (ss.run_mag->run_id == run_id)) {
      it = store.runs.emplace(run_id, *ss.run_mag).first;
      c->log.info_f("Spire run mag: ledger of run {:08X} read from the savestate (step {})", run_id, it->second.issued_step);
    } else if (!create) {
      return nullptr;
    } else {
      SpireRunMagLedger led;
      led.run_id = run_id;
      it = store.runs.emplace(run_id, led).first;
    }
  }
  store.latest_run_id = run_id;
  if (store.runs.size() > 4) {
    std::erase_if(store.runs, [&](const auto& e) { return e.first != run_id; });
  }
  return &it->second;
}

// A resume takes the savestate's ledger, or none (a savestate from before F6: the items hold no mag either)
static void spire_run_mag_adopt(std::shared_ptr<const Client> c, uint32_t run_id, const std::optional<SpireRunMagLedger>& led) {
  std::string key = spire_run_mag_key(c);
  if (!run_id || key.empty()) {
    return;
  }
  auto& store = spire_run_mag_store[key];
  store.runs.erase(run_id);
  if (led && (led->run_id == run_id)) {
    store.runs.emplace(run_id, *led);
    store.latest_run_id = run_id;
  }
  c->log.info_f("Spire run mag: ledger of run {:08X} from the resumed savestate: {}", run_id,
      led ? std::format("step {}, {} boss room(s)", led->issued_step, led->boss_rooms.size()) : std::string("none"));
}

// The 12 data1 bytes of every type-02 item of an inventory
static std::vector<SpireMagBytes> spire_run_mag_inventory_mags(const PlayerInventory& inventory) {
  std::vector<SpireMagBytes> ret;
  for (size_t z = 0; (z < inventory.num_items) && (z < inventory.items.size()); z++) {
    const auto& item = inventory.items[z].data;
    if (!item.empty() && (item.data1[0] == 0x02)) {
      SpireMagBytes b;
      memcpy(b.data(), item.data1.data(), 12);
      ret.emplace_back(b);
    }
  }
  return ret;
}

// Amendment 3: an in-game 61 refreshes the overlay with the client's save buffer, which after a push is the real
// character with the real mag; from then on in that game 'Z' is refused and 'V' no longer updates the ledger.
// Amendment 4: an 'I' (run again) in a game leaves the earlier run copy live; 'Z' is refused in that game until the
// next quest load. Both are forgotten at the quest load (spire_checkpoint_run_started), when the overlay is made anew.
struct SpireOverlayRefreshed {
  std::weak_ptr<const Client> client;
  uint32_t lobby_id = 0;
};
static std::unordered_map<const Client*, SpireOverlayRefreshed> spire_overlay_refreshed;
static std::unordered_set<uint32_t> spire_run_again_games;

void spire_run_mag_note_in_game_61(std::shared_ptr<const Client> c) {
  auto l = c->lobby.lock();
  if (!l || !l->is_game() || !c->has_overlay()) {
    return;
  }
  if (spire_overlay_refreshed.size() > 256) {
    std::erase_if(spire_overlay_refreshed, [](const auto& e) { return e.second.client.expired(); });
  }
  spire_overlay_refreshed[c.get()] = SpireOverlayRefreshed{c, l->lobby_id};
  c->log.info_f("Spire run mag: an in-game 61 refreshed the run copy in game {:X}; 'Z' and the ledger's 'V' update are off in this game",
      l->lobby_id);
}

static bool spire_run_mag_overlay_refreshed(std::shared_ptr<const Client> c, std::shared_ptr<const Lobby> l) {
  auto it = spire_overlay_refreshed.find(c.get());
  return (it != spire_overlay_refreshed.end()) && (it->second.client.lock() == c) && (it->second.lobby_id == l->lobby_id);
}

// A run end in the game (spire_run_mark_ended, the first one): delete the savestates of the game's run at every true
// end, also of a client gone by then (regie 2026-10-25, reverses B-E01-181); the run timer's "player left" keeps them
static void spire_savestate_on_run_end(std::shared_ptr<const Lobby> l, const char* why, bool player_left) {
  try {
    auto* run = spire_game_run_for(l);
    if (!run) {
      return;
    }
    if (!spire_savestate_end_deletes(player_left)) {
      l->log.info_f("Spire savestate: run over ({}) by a player who left; savestates of run {:08X} kept", why,
          run->run_id);
      return;
    }
    std::vector<std::string> present;
    for (const auto& lc : l->clients) {
      if (lc) {
        present.emplace_back(spire_savestate_filename(lc));
      }
    }
    for (const auto& filename : spire_savestate_end_files(run->savestate_files, present)) {
      SpireSavestate ss;
      try {
        ss = spire_savestate_load(filename);
      } catch (const std::exception& e) {
        l->log.warning_f("Spire savestate: unreadable file {} at the run end: {}; left alone", filename, e.what());
        continue;
      }
      if (!spire_savestate_is_of_run(ss, run->run_id)) {
        continue;
      }
      std::filesystem::remove(filename);
      l->log.info_f("Spire savestate: run over ({}) with {} of {} client(s); deleted {} ({})", why, l->count_clients(),
          run->max_clients, filename, spire_savestate_describe(ss));
    }
  } catch (const std::exception& e) {
    l->log.warning_f("Spire savestate: failed to delete at the run end: {}", e.what());
  }
}

// v35: the end of a run, per game (lobby id; a game's id is never reused while the server runs). Set at the first
// 'F' (records: run finished), 'X' (run over) or run timer end in the game; cleared at a quest start, 'B' and 'S'.
// Measured on the pc journal 2026-10-25: the run ended at 08:47:58, yet at 08:49:56 the server still sent "Room clear!"
// and r251 for room 1 of q940 and stored it as a checkpoint (SPIRE_K1 unlocked by a lost run). After the end no room
// clear is sent and no checkpoint is stored.
struct SpireRunEnd {
  std::string why;
  bool over_announced = false; // the run-over text went to every client (spire_run_over)
  std::unordered_set<uint8_t> exit_sent; // lobby client ids sent back to the lobby (spire_run_over)
};
static std::unordered_map<uint32_t, SpireRunEnd> spire_run_ends;

// v44: the first end of the run also deletes the game's savestates, unless a player left (spire_savestate_on_run_end)
static SpireRunEnd& spire_run_mark_ended(std::shared_ptr<const Lobby> l, const char* why, bool player_left = false) {
  if (spire_run_ends.size() > 4096) {
    spire_run_ends.clear();
  }
  auto [it, added] = spire_run_ends.try_emplace(l->lobby_id);
  if (added) {
    it->second.why = why;
    l->log.info_f("Spire bridge: run over in this game ({}); no room clear or checkpoint from now on", why);
    spire_savestate_on_run_end(l, why, player_left);
  }
  return it->second;
}

static void spire_run_forget_end(uint32_t lobby_id, const char* why, const phosg::PrefixedLogger& log) {
  if (spire_run_ends.erase(lobby_id)) {
    log.info_f("Spire bridge: new run in this game ({}); room clears count again", why);
  }
}

static const SpireRunEnd* spire_run_end_for(std::shared_ptr<const Lobby> l) {
  auto it = spire_run_ends.find(l->lobby_id);
  return (it == spire_run_ends.end()) ? nullptr : &it->second;
}

// v35 (regie): after the run is over every client of the game goes back to the lobby, the way $exit leaves a quest
// (6x73: the client fades out and leaves the game, as Quit does), never to Pioneer 2, whose arrival writes the
// Lv1 overlay to the card (B-E01-41). The 98 of that exit makes the guard's restore pending and the lobby join runs it.
SpireRunOverExit spire_run_over_exit_action(bool same_game, bool quest_in_progress, bool can_run_functions, bool already_sent) {
  if (!same_game || already_sent) {
    return SpireRunOverExit::NONE;
  }
  if (quest_in_progress) {
    return SpireRunOverExit::EXIT_QUEST;
  }
  return can_run_functions ? SpireRunOverExit::EXIT_ANYWHERE : SpireRunOverExit::TELL;
}

// v48: the return to base ('O', SpireFlagBridge.hh)
SpireBaseReturnRefusal spire_base_return_refusal(const SpireBaseReturnState& st) {
  using R = SpireBaseReturnRefusal;
  if (!st.has_character) {
    return R::NO_CHARACTER;
  }
  if (!st.spire_run) {
    return R::NOT_A_SPIRE_RUN;
  }
  if (!st.run_over) {
    return R::RUN_NOT_OVER;
  }
  if (!st.on_field_floor) {
    return R::NOT_ON_FIELD;
  }
  if (st.any_client_loading) {
    return R::LOADING;
  }
  if (!st.can_run_functions) {
    return R::NO_FUNCTIONS;
  }
  if (st.protected_character) {
    return R::PROTECTED;
  }
  if (!st.has_snapshot || !st.run_in_progress || st.restore_pending || st.awaiting_save) {
    return R::MARKER;
  }
  if (st.restore_running) {
    return R::RESTORE_RUNNING;
  }
  if (st.already_sent_back) {
    return R::ALREADY_SENT_BACK;
  }
  return R::NONE;
}

const char* spire_base_return_refusal_name(SpireBaseReturnRefusal r) {
  switch (r) {
    case SpireBaseReturnRefusal::NONE:
      return "none";
    case SpireBaseReturnRefusal::NO_CHARACTER:
      return "no character data";
    case SpireBaseReturnRefusal::NOT_A_SPIRE_RUN:
      return "not in a guarded Spire run with its overlay";
    case SpireBaseReturnRefusal::RUN_NOT_OVER:
      return "the run has not ended in this game";
    case SpireBaseReturnRefusal::NOT_ON_FIELD:
      return "on Pioneer 2, not on a field floor";
    case SpireBaseReturnRefusal::LOADING:
      return "a client of the game is loading";
    case SpireBaseReturnRefusal::NO_FUNCTIONS:
      return "the client cannot run functions";
    case SpireBaseReturnRefusal::PROTECTED:
      return "protected character";
    case SpireBaseReturnRefusal::MARKER:
      return "no snapshot with RunInProgress alone";
    case SpireBaseReturnRefusal::RESTORE_RUNNING:
      return "a restore already runs for the client";
    case SpireBaseReturnRefusal::ALREADY_SENT_BACK:
      return "already sent back to the lobby";
    case SpireBaseReturnRefusal::PUSH_NOT_VERIFIED:
      return "the push was not verified";
    case SpireBaseReturnRefusal::BASE_GAME:
      return "the client goes to the base game";
  }
  return "unknown";
}

uint32_t spire_base_return_answer(SpireBaseReturnRefusal r) {
  return SPIRE_BASE_RETURN_TAG | (static_cast<uint32_t>(r) << 8) |
      ((r == SpireBaseReturnRefusal::NONE) ? SPIRE_BASE_RETURN_OK : SPIRE_BASE_RETURN_FAIL);
}

// v49: the run again ('I', SpireFlagBridge.hh)
SpireRunAgainRefusal spire_run_again_refusal(const SpireRunAgainState& st) {
  using R = SpireRunAgainRefusal;
  if (!st.has_character) {
    return R::NO_CHARACTER;
  }
  if (!st.spire_run) {
    return R::NOT_A_SPIRE_RUN;
  }
  if (!st.base_returned) {
    return R::NOT_RETURNED;
  }
  if (!st.at_start_floor) {
    return R::NOT_AT_START;
  }
  if (st.any_client_loading) {
    return R::LOADING;
  }
  if (!st.can_run_functions) {
    return R::NO_FUNCTIONS;
  }
  if (st.protected_character) {
    return R::PROTECTED;
  }
  if (!st.has_snapshot || !st.run_in_progress || st.restore_pending || st.awaiting_save) {
    return R::MARKER;
  }
  if (st.restore_running) {
    return R::RESTORE_RUNNING;
  }
  return R::NONE;
}

const char* spire_run_again_refusal_name(SpireRunAgainRefusal r) {
  switch (r) {
    case SpireRunAgainRefusal::NONE:
      return "none";
    case SpireRunAgainRefusal::NO_CHARACTER:
      return "no character data";
    case SpireRunAgainRefusal::NOT_A_SPIRE_RUN:
      return "not in a guarded Spire game";
    case SpireRunAgainRefusal::NOT_RETURNED:
      return "no unused return to base (O OK) in this game";
    case SpireRunAgainRefusal::NOT_AT_START:
      return "not on Pioneer 2 or Forest 1";
    case SpireRunAgainRefusal::LOADING:
      return "a client of the game is loading";
    case SpireRunAgainRefusal::NO_FUNCTIONS:
      return "the client cannot run functions";
    case SpireRunAgainRefusal::PROTECTED:
      return "protected character";
    case SpireRunAgainRefusal::MARKER:
      return "no snapshot with RunInProgress alone";
    case SpireRunAgainRefusal::RESTORE_RUNNING:
      return "a restore already runs for the client";
    case SpireRunAgainRefusal::NOT_REAL:
      return "the 61 is not the snapshot's character, or the snapshot was not written";
    case SpireRunAgainRefusal::NO_OVERLAY:
      return "no battle overlay made";
    case SpireRunAgainRefusal::PUSH_NOT_VERIFIED:
      return "the push was not verified";
  }
  return "unknown";
}

uint32_t spire_run_again_answer(SpireRunAgainRefusal r) {
  return SPIRE_RUN_AGAIN_TAG | (static_cast<uint32_t>(r) << 8) |
      ((r == SpireRunAgainRefusal::NONE) ? SPIRE_RUN_AGAIN_OK : SPIRE_RUN_AGAIN_FAIL);
}

bool spire_run_again_floor_ok(uint32_t floor) {
  return (floor == 0) || (floor == 1);
}

// v49: the clients whose 'O' was answered OK in their current game and that have not used it for an 'I' yet. Keyed by
// the Client's address; the weak_ptr and the lobby id tell a reused address or a later game from the one it was for.
// Forgotten at every quest start.
struct SpireBaseReturned {
  std::weak_ptr<const Client> client;
  uint32_t lobby_id = 0;
};
static std::unordered_map<const Client*, SpireBaseReturned> spire_base_returned;

static void spire_base_returned_note(std::shared_ptr<const Client> c, uint32_t lobby_id) {
  if (spire_base_returned.size() > 256) {
    std::erase_if(spire_base_returned, [](const auto& e) { return e.second.client.expired(); });
  }
  spire_base_returned[c.get()] = SpireBaseReturned{c, lobby_id};
}

static bool spire_base_returned_has(std::shared_ptr<const Client> c, uint32_t lobby_id) {
  auto it = spire_base_returned.find(c.get());
  return (it != spire_base_returned.end()) && (it->second.client.lock() == c) && (it->second.lobby_id == lobby_id);
}

static asio::awaitable<void> spire_run_over_exit(
    std::weak_ptr<Client> weak_c, std::weak_ptr<Lobby> weak_l, std::chrono::seconds delay) {
  co_await async_sleep(delay);
  auto c = weak_c.lock();
  auto l = weak_l.lock();
  if (!c || !l) {
    co_return;
  }
  auto it = spire_run_ends.find(l->lobby_id);
  if (it == spire_run_ends.end()) {
    l->log.info_f("Spire bridge: client {} not sent back: a new run started in this game", c->lobby_client_id);
    co_return;
  }
  bool already_sent = it->second.exit_sent.count(c->lobby_client_id);
  auto action = spire_run_over_exit_action((c->lobby.lock() == l) && l->is_game(),
      l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS) || l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS),
      c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL) && c->check_flag(Client::Flag::SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE),
      already_sent);
  switch (action) {
    case SpireRunOverExit::NONE:
      co_return;
    case SpireRunOverExit::EXIT_QUEST: {
      it->second.exit_sent.emplace(c->lobby_client_id);
      G_UnusedHeader cmd = {0x73, 0x01, 0x0000}; // as $exit in a quest
      c->channel->send(0x60, 0x00, cmd);
      l->log.info_f("Spire bridge: run over; client {} sent back to the lobby (6x73, {} s after the run-over code)",
          c->lobby_client_id, delay.count());
      co_return;
    }
    case SpireRunOverExit::EXIT_ANYWHERE:
      it->second.exit_sent.emplace(c->lobby_client_id);
      try {
        auto s = c->require_server_state();
        auto fn = s->data->client_functions->get("ExitAnywhere", c->specific_version);
        co_await prepare_client_for_patches(c);
        co_await send_function_call(c, fn);
        l->log.info_f("Spire bridge: run over; client {} sent back to the lobby (ExitAnywhere, no quest loaded)",
            c->lobby_client_id);
      } catch (const std::exception& e) {
        l->log.warning_f("Spire bridge: run over; failed to send client {} back to the lobby: {}", c->lobby_client_id,
            e.what());
      }
      co_return;
    case SpireRunOverExit::TELL:
      l->log.warning_f("Spire bridge: run over; client {} cannot run functions and no quest is loaded: it leaves itself",
          c->lobby_client_id);
      co_return;
  }
}

static void spire_run_timer_stop(const Lobby* l);

// 'X' (run over) from a client: the first one in the game tells every client and sends the partners back after
// SPIRE_RUN_OVER_PARTNER_DELAY at the latest; each writer goes back SPIRE_RUN_OVER_LOBBY_DELAY after its own 'X'.
static void spire_run_over(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> cl, uint32_t serial_number,
    uint32_t quest_number, uint32_t value) {
  auto l = std::const_pointer_cast<Lobby>(cl);
  spire_run_timer_stop(l.get()); // v19: the run is over, so the timer must not end it again
  auto& end = spire_run_mark_ended(l, "run over");
  std::string msg = spire_bridge_message_text(value);
  auto s = c->require_server_state();
  size_t told = 0;
  if (!end.over_announced) {
    end.over_announced = true;
    for (const auto& lc : l->clients) {
      if (!lc) {
        continue;
      }
      spire_send_ticker(lc, msg);
      told++;
      if (lc != c) {
        asio::co_spawn(*s->io_context, spire_run_over_exit(lc, l, SPIRE_RUN_OVER_PARTNER_DELAY), asio::detached);
      }
    }
  }
  asio::co_spawn(*s->io_context, spire_run_over_exit(c, l, SPIRE_RUN_OVER_LOBBY_DELAY), asio::detached);
  c->log.info_f("Spire bridge: message {:08X} for serial {:010} in q{}; sent \"{}\" to {} client(s); back to the lobby in {} s",
      value, serial_number, quest_number, msg, told, SPIRE_RUN_OVER_LOBBY_DELAY.count());
}

// v19: one run timer per game. The generation tells a coroutine that its timer was replaced or stopped; the key is
// the lobby's address, and a coroutine whose lobby is gone (weak_ptr) removes nothing but its own generation.
struct SpireRunTimer {
  uint64_t generation = 0;
  std::chrono::steady_clock::time_point start;
  uint32_t limit_s = 0;
  uint32_t warn_before_s = 0;
  size_t clients_at_start = 0;
  bool warned = false;
};
static std::unordered_map<const Lobby*, SpireRunTimer> spire_run_timers;
static uint64_t spire_run_timer_next_generation = 1;

static void spire_run_timer_stop(const Lobby* l) {
  spire_run_timers.erase(l);
}

static asio::awaitable<void> spire_run_timer_tick(std::weak_ptr<Lobby> weak_l, const Lobby* key, uint64_t generation) {
  for (;;) {
    co_await async_sleep(SPIRE_BRIDGE_TIMER_TICK);
    auto it = spire_run_timers.find(key);
    if ((it == spire_run_timers.end()) || (it->second.generation != generation)) {
      co_return; // stopped, or replaced by a newer 'S'
    }
    auto l = weak_l.lock();
    if (!l || !l->is_game()) {
      spire_run_timers.erase(it);
      co_return;
    }
    auto& t = it->second;
    uint32_t elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t.start).count();
    size_t clients = l->count_clients();
    uint32_t cause = 0;
    if (clients < t.clients_at_start) {
      cause = SPIRE_BRIDGE_TIMER_PLAYER_LEFT;
    } else if (elapsed >= t.limit_s) {
      cause = SPIRE_BRIDGE_TIMER_TIME_UP;
    } else if (!t.warned && (elapsed + t.warn_before_s >= t.limit_s)) {
      t.warned = true;
      uint32_t minutes = (t.limit_s - elapsed + 59) / 60;
      std::string msg = std::format("$C4{} minutes left$C7 - the timer ends the run.", minutes);
      for (const auto& lc : l->clients) {
        if (lc) {
          spire_send_ticker(lc, msg);
        }
      }
      l->log.info_f("Spire bridge: run timer warning at {} s ({} minutes left) to {} client(s)", elapsed, minutes, clients);
      continue;
    } else {
      continue;
    }
    G_SyncQuestRegister_6x77 cmd;
    cmd.header.subcommand = 0x77;
    cmd.header.size = sizeof(cmd) / 4;
    cmd.header.unused = 0;
    cmd.register_number = SPIRE_BRIDGE_TIMER_REGISTER;
    // v21: the server's seconds in the low 16 bits; the quest takes them as its run time (lab: quest 213 s, server 240 s)
    cmd.value.as_int = (static_cast<uint32_t>(SPIRE_BRIDGE_TIMER_END) << 24) | (cause << 16) | std::min<uint32_t>(elapsed, 0xFFFF);
    const char* text = (cause == SPIRE_BRIDGE_TIMER_TIME_UP) ? "Time is up - the run ends here."
                                                             : "The second player left - the run ends here.";
    for (const auto& lc : l->clients) {
      if (lc) {
        spire_send_ticker(lc, text);
        send_command_t(lc, 0x60, 0x00, cmd);
      }
    }
    l->log.info_f("Spire bridge: run timer end ({}) at {} s with {} of {} client(s); sent r{}={:08X}",
        (cause == SPIRE_BRIDGE_TIMER_TIME_UP) ? "time up" : "player left", elapsed, clients, t.clients_at_start,
        SPIRE_BRIDGE_TIMER_REGISTER, cmd.value.as_int.load());
    spire_run_mark_ended(l, (cause == SPIRE_BRIDGE_TIMER_TIME_UP) ? "run timer: time up" : "run timer: player left",
        cause == SPIRE_BRIDGE_TIMER_PLAYER_LEFT);
    spire_run_timers.erase(it);
    co_return;
  }
}

// v19: 'S' from the leader at its run start (see SPIRE_BRIDGE_TIMER_START).
static void spire_run_timer_start(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> cl, uint32_t serial_number, uint32_t quest_number,
    uint32_t value) {
  auto l = std::const_pointer_cast<Lobby>(cl);
  SpireRunTimer t;
  t.generation = spire_run_timer_next_generation++;
  t.start = std::chrono::steady_clock::now();
  t.limit_s = value & 0xFFFF;
  t.warn_before_s = ((value >> 16) & 0xFF) * 60;
  t.clients_at_start = l->count_clients();
  const auto& lab = c->require_server_state()->data->spire_run_timer_lab;
  if (lab) { // lab config only: a short round instead of the quest's 60 min
    c->log.info_f("Spire bridge: run timer lab override (SpireRunTimerLab): limit {} s -> {} s, warning {} s -> {} s",
        t.limit_s, lab->first, t.warn_before_s, lab->second);
    t.limit_s = lab->first;
    t.warn_before_s = lab->second;
  }
  if (auto* run = spire_game_run_for(l); run && run->resumed && run->elapsed) {
    // v44: a resumed run goes on with its saved time (the quest's own clock does the same)
    t.start -= std::chrono::seconds(std::min(run->elapsed, t.limit_s));
    c->log.info_f("Spire savestate: run timer of the resumed run starts {} s back", std::min(run->elapsed, t.limit_s));
  }
  spire_run_timers[l.get()] = t;
  spire_run_forget_end(l->lobby_id, "run timer start", c->log);
  c->log.info_f("Spire bridge: run timer for serial {:010} in q{}: limit {} s, warning {} s before, {} client(s)",
      serial_number, quest_number, t.limit_s, t.warn_before_s, t.clients_at_start);
  auto s = c->require_server_state();
  asio::co_spawn(*s->io_context, spire_run_timer_tick(l, l.get(), t.generation), asio::detached);
}

// v20: the draw inputs a client sent to SPIRE_BRIDGE_DRAW_DATA_REGISTER since its last request.
static std::unordered_map<const Client*, std::vector<uint32_t>> spire_draw_inputs;

static uint64_t spire_splitmix_next(uint64_t& state) {
  uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// v62 (B-REGIE-16-13, ROUTEB8-PROTOCOL.md § 2): each client's overview line of its last 'Q', for its 'C' 2. Replaced
// (or dropped) at its next 'Q', dropped at a floor change (spire_replay_after_floor_load) and at the quest start; the
// weak_ptr tells a reused Client address from the one that wrote it.
struct SpireOverviewEntry {
  std::weak_ptr<const Client> client;
  SpireOverviewKept kept;
};
static std::unordered_map<const Client*, SpireOverviewEntry> spire_overview_entries;

static const SpireOverviewKept* spire_overview_kept_for(std::shared_ptr<const Client> c) {
  auto it = spire_overview_entries.find(c.get());
  if ((it == spire_overview_entries.end()) || (it->second.client.lock() != c)) {
    return nullptr;
  }
  return &it->second.kept;
}

static void spire_overview_floor_changed(std::shared_ptr<const Client> c) {
  const auto* kept = spire_overview_kept_for(c);
  if (kept && (kept->floor != c->floor)) {
    spire_overview_entries.erase(c.get());
  }
}

// v62: the quest's draw table for the names in a server text (SpireBridgeTextContext); std::nullopt if it cannot be
// read (the text then names nothing, as for an older draw.json)
static std::optional<SpireDrawTable> spire_draw_table_for_text(std::shared_ptr<const Client> c, uint32_t quest_number) {
  std::string filename = std::format("{}/q{}.json", SPIRE_BRIDGE_DRAW_DIRECTORY, quest_number);
  try {
    return SpireDrawTable::from_json_file(filename);
  } catch (const std::exception& e) {
    c->log.info_f("Spire bridge: text names from {}: {}; the text names nothing", filename, e.what());
    return std::nullopt;
  }
}

static void spire_draw_answer(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t value) {
  std::vector<uint32_t> values = std::move(spire_draw_inputs[c.get()]);
  spire_draw_inputs.erase(c.get());
  spire_overview_entries.erase(c.get()); // v62: a 'Q' ends the last overview, also one that gets no answer
  if (values.size() != (value & 0xFFFF)) {
    c->log.warning_f("Spire bridge: draw for serial {:010} in q{}: {} inputs announced, {} received; no answer",
        serial_number, quest_number, value & 0xFFFF, values.size());
    return;
  }
  std::string filename = std::format("{}/q{}.json", SPIRE_BRIDGE_DRAW_DIRECTORY, quest_number);
  SpireDrawTable table;
  try {
    table = SpireDrawTable::from_json_file(filename);
  } catch (const std::exception& e) {
    c->log.warning_f("Spire bridge: draw for serial {:010} in q{}: table {}: {}; no answer", serial_number,
        quest_number, filename, e.what());
    return;
  }
  auto in = spire_draw_input_from_values(values);
  // v44: seeded per (run, room, n-th draw, inputs), so replaying a room from its savestate draws the same; without a
  // run (a quest from before v44) random as before
  auto* run = spire_game_run_for(l);
  auto* cr = spire_client_run_for(c, l, false);
  uint32_t drawn;
  if (run && cr && cr->began) {
    uint32_t nth = cr->draws_per_room[in.k]++;
    uint64_t state = spire_draw_seed(run->run_id, in.k, nth, values);
    drawn = spire_draw(table, in, [&state](uint32_t n) -> uint32_t {
      return n ? static_cast<uint32_t>(spire_splitmix_next(state) % n) : 0;
    });
    c->log.info_f("Spire savestate: draw {} of room {} seeded from run {:08X}", nth + 1, in.k, run->run_id);
  } else {
    drawn = spire_draw(table, in, [](uint32_t n) -> uint32_t {
      return n ? (phosg::random_object<uint32_t>() % n) : 0;
    });
  }

  G_SyncQuestRegister_6x77 cmd;
  cmd.header.subcommand = 0x77;
  cmd.header.size = sizeof(cmd) / 4;
  cmd.header.unused = 0;
  cmd.register_number = SPIRE_BRIDGE_DRAW_RESULT_REGISTER;
  cmd.value.as_int = drawn;
  send_command_t(c, 0x60, 0x00, cmd);
  cmd.register_number = SPIRE_BRIDGE_REQUEST_REGISTER;
  cmd.value.as_int = SPIRE_BRIDGE_DONE_VALUE;
  send_command_t(c, 0x60, 0x00, cmd);
  c->log.info_f("Spire bridge: draw for serial {:010} in q{} (arch {} k {} path {} choice {} relics {:08X}): r{}={:08X}, "
      "r{}=DONE", serial_number, quest_number, in.archetype, in.k, in.path, in.choice, in.relics,
      SPIRE_BRIDGE_DRAW_RESULT_REGISTER, drawn, SPIRE_BRIDGE_REQUEST_REGISTER);

  // v62 (ROUTEB8-PROTOCOL.md § 2): the overview of the three rewards, to this client only, kept for its 'C' 2
  std::string overview = spire_draw_overview_line(table, in.archetype, drawn);
  if (overview.empty()) {
    c->log.info_f("Spire bridge: draw for serial {:010} in q{}: no overview line (not every drawn entry has a name)",
        serial_number, quest_number);
  } else {
    if (spire_overview_entries.size() > 256) {
      std::erase_if(spire_overview_entries, [](const auto& e) { return e.second.client.expired(); });
    }
    spire_overview_entries[c.get()] = SpireOverviewEntry{c, SpireOverviewKept{overview, l->lobby_id, c->floor}};
    spire_send_ticker(c, overview);
    c->log.info_f("Spire bridge: draw for serial {:010} in q{}: sent \"{}\"", serial_number, quest_number, overview);
  }
}

// v44: 'V' (SpireFlagBridge.hh): the client's room-start state and its run copy's items, replacing its savestate
static void spire_savestate_write(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t value) {
  std::vector<uint32_t> values = std::move(spire_draw_inputs[c.get()]);
  spire_draw_inputs.erase(c.get());
  auto* run = spire_game_run_for(l);
  auto* cr = spire_client_run_for(c, l, false);
  const SpireRunEnd* end = spire_run_end_for(l);
  // the overlay is the run copy; the real character is the server's character data and never goes in
  bool has_overlay = c->has_overlay();
  if (!run || !spire_savestate_write_allowed(!!end, has_overlay, cr && cr->began, values.size(), value & 0xFFFF)) {
    c->log.info_f("Spire savestate: 'V' for serial {:010} in q{} not stored ({} of {} values, {}, {}, {})", serial_number,
        quest_number, values.size(), value & 0xFFFF, end ? "run ended" : "run on", has_overlay ? "run copy" : "no run copy",
        (cr && cr->began) ? "run begun" : "no 'B' yet");
    return;
  }
  std::string filename = spire_savestate_filename(c);
  auto overlay = c->character_file(false, true);
  if (filename.empty() || !overlay) {
    c->log.warning_f("Spire savestate: no character data for serial {:010}; not stored", serial_number);
    return;
  }
  SpireSavestate ss;
  ss.exists = true;
  ss.run_id = run->run_id;
  ss.episode = static_cast<uint8_t>(l->episode);
  ss.quest_number = quest_number;
  ss.start_room = cr->start_room;
  ss.practice = cr->practice;
  // F6: the ledger follows the feed mirror (the overlay), unless an in-game 61 refreshed it; only its run mag comes back
  auto* led = spire_run_mag_ledger_for(c, run->run_id, false);
  if (spire_run_mag_savestate_follow(
          led, spire_run_mag_overlay_refreshed(c, l), spire_run_mag_inventory_mags(overlay->inventory))) {
    c->log.info_f("Spire run mag: ledger of run {:08X} follows the fed mag {}", run->run_id,
        spire_run_mag_bytes_hex(*led->last_mirrored));
  }
  ss.items = spire_savestate_items(overlay->inventory, values[SPIRE_SS_GLASS], &ss.glass_index,
      led ? led->last_mirrored : std::nullopt);
  if (led) {
    ss.run_mag = *led;
  }
  uint32_t room_k = values[SPIRE_SS_K];
  ss.values = std::move(values);
  ss.time_usecs = phosg::now();
  spire_bridge_write_durable(filename, spire_savestate_to_json(ss, serial_number));
  run->savestate_files.emplace(filename);
  c->log.info_f("Spire savestate: stored {} in {}{}", spire_savestate_describe(ss), filename,
      ss.run_mag ? std::format(" with the run mag ledger (step {})", ss.run_mag->issued_step) : "");
  // F6 / bundle 2 S: the boss burst (F7, quest) from De Rol Le on: this client's 'V' of a boss room start is the
  // server's view of its boss door (the quest casts right after it). One line to this client only, once per room and
  // run (SpireClientRun); the boss rooms are spire-mag.json's BossRooms (no file: no text, no warning)
  try {
    if ((room_k >= SPIRE_BOSS_BURST_FROM_K) && std::filesystem::exists(SPIRE_RUN_MAG_FILE)) {
      auto table = spire_mag_parse(phosg::load_file(SPIRE_RUN_MAG_FILE));
      if (spire_boss_burst_text_due(room_k, table.boss_rooms, cr->boss_burst_texts)) {
        spire_send_ticker(c, SPIRE_BOSS_BURST_TEXT);
        c->log.info_f("Spire bridge: boss room {} starts; sent \"{}\"", room_k, SPIRE_BOSS_BURST_TEXT);
      } else if (table.boss_rooms.count(room_k)) {
        c->log.info_f("Spire bridge: boss room {} starts again; burst text already sent in this run", room_k);
      }
    }
  } catch (const std::exception& e) {
    c->log.warning_f("Spire bridge: {} unreadable ({}); no boss room text", SPIRE_RUN_MAG_FILE, e.what());
  }
}

static void spire_send_register(std::shared_ptr<Client> c, uint8_t register_number, uint32_t value) {
  G_SyncQuestRegister_6x77 cmd;
  cmd.header.subcommand = 0x77;
  cmd.header.size = sizeof(cmd) / 4;
  cmd.header.unused = 0;
  cmd.register_number = register_number;
  cmd.value.as_int = value;
  send_command_t(c, 0x60, 0x00, cmd);
}

// v44: 'J' (SpireFlagBridge.hh), always answered with 'DONE' so the quest never waits on a race. Start (bits 16-23 0):
// the game's run is the leader's savestate if it fits this quest's room and variant, and this client resumes from its
// own savestate only with the same run id. Item (bits 16-23 1): item i of the copy taken at the start answer.
static void spire_resume_answer(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t value) {
  uint32_t sub = (value >> 16) & 0xFF;
  const SpireRunEnd* end = spire_run_end_for(l);
  if (sub == 1) {
    uint32_t index = value & 0xFFFF;
    auto* cr = spire_client_run_for(c, l, false);
    uint32_t ack = spire_resume_item_ack(!!end, cr && cr->answered_resume, index,
        cr ? cr->savestate.items.size() : 0, cr ? cr->savestate.glass_index : -1);
    if (ack) {
      const auto& item = cr->savestate.items[index];
      for (size_t z = 0; z < 12; z++) {
        spire_send_register(c, SPIRE_RESUME_ITEM_FIRST_REGISTER + z, item.data1[z]);
      }
      c->log.info_f("Spire savestate: item {} of {} for serial {:010} in q{}: {}", index + 1,
          cr->savestate.items.size(), serial_number, quest_number, item.hex());
    } else {
      c->log.warning_f("Spire savestate: item {} for serial {:010} in q{} not sent ({})", index + 1, serial_number,
          quest_number, end ? "run ended" : (cr && cr->answered_resume) ? "no such item" : "no resume in this game");
    }
    spire_send_register(c, SPIRE_RESUME_ITEM_ACK_REGISTER, ack);
    spire_send_register(c, SPIRE_BRIDGE_REQUEST_REGISTER, SPIRE_BRIDGE_DONE_VALUE);
    return;
  }

  uint8_t k = value & 0xFF, variant = (value >> 8) & 0xFF, episode = static_cast<uint8_t>(l->episode);
  auto* run = spire_game_run_for(l);
  if (!run) {
    // the first request of the game decides its run from the leader's savestate
    const auto& follow = c->require_server_state()->data->spire_follow_leader;
    uint8_t leader_id = follow ? follow->first : 0;
    auto leader = (leader_id < l->clients.size()) ? l->clients[leader_id] : nullptr;
    SpireSavestate leader_ss = leader ? spire_savestate_load_logged(leader) : SpireSavestate();
    if (!end && spire_savestate_matches(leader_ss, episode, k, variant)) {
      run = &spire_game_run_set(l, SpireGameRun{
          .run_id = leader_ss.run_id, .resumed = true, .elapsed = leader_ss.values[SPIRE_SS_ELAPSED], .savestate_files = {}});
    } else {
      run = &spire_game_run_set(l, SpireGameRun{.run_id = spire_new_run_id(), .savestate_files = {}});
      l->log.info_f("Spire savestate: leader savestate {} does not fit room {} variant {} of episode {}",
          spire_savestate_describe(leader_ss), k, variant, episode);
    }
  }
  auto* cr = spire_client_run_for(c, l, true);
  SpireSavestate own = spire_savestate_load_logged(c);
  bool resume = spire_resume_client(!!end, run->resumed, run->run_id, spire_savestate_matches(own, episode, k, variant),
      own.run_id);
  if (resume) {
    run->savestate_files.emplace(spire_savestate_filename(c)); // a true end deletes it even if this client is gone by then
    cr->answered_resume = true;
    cr->savestate = std::move(own);
    spire_run_mag_adopt(c, run->run_id, cr->savestate.run_mag); // F6
    for (const auto& [index, reg] : SPIRE_RESUME_HEADER) {
      spire_send_register(c, reg, cr->savestate.values[index]);
    }
    spire_send_register(c, SPIRE_RESUME_ANSWER_REGISTER,
        SPIRE_RESUME_ANSWER_TAG | SPIRE_RESUME_ANSWER_RESUME | cr->savestate.items.size());
    c->log.info_f("Spire savestate: serial {:010} resumes in q{} room {} from {}", serial_number, quest_number, k,
        spire_savestate_describe(cr->savestate));
  } else {
    cr->answered_resume = false;
    cr->savestate = SpireSavestate();
    spire_send_register(c, SPIRE_RESUME_ANSWER_REGISTER, SPIRE_RESUME_ANSWER_TAG);
    c->log.info_f("Spire savestate: serial {:010} starts q{} room {} variant {} fresh (own savestate {}, game run {:08X}{})",
        serial_number, quest_number, k, variant, spire_savestate_describe(own), run->run_id,
        run->resumed ? " resumed" : "");
  }
  spire_send_register(c, SPIRE_BRIDGE_REQUEST_REGISTER, SPIRE_BRIDGE_DONE_VALUE);
}

// v62 (B-REGIE-16-13, ROUTEB8-PROTOCOL.md § 3): the bundle 8 texts that name something from the quest's draw.json
// (SpireDraw.hh). Each returns "" when it has nothing to say, so the caller keeps the text of before.

static const std::string SPIRE_TEXT_NO_NAME;

// a name from a list by a code's "index + 1" (0 = none); "" when not known
static const std::string& spire_text_name_at(const std::vector<std::string>* names, uint32_t index_plus_1) {
  return (!names || (index_plus_1 == 0) || (index_plus_1 > names->size())) ? SPIRE_TEXT_NO_NAME : (*names)[index_plus_1 - 1];
}

static const SpireDrawExam* spire_text_exam(const SpireBridgeTextContext& ctx, uint32_t k) {
  if (!ctx.draw) {
    return nullptr;
  }
  auto it = ctx.draw->exams.find(k);
  return ((it == ctx.draw->exams.end()) || it->second.boss.empty()) ? nullptr : &it->second;
}

// the first of the forms that fits in a ticker line
static std::string spire_text_first_fitting(std::initializer_list<std::string> forms) {
  for (const auto& form : forms) {
    if (!form.empty() && (form.size() <= SPIRE_BRIDGE_TEXT_MAX)) {
      return form;
    }
  }
  return "";
}

// 'C' 6 with bits 16-23 = typeB << 4 | typeA (0 normal, 1 elite, 2 rest, 3 exam; typeB 0xF one pipe); "" for 0 (an
// older quest) or a type it does not know
static std::string spire_text_way_open(uint32_t bits, uint32_t k, const SpireBridgeTextContext& ctx) {
  static const char* const TYPE_WORDS[4] = {"normal", "ELITE", "REST", "EXAM"};
  uint32_t type_a = bits & 0x0F, type_b = (bits >> 4) & 0x0F;
  if ((bits == 0) || (type_a > 3) || ((type_b > 3) && (type_b != 0x0F))) {
    return "";
  }
  if (type_b == 0x0F) {
    if (type_a == 3) {
      // contract: the boss from exams[k]; unknown, or a line over the limit: "Room k" in its place
      const auto* exam = spire_text_exam(ctx, k);
      return spire_text_first_fitting({exam ? std::format("The way is open. Walk to the warp: Exam {}.", exam->boss) : "",
          std::format("The way is open. Walk to the warp: Exam Room {}.", k)});
    }
    // one pipe that is no exam: not in the contract; the two-pipe words for it (route-b-8 report, deviation 2)
    return std::format("The way is open. Walk to the warp: Room {} {}.", k, TYPE_WORDS[type_a]);
  }
  return spire_text_first_fitting({std::format("The way is open: Room {} {} or Room {} {}. Walk close to a warp to read it.",
      k, TYPE_WORDS[type_a], k, TYPE_WORDS[type_b])});
}

// 'A' pipe approach: type 0/1 with extra 0 (an older quest, or no tag) and any type above 3 get the text of before
static std::string spire_text_approach(uint32_t type, uint32_t extra, uint32_t k, const SpireBridgeTextContext& ctx) {
  switch (type) {
    case 0:
    case 1: {
      if (extra == 0) {
        return "";
      }
      std::string head = (type == 1) ? std::format("Room {} - Elite: harder foes, rare reward.", k) : std::format("Room {}.", k);
      const std::string& tag = spire_text_name_at(ctx.draw ? &ctx.draw->tag_names : nullptr, extra);
      return spire_text_first_fitting({tag.empty() ? "" : std::format("{} Next reward: {}.", head, tag), head});
    }
    case 2:
      // spire.json texts.sign_rest ("No enemies: full HP, then the shop."), the same in every quest version so far, so
      // also for an older quest (whose rest code has extra 0 like the new one)
      return std::format("Room {} - Rest: no enemies, full HP, then the shop.", k);
    case 3: {
      const auto* exam = spire_text_exam(ctx, k);
      if (!exam) {
        return std::format("Room {} - Exam.", k);
      }
      const std::string& modifier = spire_text_name_at(&exam->modifiers, extra);
      return spire_text_first_fitting({modifier.empty() ? "" : std::format("Room {} - Exam: {} ({}).", k, exam->boss, modifier),
          std::format("Room {} - Exam: {}.", k, exam->boss), std::format("Room {} - Exam.", k)});
    }
    default:
      return "";
  }
}

// 'C' 10 exam reward: bits 16-22 rare entry index + 1 of the writer's archetype, bit 23 the rare did not fit; low byte
// bits 0-4 exam_extras index + 1, bits 5-7 n (meseta 500 + 100 n). A part without a name is left out; over the limit
// the extra goes first, then the rare name (the full note is about the rare, so it stays)
// the name of entry "index + 1" (0 = none) of the writer's archetype; "" when not known
static const std::string& spire_text_archetype_entry_name(const SpireBridgeTextContext& ctx, uint32_t index_plus_1) {
  if (ctx.draw && (ctx.archetype >= 0) && (static_cast<size_t>(ctx.archetype) < ctx.draw->archetypes.size())) {
    const auto& entries = ctx.draw->archetypes[ctx.archetype].entries;
    if ((index_plus_1 != 0) && (index_plus_1 <= entries.size())) {
      return entries[index_plus_1 - 1].name;
    }
  }
  return SPIRE_TEXT_NO_NAME;
}

static std::string spire_text_exam_reward(uint32_t value, const SpireBridgeTextContext& ctx) {
  uint32_t rare = (value >> 16) & 0x7F, extra = value & 0x1F, n = (value >> 5) & 0x07;
  bool full = (value >> 23) & 1;
  const std::string* rare_name = &spire_text_archetype_entry_name(ctx, rare);
  const std::string& extra_name = spire_text_name_at(ctx.draw ? &ctx.draw->exam_extras : nullptr, extra);
  auto form = [&](bool with_rare, bool with_extra) {
    std::string ret = std::format("Exam reward: {} meseta", 500 + 100 * n);
    if (with_rare && !rare_name->empty()) {
      ret += ", " + *rare_name;
    }
    if (with_extra && !extra_name.empty()) {
      ret += ", " + extra_name;
    }
    ret += ".";
    if (full) {
      ret += " Inventory full: make room, it waits.";
    }
    return ret;
  };
  return spire_text_first_fitting({form(true, true), form(true, false), form(false, false)});
}

// Route B bundle 9 (ROUTEB9-PROTOCOL.md § 1, 8.1 critic NB-1): 'C' 12, an exam reward while an older exam rare still
// waits. The layout of 'C' 10, but bits 16-22 name the WAITING rare (entry index + 1 of the writer's archetype, 0 = not
// known) and bit 23 is always set (the inventory is full by definition, so the bit says nothing more). A part without a
// name is left out; a waiting rare without a name keeps the "a rare still waits" words of the last form. Over the limit
// the extra goes first, then the rare name.
static std::string spire_text_exam_reward_waiting(uint32_t value, const SpireBridgeTextContext& ctx) {
  uint32_t rare = (value >> 16) & 0x7F, extra = value & 0x1F, n = (value >> 5) & 0x07;
  const std::string& rare_name = spire_text_archetype_entry_name(ctx, rare);
  const std::string& extra_name = spire_text_name_at(ctx.draw ? &ctx.draw->exam_extras : nullptr, extra);
  auto form = [&](bool with_extra, bool with_rare) {
    std::string ret = std::format("Exam reward: {} meseta", 500 + 100 * n);
    if (with_extra && !extra_name.empty()) {
      ret += ", " + extra_name;
    }
    if (with_rare && !rare_name.empty()) {
      ret += ". Still waiting: " + rare_name + ". Make room.";
    } else {
      ret += ". A rare still waits: make room.";
    }
    return ret;
  };
  return spire_text_first_fitting({form(true, true), form(false, true), form(false, false)});
}

// v17: the text for a short-message code (SPIRE_BRIDGE_MSG_*), the words of the quest's window_msg (spire.json
// texts; <color N> as $CN, <color 0> as $C7); empty if the value is no message code. v35: one line each, the form of
// "Waiting for your partner..." (no window during a run, see spire_ticker_line); v49: the run-over text names no
// place, since the player may go back to base ('O') or to the lobby (spire_run_over). v62 (B-REGIE-16-13): the bundle 8
// texts (SpireFlagBridge.hh); bits 16-23 = 0 give the text of before for 'C' 6 and 'A' types 0 and 1.
std::string spire_bridge_message_text(uint32_t value, const SpireBridgeTextContext& ctx) {
  uint8_t kind = value >> 24;
  uint32_t a = (value >> 8) & 0xFF, b = value & 0xFF, bits = (value >> 16) & 0xFF;
  switch (kind) {
    case SPIRE_BRIDGE_MSG_WAVE:
      return std::format("Wave {}/{}", a, b);
    case SPIRE_BRIDGE_MSG_LIFE:
      return a ? std::string("Last life: fall again and you stay down until the clear.")
               : std::format("$C1Life lost$C7 - {} left. You get up on the spot.", b);
    case SPIRE_BRIDGE_MSG_TIME:
      return std::format("$C4{} minutes left$C7 - the timer ends the run.", value & 0xFFFF);
    case SPIRE_BRIDGE_MSG_PARTNER:
      return b ? "Your partner is ready. Walk to choose your reward." : "Waiting for your partner..."; // v62
    case SPIRE_BRIDGE_MSG_RUN_OVER:
      // v49 (B-BASIS-12): true for both outcomes, the return to base ('O' OK) and the 6x73 to the lobby
      return std::format("Run over. Rooms cleared: {}, {} s.", (value >> 16) & 0xFF, value & 0xFFFF);
    case SPIRE_BRIDGE_MSG_PAUSE_B:
      return b ? "Reward taken. Quest Board: Shop (optional) or Ready = leave room."
               : "Reward taken. Quest Board: Shop (optional) or Continue = leave room.";
    case SPIRE_BRIDGE_MSG_DOWN:
      return b ? "You are down - you get up when the room is clear." : "You are back on your feet.";
    case SPIRE_BRIDGE_MSG_HINT:
      return "Take a step: the menu opens."; // v62
    case SPIRE_BRIDGE_MSG_PROMPT:
      switch (a) {
        case 1:
          return "Take your reward: Quest Board, slot Reward.";
        case 2: // v62: the writer's overview line of its last 'Q' (spire_overview_kept_line)
          return ctx.overview.empty() ? "Take a step: your reward list opens." : ctx.overview;
        case 6: { // v62: both pipes, or the exam pipe's boss
          std::string text = spire_text_way_open(bits, b, ctx);
          return text.empty() ? "The way is open. Walk to a warp." : text;
        }
        case 7:
          return "Partner has not taken a reward. The way is open.";
        case 8:
          return "Partner is in the shop. The warp waits for him.";
        case 9: // v38: the low byte is 1 on the leader, 0 on the follower; same text
          return "Waiting for your partner at Forest 1.";
        case 10: // v62: the exam reward
          return spire_text_exam_reward(value, ctx);
        case 11: // v62: the start hint at the Forest 1 arrival
          return "Take a step: your run starts.";
        case 12: // bundle 9: an exam reward while an older exam rare still waits
          return spire_text_exam_reward_waiting(value, ctx);
        default:
          return ""; // 3-5 retired (spire_bridge_handle_register takes them silently)
      }
    case SPIRE_BRIDGE_MSG_APPROACH: {
      std::string text = spire_text_approach(a, bits, b, ctx); // v62
      if (!text.empty()) {
        return text;
      }
      switch (a) {
        case 1:
          return std::format("Room {} - Elite (harder, better reward)", b);
        case 2:
          return std::format("Room {} - Rest", b);
        default:
          return std::format("Room {}", b);
      }
    }
    default:
      return "";
  }
}

std::string spire_bridge_message_text(uint32_t value) {
  return spire_bridge_message_text(value, SpireBridgeTextContext());
}

std::string spire_overview_kept_line(const SpireOverviewKept* kept, uint32_t lobby_id, uint32_t floor) {
  return (kept && (kept->lobby_id == lobby_id) && (kept->floor == floor)) ? kept->line : "";
}

int spire_draw_archetype_for_class(uint8_t char_class) {
  if (char_class >= 12) {
    return -1; // the 12 classes of v3 (char_class_is_* throws past them)
  }
  if (char_class_is_hunter(char_class)) {
    return 0;
  }
  if (char_class_is_ranger(char_class)) {
    return 1;
  }
  if (char_class_is_force(char_class)) {
    return 2;
  }
  return -1;
}

const char* spire_bridge_clear_text(bool server_boss_clear) {
  return server_boss_clear ? SPIRE_BRIDGE_BOSS_CLEAR_TEXT : SPIRE_BRIDGE_ROOM_CLEAR_TEXT;
}

// v41 (B-E01-96): lives are the quest's counter r172, never a Scape Doll, and the Battle respawn to the city is always
// off. Every 'L' code is a death the quest stands up on the spot (player_recovery): the log line says so, with the
// lives before and after (the code carries the lives left).
std::string spire_lives_revive_log_line(uint32_t serial_number, uint32_t value) {
  uint32_t left = value & 0xFF;
  return std::format("Spire lives: death of serial {:010} revived on the spot, lives {}->{}, no respawn to city",
      serial_number, left + 1, left);
}

bool spire_drop_is_forbidden(const ItemData& item) {
  return (item.data1[0] == 0x03) && (item.data1[1] == 0x09); // Scape Doll
}

bool spire_drop_filter(std::shared_ptr<const Client> c, std::shared_ptr<const Lobby> l, ItemData& item) {
  if (item.empty() || !spire_drop_is_forbidden(item) || !l->is_game() || !l->quest ||
      !spire_bridge_quest_is_bridged(c, l->quest->meta.quest_number)) {
    return false;
  }
  l->log.info_f("Spire drops: Scape Doll removed from a server drop in q{} (lives are a counter)",
      l->quest->meta.quest_number);
  item.clear();
  return true;
}

// v23: lives per client from its 'L' codes, for the checkpoint; v29: cleared at every quest start, so a second run on
// the same connection (or a new Client at a reused address) never writes the previous run's lives into LastLives
static std::unordered_map<const Client*, uint32_t> spire_checkpoint_lives;

void spire_checkpoint_lives_note(const Client* key, uint32_t lives) {
  spire_checkpoint_lives[key] = lives;
}

bool spire_checkpoint_lives_forget(const Client* key) {
  return spire_checkpoint_lives.erase(key) > 0;
}

uint32_t spire_checkpoint_lives_for(const Client* key) {
  auto it = spire_checkpoint_lives.find(key);
  return (it == spire_checkpoint_lives.end()) ? 0 : it->second;
}

// Audit S-12: partners skipped by spire_follow_leader_warp because they were still loading. Keyed by the Client's
// address and never dereferenced; an entry is removed at that client's next loaded floor or quest start.
static std::unordered_set<const Client*> spire_follow_pending;

// v48: the rooms k this client's run stored as a checkpoint in its current game (spire_checkpoint_store), and which of
// them were boss rooms, for the splinters at 'F'. Forgotten at every quest start and at the client's 'B'; the weak_ptr
// and the lobby id tell a reused Client address or a later game from the one that wrote it.
struct SpireRunRooms {
  std::weak_ptr<const Client> client;
  uint32_t lobby_id = 0;
  std::set<uint8_t> rooms;
  std::set<uint8_t> boss_rooms;
};
static std::unordered_map<const Client*, SpireRunRooms> spire_run_rooms;

static SpireRunRooms& spire_run_rooms_for(const std::shared_ptr<Client>& c, uint32_t lobby_id) {
  auto it = spire_run_rooms.find(c.get());
  if ((it != spire_run_rooms.end()) && ((it->second.client.lock() != c) || (it->second.lobby_id != lobby_id))) {
    spire_run_rooms.erase(it);
    it = spire_run_rooms.end();
  }
  if (it == spire_run_rooms.end()) {
    if (spire_run_rooms.size() > 256) {
      std::erase_if(spire_run_rooms, [](const auto& e) { return e.second.client.expired(); });
    }
    it = spire_run_rooms.emplace(c.get(), SpireRunRooms{}).first;
    it->second.client = c;
    it->second.lobby_id = lobby_id;
  }
  return it->second;
}

uint32_t spire_splinters_for_run(uint32_t rooms, uint32_t bosses, uint32_t heat_choice) {
  uint64_t base = static_cast<uint64_t>(rooms) + static_cast<uint64_t>(SPIRE_SPLINTERS_PER_BOSS) * bosses;
  uint64_t ret = (base * (10 + static_cast<uint64_t>(heat_choice))) / 10;
  return static_cast<uint32_t>(std::min<uint64_t>(ret, 0xFFFFFFFF));
}

void spire_checkpoint_run_started(std::shared_ptr<const Client> c) {
  spire_follow_pending.erase(c.get());
  spire_base_returned.erase(c.get()); // v49: an 'O' OK of an earlier quest never allows an 'I'
  spire_client_runs.erase(c.get()); // v44: a new quest is a new run for this client
  spire_run_rooms.erase(c.get()); // v48
  spire_overview_entries.erase(c.get()); // v62
  spire_overlay_refreshed.erase(c.get()); // F6: the quest load makes the overlay anew
  if (auto l = c->lobby.lock()) {
    spire_run_again_games.erase(l->lobby_id); // F6
    spire_run_forget_end(l->lobby_id, "quest start", c->log);
    if (spire_game_runs.erase(l->lobby_id)) {
      c->log.info_f("Spire savestate: the run of this game forgotten at the quest start");
    }
  }
  if (spire_checkpoint_lives_forget(c.get())) {
    c->log.info_f("Spire checkpoint: lives of the previous run forgotten at the quest start");
  }
}

// v18: records per character (SPIRE_BRIDGE_REC_*). The character comes from the server's character data (the 61 at
// login), never the Battle overlay, so each character on a card has its own records (E04 AC-6).

// SpireRecords: SpireFlagBridge.hh
static const char* SPIRE_RECORDS_DIFFICULTY_KEYS[4] = {"Normal", "Hard", "VeryHard", "Ultimate"};
// The score of the run ('K'), per client until its 'F'
static std::unordered_map<const Client*, uint32_t> spire_records_pending_score;

static std::string spire_records_filename(std::shared_ptr<Client> c, uint32_t serial_number) {
  auto character = c->character_file(false, false);
  if (!character) {
    return "";
  }
  return std::format("{}/{:010}.{}.records.json", SPIRE_BRIDGE_DIRECTORY, serial_number,
      spire_guard_character_id(character->disp));
}

SpireRecords spire_records_load(const std::string& filename) {
  SpireRecords ret;
  if (std::filesystem::is_regular_file(filename)) {
    auto json = phosg::JSON::parse(phosg::load_file(filename));
    ret.best_score = json.get_int("BestScore");
    ret.best_time = json.get_int("BestTime");
    ret.runs = json.get_int("Runs");
    ret.won = json.get_int("Won");
    ret.splinters = json.get_int("Splinters", 0);
    ret.splinters_total = json.get_int("SplintersTotal", 0);
    if (json.contains("Credited")) { // route B F2
      for (const auto& e_json : json.get_list("Credited")) {
        SpireRunCredit e;
        e.run_id = e_json->get_int("Run");
        for (const auto& k : e_json->get_list("Rooms")) {
          e.rooms.emplace(k->as_int());
        }
        for (const auto& k : e_json->get_list("Bosses")) {
          e.bosses.emplace(k->as_int());
        }
        e.base = e_json->get_int("Base");
        e.finished = e_json->get_bool("Finished", false);
        // F2 fix (M1): before BonusPaid, a finished entry had its whole bonus; never pay it again
        e.bonus_paid = e_json->get_int("BonusPaid", e.finished ? 0xFFFFFFFF : 0);
        e.won = e_json->get_bool("Won", false);
        ret.credited.emplace_back(std::move(e));
      }
    }
    if (json.contains("Difficulties")) {
      const auto& d = json.at("Difficulties");
      for (uint8_t z = 0; z < 4; z++) {
        if (d.contains(SPIRE_RECORDS_DIFFICULTY_KEYS[z])) {
          const auto& e = d.at(SPIRE_RECORDS_DIFFICULTY_KEYS[z]);
          ret.per_difficulty[z] = std::make_pair(e.get_int("Won"), e.get_int("BestTime"));
        }
      }
    }
  }
  return ret;
}

void spire_records_save(const std::string& filename, uint32_t serial_number, const SpireRecords& rec) {
  auto json = phosg::JSON::dict({
      {"SerialNumber", serial_number},
      {"BestScore", rec.best_score},
      {"BestTime", rec.best_time},
      {"Runs", rec.runs},
      {"Won", rec.won},
      {"Splinters", rec.splinters},
      {"SplintersTotal", rec.splinters_total},
  });
  if (!rec.per_difficulty.empty()) {
    auto d = phosg::JSON::dict();
    for (const auto& [difficulty, e] : rec.per_difficulty) {
      d.emplace(SPIRE_RECORDS_DIFFICULTY_KEYS[difficulty & 3], phosg::JSON::dict({{"Won", e.first}, {"BestTime", e.second}}));
    }
    json.emplace("Difficulties", std::move(d));
  }
  if (!rec.credited.empty()) { // route B F2
    auto list = phosg::JSON::list();
    for (const auto& e : rec.credited) {
      auto rooms = phosg::JSON::list();
      for (uint8_t k : e.rooms) {
        rooms.emplace_back(k);
      }
      auto bosses = phosg::JSON::list();
      for (uint8_t k : e.bosses) {
        bosses.emplace_back(k);
      }
      list.emplace_back(phosg::JSON::dict({{"Run", e.run_id}, {"Rooms", std::move(rooms)}, {"Bosses", std::move(bosses)},
          {"Base", e.base}, {"Finished", e.finished}, {"BonusPaid", e.bonus_paid}, {"Won", e.won}}));
    }
    json.emplace("Credited", std::move(list));
  }
  spire_bridge_write_durable(filename, json.serialize(phosg::JSON::SerializeOption::FORMAT));
}

// Route B F2: the run's credit entry (created if asked; then the oldest runs past SPIRE_RECORDS_CREDITED_RUNS go)
static SpireRunCredit* spire_records_credit_for(SpireRecords& rec, uint32_t run_id, bool create) {
  for (auto& e : rec.credited) {
    if (e.run_id == run_id) {
      return &e;
    }
  }
  if (!create) {
    return nullptr;
  }
  rec.credited.emplace_back(SpireRunCredit{
      .run_id = run_id, .rooms = {}, .bosses = {}, .base = 0, .finished = false, .bonus_paid = 0, .won = false});
  while (rec.credited.size() > SPIRE_RECORDS_CREDITED_RUNS) {
    rec.credited.erase(rec.credited.begin());
  }
  return &rec.credited.back();
}

static uint32_t spire_records_add_splinters(SpireRecords& rec, uint64_t amount) {
  uint32_t added = static_cast<uint32_t>(std::min<uint64_t>(amount, 0xFFFFFFFF - rec.splinters));
  rec.splinters += added;
  rec.splinters_total = static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(rec.splinters_total) + added, 0xFFFFFFFF));
  return added;
}

uint32_t spire_records_credit_room(
    SpireRecords& rec, const SpireShopPayout& payout, uint32_t run_id, uint8_t k, bool boss, bool practice) {
  if (practice || !run_id || !k) {
    return 0;
  }
  auto* e = spire_records_credit_for(rec, run_id, false);
  bool new_room = !e || !e->rooms.contains(k);
  bool new_boss = boss && (!e || !e->bosses.contains(k));
  if (!new_room && !new_boss) {
    return 0;
  }
  if (!e) {
    e = spire_records_credit_for(rec, run_id, true);
  }
  uint64_t amount = 0;
  if (new_room) {
    e->rooms.emplace(k);
    amount += payout.per_room;
  }
  if (new_boss) {
    e->bosses.emplace(k);
    amount += payout.per_boss;
  }
  uint32_t added = spire_records_add_splinters(rec, amount);
  e->base = static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(e->base) + added, 0xFFFFFFFF));
  return added;
}

uint32_t spire_records_credit_room_file(const std::string& filename, uint32_t serial_number,
    const SpireShopPayout& payout, uint32_t run_id, uint8_t k, bool boss, bool practice) {
  auto rec = spire_records_load(filename);
  uint32_t added = spire_records_credit_room(rec, payout, run_id, k, boss, practice);
  if (added) {
    spire_records_save(filename, serial_number, rec);
  }
  return added;
}

SpireFinishSplinters spire_records_finish_splinters(SpireRecords& rec, const SpireShopPayout* payout, uint32_t run_id,
    uint8_t pacts, const std::set<uint8_t>& rooms, const std::set<uint8_t>& boss_rooms, uint32_t legacy_rooms,
    uint32_t legacy_bosses) {
  SpireFinishSplinters ret;
  auto* e = run_id ? spire_records_credit_for(rec, run_id, false) : nullptr;
  bool came_before = e && e->finished;
  if (payout && run_id) {
    std::set<uint8_t> all = rooms;
    all.insert(boss_rooms.begin(), boss_rooms.end());
    for (uint8_t k : all) {
      ret.caught_up += spire_records_credit_room(rec, *payout, run_id, k, boss_rooms.contains(k), false);
    }
    e = spire_records_credit_for(rec, run_id, true);
    ret.base = e->base;
    // F2 fix (M1): only what the bonus on the run's base exceeds the bonus paid at earlier 'F's by
    uint64_t bonus = (static_cast<uint64_t>(e->base) * pacts * payout->pact_tenths) / 10;
    uint64_t owed = (bonus > e->bonus_paid) ? (bonus - e->bonus_paid) : 0;
    ret.pact_bonus = spire_records_add_splinters(rec, owed);
    e->bonus_paid = static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(e->bonus_paid) + ret.pact_bonus, 0xFFFFFFFF));
    e->finished = true;
    ret.added = ret.caught_up + ret.pact_bonus;
    if (came_before) {
      ret.warning = std::format("'F' of run {:08X} came before: paid only the difference ({} for rooms, {} bonus)", run_id,
          ret.caught_up, ret.pact_bonus);
    }
    return ret;
  }
  if (came_before) {
    ret.base = e->base;
    ret.warning = std::format("'F' of run {:08X} came before: no splinters paid twice", run_id);
    return ret;
  }
  if (e) { // the rooms were paid while the run had a Payout block: never the old V on top
    ret.base = e->base;
    e->finished = true;
    ret.warning = std::format("no Payout block in {} at 'F' of run {:08X}; its {} splinter(s) for rooms stay, no pact bonus",
        SPIRE_SHOP_CATALOGUE_FILE, run_id, e->base);
    return ret;
  }
  ret.legacy = true;
  ret.added = spire_records_add_splinters(rec, spire_splinters_for_run(legacy_rooms, legacy_bosses, 0));
  if (run_id) {
    // Fix round 2: the run's entry, so a repeated 'F' of it pays the V once (came_before above). Its rooms count as
    // paid and its bonus as paid in full, so a Payout block that shows up before a later 'F' pays neither twice.
    e = spire_records_credit_for(rec, run_id, true);
    e->rooms.insert(rooms.begin(), rooms.end());
    e->bosses.insert(boss_rooms.begin(), boss_rooms.end());
    e->rooms.insert(boss_rooms.begin(), boss_rooms.end());
    e->base = ret.added;
    e->bonus_paid = 0xFFFFFFFF;
    e->finished = true;
  }
  ret.warning = payout
      ? std::string("no run id at 'F' (no 'B' in this game): paid the old rooms + 5 per boss")
      : std::format("no Payout block in {}: paid the old rooms + 5 per boss at 'F'", SPIRE_SHOP_CATALOGUE_FILE);
  return ret;
}

// F2 fix (L4): see SpireFlagBridge.hh
SpireFinishStats spire_records_finish_stats(
    SpireRecords& rec, uint32_t run_id, uint8_t difficulty, uint32_t score, uint32_t seconds, bool won) {
  SpireFinishStats ret;
  ret.new_score = score > rec.best_score;
  ret.new_time = won && (!rec.best_time || (seconds < rec.best_time));
  if (ret.new_score) {
    rec.best_score = score;
  }
  if (ret.new_time) {
    rec.best_time = seconds;
  }
  if (won) {
    auto& e = rec.per_difficulty[difficulty & 3];
    if (!e.second || (seconds < e.second)) {
      e.second = seconds;
    }
    auto* run = run_id ? spire_records_credit_for(rec, run_id, true) : nullptr;
    if (!run || !run->won) {
      rec.won++;
      e.first++;
      ret.won_counted = true;
      if (run) {
        run->won = true;
      }
    }
  }
  return ret;
}

static std::string spire_records_board_text(const SpireRecords& rec) {
  std::string time = rec.best_time
      ? std::format("{} min {} s", rec.best_time / 60, rec.best_time % 60)
      : std::string("none yet");
  std::string ret = std::format("Best score: {} - best time: {} - runs: {}, won: {}", rec.best_score, time, rec.runs, rec.won);
  static const char* names[4] = {"Normal", "Hard", "VHard", "Ult"};
  for (const auto& [difficulty, e] : rec.per_difficulty) {
    if (e.first) {
      ret += std::format(" - {}: won {}, best {}:{:02}", names[difficulty & 3], e.first, e.second / 60, e.second % 60);
    }
  }
  return ret;
}

// Handles a record code; returns false if the value is none. Throws if the file cannot be written (the caller logs).
static bool spire_records_handle(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t value) {
  uint8_t kind = value >> 24;
  if ((kind != SPIRE_BRIDGE_REC_BEGIN) && (kind != SPIRE_BRIDGE_REC_SCORE) && (kind != SPIRE_BRIDGE_REC_FINISH) &&
      (kind != SPIRE_BRIDGE_REC_BOARD)) {
    return false;
  }
  if (kind == SPIRE_BRIDGE_REC_SCORE) {
    spire_records_pending_score[c.get()] = value & 0xFFFFFF;
    c->log.info_f("Spire records: score {} for serial {:010} in q{}", value & 0xFFFFFF, serial_number, quest_number);
    return true;
  }
  std::string filename = spire_records_filename(c, serial_number);
  if (filename.empty()) {
    c->log.info_f("Spire records: no character data for serial {:010}; code {:08X} ignored", serial_number, value);
    return true;
  }
  auto rec = spire_records_load(filename);
  if (kind == SPIRE_BRIDGE_REC_BEGIN) {
    spire_run_forget_end(l->lobby_id, "run started", c->log);
    spire_run_rooms.erase(c.get()); // v48: the splinters count the rooms of this run only
    // v44: 'B' << 24 | resumed << 16 | K0 << 8 | start room (0: a quest from before v44). The game's run is set here
    // when no 'J' set it (a run from its first room); the client's run start room and practice bit follow.
    uint8_t start_k = value & 0xFF, first_k = (value >> 8) & 0xFF;
    auto* run = spire_game_run_for(l);
    if (!run) {
      run = &spire_game_run_set(l, SpireGameRun{.run_id = spire_new_run_id(), .savestate_files = {}});
    }
    auto* cr = spire_client_run_for(c, l, true);
    cr->began = true;
    cr->resumed = start_k && ((value >> 16) & 1) && cr->answered_resume;
    cr->practice = spire_run_is_practice(start_k, first_k, cr->resumed, cr->savestate.practice);
    cr->start_room = cr->resumed ? cr->savestate.start_room : start_k;
    if (spire_run_counts_as_new(start_k, cr->resumed, cr->practice)) {
      rec.runs++;
      spire_records_save(filename, serial_number, rec);
    }
    std::string ticker = spire_run_start_ticker(cr->resumed, cr->practice, cr->resumed ? &cr->savestate : nullptr);
    if (!ticker.empty()) {
      spire_send_ticker(c, ticker);
    }
    bool resumed_mag = std::any_of(cr->savestate.items.begin(), cr->savestate.items.end(),
        [](const ItemData& item) { return item.data1[0] == 0x02; });
    std::string mag_ticker = spire_run_resume_mag_ticker(cr->resumed, resumed_mag);
    if (!mag_ticker.empty()) {
      spire_send_ticker_later(c, l, mag_ticker, SPIRE_RESUME_MAG_TEXT_DELAY);
    }
    c->log.info_f("Spire records: run started (start room {}, first room {}{}{}), run {:08X}, {} run(s) in {}", start_k,
        first_k, cr->resumed ? ", resumed" : "", cr->practice ? ", practice" : "", run->run_id, rec.runs, filename);
    return true;
  }
  if (kind == SPIRE_BRIDGE_REC_BOARD) {
    spire_send_ticker(c, spire_records_board_text(rec)); // asked for on the quest board, during the run
    c->log.info_f("Spire records: board for {}", filename);
    return true;
  }

  // v44: a practice run (B-E01-182) changes no record, unlocks nothing and opens no payout
  if (auto* cr = spire_client_run_for(c, l, false); cr && cr->began && cr->practice) {
    spire_records_pending_score.erase(c.get());
    spire_run_mark_ended(l, "records: run finished");
    spire_defer_to_lobby(c, "Practice run: no records, unlocks or payout.");
    c->log.info_f("Spire records: practice run finished ({:08X}) for serial {:010} in q{} (start room {}); records, "
        "unlocks and payout unchanged", value, serial_number, quest_number, cr->start_room);
    return true;
  }
  // 'F': the run's score ('K' before it) and time against the records
  bool is_won = spire_meta_decode_finish(value).won;
  uint32_t seconds = value & 0xFFFF;
  auto it = spire_records_pending_score.find(c.get());
  uint32_t score = (it == spire_records_pending_score.end()) ? 0 : it->second;
  if (it != spire_records_pending_score.end()) {
    spire_records_pending_score.erase(it);
  }
  // Route B F2: the rooms were paid when their checkpoint was stored (spire_records_credit_room); 'F' adds the pact
  // bonus and pays rooms of this game the checkpoint could not pay. Without a Payout block, the old V (v48 below).
  // v48: photon splinters (SPIRE_SPLINTERS_PER_BOSS); the heat choice is not known on the server yet, so it counts 0
  const auto& run_rooms = spire_run_rooms_for(c, l->lobby_id);
  uint32_t rooms = run_rooms.rooms.size();
  uint32_t rooms_before_resume = 0;
  if (auto* cr = spire_client_run_for(c, l, false); cr && cr->began && cr->resumed &&
      (cr->savestate.values.size() > SPIRE_SS_K) && (cr->savestate.values[SPIRE_SS_K] > cr->start_room)) {
    rooms_before_resume = cr->savestate.values[SPIRE_SS_K] - cr->start_room; // cleared before the resume
  }
  auto finish = spire_meta_decode_finish(value);
  uint32_t bosses = std::max<uint32_t>(finish.exams, run_rooms.boss_rooms.size());
  std::optional<SpireShopPayout> payout;
  try {
    payout = spire_shop_parse(phosg::load_file(SPIRE_SHOP_CATALOGUE_FILE)).payout;
  } catch (const std::exception& e) {
    c->log.warning_f("Spire records: {} unreadable at 'F' ({}); no Payout", SPIRE_SHOP_CATALOGUE_FILE, e.what());
  }
  const auto* game_run = spire_game_run_for(l);
  uint32_t run_id = game_run ? game_run->run_id : 0;
  auto paid = spire_records_finish_splinters(rec, payout ? &*payout : nullptr, run_id, finish.pacts, run_rooms.rooms,
      run_rooms.boss_rooms, rooms + rooms_before_resume, bosses);
  // F2 fix (L4): after the splinters, since it may create the run's credit entry; a repeated 'F' counts Won once
  auto stats = spire_records_finish_stats(rec, run_id, static_cast<uint8_t>(l->difficulty), score, seconds, is_won);
  bool new_score = stats.new_score;
  bool new_time = stats.new_time;
  if (is_won && !stats.won_counted) {
    c->log.warning_f("Spire records: won 'F' of run {:08X} came before; Won not counted again", run_id);
  }
  spire_records_save(filename, serial_number, rec);
  if (!paid.warning.empty()) {
    c->log.warning_f("Spire records: {}", paid.warning);
  }
  if (paid.legacy) {
    c->log.info_f("Spire records: {} photon splinter(s) for serial {:010} in q{} ({} room(s) in this game + {} before the resume, {} boss(es), heat choice 0); balance {}, total {}",
        paid.added, serial_number, quest_number, rooms, rooms_before_resume, bosses, rec.splinters, rec.splinters_total);
  } else {
    c->log.info_f("Spire records: run {:08X} finished for serial {:010} in q{}: {} splinter(s) paid for its rooms, {} pact(s) "
        "add {}, {} caught up at 'F'; balance {}, total {}",
        run_id, serial_number, quest_number, paid.base, finish.pacts, paid.pact_bonus, paid.caught_up, rec.splinters,
        rec.splinters_total);
  }
  spire_run_mark_ended(l, "records: run finished");
  std::string text = new_score ? std::format("New best score: {}!", rec.best_score)
                               : std::format("Best score: {}.", rec.best_score);
  if (new_time) {
    text += std::format("\nNew best time: {} s!", seconds);
  }
  // v24: unlocks and payout flags for the character (SpireMeta), on the game's difficulty
  // Fix round 2: only the first won 'F' of a run opens the payout flags (stats.won_counted; run id 0 counts every time)
  text += spire_meta_on_finish(c, l, serial_number, quest_number, value, new_time, stats.won_counted);
  text += "\nRecords saved.";
  // v35: never a window in the run (Marck, pc 2026-10-25: "New best score" had to be pressed away mid-game); the
  // records text waits for the lobby (spire_deliver_lobby_texts)
  spire_defer_to_lobby(c, text);
  c->log.info_f("Spire records: run finished (score {}, {} s, {}) for serial {:010} in q{}; {}: best {} / {} s, {} run(s), {} won",
      score, seconds, is_won ? "won" : "not won", serial_number, quest_number, filename, rec.best_score,
      rec.best_time, rec.runs, rec.won);
  return true;
}

// v52: the start_level bonus each client's quest .bin carried at the quest load (spire_start_level_quest_bin), for its
// overlay in on_quest_loaded; and the bonus its current overlay (the run copy) has, for the shop's START, so P and H
// count only the levels the run really has (a level bought on Pioneer 2 after the quest load applies from the next
// quest load or run again). Keyed by the Client's address and never dereferenced; the overlay takes the first entry
// and sets the second, and a 98 (spire_guard_end_run) drops both.
struct SpireStartLevelSent {
  uint32_t lobby_id = 0;
  uint32_t quest_number = 0;
  uint32_t bonus = 0;
};
static std::unordered_map<const Client*, SpireStartLevelSent> spire_start_level_sent;
static std::unordered_map<const Client*, SpireStartLevelSent> spire_start_level_run;

// v55: the heat step of each client's quest .bin (spire_heat_quest_bin sets it at every send, SpireFlagBridge.hh
// SpireHeatSent), for the 'M' START answer. Keyed by the Client's address and never dereferenced; a 98
// (spire_guard_end_run) drops it.
static std::unordered_map<const Client*, SpireHeatSent> spire_heat_sent;

uint32_t spire_heat_start_answer(const SpireHeatSent* sent, uint32_t lobby_id, uint32_t quest_number) {
  return (sent && (sent->lobby_id == lobby_id) && (sent->quest_number == quest_number)) ? sent->step : 0;
}

uint32_t spire_shop_answer(uint8_t sub, SpireShopRefusal r) {
  return SPIRE_SHOP_TAG | (static_cast<uint32_t>(sub) << 16) | (static_cast<uint32_t>(r) << 8) |
      ((r == SpireShopRefusal::NONE) ? 1 : 0);
}

// Route B F5/F6, bundle 2 S: SPIRE_KIT_USE_TEXT, SPIRE_KIT_USE_TEXT_DELAY after the 'M' START answer, so the kit items
// exist when it shows. By then the client's 'J' has told a resume (no kit) from a fresh start; once per client and run.
static asio::awaitable<void> spire_kit_use_text_later(
    std::weak_ptr<Client> weak_c, std::weak_ptr<const Lobby> weak_l, uint32_t hp_mat, uint32_t atp_grind) {
  co_await async_sleep(SPIRE_KIT_USE_TEXT_DELAY);
  auto c = weak_c.lock();
  auto l = weak_l.lock();
  if (!c || !l || (c->lobby.lock() != l)) {
    co_return;
  }
  auto* cr = spire_client_run_for(c, l, true);
  if (spire_kit_use_text_due(true, hp_mat, atp_grind, cr->answered_resume, cr->kit_text_sent)) {
    cr->kit_text_sent = true;
    // route B bundle 9: with r143 = 1 the kit weapon carries the grind G of the client's 'C' 13 (not come yet: the
    // quest's G, 2 Monogrinders per atp_grind level, which no kit weapon's MaxGrind caps) and no Monogrinders
    bool server_equips = spire_equip_item_available_for(c);
    uint32_t grind = (cr->kit_grind >= 0) ? static_cast<uint32_t>(cr->kit_grind) : 2 * atp_grind;
    std::string text = spire_kit_use_text(server_equips, grind, hp_mat);
    if (!text.empty()) {
      spire_send_ticker(c, text);
    }
    c->log.info_f("Spire shop: kit hp_mat {} atp_grind {} r{} {}; sent \"{}\"", hp_mat, atp_grind, SPIRE_EQUIP_REGISTER,
        server_equips ? 1 : 0, text);
  } else {
    c->log.info_f("Spire shop: kit hp_mat {} atp_grind {}; no kit text ({})", hp_mat, atp_grind,
        cr->answered_resume ? "resumed: no kit" : "already sent in this run");
  }
}

// v50: the base shop ('M', SpireFlagBridge.hh and SpireMeta.hh). Always answers the client (state registers when it
// has a character and the catalogue, r41, then 'DONE'). Throws nothing.
static void spire_shop_handle(std::shared_ptr<Client> c, uint32_t serial_number,
    uint32_t quest_number, uint32_t value) {
  uint8_t sub = (value >> 16) & 0xFF;
  size_t index = value & 0xFF;
  auto refusal = SpireShopRefusal::NONE;
  std::string what = (sub == SPIRE_SHOP_LIST) ? "LIST" : (sub == SPIRE_SHOP_BUY) ? "BUY" : (sub == SPIRE_SHOP_START) ? "START" : "?";
  std::string detail;
  uint32_t kit_hp_mat = 0, kit_atp_grind = 0; // the START answer's kit levels (route B F5: the kit text)
  try {
    auto character = c->character_file(false, false);
    std::string records_filename = spire_records_filename(c, serial_number);
    std::string meta_filename;
    SpireMetaFile meta;
    if (character && !records_filename.empty()) {
      meta = spire_meta_load_for_client(c, serial_number, &meta_filename);
    }
    SpireShopCatalogue cat;
    if (!character || records_filename.empty() || meta_filename.empty()) {
      refusal = SpireShopRefusal::NO_CHARACTER;
    } else if ((sub != SPIRE_SHOP_LIST) && (sub != SPIRE_SHOP_BUY) && (sub != SPIRE_SHOP_START)) {
      refusal = SpireShopRefusal::UNKNOWN_ITEM;
    } else {
      try {
        cat = spire_shop_parse(phosg::load_file(SPIRE_SHOP_CATALOGUE_FILE));
      } catch (const std::exception& e) {
        refusal = SpireShopRefusal::NO_CATALOGUE;
        detail = std::format(" ({}: {})", SPIRE_SHOP_CATALOGUE_FILE, e.what());
      }
    }
    if (refusal == SpireShopRefusal::NONE) {
      auto rec = spire_records_load(records_filename);
      uint8_t open_rung[2] = {spire_meta_open_rung(meta, 1), spire_meta_open_rung(meta, 2)};
      uint8_t char_class = character->disp.visual.sh.char_class;
      // route B F3: the archetype's second kit already owned (its Normal flag) makes kit_tier a double buy: refused
      // with ALREADY_OPEN at BUY, and listed with price 0
      bool kit2 = spire_meta_kit2_owned(meta, char_class);
      if (sub == SPIRE_SHOP_BUY) {
        refusal = spire_shop_buy_refusal(cat, index, meta.upgrades, rec.splinters, open_rung, kit2);
        if (refusal == SpireShopRefusal::NONE) {
          uint32_t level_before = spire_shop_level(meta.upgrades, index);
          uint32_t price = spire_shop_apply_buy(cat, index, meta);
          try {
            // the level first, then the debit: a failure between the two writes costs the player nothing
            spire_meta_save_file(meta_filename, serial_number, meta);
            rec.splinters -= price;
            spire_records_save(records_filename, serial_number, rec);
          } catch (const std::exception& e) {
            refusal = SpireShopRefusal::FILE_ERROR;
            detail = std::format(" ({})", e.what());
          }
          if (refusal == SpireShopRefusal::NONE) {
            open_rung[0] = spire_meta_open_rung(meta, 1);
            open_rung[1] = spire_meta_open_rung(meta, 2);
            detail = std::format(" {} {} -> {} for {} splinter(s)", SPIRE_SHOP_ITEM_NAMES[index], level_before,
                level_before + 1, price);
          }
        } else if (index < SPIRE_SHOP_NUM_ITEMS) {
          detail = std::format(" {}", SPIRE_SHOP_ITEM_NAMES[index]);
        }
      }
      // v52: at the run start, start_level counts as the run copy has it (spire_start_level_run), not as owned
      auto upgrades = meta.upgrades;
      if ((sub == SPIRE_SHOP_START) && spire_shop_level(upgrades, 0)) {
        uint32_t run_bonus = 0;
        auto it = spire_start_level_run.find(c.get());
        auto cl = c->lobby.lock();
        if ((it != spire_start_level_run.end()) && cl && (it->second.lobby_id == cl->lobby_id) &&
            (it->second.quest_number == quest_number)) {
          run_bonus = it->second.bonus;
        }
        if (run_bonus < spire_shop_level(upgrades, 0)) {
          detail += std::format(" (start_level {} owned, {} in this run copy)", spire_shop_level(upgrades, 0), run_bonus);
          upgrades[SPIRE_SHOP_ITEM_NAMES[0]] = run_bonus;
        }
      }
      if (sub == SPIRE_SHOP_START) {
        kit_hp_mat = spire_shop_level(upgrades, 2); // hp_mat
        kit_atp_grind = spire_shop_level(upgrades, 3); // atp_grind
      }
      uint32_t strength = spire_shop_strength(cat, upgrades);
      uint32_t heat = spire_shop_heat(cat, char_class, strength, spire_meta_ladder_quest_rung(quest_number)); // v54
      std::string heat_str = std::format("H {}", heat);
      if (sub == SPIRE_SHOP_START) { // v55: the game's H, the rows this client's .bin carries (SpireHeatSent)
        auto cl = c->lobby.lock();
        auto it = spire_heat_sent.find(c.get());
        uint32_t own = heat;
        heat = spire_heat_start_answer((it != spire_heat_sent.end()) ? &it->second : nullptr,
            cl ? cl->lobby_id : 0, quest_number);
        heat_str = std::format("H {} of the game's rows (own H_auto {})", heat, own);
      }
      std::string levels_str;
      for (size_t z = 0; z < SPIRE_SHOP_NUM_ITEMS; z++) {
        uint32_t level = spire_shop_level(upgrades, z);
        uint32_t price = spire_shop_next_price(cat, z, meta.upgrades, open_rung, kit2);
        spire_send_register(c, SPIRE_SHOP_LEVEL_FIRST_REGISTER + z, level);
        spire_send_register(c, SPIRE_SHOP_PRICE_FIRST_REGISTER + z, price);
        levels_str += std::format(" {}={}/{}", SPIRE_SHOP_ITEM_NAMES[z], level, price);
      }
      spire_send_register(c, SPIRE_SHOP_BALANCE_REGISTER, rec.splinters);
      spire_send_register(c, SPIRE_SHOP_STRENGTH_REGISTER, strength);
      spire_send_register(c, SPIRE_SHOP_HEAT_REGISTER, heat);
      detail += std::format("; balance {}, P {}, {} ({}), level/next price:{}, rungs open {}/{}", rec.splinters,
          strength, heat_str, name_for_char_class(char_class), levels_str, open_rung[0], open_rung[1]);
    }
  } catch (const std::exception& e) {
    refusal = SpireShopRefusal::FILE_ERROR;
    detail = std::format(" ({})", e.what());
  }
  uint32_t answer = spire_shop_answer(sub, refusal);
  // route B bundle 9 (ROUTEB9-PROTOCOL.md § 1): r143 in every 'M' answer, before DONE, refused or not
  uint32_t equip_reg = spire_equip_item_available_for(c) ? 1 : 0;
  spire_send_register(c, SPIRE_EQUIP_REGISTER, equip_reg);
  detail += std::format("; r{}={}", SPIRE_EQUIP_REGISTER, equip_reg);
  spire_send_register(c, SPIRE_SHOP_ANSWER_REGISTER, answer);
  spire_send_register(c, SPIRE_BRIDGE_REQUEST_REGISTER, SPIRE_BRIDGE_DONE_VALUE);
  // route B F5/F6: the kit hands out the HP Materials and Monogrinders right after this answer
  if ((refusal == SpireShopRefusal::NONE) &&
      spire_kit_use_text_due(sub == SPIRE_SHOP_START, kit_hp_mat, kit_atp_grind, false, false)) {
    if (auto cl = c->lobby.lock()) {
      asio::co_spawn(*c->require_server_state()->io_context,
          spire_kit_use_text_later(c, cl, kit_hp_mat, kit_atp_grind), asio::detached);
    }
  }
  c->log.info_f("Spire shop: {} {:08X} for serial {:010} in q{}: {} ({}){}; r{}={:08X}", what, value, serial_number,
      quest_number, (refusal == SpireShopRefusal::NONE) ? "OK" : "FAIL", spire_shop_refusal_name(refusal), detail,
      SPIRE_SHOP_ANSWER_REGISTER, answer);
}

// Route B bundle 9 (ROUTEB9-PROTOCOL.md § 3): SpireEquipItem with one item id, to this client only. Never throws; the
// client's B3 answer is logged.
static asio::awaitable<void> spire_equip_item_send(std::shared_ptr<Client> c, uint32_t item_id, std::string what) {
  try {
    auto s = c->require_server_state();
    auto fn = s->data->client_functions->get(SPIRE_EQUIP_FUNCTION_NAME, c->specific_version);
    co_await prepare_client_for_patches(c);
    auto result = co_await send_function_call(c, fn, {{SPIRE_EQUIP_ITEM_ID_LABEL, item_id}});
    c->log.info_f("Spire equip: {} {:08X}; client answered {}", what, item_id, result.return_value.load());
  } catch (const std::exception& e) {
    c->log.warning_f("Spire equip: {} {:08X}; failed to send {}: {}", what, item_id, SPIRE_EQUIP_FUNCTION_NAME,
        e.what());
  }
}

// Route B bundle 9: 'C' 13 (the kit weapon with grind G in the low byte) and 'C' 14 (the run mag replaced after an
// evolution) from the writer: with r143 = 1 for it, send SpireEquipItem for the item in newserv's copy of its inventory
// (the run copy during a run). r143 = 0 or no such item: one log line, nothing sent. False for every other value.
static bool spire_equip_prompt_handle(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t value) {
  uint8_t point = (value >> 8) & 0xFF;
  if (((value >> 24) != SPIRE_BRIDGE_MSG_PROMPT) || ((value >> 16) & 0xFF) ||
      ((point != SPIRE_PROMPT_KIT_GROUND) && (point != SPIRE_PROMPT_RUN_MAG_REPLACED))) {
    return false;
  }
  uint8_t low = value & 0xFF;
  const char* what = (point == SPIRE_PROMPT_KIT_GROUND) ? "kit weapon" : "run mag";
  if (point == SPIRE_PROMPT_KIT_GROUND) {
    spire_client_run_for(c, l, true)->kit_grind = low;
  }
  bool available = spire_equip_item_available_for(c);
  std::optional<uint32_t> item_id;
  if (available) {
    if (auto character = c->character_file(false, true)) {
      item_id = spire_equip_target(point, low, true, character->inventory);
    }
  }
  if (!available) {
    c->log.info_f("Spire equip: 'C' {} ({}, low byte {}) for serial {:010} in q{}; r{} = 0 for this client, nothing sent",
        point, what, low, serial_number, quest_number, SPIRE_EQUIP_REGISTER);
  } else if (!item_id) {
    c->log.info_f("Spire equip: 'C' {} ({}, low byte {}) for serial {:010} in q{}; no such item in the inventory copy, nothing sent",
        point, what, low, serial_number, quest_number);
  } else {
    c->log.info_f("Spire equip: 'C' {} ({}, low byte {}) for serial {:010} in q{}; sending {} for item {:08X}", point,
        what, low, serial_number, quest_number, SPIRE_EQUIP_FUNCTION_NAME, *item_id);
    asio::co_spawn(*c->require_server_state()->io_context, spire_equip_item_send(c, *item_id, what), asio::detached);
  }
  return true;
}

// v48: the return to base ('O'), below with the guard's restore
static asio::awaitable<void> spire_base_return(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint32_t serial_number, uint32_t quest_number);
// v49: the run again ('I'), below with the guard's restore
static asio::awaitable<void> spire_run_again(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint32_t serial_number, uint32_t quest_number);

bool spire_base_game_handle_new_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint16_t register_number, uint32_t value);

// ---- Route B F6: the run mag request 'Z' (SpireMag.hh; contract: quests/spire/ROUTEB-F6-PROTOCOL.md)
static std::string spire_guard_filename_for(std::shared_ptr<Client> c, uint32_t serial_number);

static void spire_run_mag_handle(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint32_t serial_number,
    uint32_t quest_number, uint32_t value) {
  uint8_t sub = (value >> 16) & 0xFF;
  uint8_t k = value & 0xFF;
  uint8_t lean_bits = spire_run_mag_lean_bits(value); // F10: ASK and EXAM only
  auto* run = spire_game_run_for(l);
  auto overlay = c->has_overlay() ? c->character_file(false, true) : nullptr;

  if ((sub == SPIRE_RUN_MAG_RESULT) || (sub == SPIRE_RUN_MAG_NO_MAG) || (sub == SPIRE_RUN_MAG_LEAN)) {
    // no answer: the quest waits for none (protocol)
    auto* led = run ? spire_run_mag_ledger_for(c, run->run_id, sub == SPIRE_RUN_MAG_LEAN) : nullptr;
    if (sub == SPIRE_RUN_MAG_LEAN) {
      // F10: no quest sends a sub 5 (the lean travels in the ASK/EXAM bits); kept with the same rule
      auto stored = led ? spire_run_mag_lean_store(*led, k) : SpireRunMagLeanStore::NONE;
      c->log.info_f("Spire run mag: LEAN {} for serial {:010} in q{}: {}", k, serial_number, quest_number,
          led ? spire_run_mag_lean_store_name(stored) : "no run: not stored");
    } else if (sub == SPIRE_RUN_MAG_NO_MAG) {
      bool issued = led && (led->issued_step >= 0);
      if (issued) {
        spire_send_ticker(c, SPIRE_RUN_MAG_REMINDER_TEXT);
      }
      c->log.info_f("Spire run mag: NO_MAG at room {} for serial {:010} in q{}{}", k, serial_number, quest_number,
          issued ? "; reminder sent" : "; no mag issued in this run, no text");
    } else {
      bool created = value & 1;
      // B-REGIE-14-10: the answer issued the step already; before = the step issued before that answer
      int32_t before = led ? led->step_before_offer : -1;
      int32_t offered_step = led ? led->offered_step : -1;
      auto offered = led ? led->offered : std::nullopt;
      bool offered_shell = led && led->offered_shell; // F10: the evolve text names a Shell mag
      bool known = led && spire_run_mag_result(*led, created);
      if (!known) {
        c->log.warning_f("Spire run mag: RESULT {} for serial {:010} in q{} without an offered mag; ignored", created,
            serial_number, quest_number);
      } else if (!created) {
        // hotfix 1: one notice per client, run and given-back step (the ledger is settled above and not touched here)
        auto notice = spire_run_mag_pack_full_decide(spire_run_mag_pack_full_told, spire_run_mag_key(c), run->run_id,
            known, created, offered_step, before);
        if (notice.tell) {
          spire_send_ticker(c, notice.text);
        }
        c->log.info_f("Spire run mag: RESULT 0 (pack full) for serial {:010} in q{}; step {} given back, owed again (issued step {}){}",
            serial_number, quest_number, offered_step, led->issued_step, notice.note);
      } else {
        std::string name = std::format("Mag {:02X}", (*offered)[1]);
        try {
          auto names = c->require_server_state()->data->item_name_index_opt(Version::GC_V3);
          if (names) {
            auto it = names->all_by_primary_identifier().find(0x02000000 | (static_cast<uint32_t>((*offered)[1]) << 16));
            if (it != names->all_by_primary_identifier().end()) {
              name = it->second->name;
            }
          }
        } catch (const std::exception&) {
        }
        spire_send_ticker(c, (before < 0) ? spire_run_mag_reminder_text(offered_shell) : spire_run_mag_evolve_text(name, offered_shell, spire_equip_item_available_for(c)));
        c->log.info_f("Spire run mag: RESULT 1 for serial {:010} in q{}: step {} confirmed, {}{} {} (run {:08X})",
            serial_number, quest_number, led->issued_step, name, offered_shell ? " (Shell)" : "",
            spire_run_mag_bytes_hex(*offered), run->run_id);
      }
    }
    return;
  }

  if ((sub != SPIRE_RUN_MAG_ASK) && (sub != SPIRE_RUN_MAG_EXAM)) {
    c->log.info_f("Spire run mag: unknown sub {} ({:08X}) for serial {:010} in q{}; ignored", sub, value, serial_number,
        quest_number);
    return;
  }

  using R = SpireRunMagRefusal;
  SpireRunMagState st;
  SpireMagTable table;
  const SpireMagLine* line = nullptr;
  std::string detail;
  R refusal = R::NONE;
  try {
    st.gc_v3 = (c->version() == Version::GC_V3);
    const auto& rules = l->quest ? l->quest->meta.battle_rules : nullptr;
    st.spire_game = (c->lobby.lock() == l) && l->is_game() && (l->mode == GameMode::BATTLE) && l->quest && rules &&
        rules->replace_char && spire_guard_quest_is_guarded(c, quest_number);
    st.has_overlay = !!overlay;
    std::string filename = spire_guard_filename_for(c, serial_number);
    if (!filename.empty() && std::filesystem::is_regular_file(filename)) {
      auto snapshot = spire_guard_load(filename);
      st.restore_pending = snapshot.restore_pending;
      st.run_in_progress = snapshot.run_in_progress;
    }
    st.base_returned = spire_base_returned_has(c, l->lobby_id);
    st.run_again = spire_run_again_games.count(l->lobby_id);
    st.refreshed_by_61 = spire_run_mag_overlay_refreshed(c, l);
    st.enabled = true; // a missing or broken file is NO_TABLE
    try {
      table = spire_mag_parse(phosg::load_file(SPIRE_RUN_MAG_FILE));
      st.enabled = table.enabled;
      auto character = c->character_file(false, false);
      const char* arch = character ? spire_mag_archetype(character->disp.visual.sh.char_class) : "";
      auto it = table.classes.find(arch);
      if (it != table.classes.end()) {
        line = &it->second;
      }
      st.has_table = !!line;
    } catch (const std::exception& e) {
      detail = std::format(" ({}: {})", SPIRE_RUN_MAG_FILE, e.what());
    }
    st.overlay_mags = overlay ? spire_run_mag_inventory_mags(overlay->inventory).size() : 0;
    refusal = spire_run_mag_refusal(st);
  } catch (const std::exception& e) {
    refusal = R::MARKER;
    detail = std::format(" ({})", e.what());
  }

  SpireRunMagHandled handled; // a refusal answers result 0, old type 0xFF
  SpireRunMagLedger* led = nullptr;
  if (refusal == R::NONE) {
    if (!run) { // the kit's ASK may come before 'B': the game's run starts here as at 'B'
      run = &spire_game_run_set(l, SpireGameRun{.run_id = spire_new_run_id(), .savestate_files = {}});
    }
    led = spire_run_mag_ledger_for(c, run->run_id, true);
    // fix round (F10S critic N1): no ledger (run id 0, or no serial/character key) answers as a refusal. overlay and
    // line are non-null under NONE (has_overlay, has_table); checked here too, so the block below never meets a null
    if (!led || !overlay || !line) {
      refusal = R::NO_LEDGER;
      detail += std::format("; run {:08X}: {}", run->run_id, !led ? "no run mag ledger (run id 0 or no character key)" :
          !overlay ? "no run copy" : "no line");
    }
  }
  if (refusal == R::NONE) {
    std::set<uint8_t> server_bosses;
    auto rr_it = spire_run_rooms.find(c.get());
    if ((rr_it != spire_run_rooms.end()) && (rr_it->second.client.lock() == c) && (rr_it->second.lobby_id == l->lobby_id)) {
      server_bosses = rr_it->second.boss_rooms;
    }
    uint32_t start_bonus = 0;
    auto sl_it = spire_start_level_run.find(c.get());
    if ((sl_it != spire_start_level_run.end()) && (sl_it->second.lobby_id == l->lobby_id) &&
        (sl_it->second.quest_number == quest_number)) {
      start_bonus = sl_it->second.bonus;
    }
    auto mags = spire_run_mag_inventory_mags(overlay->inventory);
    handled = spire_run_mag_handle(*led, table, *line, sub, k, mags, server_bosses, start_bonus, lean_bits);
    if (handled.shell_fell_back) {
      c->log.warning_f("Spire run mag: lean Shell for serial {:010} in q{}, but {} has no usable ShellSteps ({}); Steps answer",
          serial_number, quest_number, SPIRE_RUN_MAG_FILE, line->shell_note);
    }
    if (handled.followed_mirror) {
      c->log.info_f("Spire run mag: ledger of run {:08X} follows the fed mag {}", run->run_id,
          spire_run_mag_bytes_hex(*led->last_mirrored));
    }
    const auto& decision = handled.decision;
    detail += std::format("; run {:08X}, start step {}, boss rooms {}, issued step {}, expected step {}, start bonus {}, {} mag(s) in the run copy, lean bits {} {}, lean {} ({})",
        run->run_id, led->start_step, led->boss_rooms.size(), led->issued_step, decision.expected_step, start_bonus,
        mags.size(), lean_bits, spire_run_mag_lean_store_name(handled.lean_store), led->lean,
        handled.shell ? "Shell column" : "Steps");
    if (decision.result) {
      for (size_t z = 0; z < 12; z++) {
        spire_send_register(c, SPIRE_RUN_MAG_ITEM_FIRST_REGISTER + z, decision.bytes[z]);
      }
      detail += std::format("; new mag {}", spire_run_mag_bytes_hex(decision.bytes));
    }
  } else if (lean_bits) {
    detail += std::format("; lean bits {} not stored (refused)", lean_bits);
  }
  uint32_t answer = handled.answer;
  spire_send_register(c, SPIRE_RUN_MAG_ANSWER_REGISTER, answer);
  spire_send_register(c, SPIRE_BRIDGE_REQUEST_REGISTER, SPIRE_BRIDGE_DONE_VALUE);
  c->log.info_f("Spire run mag: {} room {} for serial {:010} in q{}: {} ({}){}; r{}={:08X}",
      (sub == SPIRE_RUN_MAG_EXAM) ? "EXAM" : "ASK", k, serial_number, quest_number,
      (refusal != R::NONE) ? "FAIL" : handled.decision.result ? "NEW MAG" : "no change", spire_run_mag_refusal_name(refusal),
      detail, SPIRE_RUN_MAG_ANSWER_REGISTER, answer);
}

bool spire_bridge_handle_register(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint16_t register_number, uint32_t value) {
  if (!l->is_game() || !l->quest) {
    return false;
  }
  if (spire_base_game_handle_new_run(c, l, register_number, value)) { // v58: the base quest is no bridged quest
    return true;
  }
  uint32_t quest_number = l->quest->meta.quest_number;
  if (!spire_bridge_quest_is_bridged(c, quest_number)) {
    return false;
  }
  bool is_request = (register_number == SPIRE_BRIDGE_REQUEST_REGISTER);
  bool is_draw_data = (register_number == SPIRE_BRIDGE_DRAW_DATA_REGISTER);
  if (!is_request && !is_draw_data && ((register_number >= 0x100) || !spire_bridge_register_is_stored(register_number))) {
    return false;
  }
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  if (!serial_number) {
    return false;
  }

  // From here on the write belongs to the bridge: it is never forwarded, even if storing or answering fails, so a
  // co-op partner never takes over this client's meta registers.
  try {
    std::string filename = spire_bridge_filename(c, serial_number);

    if (is_draw_data) {
      auto& values = spire_draw_inputs[c.get()];
      if (values.size() < 64) { // a quest sends SPIRE_DRAW_NUM_INPUTS per request; more is a lost request
        values.emplace_back(value);
      }
      return true;
    }
    if (is_request) {
      if ((value >> 24) == SPIRE_BRIDGE_SAVESTATE_WRITE) {
        spire_savestate_write(c, l, serial_number, quest_number, value);
        return true;
      }
      if ((value >> 24) == SPIRE_BRIDGE_RESUME) {
        spire_resume_answer(c, l, serial_number, quest_number, value);
        return true;
      }
      if ((value >> 24) == SPIRE_BRIDGE_BASE_RETURN) {
        asio::co_spawn(*c->require_server_state()->io_context,
            spire_base_return(c, std::const_pointer_cast<Lobby>(l), serial_number, quest_number), asio::detached);
        return true;
      }
      if ((value >> 24) == SPIRE_BRIDGE_SHOP) { // v50
        spire_shop_handle(c, serial_number, quest_number, value);
        return true;
      }
      if ((value >> 24) == SPIRE_BRIDGE_RUN_MAG) { // route B F6
        spire_run_mag_handle(c, l, serial_number, quest_number, value);
        return true;
      }
      if ((value >> 24) == SPIRE_BRIDGE_RUN_AGAIN) { // v49
        asio::co_spawn(*c->require_server_state()->io_context,
            spire_run_again(c, std::const_pointer_cast<Lobby>(l), serial_number, quest_number), asio::detached);
        return true;
      }
      if ((value >> 24) == SPIRE_BRIDGE_DRAW_REQUEST) {
        spire_draw_answer(c, l, serial_number, quest_number, value);
        return true;
      }
      if (value == SPIRE_BRIDGE_BATTLE_END_VALUE) {
        spire_bridge_end_battle(c, l, serial_number, quest_number);
        return true;
      }
      if (value == SPIRE_BRIDGE_ROOM_CLEAR_VALUE) {
        // v16: the room clear text comes from the server (B0), not from a window_msg in the quest
        size_t sent = 0;
        for (const auto& lc : l->clients) {
          if (lc) {
            spire_send_ticker(lc, SPIRE_BRIDGE_ROOM_CLEAR_TEXT);
            sent++;
          }
        }
        c->log.info_f("Spire bridge: ROOM for serial {:010} in q{}; sent \"{}\" to {} client(s)",
            serial_number, quest_number, SPIRE_BRIDGE_ROOM_CLEAR_TEXT, sent);
        return true;
      }
      if ((value >> 24) == SPIRE_BRIDGE_MSG_LIFE) {
        spire_checkpoint_lives_note(c.get(), value & 0xFF); // falls through to the life text below
        c->log.info_f("{}", spire_lives_revive_log_line(serial_number, value));
      }
      if (spire_records_handle(c, l, serial_number, quest_number, value)) {
        return true;
      }
      // 'SPIR' starts with 'S' too; the timer code never equals it (its warning byte is below 'P' = 80 minutes)
      if (((value >> 24) == SPIRE_BRIDGE_TIMER_START) && (value != SPIRE_BRIDGE_REQUEST_VALUE)) {
        spire_run_timer_start(c, l, serial_number, quest_number, value);
        return true;
      }
      if ((value >> 24) == SPIRE_BRIDGE_MSG_RUN_OVER) {
        spire_run_over(c, l, serial_number, quest_number, value);
        return true;
      }
      if (spire_equip_prompt_handle(c, l, serial_number, quest_number, value)) {
        return true; // route B bundle 9: 'C' 13 and 'C' 14 have no text of their own
      }
      // v62 (B-REGIE-16-13): what the bundle 8 texts name: the draw table's names for 'A', 'C' 6 and 'C' 10, the
      // writer's archetype for 'C' 10 and its kept overview line for 'C' 2; bundle 9: 'C' 12 as 'C' 10
      SpireBridgeTextContext text_ctx;
      std::optional<SpireDrawTable> text_table;
      uint8_t text_kind = value >> 24;
      uint32_t text_point = (value >> 8) & 0xFF;
      if ((text_kind == SPIRE_BRIDGE_MSG_APPROACH) ||
          ((text_kind == SPIRE_BRIDGE_MSG_PROMPT) && ((text_point == 6) || (text_point == 10) || (text_point == 12)))) {
        text_table = spire_draw_table_for_text(c, quest_number);
        text_ctx.draw = text_table ? &*text_table : nullptr;
      }
      if ((text_kind == SPIRE_BRIDGE_MSG_PROMPT) && ((text_point == 10) || (text_point == 12))) {
        auto character = c->character_file(false, false);
        text_ctx.archetype = character ? spire_draw_archetype_for_class(character->disp.visual.sh.char_class) : -1;
      }
      if ((text_kind == SPIRE_BRIDGE_MSG_PROMPT) && (text_point == 2)) {
        text_ctx.overview = spire_overview_kept_line(spire_overview_kept_for(c), l->lobby_id, c->floor);
      }
      std::string msg = spire_bridge_message_text(value, text_ctx);
      if (msg.empty() && ((value >> 24) == SPIRE_BRIDGE_MSG_PROMPT)) {
        return true; // v37: a retired wait point (3-5): no text, no log line
      }
      if (!msg.empty()) {
        // v17: a short run message. Each client writes its own life, time, partner and run-over codes, so those go to
        // the writer only; the wave code comes from the leader's room thread alone (the follower mirrors the room and
        // shows no wave text), so it goes to every client in the game, like 'ROOM'
        size_t sent = 0;
        if ((value >> 24) == SPIRE_BRIDGE_MSG_WAVE) {
          for (const auto& lc : l->clients) {
            if (lc) {
              spire_send_ticker(lc, msg);
              sent++;
            }
          }
        } else {
          spire_send_ticker(c, msg);
          sent = 1;
        }
        c->log.info_f("Spire bridge: message {:08X} for serial {:010} in q{}; sent \"{}\" to {} client(s)", value,
            serial_number, quest_number, msg, sent);
        return true;
      }
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
    if (changed && filename.empty()) {
      c->log.warning_f("Spire bridge: register {} for serial {:010} in q{}: no character data; not stored",
          register_number, serial_number, quest_number);
      return true;
    }
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

// SpireGuardSnapshot: see SpireFlagBridge.hh (v39: there for the static tests of the run-start and login decisions)

// v11: one memory card holds several characters, and GC sends no character slot (only the BB login has one). So a
// snapshot belongs to a serial number plus a character: its name, class and section ID from the 61, which the battle
// overlay keeps (the restore already takes visual from the client). Two characters on one card with the same name,
// class and section ID count as one.
static bool spire_guard_same_character(const PlayerDispDataV4& a, const PlayerDispDataV4& b) {
  return !memcmp(&a.visual.name, &b.visual.name, sizeof(a.visual.name)) &&
      (a.visual.sh.char_class == b.visual.sh.char_class) && (a.visual.sh.section_id == b.visual.sh.section_id);
}

std::string spire_guard_character_id(const PlayerDispDataV4& disp) {
  // FNV-1a over the raw name bytes, class and section ID
  uint32_t hash = 0x811C9DC5;
  auto add = [&](const void* data, size_t size) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    for (size_t z = 0; z < size; z++) {
      hash = (hash ^ bytes[z]) * 0x01000193;
    }
  };
  add(&disp.visual.name, sizeof(disp.visual.name));
  add(&disp.visual.sh.char_class, 1);
  add(&disp.visual.sh.section_id, 1);
  return std::format("{:08X}", hash);
}

static std::string spire_guard_filename(uint32_t serial_number, const PlayerDispDataV4& disp) {
  return std::format("{}/{:010}.{}.snapshot.json", SPIRE_BRIDGE_DIRECTORY, serial_number, spire_guard_character_id(disp));
}

// The v6-v10 file, one per serial; only read to move it to its character's name (see spire_guard_filename_for)
static std::string spire_guard_legacy_filename(uint32_t serial_number) {
  return std::format("{}/{:010}.snapshot.json", SPIRE_BRIDGE_DIRECTORY, serial_number);
}

std::string spire_guard_hex_encode(const void* data, size_t size) {
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
void spire_guard_hex_decode(const std::string& hex, void* data, size_t size) {
  if (hex.size() != size * 2) {
    throw std::runtime_error(std::format("snapshot field has {} hex digits; expected {}", hex.size(), size * 2));
  }
  uint8_t* bytes = reinterpret_cast<uint8_t*>(data);
  for (size_t z = 0; z < size; z++) {
    bytes[z] = (spire_guard_hex_digit(hex[z * 2]) << 4) | spire_guard_hex_digit(hex[z * 2 + 1]);
  }
}

SpireGuardSnapshot spire_guard_load(const std::string& filename) {
  SpireGuardSnapshot ret;
  if (!std::filesystem::is_regular_file(filename)) {
    return ret;
  }
  auto json = phosg::JSON::parse(phosg::load_file(filename));
  ret.run_in_progress = json.get_bool("RunInProgress", false);
  ret.restore_pending = json.get_bool("RestorePending", false);
  ret.awaiting_save = json.get_bool("AwaitingSave", false);
  // F1 fix (C5): the fields below only inform the log or the re-take; a bad one reads as absent with a warning, so
  // a hand-edited file never locks the login (the markers and the character above and below stay strict)
  if (json.contains("Overlay")) {
    try {
      const auto& overlay = json.at("Overlay");
      ret.overlay_level = overlay.get_int("Level");
      ret.overlay_exp = overlay.get_int("EXP");
      ret.overlay_meseta = overlay.get_int("Meseta");
      ret.overlay_num_items = overlay.get_int("NumItems");
      ret.has_overlay = true;
    } catch (const std::exception& e) {
      ret.has_overlay = false;
      player_data_log.warning_f("Spire guard: {}: Overlay unreadable ({}); ignored", filename, e.what());
    }
  }
  try {
    ret.quest_number = json.get_int("QuestNumber", 0);
    ret.snapshot_time_usecs = json.get_int("SnapshotTime", 0);
  } catch (const std::exception& e) {
    player_data_log.warning_f("Spire guard: {}: QuestNumber or SnapshotTime unreadable ({}); read as 0", filename, e.what());
  }
  if (json.contains("RunCopyPrint")) { // F1; a bad print reads as none, and without a print no re-take follows
    try {
      const auto& print = json.at("RunCopyPrint");
      const auto& hash_text = print.get_string("Hash");
      size_t parsed = 0;
      uint64_t hash = std::stoull(hash_text, &parsed, 16);
      if ((hash_text.size() != 16) || (parsed != 16)) {
        throw std::runtime_error(std::format("Hash \"{}\" is not 16 hex digits", hash_text));
      }
      ret.run_copy_print.level = print.get_int("Level");
      ret.run_copy_print.hash = hash;
      ret.run_copy_print.exists = true;
    } catch (const std::exception& e) {
      ret.run_copy_print = SpireRunCopyPrint();
      player_data_log.warning_f("Spire guard: {}: RunCopyPrint unreadable ({}); read as no print", filename, e.what());
    }
  }
  if (json.contains("Inventory") && json.contains("Disp")) {
    spire_guard_hex_decode(json.get_string("Inventory"), &ret.inventory, sizeof(ret.inventory));
    spire_guard_hex_decode(json.get_string("Disp"), &ret.disp, sizeof(ret.disp));
    ret.has_character = true;
  }
  return ret;
}

// Writes the snapshot file atomically, the same way as spire_bridge_save. Name, Level, Class and Meseta are only there
// for people reading the file; loading uses the hex fields.
void spire_guard_save(const std::string& filename, uint32_t serial_number, const SpireGuardSnapshot& snapshot) {
  auto json = phosg::JSON::dict({
      {"SerialNumber", serial_number},
      {"RunInProgress", snapshot.run_in_progress},
      {"RestorePending", snapshot.restore_pending},
      {"AwaitingSave", snapshot.awaiting_save},
      {"QuestNumber", snapshot.quest_number},
      {"SnapshotTime", snapshot.snapshot_time_usecs},
  });
  if (snapshot.has_overlay) {
    json.emplace("Overlay", phosg::JSON::dict({
        {"Level", snapshot.overlay_level},
        {"EXP", snapshot.overlay_exp},
        {"Meseta", snapshot.overlay_meseta},
        {"NumItems", snapshot.overlay_num_items},
    }));
  }
  if (snapshot.run_copy_print.exists) { // F1; the hash as hex text, since it does not fit a signed JSON integer
    json.emplace("RunCopyPrint", phosg::JSON::dict({
        {"Level", snapshot.run_copy_print.level},
        {"Hash", std::format("{:016X}", snapshot.run_copy_print.hash)},
    }));
  }
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

  spire_bridge_write_durable(filename, data);
}

static std::string spire_guard_describe(const SpireGuardSnapshot& snapshot) {
  if (!snapshot.has_character) {
    return "no character";
  }
  return std::format("{} Lv{} meseta {}", snapshot.disp.visual.name.decode(snapshot.inventory.language),
      snapshot.disp.stats.level + 1, snapshot.disp.stats.meseta.load());
}

// The snapshot file of the character the client started its current run with (set in spire_guard_begin_run), so the 98
// and the lobby restore use the run's character even if the server's character data changed in between.
struct SpireGuardRunFile {
  std::weak_ptr<Client> client;
  std::string filename;
};
static std::unordered_map<const Client*, SpireGuardRunFile> spire_guard_run_files;

// Returns the snapshot file for this client's character: the file of its current run if there is one, else the file
// for the character in the server's character data (the 61 at login or a 98 from a non-Spire game). Moves a v6-v10
// file of the same character to the new name. Returns "" if the server has no character data for the client.
static std::string spire_guard_filename_for(std::shared_ptr<Client> c, uint32_t serial_number) {
  auto it = spire_guard_run_files.find(c.get());
  if (it != spire_guard_run_files.end()) {
    if (it->second.client.lock() == c) {
      return it->second.filename;
    }
    spire_guard_run_files.erase(it);
  }
  // allow_load=false: on GC, loading would create an empty character; allow_overlay=false: never the overlay
  auto character = c->character_file(false, false);
  if (!character) {
    return "";
  }
  std::string filename = spire_guard_filename(serial_number, character->disp);
  std::string legacy_filename = spire_guard_legacy_filename(serial_number);
  if (!std::filesystem::is_regular_file(filename) && std::filesystem::is_regular_file(legacy_filename)) {
    auto legacy = spire_guard_load(legacy_filename);
    if (legacy.has_character && spire_guard_same_character(legacy.disp, character->disp)) {
      std::filesystem::rename(legacy_filename, filename);
      c->log.info_f("Spire guard: moved the snapshot of {} for serial {:010} to {}", spire_guard_describe(legacy),
          serial_number, filename);
    }
  }
  return filename;
}

// True if any snapshot file of this serial (v6-v10 file included) has a marker set. Used only when the server has no
// character data for the client, so it cannot tell which character is on the card.
static bool spire_guard_serial_has_open_marker(std::shared_ptr<const Client> c, uint32_t serial_number) {
  if (!std::filesystem::is_directory(SPIRE_BRIDGE_DIRECTORY)) {
    return false;
  }
  std::string prefix = std::format("{:010}.", serial_number);
  for (const auto& entry : std::filesystem::directory_iterator(SPIRE_BRIDGE_DIRECTORY)) {
    std::string name = entry.path().filename().string();
    if (!name.starts_with(prefix) || !name.ends_with(".snapshot.json")) {
      continue;
    }
    auto snapshot = spire_guard_load(entry.path().string());
    // Regie pso-regie-9: a protected character's file never holds back another character of the same console
    bool protected_character = snapshot.has_character &&
        c->require_server_state()->data->is_spire_protected_character(serial_number, snapshot.disp.visual.name.decode(c->language()));
    if (spire_guard_marker_open(protected_character, snapshot.run_in_progress, snapshot.restore_pending, snapshot.awaiting_save)) {
      return true;
    }
  }
  return false;
}

static bool spire_guard_take_snapshot(std::shared_ptr<Client> c, uint32_t quest_number);

// v31: the same items (data1 and data2; the item ID differs per login) in any order, and the same meseta. Lab
// 2026-10-24 18:11: BidX logged in with the snapshot's level and EXP but the run's 7 items instead of its 20 (Dolphin
// stopped in the lobby before a save); level and EXP alone confirmed the lobby restore and the 20 items were lost.
bool spire_guard_same_inventory(const PlayerInventory& a, uint32_t a_meseta, const PlayerInventory& b, uint32_t b_meseta) {
  if ((a.num_items != b.num_items) || (a_meseta != b_meseta) || (a.num_items > a.items.size())) {
    return false;
  }
  auto key = [](const ItemData& d) {
    std::array<uint8_t, 16> ret;
    memcpy(ret.data(), d.data1.data(), 12);
    memcpy(ret.data() + 12, d.data2.data(), 4);
    return ret;
  };
  std::vector<std::array<uint8_t, 16>> ka, kb;
  for (size_t z = 0; z < a.num_items; z++) {
    ka.emplace_back(key(a.items[z].data));
    kb.emplace_back(key(b.items[z].data));
  }
  std::sort(ka.begin(), ka.end());
  std::sort(kb.begin(), kb.end());
  return ka == kb;
}

// v38 (regie, output/materials-diagnose-2026-10-25.md): the material usage counters. Power, Mind, Evade, Def and Luck
// sit in items[8..12].extension_data2 (by position, not with an item), HP and TP in hp_from_materials and
// tp_from_materials (see PlayerInventory.hh and PSOBBCharacterFile::get_material_usage). The run overlay keeps the five
// stat counters, so a restore that left them wrong would pass the item check unnoticed.
bool spire_guard_same_materials(const PlayerInventory& a, const PlayerInventory& b) {
  if ((a.hp_from_materials != b.hp_from_materials) || (a.tp_from_materials != b.tp_from_materials)) {
    return false;
  }
  for (size_t z = 8; z <= 12; z++) {
    if (a.items[z].extension_data2 != b.items[z].extension_data2) {
      return false;
    }
  }
  return true;
}

std::string spire_guard_materials_str(const PlayerInventory& inv) {
  return std::format("pow {} mind {} evade {} def {} luck {} hp {} tp {}", inv.items[8].extension_data2,
      inv.items[9].extension_data2, inv.items[10].extension_data2, inv.items[11].extension_data2,
      inv.items[12].extension_data2, inv.hp_from_materials >> 1, inv.tp_from_materials >> 1);
}

// v56 (live 2026-10-26 20:47:39, 'I' took the run copy M4ia Lv17 3 items for the real Lv162 27 items): see
// SpireFlagBridge.hh
bool spire_guard_snapshot_not_worse(
    const PlayerInventory& inventory, const PlayerDispDataV4& disp, const SpireGuardSnapshot& snapshot) {
  return spire_guard_same_character(disp, snapshot.disp) && (disp.stats.level >= snapshot.disp.stats.level) &&
      (disp.stats.exp >= snapshot.disp.stats.exp) && (inventory.num_items >= snapshot.inventory.num_items);
}

// F1 (route B): see SpireFlagBridge.hh
SpireRunCopyPrint spire_guard_run_copy_print(const PlayerInventory& inventory, const PlayerDispDataV4& disp) {
  SpireRunCopyPrint ret;
  ret.exists = true;
  ret.level = disp.stats.level;
  std::vector<std::array<uint8_t, 16>> keys;
  for (size_t z = 0; (z < inventory.num_items) && (z < inventory.items.size()); z++) {
    std::array<uint8_t, 16> key;
    memcpy(key.data(), inventory.items[z].data.data1.data(), 12);
    memcpy(key.data() + 12, inventory.items[z].data.data2.data(), 4);
    keys.emplace_back(key);
  }
  std::sort(keys.begin(), keys.end()); // the item order and IDs differ between the mirror and a 61
  uint64_t hash = phosg::FNV1A64_START;
  for (const auto& key : keys) {
    hash = phosg::fnv1a64(key.data(), key.size(), hash);
  }
  uint32_t meseta = disp.stats.meseta;
  ret.hash = phosg::fnv1a64(&meseta, sizeof(meseta), hash);
  return ret;
}

bool spire_guard_matches_print(
    const PlayerInventory& inventory, const PlayerDispDataV4& disp, const SpireRunCopyPrint& print) {
  if (!print.exists) {
    return false;
  }
  return spire_guard_run_copy_print(inventory, disp).hash == print.hash;
}

bool spire_guard_store_print(const std::string& filename, uint32_t serial_number, SpireGuardSnapshot& snapshot,
    const PlayerInventory* inventory, const PlayerDispDataV4* disp) {
  try {
    // First the file without the earlier print, so a failure below never leaves a print of another run copy
    snapshot.run_copy_print = SpireRunCopyPrint();
    spire_guard_save(filename, serial_number, snapshot);
    if (inventory && disp) {
      snapshot.run_copy_print = spire_guard_run_copy_print(*inventory, *disp);
      spire_guard_save(filename, serial_number, snapshot);
    }
    return true;
  } catch (const std::exception& e) {
    player_data_log.warning_f("Spire guard: run copy print for serial {:010} not stored in {}: {}", serial_number, filename, e.what());
    return false;
  }
}

bool spire_guard_retake_ok(
    const PlayerInventory& inventory, const PlayerDispDataV4& disp, const SpireGuardSnapshot& snapshot) {
  // F1 fix (B-REGIE-14-9): the snapshot's exact level and EXP, since the base gives neither. No run mag term
  // (routeb-f6-critic C1): the bonus-0 step-0 run mag is byte for byte Sega's unfed Mag LV5, which real characters hold.
  return spire_guard_snapshot_not_worse(inventory, disp, snapshot) && (disp.stats.level == snapshot.disp.stats.level) &&
      (disp.stats.exp == snapshot.disp.stats.exp) && snapshot.run_copy_print.exists &&
      !spire_guard_matches_print(inventory, disp, snapshot.run_copy_print);
}

// v32: level, EXP, meseta and items equal (spire_guard_same_inventory); v38: and the material counters
bool spire_guard_same_state(const PlayerInventory& inventory, const PlayerDispDataV4& disp,
    const PlayerInventory& snapshot_inventory, const PlayerDispDataV4& snapshot_disp) {
  return (disp.stats.level == snapshot_disp.stats.level) && (disp.stats.exp == snapshot_disp.stats.exp) &&
      spire_guard_same_inventory(inventory, disp.stats.meseta, snapshot_inventory, snapshot_disp.stats.meseta) &&
      spire_guard_same_materials(inventory, snapshot_inventory);
}

// v32: at a run start under AwaitingSave the stored snapshot is kept when the character data differs from it in items,
// meseta, level, EXP or (v38) material counters; a fresh snapshot replaces it only when the data equals it (then the
// fresh one is the same snapshot). A snapshot under an open marker is never overwritten with other data (lab
// 2026-10-24 18:14: a fresh snapshot with the run's 7 items replaced the one with the real 20). v39: the v10 "lower
// level" term is gone; a higher level is kept the same way (a run copy can be stronger than the real character).
bool spire_guard_run_start_keeps_snapshot(bool awaiting_save, bool same_state) {
  return awaiting_save && !same_state;
}

// v39: see SpireFlagBridge.hh; the criterion for a run copy is the open marker, never the level
SpireGuardRunStart spire_guard_run_start_apply(SpireGuardSnapshot& snapshot, const PlayerInventory& inventory,
    const PlayerDispDataV4& disp, uint32_t quest_number, uint64_t now_usecs) {
  if (snapshot.run_in_progress || snapshot.restore_pending) {
    return SpireGuardRunStart::KEPT_OPEN_RUN;
  }
  bool awaiting_save = snapshot.awaiting_save;
  snapshot.awaiting_save = false;
  snapshot.run_in_progress = true;
  snapshot.quest_number = quest_number;
  snapshot.run_copy_print = SpireRunCopyPrint(); // F1: a print belongs to one run; this run's 'O' stores its own
  if (awaiting_save && snapshot.has_character &&
      spire_guard_run_start_keeps_snapshot(true, spire_guard_same_state(inventory, disp, snapshot.inventory, snapshot.disp))) {
    return SpireGuardRunStart::KEPT_UNCONFIRMED;
  }
  snapshot.snapshot_time_usecs = now_usecs;
  snapshot.has_character = true;
  snapshot.inventory = inventory;
  snapshot.disp = disp;
  return SpireGuardRunStart::FRESH;
}

bool spire_guard_awaiting_save_login_apply(
    SpireGuardSnapshot& snapshot, const PlayerInventory& inventory, const PlayerDispDataV4& disp) {
  snapshot.awaiting_save = false;
  if (spire_guard_same_state(inventory, disp, snapshot.inventory, snapshot.disp)) {
    snapshot.has_overlay = false;
    return true;
  }
  snapshot.restore_pending = true;
  return false;
}

bool spire_guard_98_follows(bool awaiting_save, bool normal_or_solo, bool base_game) {
  return awaiting_save && normal_or_solo && !base_game;
}

bool spire_guard_follow_real_progress(
    SpireGuardSnapshot& snapshot, const PlayerInventory& inventory, const PlayerDispDataV4& disp) {
  if (!snapshot.awaiting_save || snapshot.run_in_progress || snapshot.restore_pending || !snapshot.has_character) {
    return false;
  }
  // v56: a real character never loses level or EXP, so data below the snapshot's is a run copy; the snapshot stays
  // (items may go: outside a run the player can sell or hand them over)
  if ((disp.stats.level < snapshot.disp.stats.level) || (disp.stats.exp < snapshot.disp.stats.exp)) {
    return false;
  }
  snapshot.inventory = inventory;
  snapshot.disp = disp;
  // v40: the client writes its card when it leaves the game, so the card holds the real character from now on
  snapshot.awaiting_save = false;
  snapshot.has_overlay = false;
  return true;
}

// v49: the snapshot step of the run again ('I', SpireFlagBridge.hh)
bool spire_guard_run_again_retake(SpireGuardSnapshot& snapshot, bool base_returned, const PlayerInventory& inventory,
    const PlayerDispDataV4& disp, uint32_t quest_number, uint64_t now_usecs) {
  if (!base_returned || !snapshot.has_character || !snapshot.run_in_progress || snapshot.restore_pending ||
      snapshot.awaiting_save || !spire_guard_retake_ok(inventory, disp, snapshot)) {
    return false;
  }
  snapshot.inventory = inventory;
  snapshot.disp = disp;
  snapshot.quest_number = quest_number;
  snapshot.snapshot_time_usecs = now_usecs;
  return true;
}

// v51: the 98 after a verified return to base ('O', SpireFlagBridge.hh)
bool spire_guard_base_exit_follow(
    SpireGuardSnapshot& snapshot, bool base_returned, const PlayerInventory& inventory, const PlayerDispDataV4& disp) {
  if (!base_returned || !snapshot.has_character || snapshot.run_in_progress || !snapshot.restore_pending ||
      snapshot.awaiting_save || !spire_guard_retake_ok(inventory, disp, snapshot)) {
    return false;
  }
  snapshot.inventory = inventory;
  snapshot.disp = disp;
  return true;
}

// v32: the 61 after a push verifies the restore only if it shows the snapshot's level and EXP and its items and
// meseta; a 61 with the same level and EXP but other items leaves the marker.
bool spire_guard_push_verified(bool same_stats, bool same_items) {
  return same_stats && same_items;
}
static asio::awaitable<void> spire_guard_watch_no_quest(std::weak_ptr<Client> weak_c, std::weak_ptr<Lobby> weak_l);

SpireGuardStart spire_guard_start_action(
    bool battle_game, bool has_quest, bool replace_char, bool guarded_quest, bool protected_character) {
  if (!battle_game || !has_quest || !replace_char || !guarded_quest) {
    return SpireGuardStart::NONE;
  }
  return protected_character ? SpireGuardStart::REFUSE : SpireGuardStart::MARKER;
}

bool spire_guard_marker_open(bool protected_character, bool run_in_progress, bool restore_pending, bool awaiting_save) {
  return !protected_character && (run_in_progress || restore_pending || awaiting_save);
}

bool spire_guard_begin_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  try {
    bool battle_game = l->is_game() && (l->mode == GameMode::BATTLE);
    const auto& rules = l->quest ? l->quest->meta.battle_rules : nullptr;
    bool replace_char = rules && rules->replace_char;
    uint32_t quest_number = l->quest ? l->quest->meta.quest_number : 0;
    bool guarded = battle_game && replace_char && spire_guard_quest_is_guarded(c, quest_number);
    if (battle_game && replace_char && !guarded) {
      // v12 (critic K5): a replace_char quest outside the list runs without the guard; say so
      c->log.warning_f("Spire guard: q{} is a replace_char Battle quest but not in SpireGuardQuests (or SpireBridgeQuests if that key is missing); no guard for this run",
          quest_number);
    }
    switch (spire_guard_start_action(battle_game, !!l->quest, replace_char, guarded, c->is_spire_protected())) {
      case SpireGuardStart::NONE:
        return true;
      case SpireGuardStart::REFUSE:
        c->log.warning_f("Spire guard: blocked character in q{}; no marker, no snapshot, no overlay", quest_number);
        return false;
      case SpireGuardStart::MARKER:
        break;
    }
    if (!spire_guard_take_snapshot(c, quest_number)) {
      return false;
    }
    // This game's inventory backup is now covered by the guard: $restore leaves it to the guard (InventoryBackup.hh)
    inventory_backup_note_guarded(c);
    return true;
  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to start run: {}; no overlay", e.what());
    return false;
  }
}


// v52: the owned start_level bonus of a client in a guarded Spire Battle quest with replace_char (0 otherwise, for a
// protected character, or on any error). Never throws.
static uint32_t spire_start_level_bonus_for_client(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  try {
    const auto& rules = l->quest ? l->quest->meta.battle_rules : nullptr;
    if (!l->is_game() || (l->mode != GameMode::BATTLE) || !rules || !rules->replace_char ||
        !spire_guard_quest_is_guarded(c, l->quest->meta.quest_number) || c->is_spire_protected() ||
        (c->version() != Version::GC_V3) || !c->login || !c->login->gc_license) {
      return 0;
    }
    auto character = c->character_file(true, false);
    std::string filename = spire_meta_filename_for(c->login->gc_license->serial_number,
        character ? &character->disp : nullptr);
    if (filename.empty()) {
      return 0;
    }
    auto meta = spire_meta_load_file(filename);
    std::unique_ptr<SpireShopCatalogue> cat;
    try {
      cat = std::make_unique<SpireShopCatalogue>(spire_shop_parse(phosg::load_file(SPIRE_SHOP_CATALOGUE_FILE)));
    } catch (const std::exception& e) {
      c->log.warning_f("Spire start level: no shop catalogue ({}); start_level capped at {}", e.what(),
          SPIRE_START_LEVEL_FALLBACK_MAX);
    }
    return spire_start_level_bonus(cat.get(), meta.upgrades);
  } catch (const std::exception& e) {
    c->log.warning_f("Spire start level: upgrades unreadable: {}; no start level bonus", e.what());
    return 0;
  }
}

std::shared_ptr<const std::string> spire_start_level_quest_bin(std::shared_ptr<Client> c,
    std::shared_ptr<const Lobby> l, std::shared_ptr<const std::string> bin_contents) {
  spire_start_level_sent.erase(c.get());
  uint32_t bonus = spire_start_level_bonus_for_client(c, l);
  if (!bonus || !bin_contents) {
    return bin_contents;
  }
  uint32_t quest_number = l->quest->meta.quest_number;
  try {
    uint32_t base = l->quest->meta.battle_rules->char_level;
    uint32_t level = spire_start_level_char_level(base, bonus);
    std::string bin = prs_decompress(*bin_contents);
    size_t count = spire_start_level_patch_bin(bin, base, level);
    if (count != 1) {
      c->log.warning_f("Spire start level: q{} has {} rule blocks with level {} in its .bin (needs exactly 1); quest and overlay keep Lv{}",
          quest_number, count, base + 1, base + 1);
      return bin_contents;
    }
    auto ret = std::make_shared<const std::string>(prs_compress(bin));
    spire_start_level_sent[c.get()] = SpireStartLevelSent{l->lobby_id, quest_number, level - base};
    c->log.info_f("Spire start level: q{} sent with the rule block's level Lv{} -> Lv{} (start_level +{})",
        quest_number, base + 1, level + 1, level - base);
    return ret;
  } catch (const std::exception& e) {
    c->log.warning_f("Spire start level: q{} .bin not changed: {}; quest and overlay keep the quest's level",
        quest_number, e.what());
    return bin_contents;
  }
}

// v53: the server-side heat steps (SpireFlagBridge.hh spire_heat_quest_bin, SpireMeta.hh SpireHeatPatch)
struct SpireHeatLeader {
  uint32_t heat = 0;
  std::string detail;
};

// The game leader's heat as its 'M' START computes it (spire_shop_handle): H_auto(class, P) from its meta.json upgrades
// and the shop catalogue, start_level counted as the run copy this quest load gives it (spire_start_level_bonus_for_client),
// plus the chosen heat (H_keuze), which the server does not know yet (0). Throws on a file error.
static SpireHeatLeader spire_heat_for_leader(std::shared_ptr<const Lobby> l) {
  SpireHeatLeader ret;
  auto leader = (l->leader_id < l->clients.size()) ? l->clients[l->leader_id] : nullptr;
  if (!leader || (leader->version() != Version::GC_V3) || !leader->login || !leader->login->gc_license) {
    ret.detail = "no GC leader";
    return ret;
  }
  auto character = leader->character_file(true, false);
  std::string filename = spire_meta_filename_for(leader->login->gc_license->serial_number,
      character ? &character->disp : nullptr);
  if (!character || filename.empty()) {
    ret.detail = "leader has no character data";
    return ret;
  }
  auto meta = spire_meta_load_file(filename);
  auto cat = spire_shop_parse(phosg::load_file(SPIRE_SHOP_CATALOGUE_FILE));
  auto upgrades = meta.upgrades;
  uint32_t run_bonus = spire_start_level_bonus_for_client(leader, l);
  if (run_bonus < spire_shop_level(upgrades, 0)) {
    upgrades[SPIRE_SHOP_ITEM_NAMES[0]] = run_bonus;
  }
  uint8_t char_class = character->disp.visual.sh.char_class;
  uint32_t strength = spire_shop_strength(cat, upgrades);
  uint8_t rung = l->quest ? spire_meta_ladder_quest_rung(l->quest->meta.quest_number) : 0; // v54: the rung's table
  ret.heat = spire_shop_heat(cat, char_class, strength, rung);
  ret.detail = std::format("leader {:010} {}, P {}, rung {}, H_auto {}, H_keuze 0", leader->login->gc_license->serial_number,
      name_for_char_class(char_class), strength, rung, ret.heat);
  return ret;
}

// The patched (compressed) .bin per quest .bin and step: every client of a game gets the same bytes, compressed once.
// The weak_ptr keeps a reloaded quest index from matching a freed .bin at the same address.
struct SpireHeatCacheEntry {
  std::weak_ptr<const std::string> source;
  std::shared_ptr<const std::string> patched;
  uint32_t step = 0;
};
static std::map<std::pair<const std::string*, uint32_t>, SpireHeatCacheEntry> spire_heat_bin_cache;

std::shared_ptr<const std::string> spire_heat_quest_bin(std::shared_ptr<Client> c,
    std::shared_ptr<const Lobby> l, std::shared_ptr<const std::string> bin_contents) {
  spire_heat_sent.erase(c.get()); // v55: set again below only for a .bin whose rows are of a known step
  if (!bin_contents || !l->is_game() || !l->quest) {
    return bin_contents;
  }
  uint32_t quest_number = l->quest->meta.quest_number;
  std::string filename = std::format("{}/q{}.json", SPIRE_HEAT_PATCH_DIR, quest_number);
  try {
    if (!std::filesystem::is_regular_file(filename) || !spire_guard_quest_is_guarded(c, quest_number)) {
      return bin_contents;
    }
    if (c->version() != Version::GC_V3) {
      c->log.warning_f("Spire heat: q{} not patched for a {} client; it plays the H 0 rows", quest_number,
          phosg::name_for_enum(c->version()));
      return bin_contents;
    }
    auto leader = spire_heat_for_leader(l);
    if (leader.heat == 0) {
      c->log.info_f("Spire heat: q{} sent at H 0 ({}), the quest's own rows", quest_number, leader.detail);
      spire_heat_sent[c.get()] = SpireHeatSent{l->lobby_id, quest_number, 0};
      return bin_contents;
    }
    auto key = std::make_pair(bin_contents.get(), leader.heat);
    auto it = spire_heat_bin_cache.find(key);
    if ((it != spire_heat_bin_cache.end()) && (it->second.source.lock() == bin_contents)) {
      c->log.info_f("Spire heat: q{} sent at H {} ({}), the same .bin as the game's other clients", quest_number,
          it->second.step, leader.detail);
      spire_heat_sent[c.get()] = SpireHeatSent{l->lobby_id, quest_number, it->second.step};
      return it->second.patched;
    }
    auto patch = spire_heat_patch_parse(phosg::load_file(filename));
    if (patch.quest_number != quest_number) {
      throw std::runtime_error(std::format("{} is the heat patch of q{}", filename, patch.quest_number));
    }
    std::string bin = prs_decompress(*bin_contents);
    uint32_t step = spire_heat_patch_bin(bin, patch, leader.heat);
    auto ret = std::make_shared<const std::string>(prs_compress(bin));
    std::erase_if(spire_heat_bin_cache, [](const auto& e) { return e.second.source.expired(); });
    spire_heat_bin_cache[key] = SpireHeatCacheEntry{bin_contents, ret, step};
    c->log.info_f("Spire heat: q{} sent at H {} ({}{}), {} field(s) from {}", quest_number, step, leader.detail,
        (step != leader.heat) ? std::format(", capped at HeatMax {}", patch.heat_max) : "", patch.fields.size(),
        filename);
    spire_heat_sent[c.get()] = SpireHeatSent{l->lobby_id, quest_number, step};
    return ret;
  } catch (const std::exception& e) {
    c->log.warning_f("Spire heat: q{} sent unpatched, at its H 0 rows: {}", quest_number, e.what());
    return bin_contents;
  }
}

// v49: the overlay creation of on_quest_loaded, shared with the run again ('I'). v52: with the start_level bonus
bool spire_create_battle_overlay(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, bool run_again) {
  if (is_v4(c->version())) {
    c->change_bank(c->bb_character_index);
  }
  // Spire guard (v6): store the real character before the overlay replaces it (see SpireFlagBridge.hh)
  if (!spire_guard_begin_run(c, l)) {
    spire_start_level_sent.erase(c.get());
    return false; // v12: no snapshot stored, so no overlay that the client could save over the real character
  }
  // v52: at the quest load the level its .bin carried (the client applies that one); at the run again the owned level
  // now (the push carries the overlay to the client)
  uint32_t bonus = 0;
  if (run_again) {
    bonus = spire_start_level_bonus_for_client(c, l);
  } else {
    auto it = spire_start_level_sent.find(c.get());
    if ((it != spire_start_level_sent.end()) && l->quest && (it->second.lobby_id == l->lobby_id) &&
        (it->second.quest_number == l->quest->meta.quest_number)) {
      bonus = it->second.bonus;
    }
    spire_start_level_sent.erase(c.get());
  }
  auto rules = l->quest->meta.battle_rules;
  if (bonus && rules) {
    auto raised = std::make_shared<BattleRules>(*rules);
    raised->char_level = spire_start_level_char_level(rules->char_level, bonus);
    c->log.info_f("Spire start level: overlay Lv{} -> Lv{} (start_level +{}{})", rules->char_level + 1,
        raised->char_level + 1, raised->char_level - rules->char_level, run_again ? ", run again" : "");
    rules = raised;
  }
  c->create_battle_overlay(rules, c->require_server_state()->data->level_table(c->version()));
  c->log.info_f("Created battle overlay");
  if (l->quest) {
    spire_start_level_run[c.get()] = SpireStartLevelSent{
        l->lobby_id, l->quest->meta.quest_number, rules ? (rules->char_level - l->quest->meta.battle_rules->char_level) : 0};
  }
  return true;
}

// v21, narrowed (regie pso-regie-9): the join of a Battle game sets no marker; the run's marker comes from
// spire_guard_begin_run when a guarded quest loads, before its overlay. The join only starts the no-quest watcher.
void spire_guard_battle_join(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  if (!l || !l->is_game() || (l->mode != GameMode::BATTLE) || l->quest) {
    return;
  }
  try {
    if (spire_bridge_serial_for_client(c)) {
      auto ml = std::const_pointer_cast<Lobby>(l);
      asio::co_spawn(*c->require_server_state()->io_context, spire_guard_watch_no_quest(c, ml), asio::detached);
    }
  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to watch the Battle game without a quest: {}", e.what());
  }
}

SpireNoQuestAction spire_guard_no_quest_action(
    bool client_in_game, bool battle_game, bool has_quest, bool quest_selection_in_progress, bool can_run_functions) {
  if (!client_in_game || !battle_game || has_quest) {
    return SpireNoQuestAction::NONE;
  }
  if (quest_selection_in_progress) {
    return SpireNoQuestAction::WAIT;
  }
  return can_run_functions ? SpireNoQuestAction::EXIT : SpireNoQuestAction::TELL;
}

static asio::awaitable<void> spire_guard_watch_no_quest(std::weak_ptr<Client> weak_c, std::weak_ptr<Lobby> weak_l) {
  co_await async_sleep(std::chrono::seconds(SPIRE_GUARD_NO_QUEST_SECONDS));
  for (;;) {
    auto c = weak_c.lock();
    auto l = weak_l.lock();
    if (!c || !l) {
      co_return;
    }
    auto action = spire_guard_no_quest_action(
        c->lobby.lock() == l, l->is_game() && (l->mode == GameMode::BATTLE), !!l->quest,
        l->check_flag(Lobby::Flag::QUEST_SELECTION_IN_PROGRESS),
        c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL) && c->check_flag(Client::Flag::SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE));
    if (action == SpireNoQuestAction::NONE) {
      co_return;
    }
    if (action == SpireNoQuestAction::WAIT) {
      co_await async_sleep(std::chrono::seconds(SPIRE_GUARD_NO_QUEST_RECHECK_SECONDS));
      continue;
    }
    c->log.warning_f("Spire guard: Battle game without a quest for {} s; {}", SPIRE_GUARD_NO_QUEST_SECONDS,
        (action == SpireNoQuestAction::EXIT) ? "sending the client back to the lobby" : "client cannot run functions, told to leave");
    send_text_message(c, "$C6Spire: no quest was\nloaded. Back to the\nlobby.");
    if (action == SpireNoQuestAction::EXIT) {
      try {
        auto s = c->require_server_state();
        auto fn = s->data->client_functions->get("ExitAnywhere", c->specific_version);
        co_await prepare_client_for_patches(c);
        co_await send_function_call(c, fn);
      } catch (const std::exception& e) {
        // The guard stays: the join snapshot is a pending restore as soon as the client leaves the game
        c->log.warning_f("Spire guard: failed to send the client back from the Battle game: {}", e.what());
      }
    }
    co_return;
  }
}

static bool spire_guard_take_snapshot(std::shared_ptr<Client> c, uint32_t quest_number) {
  try {
    uint32_t serial_number = spire_bridge_serial_for_client(c);
    if (!serial_number) {
      return true;
    }

    // allow_load=false: on GC, loading would create an empty character; allow_overlay=false: never the overlay
    auto character = c->character_file(false, false);
    if (!character) {
      c->log.warning_f("Spire guard: no character data for serial {:010} (q{}); no snapshot, no overlay", serial_number, quest_number);
      return false;
    }
    spire_guard_run_files.erase(c.get());
    std::string filename = spire_guard_filename_for(c, serial_number);
    spire_guard_run_files[c.get()] = SpireGuardRunFile{c, filename};
    auto snapshot = spire_guard_load(filename);
    // v39: whether the character data may be a run copy follows from the snapshot's markers, never from its level
    // (see SpireFlagBridge.hh); spire_guard_run_start_apply is the whole decision
    bool was_awaiting_save = snapshot.awaiting_save;
    std::string stored = spire_guard_describe(snapshot);
    auto action = spire_guard_run_start_apply(
        snapshot, character->inventory, character->disp, quest_number, phosg::now());
    switch (action) {
      case SpireGuardRunStart::KEPT_OPEN_RUN:
        c->log.info_f("Spire guard: {} for serial {:010} (q{}); kept snapshot {} from q{}",
            snapshot.restore_pending ? "restore pending" : "run already in progress", serial_number, quest_number,
            stored, snapshot.quest_number);
        return true;
      case SpireGuardRunStart::KEPT_UNCONFIRMED:
        // v10/v32: the lobby restore was verified, but no login has confirmed that the card holds the real character;
        // character data unequal to the snapshot may be a run copy (also a stronger one), so the snapshot stays
        spire_guard_save(filename, serial_number, snapshot);
        c->log.warning_f("Spire guard: run started for serial {:010} in q{} before a login confirmed the lobby restore; character data Lv{} ({} items meseta {}) differs from the snapshot ({} items meseta {}); marker set, kept snapshot {}",
            serial_number, quest_number, character->disp.stats.level + 1, character->inventory.num_items,
            character->disp.stats.meseta.load(), snapshot.inventory.num_items, snapshot.disp.stats.meseta.load(),
            stored);
        return true;
      case SpireGuardRunStart::FRESH:
        spire_guard_save(filename, serial_number, snapshot);
        if (was_awaiting_save) {
          c->log.info_f("Spire guard: run started for serial {:010} in q{} before a login confirmed the lobby restore; character data equals the snapshot, took a fresh snapshot (was {})",
              serial_number, quest_number, stored);
        }
        c->log.info_f("Spire guard: run started for serial {:010} in q{}; marker set, snapshot {}",
            serial_number, quest_number, spire_guard_describe(snapshot));
        break;
    }

  } catch (const std::exception& e) {
    // v12 (critic K2): fail closed; a run without a stored snapshot must not get the overlay
    c->log.warning_f("Spire guard: failed to start run: {}; no overlay", e.what());
    return false;
  }
  return true;
}

// v39: clients whose 98 from a Normal or Solo game under AwaitingSave is still to be taken into the snapshot. Keyed by
// the Client's address and never dereferenced; spire_guard_after_98 removes the entry in the same 98 handler.
static std::unordered_set<const Client*> spire_guard_follow_pending;
// v61: the clients that left the base game under AwaitingSave (spire_guard_end_run); the next 84 sends them a 97
static std::unordered_set<const Client*> spire_guard_base_game_left;

// v51: clients whose 98 leaves a Spire game after a verified 'O' (spire_guard_base_exit_follow). Keyed by the Client's
// address and never dereferenced; spire_guard_after_98 removes the entry in the same 98 handler.
static std::unordered_set<const Client*> spire_guard_base_exit_pending;

void spire_guard_end_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  spire_guard_follow_pending.erase(c.get());
  spire_guard_base_exit_pending.erase(c.get());
  spire_start_level_sent.erase(c.get()); // v52
  spire_start_level_run.erase(c.get());
  spire_heat_sent.erase(c.get()); // v55
  // v51: an 'O' OK belongs to the game the client leaves now; it never carries over to the next one
  bool base_returned = l && spire_base_returned_has(c, l->lobby_id);
  spire_base_returned.erase(c.get());
  try {
    // v8: a 98 is never proof that the card holds the real character. Measured on the Wii: a normal end of a Spire run
    // (end, warp, Quit, "Wrote to Memory Card A") also saves the overlay character. So a 98 with RunInProgress set,
    // from whichever game, turns the run into a pending restore; only a verified restore at the next login closes it.
    uint32_t serial_number = spire_bridge_serial_for_client(c);
    if (!serial_number || c->is_spire_protected()) {
      return; // regie pso-regie-9: the guard never touches a protected character's file
    }
    uint32_t quest_number = (l && l->is_game() && l->quest) ? l->quest->meta.quest_number : 0;

    std::string filename = spire_guard_filename_for(c, serial_number);
    if (filename.empty()) {
      return;
    }
    auto snapshot = spire_guard_load(filename);
    if (snapshot.restore_pending) {
      c->log.info_f("Spire guard: 98 in q{} for serial {:010} leaves the pending restore of {} open",
          quest_number, serial_number, spire_guard_describe(snapshot));
      return;
    }
    if (!snapshot.run_in_progress) {
      // v39: under AwaitingSave alone the client has had the verified real character in memory since the lobby
      // restore; the 98 of a Normal or Solo game is its real progress, which the snapshot follows (spire_guard_after_98)
      bool base_game = l && l->is_game() && l->quest &&
          (l->quest->meta.quest_number == c->require_server_state()->data->spire_base_quest_number);
      if (spire_guard_98_follows(snapshot.awaiting_save,
              l && l->is_game() && ((l->mode == GameMode::NORMAL) || (l->mode == GameMode::SOLO)), base_game)) {
        spire_guard_follow_pending.emplace(c.get());
      } else if (snapshot.awaiting_save && base_game) {
        spire_guard_base_game_left.emplace(c.get());
        c->log.info_f("Spire guard: 98 of the base game (q{}) for serial {:010}; AwaitingSave stays open until a login 61 equals the snapshot {} (v59)",
            quest_number, serial_number, spire_guard_describe(snapshot));
      }
      return;
    }
    snapshot.run_in_progress = false;
    snapshot.restore_pending = true;
    spire_guard_save(filename, serial_number, snapshot);
    c->log.info_f("Spire guard: run ended for serial {:010} (98 in q{}); restore pending for the next login, snapshot {} kept",
        serial_number, quest_number, spire_guard_describe(snapshot));
    if (base_returned) {
      spire_guard_base_exit_pending.emplace(c.get());
    }

  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to end run: {}", e.what());
  }
}

void spire_guard_after_98(std::shared_ptr<Client> c) {
  if (spire_guard_base_exit_pending.erase(c.get())) {
    // v51: the game exit after a verified 'O'; the 98 is the real character with what the base changed
    try {
      uint32_t serial_number = spire_bridge_serial_for_client(c);
      auto character = c->character_file(false, false);
      if (!serial_number || !character || c->is_spire_protected()) {
        return;
      }
      std::string filename = spire_guard_filename_for(c, serial_number);
      if (filename.empty()) {
        return;
      }
      auto snapshot = spire_guard_load(filename);
      SpireGuardSnapshot before = snapshot;
      if (!spire_guard_base_exit_follow(snapshot, true, character->inventory, character->disp)) {
        c->log.warning_f("Spire guard: 98 after a verified return to base for serial {:010} does not match the snapshot's character, level or EXP, or is the run copy (F1: print {}); the run start's snapshot {} stays",
            serial_number, !before.run_copy_print.exists ? "none stored"
                : spire_guard_matches_print(character->inventory, character->disp, before.run_copy_print) ? "matches" : "differs",
            spire_guard_describe(before));
        return;
      }
      spire_guard_save(filename, serial_number, snapshot);
      bool same = spire_guard_same_state(snapshot.inventory, snapshot.disp, before.inventory, before.disp);
      c->log.info_f("Spire guard: 98 after a verified return to base for serial {:010}: snapshot follows the real character{} ({} items, meseta {}); RestorePending",
          serial_number, same ? ", equal to the one before" : std::format(", changed in the base (was {} items, meseta {})",
              before.inventory.num_items, before.disp.stats.meseta.load()),
          snapshot.inventory.num_items, snapshot.disp.stats.meseta.load());
    } catch (const std::exception& e) {
      c->log.warning_f("Spire guard: failed to follow the real character after a return to base: {}", e.what());
    }
    return;
  }
  if (!spire_guard_follow_pending.erase(c.get())) {
    return;
  }
  try {
    uint32_t serial_number = spire_bridge_serial_for_client(c);
    auto character = c->character_file(false, false);
    if (!serial_number || !character || c->is_spire_protected()) {
      return;
    }
    std::string filename = spire_guard_filename_for(c, serial_number);
    if (filename.empty()) {
      return;
    }
    auto snapshot = spire_guard_load(filename);
    if (!snapshot.has_character || !spire_guard_same_character(character->disp, snapshot.disp)) {
      return;
    }
    std::string before = spire_guard_describe(snapshot);
    if (spire_guard_follow_real_progress(snapshot, character->inventory, character->disp)) {
      spire_guard_save(filename, serial_number, snapshot);
      c->log.info_f("Spire guard: 98 of a normal game under AwaitingSave for serial {:010}; snapshot follows the real character: {} -> {} ({} items)",
          serial_number, before, spire_guard_describe(snapshot), snapshot.inventory.num_items);
      c->log.info_f("Spire guard: v40: first 98 of a normal game closes AwaitingSave for serial {:010}; the card holds the real character",
          serial_number);
    }
  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to follow the real character after a 98: {}", e.what());
  }
}

// How long the guard waits for each client answer during a restore (30, the B3 of SetExtendedPlayerInfo, and the
// verifying 61). Each answer is at most about 10 KB on a LAN and normally arrives within a second; 15 seconds leaves
// room for a slow Wii without letting a stuck login sit forever.
static constexpr auto SPIRE_GUARD_ANSWER_TIMEOUT = std::chrono::seconds(15);

// If *done is still false when the timeout expires, cancels the pending promises (Client::cancel_pending_promises),
// so the waiting restore throws and the caller never sends 97. At login (drop) the client is also dropped: without a 97
// the login cannot go on. At the game exit (v47) the client is dropped too, before it is in a lobby, and the marker
// keeps the restore pending for the next login.
static asio::awaitable<void> spire_guard_watchdog(
    std::weak_ptr<Client> weak_c, std::shared_ptr<bool> done, uint32_t serial_number, std::string what, bool drop) {
  co_await async_sleep(SPIRE_GUARD_ANSWER_TIMEOUT);
  if (*done) {
    co_return;
  }
  auto c = weak_c.lock();
  if (!c) {
    co_return;
  }
  if (drop) {
    c->log.warning_f("Spire guard: no {} within {} s for serial {:010}; dropping the connection, no 97 (marker kept)",
        what, SPIRE_GUARD_ANSWER_TIMEOUT.count(), serial_number);
    c->channel->disconnect();
  } else {
    c->log.warning_f("Spire guard: no {} within {} s for serial {:010}; connection kept (marker kept)",
        what, SPIRE_GUARD_ANSWER_TIMEOUT.count(), serial_number);
  }
  c->cancel_pending_promises();
}

// Arms the watchdog for one client answer; the answer counts as arrived when this goes out of scope, also when the
// wait throws.
struct SpireGuardWatchdog {
  std::shared_ptr<bool> done = std::make_shared<bool>(false);
  SpireGuardWatchdog(asio::any_io_executor executor, std::shared_ptr<Client> c, uint32_t serial_number, const char* what,
      bool drop) {
    asio::co_spawn(executor, spire_guard_watchdog(c, this->done, serial_number, what, drop), asio::detached);
  }
  SpireGuardWatchdog(const SpireGuardWatchdog&) = delete;
  SpireGuardWatchdog& operator=(const SpireGuardWatchdog&) = delete;
  ~SpireGuardWatchdog() {
    *this->done = true;
  }
};

// Verifies the 61 that the client sent right after the push: level and EXP must equal the snapshot, and (v32) the
// items and meseta too. Clears RestorePending and returns true if verified. In the lobby (v10) the marker is not cleared
// but becomes AwaitingSave: the card may still hold the overlay until the client saves. v48: keep_marker (the push in a
// game, the return to base) checks the same and changes no marker and no file.
static bool spire_guard_verify_restore(std::shared_ptr<Client> c, uint32_t serial_number, const std::string& filename,
    SpireGuardSnapshot& snapshot, std::shared_ptr<const PSOBBCharacterFile> character, bool after_game,
    bool keep_marker = false) {
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
  std::string items_str = std::format("inventory {} items, snapshot {} ({} count)",
      num_items, snapshot_num_items, (num_items == snapshot_num_items) ? "equal" : "different");
  // v32: the items and meseta must equal the snapshot's too
  bool same_stats = (level == snapshot_level) && (exp == snapshot_exp);
  bool same_inventory = spire_guard_same_inventory(
      character->inventory, character->disp.stats.meseta, snapshot.inventory, snapshot.disp.stats.meseta);
  // v38: the material counters too, handled like an item mismatch (marker kept, the next login pushes again)
  bool same_materials = spire_guard_same_materials(character->inventory, snapshot.inventory);
  if (!same_materials) {
    c->log.warning_f("Spire guard: material counters after the restore for serial {:010} differ: 61 shows {}, snapshot {}",
        serial_number, spire_guard_materials_str(character->inventory), spire_guard_materials_str(snapshot.inventory));
  }
  bool same_items = same_inventory && same_materials;
  if (!spire_guard_push_verified(same_stats, same_items)) {
    c->log.warning_f("Spire guard: restore not verified for serial {:010}: 61 shows Lv{} EXP {} meseta {}, snapshot Lv{} EXP {} meseta {}; {}; {}; marker kept",
        serial_number, level + 1, exp, character->disp.stats.meseta.load(), snapshot_level + 1, snapshot_exp,
        snapshot.disp.stats.meseta.load(), items_str,
        !same_stats ? "level or EXP differ" : (same_inventory ? "the material counters differ" : "the items or meseta differ"));
    return false;
  }
  if (keep_marker) {
    c->log.info_f("Spire guard: in-game restore verified for serial {:010}: 61 shows Lv{} EXP {}; {}; marker unchanged",
        serial_number, level + 1, exp, items_str);
    return true;
  }
  snapshot.restore_pending = false;
  snapshot.awaiting_save = after_game;
  snapshot.has_overlay = after_game && snapshot.has_overlay;
  spire_guard_save(filename, serial_number, snapshot);
  c->log.info_f("Spire guard: restore verified for serial {:010}: 61 shows Lv{} EXP {}; {}; {}",
      serial_number, level + 1, exp, items_str,
      after_game ? "marker kept until a login 61 shows the same" : "marker cleared");
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

// v49: the push itself, shared by the restore and the run again ('I'): the patch preparation (the same function call
// as $loadchar), in a game one more look right before the push (a B2 during a floor load hung the client, v33, and the
// 30 before took time), SetExtendedPlayerInfo with merged, and the 61 asked for right after (on_61_98 puts it into the
// server's character data, or into the overlay if there is one). Returns that 61's character; *pushed is false (and
// nothing was sent) if the look refused the push. Throws on a failed or timed-out answer.
enum class SpireGuardPushFloors { ANY, FIELD, RUN_START, BASE }; // BASE (v59): any floor, but in a game, nobody loading
static bool spire_game_any_client_loading(std::shared_ptr<const Lobby> l);
static asio::awaitable<std::shared_ptr<PSOBBCharacterFile>> spire_guard_push_character(std::shared_ptr<Client> c,
    uint32_t serial_number, std::shared_ptr<PSOBBCharacterFile> merged, SpireGuardPushFloors floors, bool drop,
    const char* push_name, const std::string& sent_desc, uint8_t level_before, bool* pushed) {
  *pushed = false;
  auto executor = co_await asio::this_coro::executor;
  {
    // v12 (critic K12): this can wait for the client too
    SpireGuardWatchdog watchdog(executor, c, serial_number, "answer to the patch preparation", drop);
    co_await prepare_client_for_patches(c);
  }
  auto s = c->require_server_state();
  if (floors != SpireGuardPushFloors::ANY) {
    auto l = c->lobby.lock();
    const char* why = !c->channel->connected() ? "the client disconnected"
        : (!l || !l->is_game())                 ? "the client left the game"
        : ((floors == SpireGuardPushFloors::FIELD) && !c->floor) ? "the client is on Pioneer 2"
        : ((floors == SpireGuardPushFloors::RUN_START) && !spire_run_again_floor_ok(c->floor)) ? "the client is not on Pioneer 2 or Forest 1"
        : spire_game_any_client_loading(l)      ? "a client of the game is loading"
                                                : nullptr;
    if (why) {
      c->log.warning_f("Spire guard: {} for serial {:010} not pushed: {}; nothing changed", push_name, serial_number, why);
      co_return nullptr;
    }
  }
  auto fn = s->data->client_functions->get("SetExtendedPlayerInfo", c->specific_version);
  PSOGCCharacterFile::Character gc_char = *merged;
  *pushed = true;
  {
    SpireGuardWatchdog watchdog(executor, c, serial_number, "answer to SetExtendedPlayerInfo (B3)", drop);
    co_await send_function_call(c, fn, {}, &gc_char, sizeof(gc_char));
  }
  c->log.info_f("Spire guard: sent {} for serial {:010} (was Lv{}); asking for a 61 to verify", sent_desc,
      serial_number, level_before + 1);
  GetPlayerInfoResult after;
  {
    SpireGuardWatchdog watchdog(executor, c, serial_number, "verifying 61", drop);
    after = co_await send_get_player_info(c, false);
    spire_run_mag_note_in_game_61(c); // F6 (amendment 3)
  }
  co_return after.character;
}

// The restore itself, shared by the login path and the lobby path: fetch the full character (30), put the snapshot's
// inventory and disp in, send it with SetExtendedPlayerInfo, and verify the 61 requested right after. Returns true
// only if the restore was verified (RestorePending is then cleared). where is "login" or "game exit" (for the log); v48:
// "game" is the return to base: no drop, no push while a client of the game loads, the marker unchanged.
static asio::awaitable<bool> spire_guard_run_restore(std::shared_ptr<Client> c, uint32_t serial_number,
    const std::string& filename, SpireGuardSnapshot snapshot, const char* where) {
  bool base_game = !strcmp(where, "base game"); // v59: the base game's push, like "game" but on any floor
  bool in_game = !strcmp(where, "game") || base_game;
  bool drop = !in_game;
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
      SpireGuardWatchdog watchdog(executor, c, serial_number, "full character (30)", drop);
      info = co_await send_get_player_info(c, true);
      spire_run_mag_note_in_game_61(c); // F6 (amendment 3)
    }
    if (!info.is_full_info || !info.character) {
      c->log.warning_f("Spire guard: client sent no full character (no 30); nothing restored for serial {:010}, marker kept",
          serial_number);
      co_return false;
    }
    if (!spire_guard_same_character(info.character->disp, snapshot.disp)) {
      // v11: never put one character's snapshot on another character's slot
      c->log.warning_f("Spire guard: the client's character ({}) is not the snapshot's ({}); nothing restored for serial {:010}, marker kept",
          info.character->disp.visual.name.decode(info.character->inventory.language), spire_guard_describe(snapshot),
          serial_number);
      co_return false;
    }
    // v12: the overlay as the client has it now; kept with AwaitingSave if this is a lobby restore. v14: if the client
    // already shows the snapshot's level and EXP, it has the real character in memory and there is no overlay to
    // recognize later (measured: BidX Lv52, 2026-10-24 03:52); then no fingerprint is kept.
    snapshot.has_overlay = (info.character->disp.stats.level != snapshot.disp.stats.level) ||
        (info.character->disp.stats.exp != snapshot.disp.stats.exp);
    snapshot.overlay_level = info.character->disp.stats.level;
    snapshot.overlay_exp = info.character->disp.stats.exp;
    snapshot.overlay_meseta = info.character->disp.stats.meseta;
    snapshot.overlay_num_items = info.character->inventory.num_items;
    auto merged = info.character;
    uint8_t level_before = merged->disp.stats.level;
    auto visual = merged->disp.visual;
    merged->inventory = snapshot.inventory;
    merged->disp = snapshot.disp;
    merged->disp.visual = visual;

    // Only the 61 requested after the push can verify the restore (v8): no earlier 61 and no 98 counts
    bool pushed = false;
    auto after = co_await spire_guard_push_character(c, serial_number, merged,
        base_game ? SpireGuardPushFloors::BASE : (in_game ? SpireGuardPushFloors::FIELD : SpireGuardPushFloors::ANY),
        drop, "in-game restore", spire_guard_describe(snapshot), level_before, &pushed);
    if (!pushed) {
      co_return false;
    }
    if (!spire_guard_verify_restore(c, serial_number, filename, snapshot, after, !strcmp(where, "game exit"), in_game)) {
      co_return false;
    }
    co_return true;

  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to restore character for serial {:010}: {}; marker kept", serial_number, e.what());
    co_return false;
  }
}

// v48: true if this game has a client that is loading (a floor, the quest, a joinable quest or a tournament)
static bool spire_game_any_client_loading(std::shared_ptr<const Lobby> l) {
  for (const auto& lc : l->clients) {
    if (lc && (lc->check_flag(Client::Flag::LOADING) || lc->check_flag(Client::Flag::LOADING_QUEST) ||
                  lc->check_flag(Client::Flag::LOADING_RUNNING_JOINABLE_QUEST) ||
                  lc->check_flag(Client::Flag::LOADING_TOURNAMENT))) {
      return true;
    }
  }
  return false;
}

// v56: the proof of a return to base ('O', SpireFlagBridge.hh). The push reaches only the client's save buffer, and
// the client copies its live player (still the run copy) back into it at the next floor change (live 2026-10-26
// 20:43-20:47: after 'O' OK and the warp to Pioneer 2 the 61 showed the run copy, and the 'I' stored it as the real
// character). So the return to base counts (one 'I' allowed, spire_base_returned_note) only when a 61 asked on
// Pioneer 2, after the warp, equals the snapshot; otherwise the client leaves with 6x73 as before 'O' existed, marker
// unchanged, and the restore at the game exit pushes the snapshot. A client that leaves meanwhile is left to that exit.
static asio::awaitable<void> spire_base_return_prove(std::weak_ptr<Client> weak_c, std::weak_ptr<Lobby> weak_l,
    uint32_t serial_number, uint32_t quest_number, std::string filename) {
  const char* why = nullptr;
  std::string seen;
  for (size_t z = 0;; z++) {
    co_await async_sleep(SPIRE_BASE_PROOF_POLL);
    auto c = weak_c.lock();
    auto l = weak_l.lock();
    if (!c || !l || (c->lobby.lock() != l) || !c->channel->connected()) {
      co_return;
    }
    if ((c->floor == 0) && !spire_game_any_client_loading(l)) {
      break;
    }
    if (z >= SPIRE_BASE_PROOF_POLLS) {
      why = "the client did not reach Pioneer 2 in time";
      break;
    }
  }
  if (!why) {
    co_await async_sleep(SPIRE_BASE_PROOF_SETTLE);
  }
  auto c = weak_c.lock();
  auto l = weak_l.lock();
  if (!c || !l || (c->lobby.lock() != l) || !c->channel->connected()) {
    co_return;
  }
  if (!why) {
    try {
      auto executor = co_await asio::this_coro::executor;
      SpireGuardSnapshot snapshot = spire_guard_load(filename);
      GetPlayerInfoResult now;
      {
        SpireGuardWatchdog watchdog(executor, c, serial_number, "61 for the base proof", false);
        now = co_await send_get_player_info(c, false);
        spire_run_mag_note_in_game_61(c); // F6 (amendment 3)
      }
      if (!now.character) {
        why = "no 61";
      } else {
        seen = std::format(" (61 shows Lv{} EXP {}, {} items; snapshot {}, {} items)", now.character->disp.stats.level + 1,
            now.character->disp.stats.exp.load(), now.character->inventory.num_items, spire_guard_describe(snapshot),
            snapshot.inventory.num_items);
        if (!snapshot.has_character || !snapshot.run_in_progress ||
            !spire_guard_same_character(now.character->disp, snapshot.disp) ||
            !spire_guard_same_state(now.character->inventory, now.character->disp, snapshot.inventory, snapshot.disp)) {
          why = "the live player on Pioneer 2 is not the real character";
        }
      }
    } catch (const std::exception& e) {
      seen = std::format(" ({})", e.what());
      why = "the 61 failed";
    }
    c = weak_c.lock();
    l = weak_l.lock();
    if (!c || !l || (c->lobby.lock() != l) || !c->channel->connected()) {
      co_return;
    }
  }
  if (!why) {
    spire_base_returned_note(c, l->lobby_id); // v49: the one 'I' this allows
    c->log.info_f("Spire base return: proven on Pioneer 2 for serial {:010} in q{}{}; one 'I' allowed",
        serial_number, quest_number, seen);
    co_return;
  }
  c->log.warning_f("Spire base return: not proven for serial {:010} in q{}: {}{}; leaving with 6x73, marker unchanged (the game exit restores the snapshot)",
      serial_number, quest_number, why, seen);
  if (l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS) || l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) {
    G_UnusedHeader cmd = {0x73, 0x01, 0x0000}; // as $exit in a quest
    c->channel->send(0x60, 0x00, cmd);
  }
}

// v48: the return to base ('O', SpireFlagBridge.hh). Always answers the client (r41, then 'DONE'). Never throws.
static asio::awaitable<void> spire_base_return(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint32_t serial_number, uint32_t quest_number) {
  using R = SpireBaseReturnRefusal;
  R refusal = R::NONE;
  std::string filename;
  SpireGuardSnapshot snapshot;
  try {
    SpireBaseReturnState st;
    auto character = c->character_file(false, false);
    st.has_character = serial_number && character && (c->version() == Version::GC_V3);
    const auto& rules = l->quest ? l->quest->meta.battle_rules : nullptr;
    st.spire_run = (c->lobby.lock() == l) && l->is_game() && (l->mode == GameMode::BATTLE) && l->quest && rules &&
        rules->replace_char && spire_guard_quest_is_guarded(c, quest_number) && c->has_overlay();
    auto end_it = spire_run_ends.find(l->lobby_id);
    st.run_over = (end_it != spire_run_ends.end());
    st.on_field_floor = (c->floor != 0);
    st.any_client_loading = spire_game_any_client_loading(l);
    st.can_run_functions = c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL) &&
        c->check_flag(Client::Flag::SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE);
    st.protected_character = c->is_spire_protected();
    if (st.has_character) {
      filename = spire_guard_filename_for(c, serial_number);
      if (!filename.empty() && std::filesystem::is_regular_file(filename)) {
        snapshot = spire_guard_load(filename);
        st.has_snapshot = snapshot.has_character;
        st.run_in_progress = snapshot.run_in_progress;
        st.restore_pending = snapshot.restore_pending;
        st.awaiting_save = snapshot.awaiting_save;
      }
    }
    st.restore_running = spire_guard_restoring_clients.count(c.get());
    st.already_sent_back = st.run_over && end_it->second.exit_sent.count(c->lobby_client_id);
    refusal = spire_base_return_refusal(st);
  } catch (const std::exception& e) {
    c->log.warning_f("Spire base return: cannot read the state for serial {:010}: {}", serial_number, e.what());
    refusal = R::MARKER;
  }

  // v57: with a base quest the live player is never pushed in this game; the client leaves it for the base game
  // v58: the floor, a load and B2 matter only to the push; the base game needs none of them (a follower that stayed on
  // Pioneer 2 goes along)
  bool base_game = ((refusal == R::NONE) || (refusal == R::NOT_ON_FIELD) || (refusal == R::LOADING) ||
                       (refusal == R::NO_FUNCTIONS)) &&
      c->require_server_state()->data->spire_base_quest_number;
  if (base_game) {
    refusal = R::BASE_GAME;
  }
  if (refusal == R::NONE) {
    // The server's own 6x73 after an 'X' must not land during the push or the warp: claim it for this client
    uint8_t slot = c->lobby_client_id;
    if (auto it = spire_run_ends.find(l->lobby_id); it != spire_run_ends.end()) {
      it->second.exit_sent.emplace(slot);
    }
    bool verified = false;
    {
      SpireGuardRestoreLock lock(c.get());
      if (!lock.locked) {
        refusal = R::RESTORE_RUNNING;
      } else {
        // F1: the print of the run copy, before the push replaces what the client holds; a later 61 that matches it
        // never becomes the snapshot (spire_guard_retake_ok). Without a stored print no re-take happens at all. F1 fix
        // (C4): the earlier print leaves the file first, and a print that cannot be saved refuses the push.
        std::shared_ptr<const PSOBBCharacterFile> overlay;
        try {
          overlay = c->has_overlay() ? c->character_file(false, true) : nullptr;
        } catch (const std::exception& e) {
          c->log.warning_f("Spire base return: no run copy to print for serial {:010}: {}", serial_number, e.what());
        }
        bool print_stored = spire_guard_store_print(filename, serial_number, snapshot,
            overlay ? &overlay->inventory : nullptr, overlay ? &overlay->disp : nullptr);
        if (!print_stored) {
          // verified stays false: the refusal below is PUSH_NOT_VERIFIED
          c->log.warning_f("Spire base return: the guard file of serial {:010} cannot be written; no push", serial_number);
        } else {
          if (snapshot.run_copy_print.exists) {
            c->log.info_f("Spire base return: run copy print stored for serial {:010}: Lv{}, hash {:016X}",
                serial_number, snapshot.run_copy_print.level + 1, snapshot.run_copy_print.hash);
          } else {
            c->log.warning_f("Spire base return: no run copy to print for serial {:010}; the file holds no print, no re-take follows this 'O'",
                serial_number);
          }
          c->log.info_f("Spire base return: pushing the real character for serial {:010} in q{} on floor {:02X}",
              serial_number, quest_number, c->floor);
          verified = co_await spire_guard_run_restore(c, serial_number, filename, snapshot, "game");
        }
        if (!verified) {
          refusal = R::PUSH_NOT_VERIFIED;
        } else {
          // v56: the 'I' this allows comes only with the proof on Pioneer 2 (spire_base_return_prove)
          asio::co_spawn(*c->require_server_state()->io_context,
              spire_base_return_prove(c, l, serial_number, quest_number, filename), asio::detached);
        }
      }
    }
    if (!verified) {
      // Back to the default: the quest leaves with 6x73; after an 'X' the server's 6x73 follows as before
      auto it = spire_run_ends.find(l->lobby_id);
      if (it != spire_run_ends.end() && it->second.exit_sent.erase(slot) && it->second.over_announced) {
        asio::co_spawn(*c->require_server_state()->io_context, spire_run_over_exit(c, l, SPIRE_RUN_OVER_LOBBY_DELAY),
            asio::detached);
      }
    }
  }

  uint32_t answer = spire_base_return_answer(refusal);
  try {
    if (c->channel->connected()) {
      spire_send_register(c, SPIRE_BASE_RETURN_ANSWER_REGISTER, answer);
      spire_send_register(c, SPIRE_BRIDGE_REQUEST_REGISTER, SPIRE_BRIDGE_DONE_VALUE);
    }
  } catch (const std::exception& e) {
    c->log.warning_f("Spire base return: failed to answer serial {:010}: {}", serial_number, e.what());
  }
  if (base_game) {
    c->log.info_f("Spire base return: serial {:010} in q{} goes to the base game (r{}={:08X}); no push in this game",
        serial_number, quest_number, SPIRE_BASE_RETURN_ANSWER_REGISTER, answer);
    if (!spire_base_game_request(c, l, "'O'")) {
      // Back to the default: the server's 6x73 after an 'X' as before
      if (auto it = spire_run_ends.find(l->lobby_id); it != spire_run_ends.end() && it->second.over_announced) {
        asio::co_spawn(*c->require_server_state()->io_context, spire_run_over_exit(c, l, SPIRE_RUN_OVER_LOBBY_DELAY),
            asio::detached);
      }
    }
  } else if (refusal == R::NONE) {
    c->log.info_f("Spire base return: OK for serial {:010} in q{}: the real character is back, the quest may warp to Pioneer 2 (r{}={:08X}); marker unchanged",
        serial_number, quest_number, SPIRE_BASE_RETURN_ANSWER_REGISTER, answer);
  } else {
    c->log.info_f("Spire base return: FAIL for serial {:010} in q{}: {}; the quest leaves with 6x73 (r{}={:08X})",
        serial_number, quest_number, spire_base_return_refusal_name(refusal), SPIRE_BASE_RETURN_ANSWER_REGISTER, answer);
  }
}

// v57: the base game (SpireFlagBridge.hh). Marks by serial (a client object does not survive a reconnect, and a
// reconnect drops the mark on purpose: the login restore then runs alone); groups by the run's game id.
struct SpireBaseGameMark {
  uint32_t group = 0;
  Episode episode = Episode::EP1;
  Difficulty difficulty = Difficulty::NORMAL;
  size_t expected = 1;
  GameMode mode = GameMode::NORMAL; // v58: BATTLE for a new run from the base
  uint32_t quest_number = 0; // the quest the new game starts: SpireBaseQuest, or the run quest (v58)
  uint32_t run_quest_number = 0; // the run quest the base came from (v58: its "Start a new run" starts this one)
  uint32_t leader_serial = 0; // v60: the leader of the game they leave; only this client creates the next game
};
// v60: the serial of the game's leader, or 0
static uint32_t spire_base_leader_serial(std::shared_ptr<const Lobby> l) {
  auto lc = (l->leader_id < l->max_clients) ? l->clients[l->leader_id] : nullptr;
  return lc ? spire_bridge_serial_for_client(lc) : 0;
}
// v58: base game id -> the run quest it came from
static std::unordered_map<uint32_t, uint32_t> spire_base_game_run_quests;
struct SpireBaseGameGroup {
  std::weak_ptr<Lobby> game;
};
static std::unordered_map<uint32_t, SpireBaseGameMark> spire_base_game_marks;
static std::unordered_map<uint32_t, SpireBaseGameGroup> spire_base_game_groups;

bool spire_base_game_request(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, const char* why) {
  auto s = c->require_server_state();
  uint32_t quest_number = s->data->spire_base_quest_number;
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  if (!quest_number || !serial_number || !l || !l->is_game() || !l->quest || (c->lobby.lock() != l)) {
    return false;
  }
  if (!s->data->quest_index->get(quest_number)) {
    c->log.warning_f("Spire base game: base quest q{} not found; serial {:010} stays in this game", quest_number, serial_number);
    return false;
  }
  SpireBaseGameMark mark;
  mark.group = l->lobby_id;
  mark.episode = l->episode;
  mark.difficulty = l->difficulty;
  mark.expected = std::max<size_t>(1, l->count_clients());
  mark.quest_number = quest_number;
  mark.run_quest_number = l->quest->meta.quest_number;
  mark.leader_serial = spire_base_leader_serial(l);
  // v58: the whole party goes: every client of the run's game is marked and sent out once (one that the server already
  // sent back to the lobby after the run end is only marked)
  auto end_it = spire_run_ends.find(l->lobby_id);
  for (const auto& lc : l->clients) {
    uint32_t lc_serial = lc ? spire_bridge_serial_for_client(lc) : 0;
    auto have = lc_serial ? spire_base_game_marks.find(lc_serial) : spire_base_game_marks.end();
    if (!lc_serial || ((have != spire_base_game_marks.end()) && (have->second.group == mark.group) &&
                          (have->second.mode == mark.mode))) {
      continue; // already on its way from this game (a stale mark of another game is replaced)
    }
    spire_base_game_marks[lc_serial] = mark;
    bool already_sent = (end_it != spire_run_ends.end()) && !end_it->second.exit_sent.emplace(lc->lobby_client_id).second;
    if (!already_sent || (lc == c)) {
      G_UnusedHeader cmd = {0x73, 0x01, 0x0000}; // as $exit in a quest
      lc->channel->send(0x60, 0x00, cmd);
    }
    lc->log.info_f("Spire base game: serial {:010} leaves q{} game {:08X} ({} from client {}, 6x73{}); base quest q{} follows in the lobby ({} client(s) in this run)",
        lc_serial, l->quest->meta.quest_number, l->lobby_id, why, c->lobby_client_id, already_sent ? " already sent" : "",
        quest_number, mark.expected);
  }
  return spire_base_game_marks.count(serial_number);
}

// Starts the base quest once every client of the run is in the base game and none loads, or after SPIRE_BASE_GAME_WAIT
// with the ones that came. A game that empties meanwhile is dropped.
static asio::awaitable<void> spire_base_game_start(
    std::weak_ptr<Lobby> weak_game, uint32_t group, size_t expected, uint32_t quest_number) {
  auto started = std::chrono::steady_clock::now();
  for (;;) {
    co_await async_sleep(std::chrono::seconds(1));
    auto game = weak_game.lock();
    if (!game || !game->count_clients()) {
      if (auto it = spire_base_game_groups.find(group); it != spire_base_game_groups.end() && it->second.game.expired()) {
        spire_base_game_groups.erase(it);
      }
      co_return;
    }
    auto waited = std::chrono::steady_clock::now() - started;
    if (!spire_game_any_client_loading(game) && ((game->count_clients() >= expected) || (waited >= SPIRE_BASE_GAME_WAIT))) {
      break;
    }
    if (waited >= 3 * SPIRE_BASE_GAME_WAIT) {
      game->log.warning_f("Spire base game: a client is still loading after {} s; base quest not started",
          std::chrono::duration_cast<std::chrono::seconds>(waited).count());
      spire_base_game_groups.erase(group);
      co_return;
    }
  }
  spire_base_game_groups.erase(group); // a client of this run that comes later gets its own base game
  auto game = weak_game.lock();
  if (!game || game->quest) {
    co_return;
  }
  auto s = game->require_server_state();
  auto q = s->data->quest_index->get(quest_number);
  if (!q) {
    game->log.warning_f("Spire base game: quest q{} not found", quest_number);
    co_return;
  }
  for (const auto& lc : game->clients) {
    if (lc && !q->version(lc->version(), lc->language())) {
      game->log.warning_f("Spire base game: quest q{} has no version for client {}; not started", quest_number,
          lc->lobby_client_id);
      co_return;
    }
  }
  if (game->mode != GameMode::BATTLE) {
    // v59: the real character into every save buffer before the base quest can make the client write its card
    std::vector<std::shared_ptr<Client>> clients;
    for (const auto& lc : game->clients) {
      if (lc) {
        clients.emplace_back(lc);
      }
    }
    for (const auto& lc : clients) {
      uint32_t serial_number = spire_bridge_serial_for_client(lc);
      if (!serial_number || lc->is_spire_protected() || !lc->channel->connected()) {
        continue;
      }
      bool verified = false;
      try {
        std::string filename = spire_guard_filename_for(lc, serial_number);
        if (!filename.empty() && std::filesystem::is_regular_file(filename)) {
          SpireGuardSnapshot snapshot = spire_guard_load(filename);
          SpireGuardRestoreLock lock(lc.get());
          if (lock.locked && snapshot.has_character) {
            verified = co_await spire_guard_run_restore(lc, serial_number, filename, snapshot, "base game");
          }
        }
      } catch (const std::exception& e) {
        lc->log.warning_f("Spire base game: push for serial {:010} failed: {}", serial_number, e.what());
      }
      lc->log.info_f("Spire base game: real character pushed into the save buffer for serial {:010}: {}", serial_number,
          verified ? "verified by the 61" : "NOT verified (the marker stays; the next login restores)");
    }
    game = weak_game.lock();
    if (!game || game->quest) {
      co_return;
    }
  }
  set_lobby_quest(game, q, true);
  game->log.info_f("Spire base game: q{} started in {} game {:08X} with {} of {} client(s)", quest_number,
      (game->mode == GameMode::BATTLE) ? "the new run's Battle" : "the base", game->lobby_id, game->count_clients(),
      expected);
}

static asio::awaitable<void> spire_base_game_join(std::weak_ptr<Client> weak_c, uint32_t serial_number, SpireBaseGameMark mark) {
  co_await async_sleep(SPIRE_BASE_GAME_LOBBY_SETTLE);
  auto c = weak_c.lock();
  if (!c || !c->channel->connected()) {
    co_return;
  }
  auto current = c->lobby.lock();
  if (!current || current->is_game() || c->check_flag(Client::Flag::LOADING)) {
    c->log.info_f("Spire base game: serial {:010} is not standing in a lobby; no base game", serial_number);
    co_return;
  }
  try {
    auto s = c->require_server_state();
    auto q = s->data->quest_index->get(mark.quest_number);
    if (!q || !q->version(c->version(), c->language())) {
      c->log.warning_f("Spire base game: quest q{} missing for this client; serial {:010} stays in the lobby",
          mark.quest_number, serial_number);
      co_return;
    }
    // v60: the previous leader creates the next game, so he leads it again; another client waits for that game up to
    // SPIRE_BASE_GAME_LEADER_WAIT (the leader may be slower out of the lobby) and only then creates one itself
    if (mark.leader_serial && (mark.leader_serial != serial_number)) {
      for (size_t z = 0; z < SPIRE_BASE_GAME_LEADER_WAIT_POLLS; z++) {
        auto it = spire_base_game_groups.find(mark.group);
        if ((it != spire_base_game_groups.end()) && !it->second.game.expired()) {
          break;
        }
        co_await async_sleep(std::chrono::milliseconds(500));
        if (!c->channel->connected() || !c->lobby.lock() || c->lobby.lock()->is_game()) {
          co_return;
        }
      }
    }
    auto& group = spire_base_game_groups[mark.group];
    auto game = group.game.lock();
    bool created = false;
    if (game && (!game->is_game() || game->quest || (game->count_clients() >= game->max_clients))) {
      game = nullptr;
    }
    if (!game) {
      bool battle = (mark.mode == GameMode::BATTLE);
      game = create_game_generic(s, c, battle ? "Spire run" : "Spire base", "", mark.episode, mark.mode, mark.difficulty, false);
      if (!game) {
        c->log.warning_f("Spire base game: creating the base game failed for serial {:010}; stays in the lobby", serial_number);
        co_return;
      }
      group.game = game;
      created = true;
      if (mark.mode != GameMode::BATTLE) {
        spire_base_game_run_quests[game->lobby_id] = mark.run_quest_number;
      }
    }
    // v60: what the server holds for this client when it joins (the game join command carries it to the client)
    auto joined_with = c->character_file(false, false);
    s->change_client_lobby(c, game);
    c->set_flag(Client::Flag::LOADING);
    c->log.info_f("Spire base game: serial {:010} {} {} game {:08X} for q{} (from game {:08X}, {} expected; server data Lv{} {} items{})",
        serial_number, created ? "created" : "joined", (mark.mode == GameMode::BATTLE) ? "the new run's Battle" : "the base",
        game->lobby_id, mark.quest_number, mark.group, mark.expected,
        joined_with ? joined_with->disp.stats.level + 1 : 0, joined_with ? joined_with->inventory.num_items : 0,
        c->has_overlay() ? ", overlay" : "");
    if (created) {
      asio::co_spawn(*s->io_context, spire_base_game_start(game, mark.group, mark.expected, mark.quest_number),
          asio::detached);
    }
  } catch (const std::exception& e) {
    c->log.warning_f("Spire base game: failed for serial {:010}: {}", serial_number, e.what());
  }
}

// v58: "Start a new run" in the base (SpireFlagBridge.hh, SPIRE_BASE_NEW_RUN_VALUE). Never throws.
static void spire_base_new_run(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l) {
  try {
    auto s = c->require_server_state();
    if (c->lobby_client_id != l->leader_id) {
      spire_send_ticker(c, "Only the party leader can start a new run.");
      c->log.info_f("Spire base game: 'O' sub 2 (new run) from client {}, not the leader; refused", c->lobby_client_id);
      return;
    }
    uint32_t run_quest_number = SPIRE_BASE_DEFAULT_RUN_QUEST;
    if (auto it = spire_base_game_run_quests.find(l->lobby_id); (it != spire_base_game_run_quests.end()) && it->second) {
      run_quest_number = it->second;
    }
    auto q = s->data->quest_index->get(run_quest_number);
    if (!q) {
      spire_send_ticker(c, "No run quest on this server.");
      c->log.warning_f("Spire base game: 'O' sub 2 (new run): run quest q{} not found", run_quest_number);
      return;
    }
    size_t expected = std::max<size_t>(1, l->count_clients());
    for (const auto& lc : l->clients) {
      uint32_t serial_number = lc ? spire_bridge_serial_for_client(lc) : 0;
      if (!serial_number) {
        continue;
      }
      SpireBaseGameMark mark;
      mark.group = l->lobby_id;
      mark.episode = q->meta.episode;
      mark.difficulty = l->difficulty;
      mark.expected = expected;
      mark.mode = GameMode::BATTLE;
      mark.quest_number = run_quest_number;
      mark.run_quest_number = run_quest_number;
      mark.leader_serial = spire_bridge_serial_for_client(c); // the leader asked (checked above)
      spire_base_game_marks[serial_number] = mark;
      G_UnusedHeader cmd = {0x73, 0x01, 0x0000}; // as $exit in a quest
      lc->channel->send(0x60, 0x00, cmd);
      lc->log.info_f("Spire base game: serial {:010} leaves base game {:08X} for a new run in q{} (6x73, {} client(s))",
          serial_number, l->lobby_id, run_quest_number, expected);
    }
    spire_base_game_run_quests.erase(l->lobby_id);
  } catch (const std::exception& e) {
    c->log.warning_f("Spire base game: 'O' sub 2 (new run) failed: {}", e.what());
  }
}

bool spire_base_game_handle_new_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint16_t register_number, uint32_t value) {
  if ((register_number != SPIRE_BRIDGE_REQUEST_REGISTER) || ((value & 0xFFFF0000) != SPIRE_BASE_NEW_RUN_VALUE) || !l->is_game() ||
      !l->quest || (l->mode == GameMode::BATTLE)) {
    return false;
  }
  try {
    if (l->quest->meta.quest_number != c->require_server_state()->data->spire_base_quest_number) {
      return false;
    }
  } catch (const std::exception&) {
    return false;
  }
  spire_base_new_run(c, std::const_pointer_cast<Lobby>(l));
  return true;
}

void spire_base_game_after_lobby_join(std::shared_ptr<Client> c) {
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  auto it = serial_number ? spire_base_game_marks.find(serial_number) : spire_base_game_marks.end();
  if (it == spire_base_game_marks.end()) {
    return;
  }
  auto mark = it->second;
  spire_base_game_marks.erase(it);
  asio::co_spawn(*c->require_server_state()->io_context, spire_base_game_join(c, serial_number, mark), asio::detached);
}

// v49: the run again ('I', SpireFlagBridge.hh). The per-game state of a run lives further down
static void spire_bridge_forget_room_clears(uint32_t lobby_id);
static void spire_replay_forget(std::shared_ptr<const Lobby> l);

// The client's part of a new run, as spire_checkpoint_run_started forgets it at a quest start (the game's part is
// spire_run_again_forget_game)
static void spire_run_again_forget_client(std::shared_ptr<const Client> c) {
  spire_follow_pending.erase(c.get());
  spire_client_runs.erase(c.get());
  spire_run_rooms.erase(c.get());
  spire_records_pending_score.erase(c.get());
  spire_draw_inputs.erase(c.get());
  if (spire_checkpoint_lives_forget(c.get())) {
    c->log.info_f("Spire checkpoint: lives of the previous run forgotten at the run again");
  }
}

// The game's part, only at the first 'I' after the run end: once a partner's 'B' started the new run (the run end is
// gone), its run id, room clears and map belong to the new run and stay
static void spire_run_again_forget_game(std::shared_ptr<Lobby> l, std::shared_ptr<const Client> c) {
  if (!spire_run_ends.count(l->lobby_id)) {
    l->log.info_f("Spire run again: the new run of this game started already; its state stays");
    return;
  }
  spire_game_runs.erase(l->lobby_id);
  spire_bridge_forget_room_clears(l->lobby_id);
  spire_replay_forget(l);
  if (l->map_state) {
    l->map_state->reset(); // as the BB battle start (6xCF) does
  }
  auto leader = (l->leader_id < l->clients.size()) ? l->clients[l->leader_id] : nullptr;
  spire_drops_quest_loaded(l, leader ? leader : c);
  l->log.info_f("Spire run again: the earlier run of this game forgotten (run id, room clears, replay, map state, drop pity)");
}

// After the checks: the 'O' OK is used up, the snapshot taken again from a 61, the overlay made and pushed. Returns
// the refusal (NONE: the client holds the verified run copy). Never throws.
static asio::awaitable<SpireRunAgainRefusal> spire_run_again_push(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l,
    uint32_t serial_number, uint32_t quest_number, const std::string& filename, SpireGuardSnapshot snapshot) {
  using R = SpireRunAgainRefusal;
  // From here on the client's memory is no longer known to hold the real character, so no second 'I' may re-take
  spire_base_returned.erase(c.get());
  R on_error = R::NOT_REAL;
  try {
    auto executor = co_await asio::this_coro::executor;

    // The real character as the client has it now (the base may have changed it), from a 61 as at a quest load
    GetPlayerInfoResult now;
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "61 for the run again", false);
      now = co_await send_get_player_info(c, false);
      spire_run_mag_note_in_game_61(c); // F6 (amendment 3)
    }
    SpireGuardSnapshot before = snapshot;
    if (!now.character ||
        !spire_guard_run_again_retake(
            snapshot, true, now.character->inventory, now.character->disp, quest_number, phosg::now())) {
      c->log.warning_f("Spire run again: the 61 for serial {:010} is not the snapshot's character, below it or the run copy ({}{}); kept the stored snapshot, nothing changed",
          serial_number, spire_guard_describe(before),
          now.character ? std::format("; 61 shows Lv{} EXP {}, {} items, snapshot {} items; F1: print {}",
                              now.character->disp.stats.level + 1, now.character->disp.stats.exp.load(),
                              now.character->inventory.num_items, before.inventory.num_items,
                              !before.run_copy_print.exists ? "none stored"
                                  : spire_guard_matches_print(now.character->inventory, now.character->disp, before.run_copy_print) ? "matches" : "differs")
                        : std::string());
      co_return R::NOT_REAL;
    }
    auto real = c->character_file(false, false);
    if (!real) {
      c->log.warning_f("Spire run again: no character data for serial {:010}; nothing changed", serial_number);
      co_return R::NOT_REAL;
    }
    spire_guard_save(filename, serial_number, snapshot);
    real->inventory = snapshot.inventory;
    real->disp = snapshot.disp;
    bool same = spire_guard_same_state(snapshot.inventory, snapshot.disp, before.inventory, before.disp);
    c->log.info_f("Spire run again: snapshot taken again for serial {:010} in q{}: {} ({} items){}; marker RunInProgress",
        serial_number, quest_number, spire_guard_describe(snapshot), snapshot.inventory.num_items,
        same ? ", equal to the one before" : std::format(", changed in the base (was {}, {} items)",
                                                       spire_guard_describe(before), before.inventory.num_items));

    // The run copy, made as on_quest_loaded makes it
    on_error = R::NO_OVERLAY;
    c->delete_overlay();
    if (!spire_create_battle_overlay(c, l, true)) {
      c->log.warning_f("Spire run again: no battle overlay for serial {:010}; nothing pushed", serial_number);
      co_return R::NO_OVERLAY;
    }
    auto overlay = c->character_file(false, true);
    PlayerInventory want_inventory = overlay->inventory;
    PlayerDispDataV4 want_disp = overlay->disp;

    // The push: the full character first (30), as the restore does, then only inventory and disp replaced
    on_error = R::PUSH_NOT_VERIFIED;
    GetPlayerInfoResult info;
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "full character (30)", false);
      info = co_await send_get_player_info(c, true);
      spire_run_mag_note_in_game_61(c); // F6 (amendment 3)
    }
    if (!info.is_full_info || !info.character || !spire_guard_same_character(info.character->disp, snapshot.disp)) {
      c->log.warning_f("Spire run again: no full character (30) of the snapshot's character for serial {:010}; nothing pushed",
          serial_number);
      co_return R::PUSH_NOT_VERIFIED;
    }
    auto merged = info.character;
    uint8_t level_before = merged->disp.stats.level;
    auto visual = merged->disp.visual;
    merged->inventory = want_inventory;
    merged->disp = want_disp;
    merged->disp.visual = visual;
    bool pushed = false;
    auto after = co_await spire_guard_push_character(c, serial_number, merged, SpireGuardPushFloors::RUN_START, false,
        "run copy", std::format("the run copy Lv{} ({} items)", want_disp.stats.level + 1, want_inventory.num_items),
        level_before, &pushed);
    if (!pushed) {
      co_return R::PUSH_NOT_VERIFIED;
    }
    if (!after || !spire_guard_same_state(after->inventory, after->disp, want_inventory, want_disp)) {
      c->log.warning_f("Spire run again: the 61 after the push for serial {:010} does not equal the run copy{}; marker RunInProgress",
          serial_number, after ? std::format(" (61 shows Lv{}, {} items)", after->disp.stats.level + 1, after->inventory.num_items) : "");
      co_return R::PUSH_NOT_VERIFIED;
    }
    c->log.info_f("Spire run again: run copy verified for serial {:010}: 61 shows Lv{}, {} items", serial_number,
        after->disp.stats.level + 1, after->inventory.num_items);
    co_return R::NONE;

  } catch (const std::exception& e) {
    c->log.warning_f("Spire run again: failed for serial {:010}: {}; marker RunInProgress", serial_number, e.what());
    co_return on_error;
  }
}

// v49: the run again ('I', SpireFlagBridge.hh). Always answers the client (r41, then 'DONE'). Never throws.
static asio::awaitable<void> spire_run_again(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint32_t serial_number, uint32_t quest_number) {
  using R = SpireRunAgainRefusal;
  R refusal = R::NONE;
  std::string filename;
  SpireGuardSnapshot snapshot;
  try {
    SpireRunAgainState st;
    auto character = c->character_file(false, false);
    st.has_character = serial_number && character && (c->version() == Version::GC_V3);
    const auto& rules = l->quest ? l->quest->meta.battle_rules : nullptr;
    st.spire_run = (c->lobby.lock() == l) && l->is_game() && (l->mode == GameMode::BATTLE) && l->quest && rules &&
        rules->replace_char && spire_guard_quest_is_guarded(c, quest_number);
    st.base_returned = spire_base_returned_has(c, l->lobby_id);
    st.at_start_floor = spire_run_again_floor_ok(c->floor);
    st.any_client_loading = spire_game_any_client_loading(l);
    st.can_run_functions = c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL) &&
        c->check_flag(Client::Flag::SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE);
    st.protected_character = c->is_spire_protected();
    if (st.has_character) {
      filename = spire_guard_filename_for(c, serial_number);
      if (!filename.empty() && std::filesystem::is_regular_file(filename)) {
        snapshot = spire_guard_load(filename);
        st.has_snapshot = snapshot.has_character;
        st.run_in_progress = snapshot.run_in_progress;
        st.restore_pending = snapshot.restore_pending;
        st.awaiting_save = snapshot.awaiting_save;
      }
    }
    st.restore_running = spire_guard_restoring_clients.count(c.get());
    refusal = spire_run_again_refusal(st);
  } catch (const std::exception& e) {
    c->log.warning_f("Spire run again: cannot read the state for serial {:010}: {}", serial_number, e.what());
    refusal = R::MARKER;
  }

  if (refusal == R::NONE) {
    SpireGuardRestoreLock lock(c.get());
    if (!lock.locked) {
      refusal = R::RESTORE_RUNNING;
    } else {
      c->log.info_f("Spire run again: making the run copy for serial {:010} in q{} on floor {:02X}", serial_number,
          quest_number, c->floor);
      spire_run_again_games.emplace(l->lobby_id); // F6 (amendment 4): no 'Z' in this game until the next quest load
      refusal = co_await spire_run_again_push(c, l, serial_number, quest_number, filename, snapshot);
    }
  }
  if (refusal == R::NONE) {
    try {
      spire_run_again_forget_client(c);
      spire_run_again_forget_game(l, c);
    } catch (const std::exception& e) {
      c->log.warning_f("Spire run again: failed to forget the earlier run: {}", e.what());
    }
  }

  uint32_t answer = spire_run_again_answer(refusal);
  try {
    if (c->channel->connected()) {
      spire_send_register(c, SPIRE_RUN_AGAIN_ANSWER_REGISTER, answer);
      spire_send_register(c, SPIRE_BRIDGE_REQUEST_REGISTER, SPIRE_BRIDGE_DONE_VALUE);
    }
  } catch (const std::exception& e) {
    c->log.warning_f("Spire run again: failed to answer serial {:010}: {}", serial_number, e.what());
  }
  if (refusal == R::NONE) {
    c->log.info_f("Spire run again: OK for serial {:010} in q{}: the run copy is in memory, the quest may start the run (r{}={:08X})",
        serial_number, quest_number, SPIRE_RUN_AGAIN_ANSWER_REGISTER, answer);
  } else {
    c->log.info_f("Spire run again: FAIL for serial {:010} in q{}: {}; the quest starts no run (r{}={:08X})",
        serial_number, quest_number, spire_run_again_refusal_name(refusal), SPIRE_RUN_AGAIN_ANSWER_REGISTER, answer);
  }
}

const char* spire_kit_lobby_refused(bool protected_character, bool is_game, bool spire_run, bool restore_pending) {
  if (protected_character) {
    return "$C6Niet op dit personage.";
  }
  if (spire_run) {
    return "$C6Niet in een Spire-run.";
  }
  if (is_game) {
    return "$C6Alleen in de lobby.";
  }
  if (restore_pending) {
    return "$C6Wacht: herstel loopt";
  }
  return nullptr;
}

void spire_kit_lobby_apply(PSOBBCharacterFile& ch, std::shared_ptr<const LevelTable> level_table, uint32_t level) {
  using MatType = PSOBBCharacterFile::MaterialType;
  level = std::clamp<uint32_t>(level, 1, 200);
  // Never lower a counter; add up to the class's target (DataIndex::spire_kit_material_targets) within the shared cap
  uint8_t cls = ch.disp.visual.sh.char_class;
  auto targets = DataIndex::spire_kit_material_targets(cls, *level_table);
  size_t cap = char_class_is_android(cls) ? DataIndex::SPIRE_KIT_MATERIAL_CAP_ANDROID : DataIndex::SPIRE_KIT_MATERIAL_CAP;
  size_t used = 0;
  for (auto which : {MatType::POWER, MatType::MIND, MatType::EVADE, MatType::DEF, MatType::LUCK}) {
    used += ch.get_material_usage(which);
  }
  for (const auto& [which, target] : std::initializer_list<std::pair<MatType, uint8_t>>{{MatType::POWER, targets.power},
           {MatType::MIND, targets.mind}, {MatType::EVADE, targets.evade}, {MatType::DEF, targets.def},
           {MatType::LUCK, targets.luck}}) {
    size_t current = ch.get_material_usage(which);
    size_t add = (target > current) ? std::min<size_t>(target - current, (cap > used) ? (cap - used) : 0) : 0;
    ch.set_material_usage(which, current + add);
    used += add;
  }
  for (const auto& [which, target] : std::initializer_list<std::pair<MatType, uint8_t>>{
           {MatType::HP, targets.hp}, {MatType::TP, targets.tp}}) {
    if (ch.get_material_usage(which) < target) {
      ch.set_material_usage(which, target);
    }
  }
  // The level table's stats and EXP for the class at that level, plus 2 per used material (as $edit level on BB)
  ch.disp.stats.level = level - 1;
  ch.recompute_stats(level_table, true);
}

asio::awaitable<std::string> spire_kit_lobby_push(std::shared_ptr<Client> c, uint32_t level) {
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  if (!serial_number) {
    co_return "$C6Alleen voor GC.";
  }
  if (!c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL) ||
      !c->check_flag(Client::Flag::SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE)) {
    co_return "$C6Kan niet: geen\nfunctie-aanroep.";
  }
  // Never while the guard restores this client or holds a marker for this character
  SpireGuardRestoreLock lock(c.get());
  bool pending = !lock.locked;
  if (!pending) {
    try {
      std::string filename = spire_guard_filename_for(c, serial_number);
      if (filename.empty()) {
        pending = spire_guard_serial_has_open_marker(c, serial_number);
      } else if (std::filesystem::is_regular_file(filename)) {
        auto snapshot = spire_guard_load(filename);
        pending = spire_guard_marker_open(
            c->is_spire_protected(), snapshot.run_in_progress, snapshot.restore_pending, snapshot.awaiting_save);
      }
    } catch (const std::exception& e) {
      c->log.warning_f("Kit lobby: cannot read the guard marker for serial {:010}: {}", serial_number, e.what());
      pending = true;
    }
  }
  const char* refused = spire_kit_lobby_refused(c->is_spire_protected(), false, false, pending);
  if (refused) {
    co_return refused;
  }

  try {
    auto executor = co_await asio::this_coro::executor;
    // The guard's restore push (spire_guard_run_restore): full character (30), change it, SetExtendedPlayerInfo, 61
    GetPlayerInfoResult info;
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "full character (30) for $kitlobby", false);
      info = co_await send_get_player_info(c, true);
      spire_run_mag_note_in_game_61(c); // F6 (amendment 3)
    }
    if (!info.is_full_info || !info.character) {
      co_return "$C6Kan niet: geen\nvolledig personage.";
    }
    auto ch = info.character;
    std::string name = ch->disp.visual.name.decode(ch->inventory.language);
    uint32_t level_before = ch->disp.stats.level + 1;
    auto s = c->require_server_state();
    spire_kit_lobby_apply(*ch, s->data->level_table(c->version()), level);
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "answer to the patch preparation for $kitlobby", false);
      co_await prepare_client_for_patches(c);
    }
    auto fn = s->data->client_functions->get("SetExtendedPlayerInfo", c->specific_version);
    PSOGCCharacterFile::Character gc_char = *ch;
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "answer to SetExtendedPlayerInfo (B3) for $kitlobby", false);
      co_await send_function_call(c, fn, {}, &gc_char, sizeof(gc_char));
    }
    c->log.info_f("Kit lobby: {} pushed to Lv{} EXP {} (was Lv{}) for serial {:010}; {}; asking for a 61 to verify",
        name, ch->disp.stats.level + 1, ch->disp.stats.exp.load(), level_before, serial_number,
        spire_guard_materials_str(ch->inventory));
    GetPlayerInfoResult after;
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "verifying 61 for $kitlobby", false);
      after = co_await send_get_player_info(c, false);
      spire_run_mag_note_in_game_61(c); // F6 (amendment 3)
    }
    if (!after.character || (after.character->disp.stats.level != ch->disp.stats.level) ||
        (after.character->disp.stats.exp != ch->disp.stats.exp) ||
        !spire_guard_same_materials(after.character->inventory, ch->inventory)) {
      c->log.warning_f("Kit lobby: {} not verified for serial {:010}: 61 shows {}", name, serial_number,
          after.character ? std::format("Lv{} EXP {}; {}", after.character->disp.stats.level + 1,
                                after.character->disp.stats.exp.load(), spire_guard_materials_str(after.character->inventory))
                          : std::string("no character"));
      co_return "$C6Niet bevestigd:\nlevel of materialen\nanders na de push.";
    }
    c->log.info_f("Kit lobby: {} verified by the 61: Lv{} EXP {}; {}", name, after.character->disp.stats.level + 1,
        after.character->disp.stats.exp.load(), spire_guard_materials_str(after.character->inventory));
    co_return std::format("$C7{} is nu Lv{}.", name, ch->disp.stats.level + 1);
  } catch (const std::exception& e) {
    c->log.warning_f("Kit lobby: push failed for serial {:010}: {}", serial_number, e.what());
    co_return "$C6Push mislukt.";
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
    filename = spire_guard_filename_for(c, serial_number);
    if (filename.empty()) {
      // No 61 yet, so the character on the card is unknown; only a serial without any open marker may save
      if (spire_guard_serial_has_open_marker(c, serial_number)) {
        c->log.warning_f("Spire guard: no login 61 for serial {:010} and a marker is open; no 97", serial_number);
        co_return false;
      }
      co_return true;
    }
    snapshot = spire_guard_load(filename);
    if (snapshot.has_character) {
      auto login_character = c->character_file(false, false);
      if (!login_character || !spire_guard_same_character(login_character->disp, snapshot.disp)) {
        // Cannot happen unless two characters share a file name (hash collision); restoring could cross slots
        c->log.warning_f("Spire guard: login 61 for serial {:010} shows another character than the snapshot {} in {}; no restore, no 97",
            serial_number, spire_guard_describe(snapshot), filename);
        co_return false;
      }
    }
    bool protected_character = c->is_spire_protected();
    if (!spire_guard_marker_open(protected_character, snapshot.run_in_progress, snapshot.restore_pending, snapshot.awaiting_save)) {
      if (protected_character && (snapshot.run_in_progress || snapshot.restore_pending || snapshot.awaiting_save)) {
        c->log.warning_f("Spire guard: blocked character for serial {:010} has an old marker in {} ({}); ignored, no restore",
            serial_number, filename, spire_guard_describe(snapshot));
      }
      co_return true;
    }
    if (snapshot.awaiting_save && !snapshot.run_in_progress && !snapshot.restore_pending) {
      // v10: after a verified lobby restore the card may still hold the run copy until the client saves. v39 (regie):
      // the run copy is no longer told apart by level, EXP or the v12 fingerprint ("EXP never goes down in normal
      // play" fails once a run copy is stronger than the real character); only a 61 that equals the snapshot confirms
      // the restore. Real progress from a normal game since the lobby restore is in the snapshot already
      // (spire_guard_after_98, which since v40 also closes AwaitingSave), so any other 61 may be a run copy and gets
      // the snapshot pushed again.
      auto character = c->character_file(false, false);
      if (character) {
        uint32_t level = character->disp.stats.level;
        uint32_t exp = character->disp.stats.exp;
        bool shows_overlay = snapshot.has_overlay && (level == snapshot.overlay_level) && (exp == snapshot.overlay_exp) &&
            (character->disp.stats.meseta == snapshot.overlay_meseta) &&
            (character->inventory.num_items == snapshot.overlay_num_items);
        bool same_stats = (level == snapshot.disp.stats.level) && (exp == snapshot.disp.stats.exp);
        bool same_inventory = spire_guard_same_inventory(
            character->inventory, character->disp.stats.meseta, snapshot.inventory, snapshot.disp.stats.meseta);
        if (same_stats && same_inventory && !spire_guard_same_materials(character->inventory, snapshot.inventory)) {
          c->log.warning_f("Spire guard: login 61 for serial {:010}: material counters {}, snapshot {}",
              serial_number, spire_guard_materials_str(character->inventory), spire_guard_materials_str(snapshot.inventory));
        }
        if (spire_guard_awaiting_save_login_apply(snapshot, character->inventory, character->disp)) {
          spire_guard_save(filename, serial_number, snapshot);
          c->log.info_f("Spire guard: login 61 for serial {:010} shows Lv{} EXP {}, equal to the snapshot; lobby restore confirmed, marker cleared, no push",
              serial_number, level + 1, exp);
          co_return true;
        }
        c->log.info_f("Spire guard: login 61 for serial {:010} shows Lv{} EXP {} with {} items meseta {}, snapshot Lv{} EXP {} with {} items meseta {}; {}, pushing again",
            serial_number, level + 1, exp, character->inventory.num_items, character->disp.stats.meseta.load(),
            snapshot.disp.stats.level + 1, snapshot.disp.stats.exp.load(), snapshot.inventory.num_items,
            snapshot.disp.stats.meseta.load(),
            shows_overlay ? "the card still holds the run copy (fingerprint)"
                          : (same_stats ? "the items or material counters differ from the snapshot (the card may still hold the run)"
                                        : "not the snapshot (the card may still hold a run copy)"));
      } else {
        snapshot.awaiting_save = false;
        snapshot.restore_pending = true;
        c->log.info_f("Spire guard: no login 61 for serial {:010}; cannot confirm the lobby restore, pushing again", serial_number);
      }
      spire_guard_save(filename, serial_number, snapshot);
    }
    if (snapshot.run_in_progress) {
      // The run ended without a 98. From here on only a verified restore closes it (v7).
      snapshot.run_in_progress = false;
      snapshot.restore_pending = true;
      snapshot.awaiting_save = false;
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

// v47: the restore runs when the client leaves the game, before the lobby join (see SpireFlagBridge.hh)

// v61: 97 with flag 1 makes the client save its character to the memory card (CommandFormats.hh). Measured 2026-10-26
// (lab, cardwatch): the client writes its card at every game join with only the equipped items (Sega's protection
// against a game that is not left properly) and wrote nothing at a server-sent 6x73 exit, so after a Spire run and the
// base the card kept 8 of 23 items until the next login restore. Sent only when the save buffer holds the verified real
// character: after a verified game exit restore and after the base game (pushed and verified before q925).
static void spire_guard_save_to_card(std::shared_ptr<Client> c, uint32_t serial_number, const char* why) {
  if (!c->channel->connected() || c->lobby.lock()) {
    return; // only between the game and the lobby, like the restore itself
  }
  send_command(c, 0x97, 0x01);
  c->log.info_f("Spire guard: 97 (save to memory card) sent for serial {:010} after {} (v61)", serial_number, why);
}

asio::awaitable<bool> spire_guard_restore_at_game_exit(std::shared_ptr<Client> c) {
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  if (!serial_number) {
    co_return true;
  }

  std::string filename;
  SpireGuardSnapshot snapshot;
  try {
    filename = spire_guard_filename_for(c, serial_number);
    if (filename.empty()) {
      co_return true;
    }
    snapshot = spire_guard_load(filename);
  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to read snapshot for serial {:010} after a game: {}; dropping the client (marker kept)",
        serial_number, e.what());
    co_return false;
  }
  if (!spire_guard_marker_open(c->is_spire_protected(), false, snapshot.restore_pending, false)) {
    if (spire_guard_base_game_left.erase(c.get()) && snapshot.awaiting_save && !c->is_spire_protected()) {
      spire_guard_save_to_card(c, serial_number, "the base game");
    }
    co_return true;
  }

  SpireGuardRestoreLock lock(c.get());
  if (!lock.locked) {
    c->log.warning_f("Spire guard: a restore is already running for serial {:010}; dropping the client (marker kept)", serial_number);
    co_return false;
  }
  c->log.info_f("Spire guard: game exit restore for serial {:010} starts before the lobby join", serial_number);
  if (!co_await spire_guard_run_restore(c, serial_number, filename, snapshot, "game exit")) {
    c->log.warning_f("Spire guard: game exit restore for serial {:010} not verified; dropping the client, the restore follows at the next login (marker kept)",
        serial_number);
    co_return false;
  }
  c->log.info_f("Spire guard: restored {} for serial {:010} at the game exit; the lobby join follows; marker stays until the next login confirms it",
      spire_guard_describe(snapshot), serial_number);
  spire_guard_save_to_card(c, serial_number, "the game exit restore");
  co_return true;
}

bool spire_guard_game_join_allowed(std::shared_ptr<Client> c, bool tell_client) {
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  if (!serial_number) {
    return true;
  }
  bool restoring = spire_guard_restoring_clients.count(c.get());
  bool pending = false;
  try {
    std::string filename = spire_guard_filename_for(c, serial_number);
    if (filename.empty()) {
      pending = spire_guard_serial_has_open_marker(c, serial_number);
    } else {
      // v12 (critic K7): a run that never got its 98 counts too
      auto snapshot = spire_guard_load(filename);
      bool protected_character = c->is_spire_protected();
      pending = spire_guard_marker_open(protected_character, snapshot.run_in_progress, snapshot.restore_pending, false);
    }
  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to read snapshot for serial {:010} at a game join: {}", serial_number, e.what());
    pending = true;
  }
  if (!restoring && !pending) {
    return true;
  }
  c->log.info_f("Spire guard: game join refused for serial {:010}: {}", serial_number,
      restoring ? "a restore is running" : "the restore is not verified");
  if (tell_client) {
    send_lobby_message_box(c, restoring ? "$C7Your character is\nstill being restored.\nTry again in a\nmoment." : "$C7Your character is\nrestored at the next\nlogin. Log out and\nin again.");
  }
  return false;
}

// v19: rooms already announced, per game (lobby id) as floor << 16 | room. A game's id is never reused while the server
// runs, so entries of ended games are harmless; the set is cleared when it grows past 4096 entries.
static std::unordered_set<uint64_t> spire_bridge_cleared_rooms;
// v49: the run again ('I') makes every room of the game clear again for the new run
static void spire_bridge_forget_room_clears(uint32_t lobby_id) {
  std::erase_if(spire_bridge_cleared_rooms, [lobby_id](uint64_t key) { return (key >> 32) == lobby_id; });
}
static void spire_checkpoint_record_room(std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, bool boss = false);
static void spire_checkpoint_store(std::shared_ptr<const Lobby> l, const auto& clients, uint8_t k, bool boss = false);
static void spire_replay_note_room_cleared(std::shared_ptr<const Lobby> l, uint8_t floor, uint32_t value);

std::string spire_room_actions_hold(const void* action_stream, size_t action_stream_size, uint16_t room,
    const std::function<bool(uint16_t wave)>& wave_undefeated,
    const std::function<bool(uint32_t event_id)>& event_unfinished) {
  phosg::StringReader r(action_stream, action_stream_size);
  while (!r.eof()) {
    uint8_t opcode = r.get_u8();
    if ((opcode == 0x00) || (opcode == 0x0A) || (opcode == 0x0B)) {
      if (opcode != 0x00) {
        r.skip(2);
      }
    } else if (opcode == 0x08) {
      r.skip(4);
    } else if ((opcode == 0x09) || (opcode == 0x0D)) {
      uint16_t c_room = r.get_u16l();
      uint16_t c_wave = r.get_u16l();
      if ((c_room == room) && wave_undefeated(c_wave)) {
        return std::format("constructs wave {} of the room, not defeated", c_wave);
      }
      if (opcode == 0x0D) {
        break;
      }
    } else if (opcode == 0x0C) {
      uint32_t event_id = r.get_u32l();
      if (event_unfinished(event_id)) {
        return std::format("triggers event {}, not finished", event_id);
      }
    } else {
      break; // 01 stop, or an opcode the 6x76 handler also stops at
    }
  }
  return "";
}

// v19: room clear detection. The wave events of one room on one floor, read from the map the server keeps per game.
// An event holds the room while it is started (0004 or 0002) and not finished (0008). A finished event holds it while
// its post-wave actions point at work in the same room that is not done: a trigger_event (0C) of an unfinished event
// (covers the moment between the client's 6x76 and its 6x67 for the next wave), or a construct_enemies (09/0D) of a
// wave whose enemies are not all defeated.
// treat_finished (v42): W-ids of events the dead-event net counts as finished (flag 0008 not yet set).
// waits_on_trigger (v43): set when the room is held only by a finished event's trigger_event of an event that has not
// started yet (the moment between a wave's 6x76 and the clients' 6x67 for the next one).
static bool spire_bridge_room_is_clear(std::shared_ptr<Lobby> l, Version v, uint8_t floor, uint16_t room,
    std::string* why, const std::unordered_set<size_t>* treat_finished = nullptr, bool* waits_on_trigger = nullptr) {
  auto& ms = l->map_state;
  std::vector<std::shared_ptr<MapState::EventState>> room_evs;
  for (const auto& ev_st : ms->event_states_for_floor(v, floor)) {
    const auto* set_entry = ev_st->super_ev->version(v).set_entry;
    if (set_entry && (set_entry->room == room)) {
      room_evs.emplace_back(ev_st);
    }
  }
  auto finished = [&](const MapState::EventState& ev_st) -> bool {
    return (ev_st.flags & 0x08) || (treat_finished && treat_finished->count(ev_st.w_id));
  };
  bool trigger_held = false;
  auto event_unfinished = [&](uint32_t event_id) -> bool {
    for (const auto& ev_st : room_evs) {
      if ((ev_st->super_ev->version(v).set_entry->event_id == event_id) && !finished(*ev_st)) {
        trigger_held = true;
        return true;
      }
    }
    return false;
  };
  for (const auto& ev_st : room_evs) {
    const auto& ev_ver = ev_st->super_ev->version(v);
    if (!finished(*ev_st)) {
      if (ev_st->flags & 0x06) {
        *why = std::format("W-{:03X} (event {}) is running", ev_st->w_id, ev_ver.set_entry->event_id.load());
        return false;
      }
      continue;
    }
    auto wave_undefeated = [&](uint16_t c_wave) -> bool {
      for (const auto& ene_st : ms->enemy_states_for_floor_room_wave(v, floor, room, c_wave)) {
        if (!ene_st->super_ene->child_index && !(ene_st->set_flags & 8)) {
          return true;
        }
      }
      return false;
    };
    std::string hold = spire_room_actions_hold(
        ev_ver.action_stream, ev_ver.action_stream_size, room, wave_undefeated, event_unfinished);
    if (!hold.empty()) {
      *why = std::format("W-{:03X} {}", ev_st->w_id, hold);
      if (waits_on_trigger) {
        *waits_on_trigger = trigger_held;
      }
      return false;
    }
  }
  return !room_evs.empty();
}

static void spire_dead_event_watch(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room,
    uint16_t wave_number, bool waits_on_trigger);

void spire_bridge_check_room_clear(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, uint16_t wave_number) {
  try {
    if ((c->version() != Version::GC_V3) || !l->is_game() || !l->quest || !l->map_state ||
        !spire_bridge_quest_is_bridged(c, l->quest->meta.quest_number)) {
      return;
    }
    if (auto end = spire_run_end_for(l)) {
      l->log.info_f("Spire bridge: wave {} of room {} on floor {:02X} done after the run ended ({}); no room clear, no checkpoint",
          wave_number, room, floor, end->why);
      return;
    }
    Version v = c->version();
    // A wave with a boss sends nothing: the boss rooms (k4, k8) keep their own clear in the quest
    for (const auto& ene_st : l->map_state->enemy_states_for_floor_room_wave(v, floor, room, wave_number)) {
      if (type_definition_for_enemy(ene_st->super_ene->type).is_boss()) {
        l->log.info_f("Spire bridge: wave {} of room {} on floor {:02X} has a boss; no room clear", wave_number, room,
            floor);
        // v23: a boss wave done still counts as the room's clear for the checkpoint (no text, no r251)
        spire_checkpoint_record_room(l, floor, room, true);
        return;
      }
    }
    // Event rule (spire-server-all): a wave that never started (the elite wave on the normal path) does not hold the
    // room; a started or chained one does until it is finished
    std::string why;
    bool waits_on_trigger = false;
    if (!spire_bridge_room_is_clear(l, v, floor, room, &why, nullptr, &waits_on_trigger)) {
      l->log.info_f("Spire bridge: wave {} of room {} on floor {:02X} done, room not clear: {}", wave_number, room,
          floor, why.empty() ? "no wave events" : why);
      spire_dead_event_watch(c, l, floor, room, wave_number, waits_on_trigger);
      return;
    }

    uint64_t key = (static_cast<uint64_t>(l->lobby_id) << 32) | (static_cast<uint32_t>(floor) << 16) | room;
    if (spire_bridge_cleared_rooms.size() > 4096) {
      spire_bridge_cleared_rooms.clear();
    }
    if (!spire_bridge_cleared_rooms.emplace(key).second) {
      l->log.info_f("Spire bridge: room {} on floor {:02X} already announced in this game; no second text", room, floor);
      return;
    }
    G_SyncQuestRegister_6x77 cmd;
    cmd.header = {0x77, 0x03, 0x0000};
    cmd.register_number = SPIRE_BRIDGE_ROOM_CLEARED_REGISTER;
    cmd.value.as_int = SPIRE_BRIDGE_ROOM_CLEARED_TAG | (static_cast<uint32_t>(floor) << 8) | (room & 0xFF);
    size_t sent = 0;
    for (const auto& lc : l->clients) {
      if (lc) {
        send_command_t(lc, 0x60, 0x00, cmd);
        spire_send_ticker_later(lc, l, SPIRE_BRIDGE_ROOM_CLEAR_TEXT, SPIRE_BRIDGE_ROOM_CLEAR_TEXT_DELAY); // v36: after the r251
        sent++;
      }
    }
    l->log.info_f("Spire bridge: room {} on floor {:02X} cleared in q{}; sent \"{}\" and r{} = {:08X} to {} client(s)",
        room, floor, l->quest->meta.quest_number, SPIRE_BRIDGE_ROOM_CLEAR_TEXT, SPIRE_BRIDGE_ROOM_CLEARED_REGISTER,
        cmd.value.as_int.load(), sent);
    spire_replay_note_room_cleared(l, floor, cmd.value.as_int);
    spire_checkpoint_record_room(l, floor, room);
  } catch (const std::exception& e) {
    l->log.warning_f("Spire bridge: room clear check failed: {}", e.what());
  }
}

// v36: safety net for an enemy nobody ever hit (SpireFlagBridge.hh)
bool spire_wave_held_only_by_unhit(const std::vector<SpireWaveEnemy>& enemies) {
  size_t defeated = 0, held = 0;
  for (const auto& e : enemies) {
    if (e.defeated) {
      defeated++;
    } else if (e.ever_hit || e.is_boss) {
      return false;
    } else {
      held++;
    }
  }
  return defeated && held;
}

static bool spire_enemy_ever_hit(const MapState::EnemyState& ene_st) {
  return (ene_st.server_flags & MapState::EnemyState::Flag::ALL_HITS_MASK) || ene_st.total_damage || ene_st.game_flags;
}

static std::vector<SpireWaveEnemy> spire_wave_enemies(
    std::shared_ptr<Lobby> l, Version v, uint8_t floor, uint16_t room, uint16_t wave_number) {
  std::vector<SpireWaveEnemy> ret;
  for (const auto& ene_st : l->map_state->enemy_states_for_floor_room_wave(v, floor, room, wave_number)) {
    if (ene_st->super_ene->child_index) {
      continue; // as in the wave check: a child counts through its parent
    }
    ret.emplace_back(SpireWaveEnemy{.defeated = !!(ene_st->set_flags & 8), .ever_hit = spire_enemy_ever_hit(*ene_st),
        .is_boss = type_definition_for_enemy(ene_st->super_ene->type).is_boss()});
  }
  return ret;
}

// Per (game, floor, room, wave): the generation of the latest wait; a later defeat in the wave starts a new one
static std::map<std::tuple<uint32_t, uint8_t, uint16_t, uint16_t>, uint64_t> spire_unhit_waits;
static uint64_t spire_unhit_next_generation = 1;

static asio::awaitable<void> spire_unhit_wave_wait(std::weak_ptr<Client> weak_c, std::weak_ptr<Lobby> weak_l,
    uint8_t floor, uint16_t room, uint16_t wave_number, uint64_t generation, SpireWaveCheck recheck) {
  co_await async_sleep(SPIRE_UNHIT_ENEMY_SECONDS);
  auto l = weak_l.lock();
  if (!l || !l->is_game() || !l->map_state) {
    co_return;
  }
  auto key = std::make_tuple(l->lobby_id, floor, room, wave_number);
  auto it = spire_unhit_waits.find(key);
  if ((it == spire_unhit_waits.end()) || (it->second != generation)) {
    co_return; // another enemy of the wave died meanwhile: its own wait runs
  }
  spire_unhit_waits.erase(it);
  auto c = weak_c.lock();
  if (!c || (c->lobby.lock() != l)) {
    c = nullptr;
    for (const auto& lc : l->clients) {
      if (lc && (lc->version() == Version::GC_V3)) {
        c = lc;
        break;
      }
    }
    if (!c) {
      co_return;
    }
  }
  if (spire_run_end_for(l)) {
    co_return;
  }
  Version v = c->version();
  if (!spire_wave_held_only_by_unhit(spire_wave_enemies(l, v, floor, room, wave_number))) {
    l->log.info_f("Spire bridge: wave {} of room {} on floor {:02X} no longer held only by enemies never hit; not counted",
        wave_number, room, floor);
    co_return;
  }
  for (const auto& ene_st : l->map_state->enemy_states_for_floor_room_wave(v, floor, room, wave_number)) {
    if (!ene_st->super_ene->child_index && !(ene_st->set_flags & 8) && !spire_enemy_ever_hit(*ene_st)) {
      ene_st->set_flags |= 8;
      l->log.info_f("Spire bridge: E-{:03X} never hit {} s after the rest of wave {} of room {}; counted as defeated",
          ene_st->e_id, SPIRE_UNHIT_ENEMY_SECONDS.count(), wave_number, room);
    }
  }
  recheck(c, l, floor, room, wave_number, false);
}

void spire_bridge_watch_unhit_wave(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room,
    uint16_t wave_number, SpireWaveCheck recheck) {
  try {
    if ((c->version() != Version::GC_V3) || !l->is_game() || !l->quest || !l->map_state ||
        !spire_bridge_quest_is_bridged(c, l->quest->meta.quest_number) || spire_run_end_for(l)) {
      return;
    }
    if (!spire_wave_held_only_by_unhit(spire_wave_enemies(l, c->version(), floor, room, wave_number))) {
      return;
    }
    if (spire_unhit_waits.size() > 4096) {
      spire_unhit_waits.clear();
    }
    uint64_t generation = spire_unhit_next_generation++;
    spire_unhit_waits[std::make_tuple(l->lobby_id, floor, room, wave_number)] = generation;
    l->log.info_f("Spire bridge: wave {} of room {} on floor {:02X} is held only by enemies never hit; counted as defeated in {} s unless one is hit or another dies",
        wave_number, room, floor, SPIRE_UNHIT_ENEMY_SECONDS.count());
    asio::co_spawn(*c->require_server_state()->io_context,
        spire_unhit_wave_wait(c, l, floor, room, wave_number, generation, recheck), asio::detached);
  } catch (const std::exception& e) {
    l->log.warning_f("Spire bridge: unhit enemy watch failed: {}", e.what());
  }
}

// v42: safety net for a started wave event without a living enemy (SpireFlagBridge.hh)
bool spire_event_without_living_enemy(const std::vector<SpireWaveEnemy>& enemies) {
  for (const auto& e : enemies) {
    if (!e.defeated && (e.ever_hit || e.is_boss)) {
      return false;
    }
  }
  return true;
}

// The W-ids of the room's started, unfinished wave events whose enemies are all defeated or never hit
static std::unordered_set<size_t> spire_dead_room_events(
    std::shared_ptr<Lobby> l, Version v, uint8_t floor, uint16_t room) {
  std::unordered_set<size_t> ret;
  for (const auto& ev_st : l->map_state->event_states_for_floor(v, floor)) {
    const auto* set_entry = ev_st->super_ev->version(v).set_entry;
    if (!set_entry || (set_entry->room != room) || (ev_st->flags & 0x08) || !(ev_st->flags & 0x06)) {
      continue;
    }
    if (spire_event_without_living_enemy(spire_wave_enemies(l, v, floor, room, set_entry->wave_number))) {
      ret.emplace(ev_st->w_id);
    }
  }
  return ret;
}

// Per (game, floor, room): the generation of the running watch; one watch per room
static std::map<std::tuple<uint32_t, uint8_t, uint16_t>, uint64_t> spire_dead_event_waits;
static uint64_t spire_dead_event_next_generation = 1;

static asio::awaitable<void> spire_dead_event_wait(std::weak_ptr<Client> weak_c, std::weak_ptr<Lobby> weak_l,
    uint8_t floor, uint16_t room, uint16_t wave_number, uint64_t generation, std::unordered_set<size_t> seen) {
  for (;;) {
    co_await async_sleep(SPIRE_UNHIT_ENEMY_SECONDS);
    auto l = weak_l.lock();
    if (!l || !l->is_game() || !l->map_state) {
      co_return;
    }
    auto key = std::make_tuple(l->lobby_id, floor, room);
    auto it = spire_dead_event_waits.find(key);
    if ((it == spire_dead_event_waits.end()) || (it->second != generation)) {
      co_return;
    }
    auto c = weak_c.lock();
    if (!c || (c->lobby.lock() != l)) {
      c = nullptr;
      for (const auto& lc : l->clients) {
        if (lc && (lc->version() == Version::GC_V3)) {
          c = lc;
          break;
        }
      }
    }
    if (!c || spire_run_end_for(l)) {
      spire_dead_event_waits.erase(it);
      co_return;
    }
    Version v = c->version();
    std::string why;
    if (spire_bridge_room_is_clear(l, v, floor, room, &why)) {
      spire_dead_event_waits.erase(it); // the room cleared by itself: the wave check announced it
      co_return;
    }
    // Only events without a living enemy at the previous look and still now: at least SPIRE_UNHIT_ENEMY_SECONDS long
    auto now = spire_dead_room_events(l, v, floor, room);
    std::unordered_set<size_t> both;
    for (size_t w_id : now) {
      if (seen.count(w_id)) {
        both.emplace(w_id);
      }
    }
    if (!both.empty() && spire_bridge_room_is_clear(l, v, floor, room, &why, &both)) {
      spire_dead_event_waits.erase(it);
      for (const auto& ev_st : l->map_state->event_states_for_floor(v, floor)) {
        if (!both.count(ev_st->w_id)) {
          continue;
        }
        const auto* set_entry = ev_st->super_ev->version(v).set_entry;
        for (const auto& ene_st : l->map_state->enemy_states_for_floor_room_wave(v, floor, room, set_entry->wave_number)) {
          if (!ene_st->super_ene->child_index) {
            ene_st->set_flags |= 8;
          }
        }
        ev_st->flags |= 0x18; // all enemies killed, post-wave actions run (server's map state only)
        l->log.info_f("Spire bridge: event {} of room {} on floor {:02X} running without a living enemy; counted as finished",
            set_entry->event_id.load(), room, floor);
      }
      spire_bridge_check_room_clear(c, l, floor, room, wave_number);
      co_return;
    }
    if (now.empty()) {
      spire_dead_event_waits.erase(it); // every started event has a living enemy again: the next wave check looks anew
      co_return;
    }
    seen = std::move(now);
  }
}

// v43 (pc 2026-10-25 20:43, q921 room 7): the last wave's check ran before the clients' 6x67 started the chained elite
// wave (event 117), so no started event was dead yet and the watch never ran; the elite never spawned and the room
// stayed open. A room held by a trigger of an event not yet started gets the watch too: its first look (after
// SPIRE_UNHIT_ENEMY_SECONDS) finds the event started or gives up, the second counts it as above.
static void spire_dead_event_watch(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room,
    uint16_t wave_number, bool waits_on_trigger) {
  try {
    auto seen = spire_dead_room_events(l, c->version(), floor, room);
    if (seen.empty() && !waits_on_trigger) {
      return;
    }
    auto key = std::make_tuple(l->lobby_id, floor, room);
    if (spire_dead_event_waits.count(key)) {
      return; // this room's watch runs already
    }
    if (spire_dead_event_waits.size() > 4096) {
      spire_dead_event_waits.clear();
    }
    uint64_t generation = spire_dead_event_next_generation++;
    spire_dead_event_waits[key] = generation;
    if (seen.empty()) {
      l->log.info_f("Spire bridge: room {} on floor {:02X} waits on a chained event; watching it for a wave without a living enemy",
          room, floor);
    } else {
      l->log.info_f("Spire bridge: room {} on floor {:02X} has {} started event(s) without a living enemy; counted as finished after {} s unless one is hit",
          room, floor, seen.size(), SPIRE_UNHIT_ENEMY_SECONDS.count());
    }
    asio::co_spawn(*c->require_server_state()->io_context,
        spire_dead_event_wait(c, l, floor, room, wave_number, generation, std::move(seen)), asio::detached);
  } catch (const std::exception& e) {
    l->log.warning_f("Spire bridge: dead event watch failed: {}", e.what());
  }
}

// The boss types whose 6x76 defeat is the room clear (v25): the last part of a boss Sega's quests never zone-clear.
// Not Dark Falz: its Darvants are children of the Falz root, so their 6x76 is redirected to the root and would clear the
// room on the first Darvant; Dark Falz clears on its final-phase quest flag (spire_bridge_check_dark_falz_flag).
bool spire_bridge_is_server_clear_boss(EnemyType type) {
  return type == EnemyType::VOL_OPT_2;
}

EnemyType spire_bridge_dark_falz_final_phase(Difficulty difficulty, uint16_t flag_num) {
  if (difficulty == Difficulty::NORMAL) {
    return (flag_num == 0x0035) ? EnemyType::DARK_FALZ_2 : EnemyType::NONE;
  }
  return (flag_num == 0x0037) ? EnemyType::DARK_FALZ_3 : EnemyType::NONE;
}

static void spire_bridge_announce_boss_clear(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, EnemyType type);

void spire_bridge_check_boss_defeated(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, EnemyType type) {
  if (spire_bridge_is_server_clear_boss(type)) {
    spire_bridge_announce_boss_clear(c, l, floor, room, type);
  }
}

void spire_bridge_check_dark_falz_flag(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, Difficulty difficulty, uint16_t flag_num) {
  EnemyType type = spire_bridge_dark_falz_final_phase(difficulty, flag_num);
  if ((type == EnemyType::NONE) || (c->version() != Version::GC_V3) || !l->is_game() || !l->quest || !l->map_state) {
    return;
  }
  uint16_t room = 0;
  try {
    auto ene_st = l->map_state->enemy_state_for_floor_type(c->version(), floor, type);
    if (ene_st->alias_target_ene_st) {
      ene_st = ene_st->alias_target_ene_st;
    }
    if (const auto* set_entry = ene_st->super_ene->version(c->version()).set_entry) {
      room = set_entry->room;
    }
  } catch (const std::exception& e) {
    l->log.warning_f("Spire bridge: no Dark Falz on floor {:02X} ({}); room clear for room 0", floor, e.what());
  }
  spire_bridge_announce_boss_clear(c, l, floor, room, type);
}

static void spire_bridge_announce_boss_clear(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, EnemyType type) {
  try {
    if ((c->version() != Version::GC_V3) || !l->is_game() || !l->quest) {
      return;
    }
    uint32_t quest_number = l->quest->meta.quest_number;
    const auto& data = c->require_server_state()->data;
    if (!data->spire_boss_clear_quest_numbers.count(quest_number) || !spire_bridge_quest_is_bridged(c, quest_number)) {
      l->log.info_f("Spire bridge: {} defeated in q{}, which is not in SpireBossClearQuests; no room clear",
          phosg::name_for_enum(type), quest_number);
      return;
    }
    if (auto end = spire_run_end_for(l)) {
      l->log.info_f("Spire bridge: {} defeated (room {} on floor {:02X}) after the run ended ({}); no room clear, no checkpoint",
          phosg::name_for_enum(type), room, floor, end->why);
      return;
    }
    uint64_t key = (static_cast<uint64_t>(l->lobby_id) << 32) | (static_cast<uint32_t>(floor) << 16) | room;
    if (spire_bridge_cleared_rooms.size() > 4096) {
      spire_bridge_cleared_rooms.clear();
    }
    if (!spire_bridge_cleared_rooms.emplace(key).second) {
      l->log.info_f("Spire bridge: boss room {} on floor {:02X} already announced in this game", room, floor);
      return;
    }
    G_SyncQuestRegister_6x77 cmd;
    cmd.header = {0x77, 0x03, 0x0000};
    cmd.register_number = SPIRE_BRIDGE_ROOM_CLEARED_REGISTER;
    cmd.value.as_int = SPIRE_BRIDGE_ROOM_CLEARED_TAG | (static_cast<uint32_t>(floor) << 8) | (room & 0xFF);
    size_t sent = 0;
    for (const auto& lc : l->clients) {
      if (lc) {
        send_command_t(lc, 0x60, 0x00, cmd);
        // v62 (B-REGIE-16-13, critic route-b-8 C5): a boss clear sends no ticker; the quest's 'C' 10 names the reward
        if (*spire_bridge_clear_text(true)) {
          spire_send_ticker_later(lc, l, spire_bridge_clear_text(true), SPIRE_BRIDGE_ROOM_CLEAR_TEXT_DELAY);
        }
        sent++;
      }
    }
    l->log.info_f("Spire bridge: {} defeated (room {} on floor {:02X}) in q{}; sent \"{}\" and r{} = {:08X} to {} client(s)",
        phosg::name_for_enum(type), room, floor, quest_number, spire_bridge_clear_text(true),
        SPIRE_BRIDGE_ROOM_CLEARED_REGISTER, cmd.value.as_int.load(), sent);
    spire_replay_note_room_cleared(l, floor, cmd.value.as_int);
    spire_checkpoint_record_room(l, floor, room, true);
  } catch (const std::exception& e) {
    l->log.warning_f("Spire bridge: boss clear check failed: {}", e.what());
  }
}

// ---- v23: checkpoint per character and replay after a floor load (SpireFlagBridge.hh)

struct SpireCheckpoint {
  uint32_t rooms = 0; // bit k-1 = room k cleared in some run
  uint32_t last_room = 0;
  uint32_t last_quest = 0;
  uint32_t last_lives = 0; // lives at the last clear; 0 = not known
};

static std::string spire_checkpoint_filename(std::shared_ptr<const Client> c, uint32_t serial_number) {
  auto character = c->character_file(false, false);
  if (!character || !serial_number) {
    return "";
  }
  return std::format("{}/{:010}.{}.checkpoint.json", SPIRE_BRIDGE_DIRECTORY, serial_number,
      spire_guard_character_id(character->disp));
}

static SpireCheckpoint spire_checkpoint_load(const std::string& filename) {
  SpireCheckpoint ret;
  if (std::filesystem::is_regular_file(filename)) {
    auto json = phosg::JSON::parse(phosg::load_file(filename));
    ret.rooms = json.get_int("Rooms");
    ret.last_room = json.get_int("LastRoom");
    ret.last_quest = json.get_int("LastQuest");
    ret.last_lives = json.get_int("LastLives", 0);
  }
  return ret;
}

uint8_t spire_checkpoint_room_k(
    const std::map<std::pair<uint8_t, int32_t>, uint8_t>& rooms, uint8_t floor, uint16_t room) {
  auto it = rooms.find(std::make_pair(floor, static_cast<int32_t>(room)));
  if (it == rooms.end()) {
    it = rooms.find(std::make_pair(floor, -1));
  }
  return (it == rooms.end()) ? 0 : it->second;
}

uint8_t spire_checkpoint_room_k(const std::map<uint8_t, std::map<std::pair<uint8_t, int32_t>, uint8_t>>& rooms_by_episode,
    Episode episode, uint8_t floor, uint16_t room) {
  auto it = rooms_by_episode.find(static_cast<uint8_t>(episode));
  return (it == rooms_by_episode.end()) ? 0 : spire_checkpoint_room_k(it->second, floor, room);
}

uint8_t spire_checkpoint_arrival_k(
    const std::map<uint8_t, std::map<uint8_t, uint8_t>>& arrivals_by_episode, Episode episode, uint8_t floor) {
  auto ep_it = arrivals_by_episode.find(static_cast<uint8_t>(episode));
  if (ep_it == arrivals_by_episode.end()) {
    return 0;
  }
  auto it = ep_it->second.find(floor);
  return (it == ep_it->second.end()) ? 0 : it->second;
}

static void spire_checkpoint_record_room(std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, bool boss) {
  uint8_t k = spire_checkpoint_room_k(l->require_server_state()->data->spire_checkpoint_rooms, l->episode, floor, room);
  if (!k) {
    l->log.info_f("Spire checkpoint: room {} on floor {:02X} (episode {}) is not in SpireCheckpointRooms", room, floor,
        static_cast<int>(l->episode));
    return;
  }
  spire_checkpoint_store(l, l->clients, k, boss);
}

// Route B F2: pays room k to the client's records (SpireShopPayout, spire_records_credit_room), after the checkpoint
// file (as the shop's BUY: the other file first, records.json second). Only a run that is no practice run and has a run
// id ('B' came); a failure here is caught up at 'F' from the rooms of this game. Never throws.
static void spire_checkpoint_credit(std::shared_ptr<const Lobby> l, std::shared_ptr<Client> lc, uint32_t serial_number,
    const std::optional<SpireShopPayout>& payout, uint8_t k, bool boss) {
  try {
    if (!payout) {
      return;
    }
    auto* cr = spire_client_run_for(lc, l, false);
    const auto* game_run = spire_game_run_for(l);
    if (!cr || !cr->began || !game_run) {
      lc->log.info_f("Spire splinters: room {} not paid: no run started in this game", k);
      return;
    }
    std::string filename = spire_records_filename(lc, serial_number);
    if (filename.empty()) {
      return;
    }
    uint32_t added = spire_records_credit_room_file(filename, serial_number, *payout, game_run->run_id, k, boss,
        cr->practice);
    if (added) {
      lc->log.info_f("Spire splinters: room {}{} of run {:08X} pays {} for serial {:010}", k, boss ? " (boss)" : "",
          game_run->run_id, added, serial_number);
    }
  } catch (const std::exception& e) {
    lc->log.warning_f("Spire splinters: failed to pay room {}: {}", k, e.what());
  }
}

static void spire_checkpoint_store(std::shared_ptr<const Lobby> l, const auto& clients, uint8_t k, bool boss) {
  // Route B F2: the Payout block, read at every store (SpireMeta.hh); unreadable = no payout now ('F' catches up)
  std::optional<SpireShopPayout> payout;
  try {
    payout = spire_shop_parse(phosg::load_file(SPIRE_SHOP_CATALOGUE_FILE)).payout;
  } catch (const std::exception& e) {
    l->log.warning_f("Spire splinters: {} unreadable ({}); room {} not paid now", SPIRE_SHOP_CATALOGUE_FILE, e.what(), k);
  }
  for (const auto& lc : clients) {
    if (!lc || (lc->version() != Version::GC_V3)) {
      continue;
    }
    try {
      // v48: the rooms of this run in this game, for the splinters at 'F' (before the file, which keeps every run)
      auto& run_rooms = spire_run_rooms_for(lc, l->lobby_id);
      bool new_room = run_rooms.rooms.emplace(k).second;
      bool new_boss = boss && run_rooms.boss_rooms.emplace(k).second;
      uint32_t serial_number = spire_bridge_serial_for_client(lc);
      std::string filename = spire_checkpoint_filename(lc, serial_number);
      if (filename.empty()) {
        lc->log.info_f("Spire checkpoint: no character data; room {} not stored", k);
        continue;
      }
      auto cp = spire_checkpoint_load(filename);
      // A room already stored (a respawn reloads its floor, an arrival repeats) never moves the last room back
      bool known = (cp.rooms >> (k - 1)) & 1;
      if (known && (k < cp.last_room)) {
        if (new_room || new_boss) {
          spire_checkpoint_credit(l, lc, serial_number, payout, k, new_boss);
        }
        continue;
      }
      cp.rooms |= (1u << (k - 1));
      cp.last_room = k;
      cp.last_quest = l->quest->meta.quest_number;
      cp.last_lives = spire_checkpoint_lives_for(lc.get());
      auto json = phosg::JSON::dict({
          {"SerialNumber", serial_number},
          {"Rooms", cp.rooms},
          {"LastRoom", cp.last_room},
          {"LastQuest", cp.last_quest},
          {"LastLives", cp.last_lives},
      });
      spire_bridge_write_durable(filename, json.serialize(phosg::JSON::SerializeOption::FORMAT));
      lc->log.info_f("Spire checkpoint: room {} cleared in q{}; rooms {:08X} in {}", k, cp.last_quest, cp.rooms,
          filename);
      if (new_room || new_boss) {
        spire_checkpoint_credit(l, lc, serial_number, payout, k, new_boss);
      }
    } catch (const std::exception& e) {
      lc->log.warning_f("Spire checkpoint: failed to store room {}: {}", k, e.what());
    }
  }
}

uint32_t spire_checkpoint_mask_for_client(std::shared_ptr<const Client> c) {
  try {
    if (c->version() != Version::GC_V3) {
      return 0;
    }
    std::string filename = spire_checkpoint_filename(c, spire_bridge_serial_for_client(c));
    return filename.empty() ? 0 : spire_checkpoint_load(filename).rooms;
  } catch (const std::exception& e) {
    c->log.warning_f("Spire checkpoint: failed to read: {}", e.what());
    return 0;
  }
}

// Per game: register -> client id -> latest value, and floor -> latest r251. The weak_ptr tells a reused Lobby
// address from the game that wrote the state.
struct SpireReplayState {
  std::weak_ptr<const Lobby> game;
  std::map<uint8_t, std::map<uint8_t, uint32_t>> registers;
  std::map<uint8_t, uint32_t> room_cleared;
};
static std::unordered_map<const Lobby*, SpireReplayState> spire_replay_states;

static SpireReplayState* spire_replay_state_for(std::shared_ptr<const Lobby> l, bool create) {
  auto it = spire_replay_states.find(l.get());
  if ((it != spire_replay_states.end()) && (it->second.game.lock() != l)) {
    spire_replay_states.erase(it);
    it = spire_replay_states.end();
  }
  if (it == spire_replay_states.end()) {
    if (!create) {
      return nullptr;
    }
    if (spire_replay_states.size() > 256) {
      std::erase_if(spire_replay_states, [](const auto& e) { return e.second.game.expired(); });
    }
    it = spire_replay_states.emplace(l.get(), SpireReplayState{.game = l, .registers = {}, .room_cleared = {}}).first;
  }
  return &it->second;
}

// v49: the run again ('I'): no register or room clear of the earlier run is replayed in the new one
static void spire_replay_forget(std::shared_ptr<const Lobby> l) {
  spire_replay_states.erase(l.get());
}

static bool spire_replay_applies(std::shared_ptr<const Client> c, std::shared_ptr<const Lobby> l) {
  return (c->version() == Version::GC_V3) && l->is_game() && l->quest &&
      spire_bridge_quest_is_bridged(c, l->quest->meta.quest_number);
}

void spire_replay_note_register(
    std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l, uint8_t register_number, uint32_t value) {
  try {
    const auto& regs = c->require_server_state()->data->spire_replay_registers;
    if (!regs.contains(register_number) || !spire_replay_applies(c, l)) {
      return;
    }
    spire_replay_state_for(l, true)->registers[register_number][c->lobby_client_id] = value;
  } catch (const std::exception& e) {
    c->log.warning_f("Spire replay: failed to note r{}: {}", register_number, e.what());
  }
}

static void spire_replay_note_room_cleared(std::shared_ptr<const Lobby> l, uint8_t floor, uint32_t value) {
  spire_replay_state_for(l, true)->room_cleared[floor] = value;
}

static asio::awaitable<void> spire_replay_send(
    std::weak_ptr<Client> weak_c, std::weak_ptr<const Lobby> weak_l, uint32_t floor, size_t attempt) {
  co_await async_sleep(attempt ? SPIRE_REPLAY_SECOND_DELAY : SPIRE_REPLAY_DELAY);
  auto c = weak_c.lock();
  auto l = weak_l.lock();
  if (!c || !l || (c->lobby.lock() != l) || (c->floor != floor)) {
    co_return; // left the game, or another floor load is under way (that one replays again)
  }
  auto* st = spire_replay_state_for(l, false);
  if (!st) {
    co_return;
  }
  size_t sent = 0;
  auto send_reg = [&](uint8_t reg, uint32_t value) {
    G_SyncQuestRegister_6x77 cmd;
    cmd.header = {0x77, 0x03, 0x0000};
    cmd.register_number = reg;
    cmd.value.as_int = value;
    send_command_t(c, 0x60, 0x00, cmd);
    c->log.info_f("Spire replay: r{} = {:08X} after the load of floor {:02X}", reg, value, floor);
    sent++;
  };
  for (const auto& [reg, by_client] : st->registers) {
    // A register only one other client writes: its value. Written by this client itself: nothing (it has its own).
    for (const auto& [client_id, value] : by_client) {
      if (client_id != c->lobby_client_id) {
        send_reg(reg, value);
      }
    }
  }
  auto rc = st->room_cleared.find(floor);
  if (rc != st->room_cleared.end()) {
    send_reg(SPIRE_BRIDGE_ROOM_CLEARED_REGISTER, rc->second);
  }
  if (sent) {
    l->log.info_f("Spire replay: {} register(s) to client {} after its load of floor {:02X} (send {})", sent,
        c->lobby_client_id, floor, attempt + 1);
  }
  if (attempt == 0) {
    // 6x1F can come before the load is done; the values are states, so a second send changes nothing that arrived
    co_await spire_replay_send(weak_c, weak_l, floor, 1);
  }
}

bool spire_follow_partner_pending(uint32_t partner_floor, uint32_t leader_floor, bool partner_loading) {
  return partner_loading && spire_follow_should_warp(partner_floor, leader_floor, false);
}

bool spire_follow_should_warp(uint32_t partner_floor, uint32_t leader_floor, bool partner_loading) {
  // never to the city (B-E01-41), never from the city (v29), never when already there, never a client that is still
  // loading (audit S-12): a 6x94 mid-load leaves the partner between two floors
  return !partner_loading && (leader_floor != SPIRE_FOLLOW_CITY_FLOOR) && (partner_floor != SPIRE_FOLLOW_CITY_FLOOR) &&
      (partner_floor != leader_floor);
}

static asio::awaitable<void> spire_follow_leader_warp(
    std::weak_ptr<Client> weak_leader, std::weak_ptr<Lobby> weak_l, uint32_t floor, uint32_t delay_ms) {
  co_await async_sleep(std::chrono::milliseconds(delay_ms));
  auto leader = weak_leader.lock();
  auto l = weak_l.lock();
  if (!leader || !l || (leader->lobby.lock() != l) || (leader->floor != floor)) {
    co_return; // the leader left the game or moved on; its next floor load warps again
  }
  for (const auto& lc : l->clients) {
    if (!lc || (lc == leader) || (lc->floor == floor)) {
      continue;
    }
    bool loading = lc->check_flag(Client::Flag::LOADING);
    if (spire_follow_partner_pending(lc->floor, floor, loading)) {
      // Remembered, so the partner's own floor load (spire_follow_leader) warps it to the leader's floor afterwards
      spire_follow_pending.insert(lc.get());
      l->log.info_f("Spire follow: client {} is still loading; not warped to the leader's floor {:02X} (follows after its own floor load)",
          lc->lobby_client_id, floor);
      continue;
    }
    if (!spire_follow_should_warp(lc->floor, floor, loading)) {
      // v29 (regie 2026-10-24): a partner still on Pioneer 2 takes the Ragol warp itself, as in a Sega quest; the
      // server only follows room transitions during the run
      l->log.info_f("Spire follow: client {} is in the city; not warped to the leader's floor {:02X} (takes the Ragol warp itself)",
          lc->lobby_client_id, floor);
      continue;
    }
    l->log.info_f("Spire follow: client {} on floor {:02X} warped to the leader's floor {:02X}", lc->lobby_client_id,
        lc->floor, floor);
    send_warp(lc, floor, true);
  }
}

static void spire_follow_leader(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l) {
  const auto& follow = c->require_server_state()->data->spire_follow_leader;
  if (follow && (c->lobby_client_id != follow->first) && spire_follow_pending.count(c.get())) {
    // A partner skipped mid-load reports a floor: once it is loaded, follow the leader to its current floor, with the
    // same checks. While it is still loading it stays pending.
    if (c->check_flag(Client::Flag::LOADING)) {
      return;
    }
    spire_follow_pending.erase(c.get());
    auto leader = (follow->first < l->clients.size()) ? l->clients[follow->first] : nullptr;
    if (leader && spire_follow_should_warp(c->floor, leader->floor)) {
      auto s = c->require_server_state();
      asio::co_spawn(*s->io_context, spire_follow_leader_warp(leader, l, leader->floor, follow->second), asio::detached);
    }
    return;
  }
  if (!follow || (c->lobby_client_id != follow->first) || (c->floor == 0)) {
    return;
  }
  auto s = c->require_server_state();
  asio::co_spawn(*s->io_context, spire_follow_leader_warp(c, l, c->floor, follow->second), asio::detached);
}

void spire_replay_after_floor_load(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l) {
  try {
    spire_overview_floor_changed(c); // v62: a floor change ends the kept overview line
    if (!spire_replay_applies(c, l)) {
      return;
    }
    spire_follow_leader(c, l);
    uint8_t arrival_k = spire_checkpoint_arrival_k(
        c->require_server_state()->data->spire_checkpoint_arrivals, l->episode, c->floor);
    if (arrival_k) {
      if (auto end = spire_run_end_for(l)) {
        c->log.info_f("Spire checkpoint: arrival on floor {:02X} after the run ended ({}); room {} not stored", c->floor,
            end->why, arrival_k);
      } else {
        std::array<std::shared_ptr<Client>, 1> just_c{c}; // each arriving client for its own character
        spire_checkpoint_store(l, just_c, arrival_k);
      }
    }
    if (!spire_replay_state_for(l, false)) {
      return;
    }
    auto s = c->require_server_state();
    asio::co_spawn(*s->io_context, spire_replay_send(c, l, c->floor, 0), asio::detached);
  } catch (const std::exception& e) {
    c->log.warning_f("Spire replay: failed to schedule: {}", e.what());
  }
}

// v35: one line each, so the answer is no window when the command is typed during a run
std::string spire_checkpoint_command(std::shared_ptr<Client> c, const std::string& args) {
  std::string arg = args;
  phosg::strip_whitespace(arg);
  std::string filename = spire_checkpoint_filename(c, spire_bridge_serial_for_client(c));
  if (filename.empty()) {
    throw std::runtime_error("No character data");
  }
  auto cp = spire_checkpoint_load(filename);
  if (arg.empty() || (arg == "status")) {
    std::string rooms;
    for (size_t k = 1; k <= 31; k++) {
      if ((cp.rooms >> (k - 1)) & 1) {
        rooms += (rooms.empty() ? "" : " ") + std::to_string(k);
      }
    }
    return cp.rooms ? std::format("Rooms cleared: {} - last: room {}", rooms, cp.last_room)
                    : std::string("No rooms cleared yet");
  }
  if (arg == "continue") {
    // v44: a savestate names the exact room quest and what the run had at its start
    auto ss = spire_savestate_load_logged(c);
    if (ss.exists) {
      return spire_savestate_continue_text(ss);
    }
    if (!cp.last_room) {
      return "No checkpoint yet: start at room 1";
    }
    return cp.last_lives ? std::format("Continue at room {} (you had {} lives)", cp.last_room + 1, cp.last_lives)
                         : std::format("Continue at room {}", cp.last_room + 1);
  }
  throw std::runtime_error("Use: $spire status or $spire continue");
}

void spire_savestate_notice_on_game_join(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  try {
    if (c->spire_savestate_told || !l || !l->is_game() || !spire_bridge_serial_for_client(c)) {
      return;
    }
    SpireSavestate ss = spire_savestate_load_logged(c); // unreadable: logged there, read as none
    if (!ss.exists) {
      return;
    }
    // the room quest by the name the quest build gives it, of the savestate's episode, for this client's version
    std::string category_name, quest_name = spire_savestate_room_quest_name(ss);
    auto s = c->require_server_state();
    auto q = (quest_name.empty() || !s->data->quest_index) ? nullptr : s->data->quest_index->get(quest_name);
    if (q && (static_cast<uint8_t>(q->meta.episode) == ss.episode) && q->has_version_any_language(c->version()) &&
        s->data->quest_index->category_index) {
      try {
        auto cat = s->data->quest_index->category_index->at(q->meta.category_id);
        category_name = cat ? cat->name : "";
      } catch (const std::out_of_range&) {
      }
    }
    if (category_name.empty()) {
      quest_name.clear();
    }
    auto lines = spire_savestate_notice_take(&c->spire_savestate_told, ss, category_name, quest_name);
    if (lines.size() != 2) {
      return;
    }
    spire_send_ticker(c, lines[0]);
    spire_send_ticker_later(c, l, lines[1], SPIRE_SAVESTATE_NOTICE_SECOND_DELAY);
    c->log.info_f("Spire savestate: notice at game join ({}): \"{}\" and \"{}\"", spire_savestate_describe(ss),
        lines[0], lines[1]);
  } catch (const std::exception& e) {
    c->log.warning_f("Spire savestate: notice at game join failed: {}", e.what());
  }
}

asio::awaitable<void> spire_bba_tx_guard_if_needed(std::shared_ptr<Client> c) {
  auto s = c->require_server_state();
  if (!s->data->spire_bba_tx_guard_patch) {
    co_return;
  }
  if (!spire_bba_tx_guard_applies(c->specific_version)) {
    c->log.info_f("Spire BBA TX guard: not sent; specific version {} is not {}",
        str_for_specific_version(c->specific_version), str_for_specific_version(SPIRE_BBA_TX_GUARD_SPECIFIC_VERSION));
    co_return;
  }
  if (!c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL) ||
      !c->check_flag(Client::Flag::SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE)) {
    c->log.warning_f("Spire BBA TX guard: not sent; client cannot run functions (B2)");
    co_return;
  }
  std::shared_ptr<const ClientFunctionIndex::Function> fn;
  try {
    fn = s->data->client_functions->get(SPIRE_BBA_TX_GUARD_FUNCTION_NAME, c->specific_version);
  } catch (const std::out_of_range&) {
    c->log.warning_f("Spire BBA TX guard: not sent; no client function {} for specific version {}",
        SPIRE_BBA_TX_GUARD_FUNCTION_NAME, str_for_specific_version(c->specific_version));
    co_return;
  }
  try {
    auto executor = co_await asio::this_coro::executor;
    uint32_t serial_number = spire_bridge_serial_for_client(c);
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "answer to the patch preparation (BBA TX guard)", true);
      co_await prepare_client_for_patches(c);
    }
    C_ExecuteCodeResult_B3 result;
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "answer to SpireBBATxGuard (B3)", true);
      result = co_await send_function_call(c, fn);
    }
    c->log.info_f("Spire BBA TX guard: client answered {} ({}) for the 8 words at {:08X}",
        result.return_value.load(), spire_bba_tx_guard_describe(result.return_value), SPIRE_BBA_TX_GUARD_ADDRESS);
  } catch (const std::exception& e) {
    // The guard is a safety net for the client, never a reason to keep it out: log and let the login go on
    c->log.warning_f("Spire BBA TX guard: failed to send the patch: {}", e.what());
  }
}


bool spire_game_is_spire(std::shared_ptr<const Client> c, std::shared_ptr<const Lobby> l) {
  if (!l || !l->is_game() || !l->quest) {
    return false;
  }
  uint32_t quest_number = l->quest->meta.quest_number;
  return spire_guard_quest_is_guarded(c, quest_number) || spire_bridge_quest_is_bridged(c, quest_number);
}

bool spire_pvp_tech_is_support(uint8_t technique_number) {
  // Tech ids of StaticGameData.cc tech_id_to_name: 10 Deband, 13 Shifta, 14 Ryuker, 15 Resta, 16 Anti, 17 Reverser.
  // Everything else (Foie through Razonde, Grants, Jellen, Zalure, Megid, unknown ids) counts as an attack.
  switch (technique_number) {
    case 10:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
      return true;
    default:
      return false;
  }
}

static size_t spire_pvp_remove_player_targets(std::vector<TargetEntry>& targets) {
  size_t before = targets.size();
  std::erase_if(targets, [](const TargetEntry& t) { return t.entity_id < 0x1000; });
  return before - targets.size();
}

template <typename HeaderT>
static size_t spire_pvp_strip_t(HeaderT& header, std::vector<TargetEntry>& targets) {
  size_t removed = spire_pvp_remove_player_targets(targets);
  if (removed) {
    header.target_count = targets.size();
    header.header.size = (sizeof(HeaderT) + sizeof(TargetEntry) * targets.size()) / 4;
  }
  return removed;
}

size_t spire_pvp_strip_player_targets(G_AttackFinished_Header_6x46& header, std::vector<TargetEntry>& targets) {
  return spire_pvp_strip_t(header, targets);
}

size_t spire_pvp_strip_player_targets(G_CastTechnique_Header_6x47& header, std::vector<TargetEntry>& targets) {
  if (spire_pvp_tech_is_support(header.technique_number)) {
    return 0;
  }
  return spire_pvp_strip_t(header, targets);
}

size_t spire_pvp_strip_player_targets(G_ExecutePhotonBlast_Header_6x49& header, std::vector<TargetEntry>& targets) {
  return spire_pvp_strip_t(header, targets);
}

void spire_pvp_log_strip(std::shared_ptr<const Client> c, std::shared_ptr<Lobby> l, uint8_t subcommand, size_t count) {
  if (!count || !c || !l || (c->lobby_client_id >= 16)) {
    return;
  }
  uint16_t bit = 1 << c->lobby_client_id;
  if (l->spire_pvp_logged_clients & bit) {
    return;
  }
  l->spire_pvp_logged_clients |= bit;
  l->log.info_f("Spire PvP: stripped {} player target(s) from 6x{:02X} of C-{:X} (no player damage in a Spire run)",
      count, subcommand, c->id);
}
