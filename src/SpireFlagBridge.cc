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
#include "CommandFormats.hh"
#include "SendCommands.hh"
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

  spire_bridge_write_durable(filename, data);
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
  if (!l->is_game()) {
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
      std::string msg = std::format("$C4{} minutes left.$C7\nThe timer ends the run.", minutes);
      for (const auto& lc : l->clients) {
        if (lc) {
          send_text_message(lc, msg);
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
    const char* text = (cause == SPIRE_BRIDGE_TIMER_TIME_UP) ? "Time is up.\nThe run ends here."
                                                             : "The second player left.\nThe run ends here.";
    for (const auto& lc : l->clients) {
      if (lc) {
        send_text_message(lc, text);
        send_command_t(lc, 0x60, 0x00, cmd);
      }
    }
    l->log.info_f("Spire bridge: run timer end ({}) at {} s with {} of {} client(s); sent r{}={:08X}",
        (cause == SPIRE_BRIDGE_TIMER_TIME_UP) ? "time up" : "player left", elapsed, clients, t.clients_at_start,
        SPIRE_BRIDGE_TIMER_REGISTER, cmd.value.as_int.load());
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

// v17: the text for a short-message code (SPIRE_BRIDGE_MSG_*), the same words as the quest's window_msg (spire.json
// texts; <color N> as $CN, <color 0> as $C7); empty if the value is no message code.
static std::string spire_bridge_message_text(uint32_t value) {
  uint8_t kind = value >> 24;
  uint32_t a = (value >> 8) & 0xFF, b = value & 0xFF;
  switch (kind) {
    case SPIRE_BRIDGE_MSG_WAVE:
      return std::format("Wave {}/{}", a, b);
    case SPIRE_BRIDGE_MSG_LIFE:
      return a ? std::format("$C1Life lost$C7 - {} left.\nNo more Scape Dolls:\nthe next death ends the run.", b)
               : std::format("$C1Life lost$C7 - {} left.\nA new Scape Doll is yours.", b);
    case SPIRE_BRIDGE_MSG_TIME:
      return std::format("$C4{} minutes left.$C7\nThe timer ends the run.", value & 0xFFFF);
    case SPIRE_BRIDGE_MSG_PARTNER:
      return b ? "$C4Your partner is waiting.$C7\nOpen the quest board and\nmake your choice." : "Waiting for your partner...";
    case SPIRE_BRIDGE_MSG_RUN_OVER:
      return std::format("Run over after {} s.\nRooms cleared: {}.\nLeave the game now.", value & 0xFFFF, (value >> 16) & 0xFF);
    default:
      return "";
  }
}

// v23: lives per client from its 'L' codes, for the checkpoint
static std::unordered_map<const Client*, uint32_t> spire_checkpoint_lives;

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
  return std::format("Best score: {}\nBest time: {}\nRuns: {}  Won: {}", rec.best_score, time, rec.runs, rec.won);
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
    rec.runs++;
    spire_records_save(filename, serial_number, rec);
    c->log.info_f("Spire records: run started, {} run(s) in {}", rec.runs, filename);
    return true;
  }
  if (kind == SPIRE_BRIDGE_REC_BOARD) {
    send_text_message(c, spire_records_board_text(rec));
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
  std::string text = new_score ? std::format("New best score: {}!", rec.best_score)
                               : std::format("Best score: {}.", rec.best_score);
  if (new_time) {
    text += std::format("\nNew best time: {} s!", seconds);
  }
  // v24: unlocks and payout flags for the character (SpireMeta), on the game's difficulty
  text += spire_meta_on_finish(c, l, serial_number, value, new_time);
  text += "\nRecords saved.";
  send_text_message(c, text);
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
    std::string filename = spire_bridge_filename(serial_number);

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
            send_text_message(lc, SPIRE_BRIDGE_ROOM_CLEAR_TEXT);
            sent++;
          }
        }
        c->log.info_f("Spire bridge: ROOM for serial {:010} in q{}; sent \"{}\" to {} client(s)",
            serial_number, quest_number, SPIRE_BRIDGE_ROOM_CLEAR_TEXT, sent);
        return true;
      }
      if ((value >> 24) == SPIRE_BRIDGE_MSG_LIFE) {
        spire_checkpoint_lives[c.get()] = value & 0xFF; // falls through to the life text below
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
        spire_run_timer_stop(l.get()); // v19: the run is over, so the timer must not end it again
      }
      std::string msg = spire_bridge_message_text(value);
      if (!msg.empty()) {
        // v17: a short run message. Each client writes its own life, time, partner and run-over codes, so those go to
        // the writer only; the wave code comes from the leader's room thread alone (the follower mirrors the room and
        // shows no wave text), so it goes to every client in the game, like 'ROOM'
        size_t sent = 0;
        if ((value >> 24) == SPIRE_BRIDGE_MSG_WAVE) {
          for (const auto& lc : l->clients) {
            if (lc) {
              send_text_message(lc, msg);
              sent++;
            }
          }
        } else {
          send_text_message(c, msg);
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
  // v10: set instead of clearing restore_pending when the lobby restore is verified. The real character is then only in
  // the client's memory until the client saves it, so the card may still hold the overlay. Cleared at the next login
  // whose 61 already shows the snapshot's level and EXP (no push); any other login turns it back into restore_pending.
  bool awaiting_save = false;
  // v12: what the client had in memory right before the lobby restore (from its 30): the overlay. Set together with
  // awaiting_save. Only a login 61 that still shows exactly this gets a push; any other unequal 61 is real progress.
  bool has_overlay = false;
  uint32_t overlay_level = 0;
  uint32_t overlay_exp = 0;
  uint32_t overlay_meseta = 0;
  uint32_t overlay_num_items = 0;
  uint32_t quest_number = 0;
  uint64_t snapshot_time_usecs = 0;
  bool has_character = false;
  PlayerInventory inventory;
  PlayerDispDataV4 disp;
};

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
static bool spire_guard_serial_has_open_marker(uint32_t serial_number) {
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
    if (snapshot.run_in_progress || snapshot.restore_pending || snapshot.awaiting_save) {
      return true;
    }
  }
  return false;
}

static bool spire_guard_take_snapshot(std::shared_ptr<Client> c, uint32_t quest_number);

bool spire_guard_begin_run(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  try {
    if (!l->is_game() || (l->mode != GameMode::BATTLE) || !l->quest) {
      return true;
    }
    const auto& rules = l->quest->meta.battle_rules;
    if (!rules || !rules->replace_char) {
      return true;
    }
    uint32_t quest_number = l->quest->meta.quest_number;
    if (!spire_guard_quest_is_guarded(c, quest_number)) {
      // v12 (critic K5): a replace_char quest outside the list runs without the guard; say so
      c->log.warning_f("Spire guard: q{} is a replace_char Battle quest but not in SpireGuardQuests (or SpireBridgeQuests if that key is missing); no guard for this run",
          quest_number);
      return true;
    }
    return spire_guard_take_snapshot(c, quest_number);
  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to start run: {}; no overlay", e.what());
    return false;
  }
}

// v21: every Battle game is guarded, also one without a quest (lab 10:26: Kamako 23 -> 8 items, BidX 20 -> 7 in a
// Battle game without quest). The join takes the snapshot (quest 0); the guarded quest that may follow in the same game
// keeps it (run already in progress), and the 98 at the end turns it into a pending restore, as for a run.
void spire_guard_battle_join(std::shared_ptr<Client> c, std::shared_ptr<const Lobby> l) {
  if (!l || !l->is_game() || (l->mode != GameMode::BATTLE)) {
    return;
  }
  try {
    if (spire_guard_take_snapshot(c, 0)) {
      return;
    }
  } catch (const std::exception& e) {
    c->log.warning_f("Spire guard: failed to guard the Battle game join: {}", e.what());
  }
  // fail closed, like begin_run: no Battle game without a stored snapshot
  c->log.warning_f("Spire guard: no snapshot at the Battle game join; dropping the connection");
  c->channel->disconnect();
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
    if (snapshot.run_in_progress || snapshot.restore_pending) {
      c->log.info_f("Spire guard: {} for serial {:010} (q{}); kept snapshot {} from q{}",
          snapshot.restore_pending ? "restore pending" : "run already in progress", serial_number, quest_number,
          spire_guard_describe(snapshot), snapshot.quest_number);
      return true;
    }

    if (snapshot.awaiting_save) {
      // v10: the lobby restore was verified, so since then the server's character data is the real character: the
      // verifying 61, and after that only 61s and 98s from non-Spire games (a Spire game would have set a marker). A
      // fresh snapshot keeps any progress from normal games played in between. Only if that data shows a lower level
      // than the snapshot (impossible in normal play) is the old snapshot kept.
      if (character->disp.stats.level < snapshot.disp.stats.level) {
        snapshot.awaiting_save = false;
        snapshot.run_in_progress = true;
        snapshot.quest_number = quest_number;
        spire_guard_save(filename, serial_number, snapshot);
        c->log.warning_f("Spire guard: run started for serial {:010} in q{} before a login confirmed the lobby restore; character data {}; marker set, kept snapshot {}",
            serial_number, quest_number, "shows a lower level", spire_guard_describe(snapshot));
        return true;
      }
      snapshot.awaiting_save = false;
      c->log.info_f("Spire guard: run started for serial {:010} in q{} before a login confirmed the lobby restore; taking a fresh snapshot (was {})",
          serial_number, quest_number, spire_guard_describe(snapshot));
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
    // v12 (critic K2): fail closed; a run without a stored snapshot must not get the overlay
    c->log.warning_f("Spire guard: failed to start run: {}; no overlay", e.what());
    return false;
  }
  return true;
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

// Verifies the 61 that the client sent right after the push: level and EXP must equal the snapshot. The inventory
// count is only logged. Clears RestorePending and returns true if verified. In the lobby (v10) the marker is not cleared
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
  std::string items_str = std::format("inventory {} items, snapshot {} ({})",
      num_items, snapshot_num_items, (num_items == snapshot_num_items) ? "equal" : "different");
  if ((level != snapshot_level) || (exp != snapshot_exp)) {
    c->log.warning_f("Spire guard: restore not verified for serial {:010}: 61 shows Lv{} EXP {}, snapshot Lv{} EXP {}; {}; marker kept",
        serial_number, level + 1, exp, snapshot_level + 1, snapshot_exp, items_str);
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
      if (spire_guard_serial_has_open_marker(serial_number)) {
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
    if (!snapshot.run_in_progress && !snapshot.restore_pending && !snapshot.awaiting_save) {
      co_return true;
    }
    if (snapshot.awaiting_save && !snapshot.run_in_progress && !snapshot.restore_pending) {
      // v10: after a verified lobby restore, the client may have saved the real character since. v12 (critic K1): the
      // client may also have played a normal game since and saved real progress, which a push would undo. EXP never
      // goes down in normal play, so only a 61 with less EXP than the snapshot, or one that still shows exactly the
      // overlay the lobby restore replaced, means the card holds the overlay. Any other 61 clears the marker.
      snapshot.awaiting_save = false;
      auto character = c->character_file(false, false);
      if (character) {
        uint32_t level = character->disp.stats.level;
        uint32_t exp = character->disp.stats.exp;
        bool shows_overlay = snapshot.has_overlay && (level == snapshot.overlay_level) && (exp == snapshot.overlay_exp) &&
            (character->disp.stats.meseta == snapshot.overlay_meseta) &&
            (character->inventory.num_items == snapshot.overlay_num_items);
        bool lost_exp = exp < snapshot.disp.stats.exp;
        // v14: a 61 with the snapshot's level and EXP is the real character, also if an (older) fingerprint matches it
        bool same = (level == snapshot.disp.stats.level) && (exp == snapshot.disp.stats.exp);
        if (same || (!shows_overlay && !lost_exp)) {
          snapshot.has_overlay = false;
          spire_guard_save(filename, serial_number, snapshot);
          c->log.info_f("Spire guard: login 61 for serial {:010} shows Lv{} EXP {}, snapshot Lv{} EXP {}; {}, marker cleared, no push",
              serial_number, level + 1, exp, snapshot.disp.stats.level + 1, snapshot.disp.stats.exp.load(),
              same ? "lobby restore confirmed" : "progress since the lobby restore");
          co_return true;
        }
        c->log.info_f("Spire guard: login 61 for serial {:010} shows Lv{} EXP {}, snapshot Lv{} EXP {}; {}, pushing again",
            serial_number, level + 1, exp, snapshot.disp.stats.level + 1, snapshot.disp.stats.exp.load(),
            shows_overlay ? "the card still holds the overlay" : "less EXP than the snapshot");
      } else {
        c->log.info_f("Spire guard: no login 61 for serial {:010}; cannot confirm the lobby restore, pushing again", serial_number);
      }
      snapshot.restore_pending = true;
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
  if (!snapshot.restore_pending) {
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
      pending = spire_guard_serial_has_open_marker(serial_number);
    } else {
      // v12 (critic K7): a run that never got its 98 counts too
      auto snapshot = spire_guard_load(filename);
      pending = snapshot.restore_pending || snapshot.run_in_progress;
      can_restore_now = snapshot.restore_pending;
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

// v19: room clear detection. The wave events of one room on one floor, read from the map the server keeps per game.
// An event holds the room while it is started (0004 or 0002) and not finished (0008). A finished event holds it while
// its post-wave actions point at work in the same room that is not done: a trigger_event (0C) of an unfinished event
// (covers the moment between the client's 6x76 and its 6x67 for the next wave), or a construct_enemies (09/0D) of a
// wave whose enemies are not all defeated.
static bool spire_bridge_room_is_clear(
    std::shared_ptr<Lobby> l, Version v, uint8_t floor, uint16_t room, std::string* why) {
  auto& ms = l->map_state;
  std::vector<std::shared_ptr<MapState::EventState>> room_evs;
  for (const auto& ev_st : ms->event_states_for_floor(v, floor)) {
    const auto* set_entry = ev_st->super_ev->version(v).set_entry;
    if (set_entry && (set_entry->room == room)) {
      room_evs.emplace_back(ev_st);
    }
  }
  auto event_unfinished = [&](uint32_t event_id) -> bool {
    for (const auto& ev_st : room_evs) {
      if ((ev_st->super_ev->version(v).set_entry->event_id == event_id) && !(ev_st->flags & 0x08)) {
        return true;
      }
    }
    return false;
  };
  for (const auto& ev_st : room_evs) {
    const auto& ev_ver = ev_st->super_ev->version(v);
    if (!(ev_st->flags & 0x08)) {
      if (ev_st->flags & 0x06) {
        *why = std::format("W-{:03X} (event {}) is running", ev_st->w_id, ev_ver.set_entry->event_id.load());
        return false;
      }
      continue;
    }
    phosg::StringReader r(ev_ver.action_stream, ev_ver.action_stream_size);
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
        if (c_room == room) {
          for (const auto& ene_st : ms->enemy_states_for_floor_room_wave(v, floor, c_room, c_wave)) {
            if (!ene_st->super_ene->child_index && !(ene_st->set_flags & 8)) {
              *why = std::format("W-{:03X} constructs wave {} of the room, not defeated", ev_st->w_id, c_wave);
              return false;
            }
          }
        }
        if (opcode == 0x0D) {
          break;
        }
      } else if (opcode == 0x0C) {
        uint32_t event_id = r.get_u32l();
        if (event_unfinished(event_id)) {
          *why = std::format("W-{:03X} triggers event {}, not finished", ev_st->w_id, event_id);
          return false;
        }
      } else {
        break; // 01 stop, or an opcode the 6x76 handler also stops at
      }
    }
  }
  return !room_evs.empty();
}

void spire_bridge_check_room_clear(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, uint16_t wave_number) {
  try {
    if ((c->version() != Version::GC_V3) || !l->is_game() || !l->quest || !l->map_state ||
        !spire_bridge_quest_is_bridged(c, l->quest->meta.quest_number)) {
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
        send_text_message(lc, SPIRE_BRIDGE_ROOM_CLEAR_TEXT);
        send_command_t(lc, 0x60, 0x00, cmd);
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

// The boss types whose defeat is the room clear (v25): the last part of a boss Sega's quests never zone-clear.
static bool spire_bridge_is_server_clear_boss(EnemyType type) {
  return type == EnemyType::VOL_OPT_2;
}

void spire_bridge_check_boss_defeated(
    std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room, EnemyType type) {
  try {
    if ((c->version() != Version::GC_V3) || !l->is_game() || !l->quest || !spire_bridge_is_server_clear_boss(type)) {
      return;
    }
    uint32_t quest_number = l->quest->meta.quest_number;
    const auto& data = c->require_server_state()->data;
    if (!data->spire_boss_clear_quest_numbers.count(quest_number) || !spire_bridge_quest_is_bridged(c, quest_number)) {
      l->log.info_f("Spire bridge: {} defeated in q{}, which is not in SpireBossClearQuests; no room clear",
          phosg::name_for_enum(type), quest_number);
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
        send_text_message(lc, SPIRE_BRIDGE_ROOM_CLEAR_TEXT);
        send_command_t(lc, 0x60, 0x00, cmd);
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

static void spire_checkpoint_record_room(std::shared_ptr<Lobby> l, uint8_t floor, uint16_t room) {
  const auto& map = l->require_server_state()->data->spire_checkpoint_rooms;
  auto it = map.find(std::make_pair(floor, static_cast<int32_t>(room)));
  if (it == map.end()) {
    it = map.find(std::make_pair(floor, -1));
  }
  if (it == map.end()) {
    l->log.info_f("Spire checkpoint: room {} on floor {:02X} is not in SpireCheckpointRooms", room, floor);
    return;
  }
  uint8_t k = it->second;
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
      auto lives_it = spire_checkpoint_lives.find(lc.get());
      cp.last_lives = (lives_it == spire_checkpoint_lives.end()) ? 0 : lives_it->second;
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

static asio::awaitable<void> spire_follow_leader_warp(
    std::weak_ptr<Client> weak_leader, std::weak_ptr<Lobby> weak_l, uint32_t floor, uint32_t delay_ms) {
  co_await async_sleep(std::chrono::milliseconds(delay_ms));
  auto leader = weak_leader.lock();
  auto l = weak_l.lock();
  if (!leader || !l || (leader->lobby.lock() != l) || (leader->floor != floor)) {
    co_return; // the leader left the game or moved on; its next floor load warps again
  }
  for (const auto& lc : l->clients) {
    if (lc && (lc != leader) && (lc->floor != floor)) {
      l->log.info_f("Spire follow: client {} on floor {:02X} warped to the leader's floor {:02X}", lc->lobby_client_id,
          lc->floor, floor);
      send_warp(lc, floor, true);
    }
  }
}

static void spire_follow_leader(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l) {
  const auto& follow = c->require_server_state()->data->spire_follow_leader;
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
      std::array<std::shared_ptr<Client>, 1> just_c{c}; // each arriving client for its own character
      spire_checkpoint_store(l, just_c, arr->second);
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
    return cp.rooms ? std::format("Rooms cleared: {}\nLast: room {}", rooms, cp.last_room)
                    : std::string("No rooms cleared yet");
  }
  if (arg == "continue") {
    if (!cp.last_room) {
      return "No checkpoint yet:\nstart at room 1";
    }
    return cp.last_lives ? std::format("Continue at room {}\n(you had {} lives)", cp.last_room + 1, cp.last_lives)
                         : std::format("Continue at room {}", cp.last_room + 1);
  }
  throw std::runtime_error("Use: $spire status\n$spire continue");
}
