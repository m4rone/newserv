#include "SpireFlagBridge.hh"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <unordered_set>
#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <phosg/Strings.hh>
#include <utility>
#include <fcntl.h>
#include <unistd.h>

#include "AsyncUtils.hh"
#include "InventoryBackup.hh"
#include "CommandFormats.hh"
#include "SendCommands.hh"
#include "SpireBBATxGuard.hh"
#include "SpireDraw.hh"
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

static SpireRunEnd& spire_run_mark_ended(std::shared_ptr<const Lobby> l, const char* why) {
  if (spire_run_ends.size() > 4096) {
    spire_run_ends.clear();
  }
  auto [it, added] = spire_run_ends.try_emplace(l->lobby_id);
  if (added) {
    it->second.why = why;
    l->log.info_f("Spire bridge: run over in this game ({}); no room clear or checkpoint from now on", why);
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
    spire_run_mark_ended(l, (cause == SPIRE_BRIDGE_TIMER_TIME_UP) ? "run timer: time up" : "run timer: player left");
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
  spire_run_timers[l.get()] = t;
  spire_run_forget_end(l->lobby_id, "run timer start", c->log);
  c->log.info_f("Spire bridge: run timer for serial {:010} in q{}: limit {} s, warning {} s before, {} client(s)",
      serial_number, quest_number, t.limit_s, t.warn_before_s, t.clients_at_start);
  auto s = c->require_server_state();
  asio::co_spawn(*s->io_context, spire_run_timer_tick(l, l.get(), t.generation), asio::detached);
}

// v20: the draw inputs a client sent to SPIRE_BRIDGE_DRAW_DATA_REGISTER since its last request.
static std::unordered_map<const Client*, std::vector<uint32_t>> spire_draw_inputs;

static void spire_draw_answer(std::shared_ptr<Client> c, uint32_t serial_number,
    uint32_t quest_number, uint32_t value) {
  std::vector<uint32_t> values = std::move(spire_draw_inputs[c.get()]);
  spire_draw_inputs.erase(c.get());
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
  uint32_t drawn = spire_draw(table, in, [](uint32_t n) -> uint32_t {
    return n ? (phosg::random_object<uint32_t>() % n) : 0;
  });

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
}

// v17: the text for a short-message code (SPIRE_BRIDGE_MSG_*), the words of the quest's window_msg (spire.json
// texts; <color N> as $CN, <color 0> as $C7); empty if the value is no message code. v35: one line each, the form of
// "Waiting for your partner..." (no window during a run, see spire_ticker_line); the run-over text says where the
// server sends the player (spire_run_over).
std::string spire_bridge_message_text(uint32_t value) {
  uint8_t kind = value >> 24;
  uint32_t a = (value >> 8) & 0xFF, b = value & 0xFF;
  switch (kind) {
    case SPIRE_BRIDGE_MSG_WAVE:
      return std::format("Wave {}/{}", a, b);
    case SPIRE_BRIDGE_MSG_LIFE:
      return a ? std::string("Last life: fall again and you stay down until the clear.")
               : std::format("$C1Life lost$C7 - {} left. You get up on the spot.", b);
    case SPIRE_BRIDGE_MSG_TIME:
      return std::format("$C4{} minutes left$C7 - the timer ends the run.", value & 0xFFFF);
    case SPIRE_BRIDGE_MSG_PARTNER:
      return b ? "$C4Your partner is waiting.$C7 Open the quest board and make your choice." : "Waiting for your partner...";
    case SPIRE_BRIDGE_MSG_RUN_OVER:
      return std::format("Run over - back to the lobby. Rooms cleared: {}, {} s.", (value >> 16) & 0xFF, value & 0xFFFF);
    case SPIRE_BRIDGE_MSG_PAUSE_B:
      return b ? "Reward taken. Quest Board: Shop (optional) or Ready = leave room."
               : "Reward taken. Quest Board: Shop (optional) or Continue = leave room.";
    case SPIRE_BRIDGE_MSG_DOWN:
      return b ? "You are down - you get up when the room is clear." : "You are back on your feet.";
    case SPIRE_BRIDGE_MSG_HINT:
      return "Close the board and take a step.";
    case SPIRE_BRIDGE_MSG_PROMPT:
      switch (a) {
        case 1:
          return "Take your reward: Quest Board, slot Reward.";
        case 2:
          return "Take a step: your reward pages open.";
        case 6:
          return "The way is open. Walk to a warp.";
        case 7:
          return "Partner has not taken a reward. The way is open.";
        case 8:
          return "Partner is in the shop. The warp waits for him.";
        case 9: // v38: the low byte is 1 on the leader, 0 on the follower; same text
          return "Waiting for your partner at Forest 1.";
        default:
          return ""; // 3-5 retired (spire_bridge_handle_register takes them silently)
      }
    case SPIRE_BRIDGE_MSG_APPROACH:
      switch (a) {
        case 1:
          return std::format("Room {} - Elite (harder, better reward)", b);
        case 2:
          return std::format("Room {} - Rest", b);
        default:
          return std::format("Room {}", b);
      }
    default:
      return "";
  }
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

void spire_checkpoint_run_started(std::shared_ptr<const Client> c) {
  spire_follow_pending.erase(c.get());
  if (auto l = c->lobby.lock()) {
    spire_run_forget_end(l->lobby_id, "quest start", c->log);
  }
  if (spire_checkpoint_lives_forget(c.get())) {
    c->log.info_f("Spire checkpoint: lives of the previous run forgotten at the quest start");
  }
}

// v18: records per character (SPIRE_BRIDGE_REC_*). The character comes from the server's character data (the 61 at
// login), never the Battle overlay, so each character on a card has its own records (E04 AC-6).

struct SpireRecords {
  uint32_t best_score = 0;
  uint32_t best_time = 0; // seconds; 0 = no won run yet
  uint32_t runs = 0;
  uint32_t won = 0;
};
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

static SpireRecords spire_records_load(const std::string& filename) {
  SpireRecords ret;
  if (std::filesystem::is_regular_file(filename)) {
    auto json = phosg::JSON::parse(phosg::load_file(filename));
    ret.best_score = json.get_int("BestScore");
    ret.best_time = json.get_int("BestTime");
    ret.runs = json.get_int("Runs");
    ret.won = json.get_int("Won");
  }
  return ret;
}

static void spire_records_save(const std::string& filename, uint32_t serial_number, const SpireRecords& rec) {
  auto json = phosg::JSON::dict({
      {"SerialNumber", serial_number},
      {"BestScore", rec.best_score},
      {"BestTime", rec.best_time},
      {"Runs", rec.runs},
      {"Won", rec.won},
  });
  spire_bridge_write_durable(filename, json.serialize(phosg::JSON::SerializeOption::FORMAT));
}

static std::string spire_records_board_text(const SpireRecords& rec) {
  std::string time = rec.best_time
      ? std::format("{} min {} s", rec.best_time / 60, rec.best_time % 60)
      : std::string("none yet");
  return std::format("Best score: {} - best time: {} - runs: {}, won: {}", rec.best_score, time, rec.runs, rec.won);
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
    rec.runs++;
    spire_records_save(filename, serial_number, rec);
    c->log.info_f("Spire records: run started, {} run(s) in {}", rec.runs, filename);
    return true;
  }
  if (kind == SPIRE_BRIDGE_REC_BOARD) {
    spire_send_ticker(c, spire_records_board_text(rec)); // asked for on the quest board, during the run
    c->log.info_f("Spire records: board for {}", filename);
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
  bool new_score = score > rec.best_score;
  bool new_time = is_won && (!rec.best_time || (seconds < rec.best_time));
  if (new_score) {
    rec.best_score = score;
  }
  if (is_won) {
    rec.won++;
  }
  if (new_time) {
    rec.best_time = seconds;
  }
  spire_records_save(filename, serial_number, rec);
  spire_run_mark_ended(l, "records: run finished");
  std::string text = new_score ? std::format("New best score: {}!", rec.best_score)
                               : std::format("Best score: {}.", rec.best_score);
  if (new_time) {
    text += std::format("\nNew best time: {} s!", seconds);
  }
  // v24: unlocks and payout flags for the character (SpireMeta), on the game's difficulty
  text += spire_meta_on_finish(c, l, serial_number, value, new_time);
  text += "\nRecords saved.";
  // v35: never a window in the run (Marck, pc 2026-10-25: "New best score" had to be pressed away mid-game); the
  // records text waits for the lobby (spire_deliver_lobby_texts)
  spire_defer_to_lobby(c, text);
  c->log.info_f("Spire records: run finished (score {}, {} s, {}) for serial {:010} in q{}; {}: best {} / {} s, {} run(s), {} won",
      score, seconds, is_won ? "won" : "not won", serial_number, quest_number, filename, rec.best_score,
      rec.best_time, rec.runs, rec.won);
  return true;
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
      if ((value >> 24) == SPIRE_BRIDGE_DRAW_REQUEST) {
        spire_draw_answer(c, serial_number, quest_number, value);
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
      std::string msg = spire_bridge_message_text(value);
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

static SpireGuardSnapshot spire_guard_load(const std::string& filename) {
  SpireGuardSnapshot ret;
  if (!std::filesystem::is_regular_file(filename)) {
    return ret;
  }
  auto json = phosg::JSON::parse(phosg::load_file(filename));
  ret.run_in_progress = json.get_bool("RunInProgress", false);
  ret.restore_pending = json.get_bool("RestorePending", false);
  ret.awaiting_save = json.get_bool("AwaitingSave", false);
  if (json.contains("Overlay")) {
    const auto& overlay = json.at("Overlay");
    ret.has_overlay = true;
    ret.overlay_level = overlay.get_int("Level");
    ret.overlay_exp = overlay.get_int("EXP");
    ret.overlay_meseta = overlay.get_int("Meseta");
    ret.overlay_num_items = overlay.get_int("NumItems");
  }
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

bool spire_guard_follow_real_progress(
    SpireGuardSnapshot& snapshot, const PlayerInventory& inventory, const PlayerDispDataV4& disp) {
  if (!snapshot.awaiting_save || snapshot.run_in_progress || snapshot.restore_pending || !snapshot.has_character) {
    return false;
  }
  snapshot.inventory = inventory;
  snapshot.disp = disp;
  // v40: the client writes its card when it leaves the game, so the card holds the real character from now on
  snapshot.awaiting_save = false;
  snapshot.has_overlay = false;
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

void spire_guard_end_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  spire_guard_follow_pending.erase(c.get());
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
      if (snapshot.awaiting_save && l && l->is_game() && ((l->mode == GameMode::NORMAL) || (l->mode == GameMode::SOLO))) {
        spire_guard_follow_pending.emplace(c.get());
      }
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

void spire_guard_after_98(std::shared_ptr<Client> c) {
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
// v28: a lobby restore asks this often before it leaves the restore to the next lobby join or login
static constexpr size_t SPIRE_GUARD_LOBBY_ATTEMPTS = 3;
static constexpr auto SPIRE_GUARD_LOBBY_RETRY_DELAY = std::chrono::seconds(5);

// If *done is still false when the timeout expires, cancels the pending promises (Client::cancel_pending_promises),
// so the waiting restore throws and the caller never sends 97. At login (drop) the client is also dropped: without a 97
// the login cannot go on. In the lobby (v28) the connection stays: the lobby restore asks again, and the marker keeps
// the restore pending for the next lobby join or login (measured 2026-10-24 15:56: a drop left Kamako in a dimmed
// lobby that could not log in again).
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
// but becomes AwaitingSave: the card may still hold the overlay until the client saves.
static bool spire_guard_verify_restore(std::shared_ptr<Client> c, uint32_t serial_number, const std::string& filename,
    SpireGuardSnapshot& snapshot, std::shared_ptr<const PSOBBCharacterFile> character, bool in_lobby) {
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
  snapshot.restore_pending = false;
  snapshot.awaiting_save = in_lobby;
  snapshot.has_overlay = in_lobby && snapshot.has_overlay;
  spire_guard_save(filename, serial_number, snapshot);
  c->log.info_f("Spire guard: restore verified for serial {:010}: 61 shows Lv{} EXP {}; {}; {}",
      serial_number, level + 1, exp, items_str,
      in_lobby ? "marker kept until a login 61 shows the same" : "marker cleared");
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
  bool drop = (std::string(where) != "lobby");
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

    // Same function call as $loadchar
    {
      // v12 (critic K12): this can wait for the client too
      SpireGuardWatchdog watchdog(executor, c, serial_number, "answer to the patch preparation", drop);
      co_await prepare_client_for_patches(c);
    }
    auto s = c->require_server_state();
    auto fn = s->data->client_functions->get("SetExtendedPlayerInfo", c->specific_version);
    PSOGCCharacterFile::Character gc_char = *merged;
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "answer to SetExtendedPlayerInfo (B3)", drop);
      co_await send_function_call(c, fn, {}, &gc_char, sizeof(gc_char));
    }
    c->log.info_f("Spire guard: sent {} for serial {:010} (was Lv{}); asking for a 61 to verify",
        spire_guard_describe(snapshot), serial_number, level_before + 1);

    // Only this 61, requested after the push, can verify the restore (v8): no earlier 61 and no 98 counts. on_61_98
    // also puts it into the server's character data.
    GetPlayerInfoResult after;
    {
      SpireGuardWatchdog watchdog(executor, c, serial_number, "verifying 61", drop);
      after = co_await send_get_player_info(c, false);
    }
    if (!spire_guard_verify_restore(c, serial_number, filename, snapshot, after.character, !strcmp(where, "lobby"))) {
      co_return false;
    }
    co_return true;

  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to restore character for serial {:010}: {}; marker kept", serial_number, e.what());
    co_return false;
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

void spire_guard_lobby_loaded(std::shared_ptr<Client> c) {
  if (c->spire_lobby_gate.on_lobby_loaded() && c->spire_lobby_settle_promise && !c->spire_lobby_settle_promise->done()) {
    c->spire_lobby_settle_promise->set_value();
  }
}

static asio::awaitable<void> spire_guard_lobby_settle_timer(std::weak_ptr<Client> weak_c, std::shared_ptr<AsyncPromise<void>> promise) {
  co_await async_sleep(std::chrono::milliseconds(SPIRE_GUARD_LOBBY_SETTLE_MS));
  auto c = weak_c.lock();
  if (!c || promise->done()) {
    co_return;
  }
  if (c->spire_lobby_gate.on_timeout()) {
    promise->set_value();
  }
}

// v33: waits until the client has loaded the lobby: a B1 goes out now, the client answers it with a 99 only once the
// lobby is loaded (see SpireLobbyGate.hh); fallback SPIRE_GUARD_LOBBY_SETTLE_MS. Returns false if the client
// disconnected or left the lobby meanwhile (the marker stays; the restore follows at the next lobby join or login).
// Measured 2026-10-24 15:56 (lab) and 18:39 (pc): a B2 in the same second as the 67 hung the lobby join.
static asio::awaitable<bool> spire_guard_wait_for_lobby_settle(std::shared_ptr<Client> c, uint32_t serial_number) {
  auto promise = std::make_shared<AsyncPromise<void>>();
  c->spire_lobby_settle_promise = promise;
  c->spire_lobby_gate.arm();
  uint64_t start_usecs = phosg::now();
  asio::co_spawn(co_await asio::this_coro::executor, spire_guard_lobby_settle_timer(c, promise), asio::detached);
  try {
    send_server_time(c);
    co_await promise->get();
  } catch (const std::exception& e) {
    c->log.info_f("Spire guard: lobby restore for serial {:010} not started: {} while waiting for the 99 (marker kept)",
        serial_number, e.what());
    co_return false;
  }
  if (c->spire_lobby_settle_promise == promise) {
    c->spire_lobby_settle_promise.reset();
  }
  uint64_t waited_ms = (phosg::now() - start_usecs) / 1000;
  const char* why = (c->spire_lobby_gate.fired == SpireGuardLobbyGate::Event::LOBBY_LOADED)
      ? "the 99 after the lobby join"
      : "no 99 within the timeout";
  auto l = c->lobby.lock();
  if (!c->channel->connected() || !l || l->is_game()) {
    c->log.info_f("Spire guard: lobby restore for serial {:010} not started: client {} after {} ms ({}); marker kept",
        serial_number, !c->channel->connected() ? "disconnected" : "left the lobby", waited_ms, why);
    co_return false;
  }
  c->log.info_f("Spire guard: lobby restore for serial {:010} starts {} ms after the lobby join ({})", serial_number,
      waited_ms, why);
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
    filename = spire_guard_filename_for(c, serial_number);
    if (filename.empty()) {
      co_return;
    }
    snapshot = spire_guard_load(filename);
  } catch (const std::exception& e) {
    // The login restore stays the safety net; dropping here would not tell more than the next login does
    c->log.warning_f("Spire guard: failed to read snapshot for serial {:010} after a game: {}", serial_number, e.what());
    co_return;
  }
  if (!spire_guard_marker_open(c->is_spire_protected(), false, snapshot.restore_pending, false)) {
    co_return;
  }
  // v33: not while the client is still loading the lobby
  if (!co_await spire_guard_wait_for_lobby_settle(c, serial_number)) {
    co_return;
  }

  SpireGuardRestoreLock lock(c.get());
  if (!lock.locked) {
    c->log.info_f("Spire guard: a restore is already running for serial {:010}; lobby restore skipped", serial_number);
    co_return;
  }
  // v28: ask again instead of dropping; only while the client is still connected and in a lobby (in a game the
  // join guard has refused it anyway, and the marker stays)
  bool restored = false;
  for (size_t attempt = 1; attempt <= SPIRE_GUARD_LOBBY_ATTEMPTS; attempt++) {
    if (attempt > 1) {
      co_await async_sleep(SPIRE_GUARD_LOBBY_RETRY_DELAY);
      auto l = c->lobby.lock();
      if (!c->channel->connected() || !l || l->is_game()) {
        break;
      }
      c->log.info_f("Spire guard: asking again for the lobby restore of serial {:010} (attempt {} of {})",
          serial_number, attempt, SPIRE_GUARD_LOBBY_ATTEMPTS);
      try {
        snapshot = spire_guard_load(filename);
      } catch (const std::exception& e) {
        c->log.warning_f("Spire guard: failed to read snapshot for serial {:010} before attempt {}: {}", serial_number, attempt, e.what());
        break;
      }
      if (!snapshot.restore_pending) {
        restored = true;
        break;
      }
    }
    if (co_await spire_guard_run_restore(c, serial_number, filename, snapshot, "lobby")) {
      restored = true;
      break;
    }
  }
  if (!restored) {
    c->log.warning_f("Spire guard: lobby restore for serial {:010} not verified; connection kept, marker kept, the restore follows at the next lobby join or login",
        serial_number);
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
  c->log.info_f("Spire guard: restored {} for serial {:010} in the lobby; marker stays until the next login confirms it",
      spire_guard_describe(snapshot), serial_number);
}

bool spire_guard_game_join_allowed(std::shared_ptr<Client> c, bool tell_client) {
  uint32_t serial_number = spire_bridge_serial_for_client(c);
  if (!serial_number) {
    return true;
  }
  bool restoring = spire_guard_restoring_clients.count(c.get());
  bool pending = false;
  bool can_restore_now = false;
  try {
    std::string filename = spire_guard_filename_for(c, serial_number);
    if (filename.empty()) {
      pending = spire_guard_serial_has_open_marker(c, serial_number);
    } else {
      // v12 (critic K7): a run that never got its 98 counts too
      auto snapshot = spire_guard_load(filename);
      bool protected_character = c->is_spire_protected();
      pending = spire_guard_marker_open(protected_character, snapshot.run_in_progress, snapshot.restore_pending, false);
      can_restore_now = pending && snapshot.restore_pending;
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
  // v13 (critic K10): if no restore runs but one is pending and the client is in a lobby (the lobby restore did not
  // run, e.g. after $exit), start it now, so "try again" can succeed
  auto l = c->lobby.lock();
  if (tell_client && !restoring && can_restore_now && l && !l->is_game()) {
    c->log.info_f("Spire guard: starting the pending restore for serial {:010} after a refused game join", serial_number);
    asio::co_spawn(*c->require_server_state()->io_context, spire_guard_restore_in_lobby(c), asio::detached);
  }
  if (tell_client) {
    send_lobby_message_box(c, "$C7Your character is\nstill being restored.\nTry again in a\nmoment.");
  }
  return false;
}

// v19: rooms already announced, per game (lobby id) as floor << 16 | room. A game's id is never reused while the server
// runs, so entries of ended games are harmless; the set is cleared when it grows past 4096 entries.
static std::unordered_set<uint64_t> spire_bridge_cleared_rooms;
static void spire_checkpoint_record_room(std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room);
static void spire_checkpoint_store(std::shared_ptr<const Lobby> l, const auto& clients, uint8_t k);
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
static bool spire_bridge_room_is_clear(std::shared_ptr<Lobby> l, Version v, uint8_t floor, uint16_t room,
    std::string* why, const std::unordered_set<size_t>* treat_finished = nullptr) {
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
  auto event_unfinished = [&](uint32_t event_id) -> bool {
    for (const auto& ev_st : room_evs) {
      if ((ev_st->super_ev->version(v).set_entry->event_id == event_id) && !finished(*ev_st)) {
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
      return false;
    }
  }
  return !room_evs.empty();
}

static void spire_dead_event_watch(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, uint16_t wave_number);

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
        spire_checkpoint_record_room(l, floor, room);
        return;
      }
    }
    // Event rule (spire-server-all): a wave that never started (the elite wave on the normal path) does not hold the
    // room; a started or chained one does until it is finished
    std::string why;
    if (!spire_bridge_room_is_clear(l, v, floor, room, &why)) {
      l->log.info_f("Spire bridge: wave {} of room {} on floor {:02X} done, room not clear: {}", wave_number, room,
          floor, why.empty() ? "no wave events" : why);
      spire_dead_event_watch(c, l, floor, room, wave_number);
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

static void spire_dead_event_watch(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, uint16_t wave_number) {
  try {
    auto seen = spire_dead_room_events(l, c->version(), floor, room);
    if (seen.empty()) {
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
    l->log.info_f("Spire bridge: room {} on floor {:02X} has {} started event(s) without a living enemy; counted as finished after {} s unless one is hit",
        room, floor, seen.size(), SPIRE_UNHIT_ENEMY_SECONDS.count());
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
        spire_send_ticker_later(lc, l, SPIRE_BRIDGE_ROOM_CLEAR_TEXT, SPIRE_BRIDGE_ROOM_CLEAR_TEXT_DELAY); // v36: after the r251
        sent++;
      }
    }
    l->log.info_f("Spire bridge: {} defeated (room {} on floor {:02X}) in q{}; sent \"{}\" and r{} = {:08X} to {} client(s)",
        phosg::name_for_enum(type), room, floor, quest_number, SPIRE_BRIDGE_ROOM_CLEAR_TEXT,
        SPIRE_BRIDGE_ROOM_CLEARED_REGISTER, cmd.value.as_int.load(), sent);
    spire_replay_note_room_cleared(l, floor, cmd.value.as_int);
    spire_checkpoint_record_room(l, floor, room);
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

static void spire_checkpoint_record_room(std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room) {
  uint8_t k = spire_checkpoint_room_k(l->require_server_state()->data->spire_checkpoint_rooms, floor, room);
  if (!k) {
    l->log.info_f("Spire checkpoint: room {} on floor {:02X} is not in SpireCheckpointRooms", room, floor);
    return;
  }
  spire_checkpoint_store(l, l->clients, k);
}

static void spire_checkpoint_store(std::shared_ptr<const Lobby> l, const auto& clients, uint8_t k) {
  for (const auto& lc : clients) {
    if (!lc || (lc->version() != Version::GC_V3)) {
      continue;
    }
    try {
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
    if (!spire_replay_applies(c, l)) {
      return;
    }
    spire_follow_leader(c, l);
    const auto& arrivals = c->require_server_state()->data->spire_checkpoint_arrivals;
    auto arr = arrivals.find(c->floor);
    if (arr != arrivals.end()) {
      if (auto end = spire_run_end_for(l)) {
        c->log.info_f("Spire checkpoint: arrival on floor {:02X} after the run ended ({}); room {} not stored", c->floor,
            end->why, arr->second);
      } else {
        std::array<std::shared_ptr<Client>, 1> just_c{c}; // each arriving client for its own character
        spire_checkpoint_store(l, just_c, arr->second);
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
    if (!cp.last_room) {
      return "No checkpoint yet: start at room 1";
    }
    return cp.last_lives ? std::format("Continue at room {} (you had {} lives)", cp.last_room + 1, cp.last_lives)
                         : std::format("Continue at room {}", cp.last_room + 1);
  }
  throw std::runtime_error("Use: $spire status or $spire continue");
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
