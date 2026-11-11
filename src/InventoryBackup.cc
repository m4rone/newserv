#include "InventoryBackup.hh"

#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <phosg/Filesystem.hh>
#include <phosg/Time.hh>
#include <stdexcept>
#include <unordered_map>

#include "AsyncUtils.hh"
#include "Client.hh"
#include "Lobby.hh"
#include "SendCommands.hh"
#include "ServerState.hh"
#include "SpireFlagBridge.hh"
#include "GameServer.hh"

static std::string item_code(const ItemData& item) {
  // The $item form: data1 (24 hex digits), a space, data2 (8 hex digits)
  return std::format("{:08X}{:08X}{:08X} {:08X}", item.data1db[0], item.data1db[1], item.data1db[2], item.data2db);
}

static phosg::JSON items_json(const PlayerInventory& inventory) {
  auto ret = phosg::JSON::list();
  for (size_t z = 0; z < std::min<size_t>(inventory.num_items, inventory.items.size()); z++) {
    const auto& it = inventory.items[z];
    ret.emplace_back(phosg::JSON::dict({
        {"Code", item_code(it.data)},
        {"Equipped", (it.flags & 0x00000008) != 0},
    }));
  }
  return ret;
}

phosg::JSON inventory_backup_to_json(const InventoryBackup& b) {
  Language language = b.inventory.language;
  auto json = phosg::JSON::dict({
      {"SerialNumber", b.serial_number},
      {"CharacterID", b.character_id},
      {"Name", b.disp.visual.name.decode(language)},
      {"Level", b.disp.stats.level + 1},
      {"Class", b.disp.visual.sh.char_class},
      {"Time", b.time_usecs},
      {"TimeText", phosg::format_time(b.time_usecs)},
      {"QuestNumber", b.quest_number},
      {"Spire", b.spire},
      {"LeftNormally", b.left_normally},
      {"Restored", b.restored},
      {"RestoredTime", b.restored_time_usecs},
      {"Meseta", b.disp.stats.meseta.load()},
      {"Items", items_json(b.inventory)},
      {"Inventory", spire_guard_hex_encode(&b.inventory, sizeof(b.inventory))},
      {"Disp", spire_guard_hex_encode(&b.disp, sizeof(b.disp))},
  });
  if (!b.imported_from.empty()) {
    json.emplace("ImportedFrom", b.imported_from);
  }
  if (b.loss_time_usecs) {
    json.emplace("Loss", phosg::JSON::dict({
        {"Time", b.loss_time_usecs},
        {"TimeText", phosg::format_time(b.loss_time_usecs)},
        {"Items", b.loss_items},
        {"Meseta", b.loss_meseta},
    }));
  }
  if (b.has_exit) {
    json.emplace("Exit", phosg::JSON::dict({
        {"Meseta", b.exit_meseta},
        {"Items", items_json(b.exit_inventory)},
        {"Inventory", spire_guard_hex_encode(&b.exit_inventory, sizeof(b.exit_inventory))},
    }));
  }
  // Added last and only when present: a backup without an interim state serializes byte for byte as it did before
  if (b.has_interim) {
    json.emplace("Interim", phosg::JSON::dict({
        {"Time", b.interim_time_usecs},
        {"TimeText", phosg::format_time(b.interim_time_usecs)},
        {"Boot", b.interim_boot},
        {"Count", b.interim_count},
        {"Meseta", b.interim_meseta},
        {"Items", items_json(b.interim_inventory)},
        {"Inventory", spire_guard_hex_encode(&b.interim_inventory, sizeof(b.interim_inventory))},
    }));
  }
  return json;
}

InventoryBackup inventory_backup_from_json(const phosg::JSON& json) {
  InventoryBackup b;
  b.serial_number = json.get_int("SerialNumber");
  b.character_id = json.get_string("CharacterID");
  b.time_usecs = json.get_int("Time");
  b.quest_number = json.get_int("QuestNumber", 0);
  b.spire = json.get_bool("Spire", false);
  b.left_normally = json.get_bool("LeftNormally", false);
  b.restored = json.get_bool("Restored", false);
  b.restored_time_usecs = json.get_int("RestoredTime", 0);
  b.imported_from = json.get_string("ImportedFrom", "");
  spire_guard_hex_decode(json.get_string("Inventory"), &b.inventory, sizeof(b.inventory));
  spire_guard_hex_decode(json.get_string("Disp"), &b.disp, sizeof(b.disp));
  if (json.contains("Loss")) {
    const auto& loss = json.at("Loss");
    b.loss_time_usecs = loss.get_int("Time");
    b.loss_items = loss.get_int("Items", 0);
    b.loss_meseta = loss.get_int("Meseta", 0);
  }
  if (json.contains("Exit")) {
    const auto& exit = json.at("Exit");
    b.has_exit = true;
    b.exit_meseta = exit.get_int("Meseta");
    spire_guard_hex_decode(exit.get_string("Inventory"), &b.exit_inventory, sizeof(b.exit_inventory));
  }
  if (json.contains("Interim")) {
    const auto& interim = json.at("Interim");
    b.has_interim = true;
    b.interim_time_usecs = interim.get_int("Time");
    b.interim_boot = interim.get_int("Boot");
    b.interim_count = interim.get_int("Count", 0);
    b.interim_meseta = interim.get_int("Meseta");
    spire_guard_hex_decode(interim.get_string("Inventory"), &b.interim_inventory, sizeof(b.interim_inventory));
  }
  return b;
}

std::string inventory_backup_character_directory(const std::string& root, uint32_t serial_number, const PlayerDispDataV4& disp) {
  return std::format("{}/{:010}.{}", root, serial_number, spire_guard_character_id(disp));
}

std::string inventory_backup_basename_for_time(uint64_t usecs) {
  time_t secs = usecs / 1000000;
  struct tm tm_value;
  gmtime_r(&secs, &tm_value);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm_value);
  return std::format("{}-{:06}.json", buf, usecs % 1000000);
}

std::vector<std::string> inventory_backup_list(const std::string& dir) {
  std::vector<std::string> ret;
  if (!std::filesystem::is_directory(dir)) {
    return ret;
  }
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    std::string name = entry.path().filename().string();
    if (entry.is_regular_file() && name.ends_with(".json")) {
      ret.emplace_back(std::move(name));
    }
  }
  std::sort(ret.begin(), ret.end());
  return ret;
}

bool inventory_backup_pinned(const InventoryBackupPickFlags& f) {
  return (f.imported || f.loss) && !f.restored;
}

std::vector<std::string> inventory_backup_files_to_delete(const std::vector<std::string>& sorted_names, size_t keep,
    const std::vector<InventoryBackupPickFlags>& flags) {
  // Newest first: keep the newest keep unpinned names; pinned names always stay and do not use up keep
  std::vector<std::string> ret;
  size_t kept = 0;
  for (ssize_t z = static_cast<ssize_t>(sorted_names.size()) - 1; z >= 0; z--) {
    if ((static_cast<size_t>(z) < flags.size()) && inventory_backup_pinned(flags[z])) {
      continue;
    }
    if (kept < keep) {
      kept++;
    } else {
      ret.emplace_back(sorted_names[z]);
    }
  }
  std::reverse(ret.begin(), ret.end());
  return ret;
}

static InventoryBackupPickFlags backup_pick_flags(const InventoryBackup& b) {
  return InventoryBackupPickFlags{!b.imported_from.empty(), b.restored, b.loss_time_usecs != 0};
}

// The flags of every backup in dir (an unreadable file has none)
static std::vector<InventoryBackupPickFlags> backup_flags_in(const std::string& dir, const std::vector<std::string>& names) {
  std::vector<InventoryBackupPickFlags> ret;
  for (const auto& name : names) {
    try {
      ret.emplace_back(backup_pick_flags(inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(dir + "/" + name)))));
    } catch (const std::exception&) {
      ret.emplace_back();
    }
  }
  return ret;
}

void inventory_backup_write_file(const std::string& path, const InventoryBackup& b) {
  std::string data = inventory_backup_to_json(b).serialize(phosg::JSON::SerializeOption::FORMAT);
  std::string temp = path + ".tmp";
  int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) {
    throw std::runtime_error(std::format("cannot open {}: errno {}", temp, errno));
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
      throw std::runtime_error(std::format("cannot write {}: errno {}", temp, error));
    }
    offset += written;
  }
  if (::fsync(fd) != 0) {
    int error = errno;
    ::close(fd);
    throw std::runtime_error(std::format("cannot fsync {}: errno {}", temp, error));
  }
  ::close(fd);
  std::filesystem::rename(temp, path);
  // The rename itself is only durable once the directory is synced (as spire_bridge_write_durable does)
  std::string dir = std::filesystem::path(path).parent_path().string();
  int dir_fd = ::open(dir.empty() ? "." : dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dir_fd >= 0) {
    ::fsync(dir_fd);
    ::close(dir_fd);
  }
}

std::string inventory_backup_save(const std::string& dir, const InventoryBackup& b, size_t keep) {
  std::filesystem::create_directories(dir);
  std::string basename = inventory_backup_basename_for_time(b.time_usecs);
  std::string path = dir + "/" + basename;
  // Two joins in the same microsecond cannot happen for one character, but never overwrite a backup
  for (size_t z = 1; std::filesystem::exists(path); z++) {
    path = std::format("{}/{}.{}.json", dir, basename.substr(0, basename.size() - 5), z);
  }
  inventory_backup_write_file(path, b);
  auto names = inventory_backup_list(dir);
  auto flags = (names.size() > keep) ? backup_flags_in(dir, names) : std::vector<InventoryBackupPickFlags>();
  for (const auto& name : inventory_backup_files_to_delete(names, keep, flags)) {
    std::filesystem::remove(dir + "/" + name);
  }
  return path;
}

ssize_t inventory_backup_pick_previous(const std::vector<std::string>& sorted_names, const std::string& current_basename) {
  for (ssize_t z = static_cast<ssize_t>(sorted_names.size()) - 1; z >= 0; z--) {
    if (sorted_names[z] != current_basename) {
      return z;
    }
  }
  return -1;
}

ssize_t inventory_backup_pick_restore(const std::vector<std::string>& sorted_names,
    const std::vector<InventoryBackupPickFlags>& flags, const std::string& current_basename) {
  for (ssize_t z = static_cast<ssize_t>(sorted_names.size()) - 1; z >= 0; z--) {
    if ((sorted_names[z] != current_basename) && (static_cast<size_t>(z) < flags.size()) &&
        inventory_backup_pinned(flags[z])) {
      return z;
    }
  }
  return inventory_backup_pick_previous(sorted_names, current_basename);
}

void inventory_removals_add(InventoryRemovals& removals, const ItemData& item) {
  if (item.data1[0] == 0x04) {
    removals.meseta += item.data2d;
  } else if (!item.empty()) {
    removals.items.emplace_back(item);
  }
}

namespace {
struct RestoreEntry {
  ItemData item;
  uint32_t count;
};

bool is_tool(const ItemData& item) {
  return item.data1[0] == 0x03;
}

uint32_t entry_count(const ItemData& item) {
  return (is_tool(item) && item.data1[5]) ? item.data1[5] : 1;
}

bool same_exact(const ItemData& a, const ItemData& b) {
  ItemData ka = a, kb = b;
  ka.id = 0;
  kb.id = 0;
  if (is_tool(ka)) {
    ka.data1[5] = 0;
  }
  if (is_tool(kb)) {
    kb.data1[5] = 0;
  }
  return !memcmp(&ka.data1[0], &kb.data1[0], 12) && (ka.data2d == kb.data2d);
}

bool same_type(const ItemData& a, const ItemData& b) {
  // A weapon, armor or unit is its data1[0-2]; a mag is data1[0-1] (data1[2] is its level, which feeding changes)
  size_t len = (a.data1[0] == 0x02) ? 2 : 3;
  return !is_tool(a) && !memcmp(&a.data1[0], &b.data1[0], len);
}
} // namespace

InventoryRestorePlan inventory_restore_missing(const PlayerInventory& source, uint32_t source_meseta,
    const PlayerInventory& current, uint32_t current_meseta, const InventoryRemovals& removed) {
  std::vector<RestoreEntry> need;
  for (size_t z = 0; z < std::min<size_t>(source.num_items, source.items.size()); z++) {
    const auto& item = source.items[z].data;
    if (!item.empty() && (item.data1[0] != 0x04)) {
      need.emplace_back(RestoreEntry{item, entry_count(item)});
    }
  }
  std::vector<RestoreEntry> have;
  for (size_t z = 0; z < std::min<size_t>(current.num_items, current.items.size()); z++) {
    have.emplace_back(RestoreEntry{current.items[z].data, entry_count(current.items[z].data)});
  }
  for (const auto& item : removed.items) {
    have.emplace_back(RestoreEntry{item, entry_count(item)});
  }

  for (auto* match : {&same_exact, &same_type}) {
    for (auto& n : need) {
      for (auto& h : have) {
        if (!n.count) {
          break;
        }
        if (h.count && match(n.item, h.item)) {
          uint32_t take = std::min(n.count, h.count);
          n.count -= take;
          h.count -= take;
        }
      }
    }
  }

  InventoryRestorePlan plan;
  for (const auto& n : need) {
    if (!n.count) {
      continue;
    }
    ItemData item = n.item;
    item.id = 0xFFFFFFFF;
    if (is_tool(item) && item.data1[5]) {
      item.data1[5] = n.count;
    }
    plan.items.emplace_back(item);
  }
  int64_t meseta = static_cast<int64_t>(source_meseta) - current_meseta - static_cast<int64_t>(removed.meseta);
  plan.meseta = std::clamp<int64_t>(meseta, 0, 999999);
  return plan;
}

InventoryRestoreRefusal inventory_restore_refusal(bool in_game, bool spire_game, bool,
    uint32_t serial_number, const std::string& character_id, const InventoryBackup* backup, bool interim_is_end) {
  using R = InventoryRestoreRefusal;
  if (!in_game) {
    return R::NOT_IN_GAME; // the items are dropped as floor items, which needs a game
  }
  if (spire_game) {
    return R::SPIRE_GAME;
  }
  if (!backup) {
    return R::NO_BACKUP;
  }
  if ((backup->serial_number != serial_number) || (backup->character_id != character_id)) {
    return R::OTHER_CHARACTER;
  }
  if (backup->spire) {
    return R::SPIRE_BACKUP;
  }
  if (backup->restored) {
    return R::ALREADY_RESTORED;
  }
  if (backup->left_normally) {
    return R::LEFT_NORMALLY;
  }
  if (!backup->has_exit && backup->imported_from.empty() && !interim_is_end) {
    return R::UNKNOWN_END;
  }
  return R::NONE;
}

InventoryRestorePlan inventory_restore_take(InventoryBackup& backup, const InventoryRestorePlan& plan, bool check_only,
    uint64_t now_usecs) {
  if (check_only) {
    return InventoryRestorePlan();
  }
  backup.restored = true;
  backup.restored_time_usecs = now_usecs;
  return plan;
}

const char* inventory_restore_refusal_text(InventoryRestoreRefusal r) {
  switch (r) {
    case InventoryRestoreRefusal::NONE:
      return "";
    case InventoryRestoreRefusal::NOT_IN_GAME:
      return "Alleen in een game.";
    case InventoryRestoreRefusal::SPIRE_GAME:
      return "Niet in een Spire-run.";
    case InventoryRestoreRefusal::NO_BACKUP:
      return "Geen back-up van dit personage.";
    case InventoryRestoreRefusal::OTHER_CHARACTER:
      return "Die back-up is van een ander personage.";
    case InventoryRestoreRefusal::SPIRE_BACKUP:
      return "Die back-up is van een Spire-run;\nde Spire-bewaker herstelt dat zelf.";
    case InventoryRestoreRefusal::ALREADY_RESTORED:
      return "Deze back-up is al hersteld.";
    case InventoryRestoreRefusal::LEFT_NORMALLY:
      return "Je vorige game is netjes afgesloten:\nniets te herstellen.";
    case InventoryRestoreRefusal::UNKNOWN_END:
      return "Onbekend hoe je vorige game eindigde:\nniet herstelbaar.";
  }
  return "";
}

void inventory_backup_record_end(InventoryBackup& b, bool normal, bool guard_covered, const PlayerInventory* inventory,
    uint32_t meseta) {
  b.spire = b.spire || guard_covered;
  if (normal) {
    b.left_normally = true;
  } else if (inventory) {
    b.has_exit = true;
    b.exit_inventory = *inventory;
    b.exit_meseta = meseta;
  }
  if (b.left_normally || b.has_exit) {
    // The game has its end: the interim state is over, and the file is what it was before interim states existed
    b.has_interim = false;
    b.interim_inventory = PlayerInventory();
    b.interim_meseta = 0;
    b.interim_time_usecs = 0;
    b.interim_boot = 0;
    b.interim_count = 0;
  }
}

uint64_t inventory_backup_boot_id() {
  static const uint64_t boot_id = std::max<uint64_t>(phosg::now(), 1);
  return boot_id;
}

bool inventory_backup_interim_is_end(const InventoryBackup& b, uint64_t boot_id) {
  return b.has_interim && !b.has_exit && !b.left_normally && (b.interim_boot != boot_id);
}

InventoryBackupSource inventory_backup_restore_source(const InventoryBackup& b, uint64_t boot_id) {
  using K = InventoryBackupSource::Kind;
  if (b.has_exit) {
    return InventoryBackupSource{K::EXIT, &b.exit_inventory, b.exit_meseta};
  }
  if (inventory_backup_interim_is_end(b, boot_id)) {
    return InventoryBackupSource{K::INTERIM, &b.interim_inventory, b.interim_meseta};
  }
  return InventoryBackupSource{K::JOIN, &b.inventory, b.disp.stats.meseta.load()};
}

void inventory_backup_record_interim(InventoryBackup& b, const PlayerInventory& inventory, uint32_t meseta,
    bool guard_covered, uint64_t now_usecs, uint64_t boot_id, uint32_t count) {
  b.spire = b.spire || guard_covered;
  b.has_interim = true;
  b.interim_inventory = inventory;
  b.interim_meseta = meseta;
  b.interim_time_usecs = now_usecs;
  b.interim_boot = boot_id;
  b.interim_count = count;
}

bool inventory_backup_interim_changed(const InventoryInterimState& state, const PlayerInventory& inventory,
    uint32_t meseta, bool guard_covered) {
  return !state.written || (state.meseta != meseta) || (state.guard_covered != guard_covered) ||
      (memcmp(&state.inventory, &inventory, sizeof(inventory)) != 0);
}

bool inventory_backup_write_interim(const std::string& path, InventoryInterimState& state,
    const PlayerInventory& inventory, uint32_t meseta, bool guard_covered, uint64_t now_usecs, uint64_t boot_id) {
  if (!inventory_backup_interim_changed(state, inventory, meseta, guard_covered)) {
    return false;
  }
  // Read first: the Loss mark of a later login of the same character lives in this file too
  auto b = inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(path)));
  if (b.has_exit || b.left_normally) {
    return false; // the game has its end; an interim state never goes on top of it
  }
  inventory_backup_record_interim(b, inventory, meseta, guard_covered, now_usecs, boot_id, state.count + 1);
  inventory_backup_write_file(path, b);
  state.written = true;
  state.inventory = inventory;
  state.meseta = meseta;
  state.guard_covered = guard_covered;
  state.count++;
  return true;
}

InventoryLossVerdict inventory_loss_verdict(const InventoryBackup& b, bool session_alive, size_t missing_items,
    uint32_t missing_meseta, bool interim_is_end) {
  using V = InventoryLossVerdict;
  if (b.restored || b.left_normally || b.spire || (!missing_items && !missing_meseta)) {
    return V::NONE;
  }
  return (b.has_exit || !b.imported_from.empty() || session_alive || interim_is_end) ? V::RESTORABLE : V::SERVER_STOP;
}

std::string inventory_loss_text(InventoryLossVerdict v, size_t missing_items, uint32_t missing_meseta) {
  std::string what = std::format("{} item{}", missing_items, (missing_items == 1) ? "" : "s");
  if (missing_meseta) {
    what = missing_items ? std::format("{} en {} Meseta", what, missing_meseta) : std::format("{} Meseta", missing_meseta);
  }
  switch (v) {
    case InventoryLossVerdict::NONE:
      return "";
    case InventoryLossVerdict::RESTORABLE:
      return std::format("Je verloor {} bij een\nhard einde van je vorige game.\nTyp %srestore in een gewone game.", what);
    case InventoryLossVerdict::SERVER_STOP:
      return std::format("Je verloor {} bij een\nharde serverstop. %srestore kan\ndit niet terugzetten.", what);
  }
  return "";
}

const char* inventory_restore_refusal_name(InventoryRestoreRefusal r) {
  switch (r) {
    case InventoryRestoreRefusal::NONE:
      return "none";
    case InventoryRestoreRefusal::NOT_IN_GAME:
      return "not in a game";
    case InventoryRestoreRefusal::SPIRE_GAME:
      return "Spire run";
    case InventoryRestoreRefusal::NO_BACKUP:
      return "no backup";
    case InventoryRestoreRefusal::OTHER_CHARACTER:
      return "backup of another character";
    case InventoryRestoreRefusal::SPIRE_BACKUP:
      return "backup from a Spire run";
    case InventoryRestoreRefusal::ALREADY_RESTORED:
      return "backup already restored";
    case InventoryRestoreRefusal::LEFT_NORMALLY:
      return "previous game ended normally";
    case InventoryRestoreRefusal::UNKNOWN_END:
      return "end of previous game unknown";
  }
  return "?";
}

// Server side

static uint32_t backup_serial_for_client(std::shared_ptr<const Client> c) {
  if (!c->login) {
    return 0;
  }
  if (c->login->gc_license) {
    return c->login->gc_license->serial_number;
  }
  return c->login->account ? c->login->account->account_id : 0;
}

static bool lobby_is_spire(std::shared_ptr<const Client> c, std::shared_ptr<const Lobby> l) {
  if (!l || !l->is_game()) {
    return false;
  }
  if (spire_game_is_spire(c, l)) {
    return true;
  }
  auto s = c->require_server_state();
  return l->quest && s->data->spire_meta_quest_numbers.count(l->quest->meta.quest_number);
}

// The backups whose game is still running, with the client in it: a login of the same character while the older
// connection still sits in the game (until its idle timeout) sees no Exit yet, but one will come
static std::unordered_map<std::string, std::weak_ptr<Client>> open_backups;

static bool backup_session_alive(const std::string& path) {
  auto it = open_backups.find(path);
  if (it == open_backups.end()) {
    return false;
  }
  auto c = it->second.lock();
  return c && (c->inventory_backup_current == path);
}

// Every INVENTORY_BACKUP_INTERIM_USECS while c is in the game of the backup at path: writes the interim state if the
// inventory changed (what came in since the last write; what went out was written at once)
static asio::awaitable<void> interim_ticker(std::weak_ptr<Client> weak_c, std::string path) {
  for (;;) {
    co_await async_sleep(std::chrono::microseconds(INVENTORY_BACKUP_INTERIM_USECS));
    auto c = weak_c.lock();
    if (!c || (c->inventory_backup_current != path)) {
      co_return;
    }
    inventory_backup_flush_interim(c);
  }
}

void inventory_backup_on_game_join(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l) {
  open_backups.erase(c->inventory_backup_current);
  c->inventory_backup_current.clear();
  c->inventory_backup_removals = InventoryRemovals();
  c->inventory_backup_interim = InventoryInterimState();
  c->inventory_backup_interim_urgent = false;
  if (!l || !l->is_game()) {
    return;
  }
  uint32_t serial = backup_serial_for_client(c);
  // The real character, never a Spire run copy (overlay)
  auto p = c->character_file(false, false);
  if (!serial || !p) {
    return;
  }
  try {
    InventoryBackup b;
    b.serial_number = serial;
    b.character_id = spire_guard_character_id(p->disp);
    b.time_usecs = phosg::now();
    b.quest_number = l->quest ? l->quest->meta.quest_number : 0;
    // A Spire backup only once the guard takes its snapshot in this game (inventory_backup_note_guarded)
    b.spire = false;
    c->inventory_backup_guarded = false;
    b.inventory = p->inventory;
    b.disp = p->disp;
    // The first interim state is the join itself, in the same write (B-REGIE-20-4)
    inventory_backup_record_interim(b, p->inventory, p->disp.stats.meseta, false, b.time_usecs,
        inventory_backup_boot_id(), 1);
    std::string dir = inventory_backup_character_directory(INVENTORY_BACKUP_DIRECTORY, serial, p->disp);
    c->inventory_backup_current = inventory_backup_save(dir, b, INVENTORY_BACKUP_KEEP);
    open_backups[c->inventory_backup_current] = c;
    auto& interim = c->inventory_backup_interim;
    interim.written = true;
    interim.inventory = p->inventory;
    interim.meseta = p->disp.stats.meseta;
    interim.guard_covered = false;
    interim.count = 1;
    auto s = c->require_server_state();
    if (s->io_context) {
      asio::co_spawn(*s->io_context, interim_ticker(c, c->inventory_backup_current), asio::detached);
    }
    c->log.info_f("Inventory backup: saved {} Lv{} ({} items, {} Meseta{}) to {}",
        p->disp.visual.name.decode(c->language()), p->disp.stats.level + 1, p->inventory.num_items,
        p->disp.stats.meseta.load(), c->is_spire_protected() ? ", protected character, read only" : "",
        c->inventory_backup_current);
  } catch (const std::exception& e) {
    c->log.warning_f("Inventory backup: cannot save: {}", e.what());
  }
}

void inventory_backup_on_game_end(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, bool normal) {
  if (c->inventory_backup_current.empty() || !l || !l->is_game()) {
    return;
  }
  std::string path = c->inventory_backup_current;
  c->inventory_backup_current.clear();
  open_backups.erase(path);
  bool guarded = c->inventory_backup_guarded;
  c->inventory_backup_guarded = false;
  c->inventory_backup_interim = InventoryInterimState();
  c->inventory_backup_interim_urgent = false;
  try {
    auto b = inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(path)));
    auto p = normal ? nullptr : c->character_file(false, false);
    inventory_backup_record_end(b, normal, guarded, p ? &p->inventory : nullptr, p ? p->disp.stats.meseta.load() : 0);
    inventory_backup_write_file(path, b);
    c->log.info_f("Inventory backup: game ended {} ({}{})", normal ? "normally (98)" : "without a 98 (exit state saved)",
        path, b.spire ? ", Spire run" : "");
  } catch (const std::exception& e) {
    c->log.warning_f("Inventory backup: cannot record the end of the game in {}: {}", path, e.what());
  }
}

bool inventory_backup_set_aside(const std::string& path) {
  std::error_code ec;
  std::filesystem::rename(path, path + INVENTORY_BACKUP_SET_ASIDE_SUFFIX, ec);
  return !ec;
}

void inventory_backup_flush_interim(std::shared_ptr<Client> c) {
  if (c->inventory_backup_current.empty()) {
    return;
  }
  auto& state = c->inventory_backup_interim;
  try {
    auto p = c->character_file(false, false);
    if (!p) {
      return;
    }
    uint64_t start = phosg::now();
    if (!inventory_backup_write_interim(c->inventory_backup_current, state, p->inventory, p->disp.stats.meseta,
            c->inventory_backup_guarded, start, inventory_backup_boot_id())) {
      return;
    }
    uint64_t usecs = phosg::now() - start;
    state.failed = false;
    c->log.info_f("Inventory backup: interim state {} written ({} items, {} Meseta{}, {} us)", state.count,
        p->inventory.num_items, p->disp.stats.meseta.load(), state.guard_covered ? ", Spire run" : "", usecs);
    if (usecs > 50000) {
      c->log.warning_f("Inventory backup: the interim write took {} ms", usecs / 1000);
    }
  } catch (const std::exception& e) {
    if (!state.failed) {
      c->log.warning_f("Inventory backup: cannot write the interim state into {}: {} (retried every {} s)",
          c->inventory_backup_current, e.what(), INVENTORY_BACKUP_INTERIM_USECS / 1000000);
    }
    state.failed = true;
  }
}

void inventory_backup_after_command(std::shared_ptr<Client> c) {
  if (c->inventory_backup_interim_urgent) {
    c->inventory_backup_interim_urgent = false;
    inventory_backup_flush_interim(c);
  }
}

void inventory_backup_note_guarded(std::shared_ptr<Client> c) {
  c->inventory_backup_guarded = true;
  if (c->inventory_backup_current.empty()) {
    return;
  }
  // The Spire mark must be on disk before the run copy exists: after a hard stop the guard restores this game, and an
  // interim state without the mark would let $restore give the same items a second time
  inventory_backup_flush_interim(c);
  if (!c->inventory_backup_interim.written || !c->inventory_backup_interim.guard_covered) {
    // Not written. Take the backup out of $restore's reach instead (it stays on disk under another name)
    std::string path = c->inventory_backup_current;
    bool set_aside = inventory_backup_set_aside(path);
    c->log.error_f("Inventory backup: the Spire mark could not be written into {}; {}", path,
        set_aside ? "set aside as " + path + INVENTORY_BACKUP_SET_ASIDE_SUFFIX + ", $restore will not use it"
                  : "and it could not be set aside either");
    if (set_aside) {
      open_backups.erase(path);
      c->inventory_backup_current.clear();
      c->inventory_backup_interim = InventoryInterimState();
    }
  }
}

static asio::awaitable<void> send_loss_text(std::weak_ptr<Client> weak_c, std::string text) {
  // After the login the client goes to the lobby; the text waits for it (at most two minutes)
  for (size_t tries = 0; tries < 24; tries++) {
    co_await async_sleep(std::chrono::seconds(5));
    auto c = weak_c.lock();
    if (!c || !c->channel->connected()) {
      co_return;
    }
    auto l = c->lobby.lock();
    if (l && !l->is_game() && !c->check_flag(Client::Flag::LOADING)) {
      send_text_message(c, text);
      c->log.info_f("Inventory backup: hard-end text sent in the lobby");
      co_return;
    }
  }
}

void inventory_backup_on_login(std::shared_ptr<Client> c) {
  try {
    uint32_t serial = backup_serial_for_client(c);
    auto p = c->character_file(false, false);
    if (!serial || !p || c->inventory_loss_told) {
      return;
    }
    std::string dir = inventory_backup_character_directory(INVENTORY_BACKUP_DIRECTORY, serial, p->disp);
    auto names = inventory_backup_list(dir);
    auto flags = backup_flags_in(dir, names);
    ssize_t index = inventory_backup_pick_restore(names, flags, "");
    if (index < 0) {
      return;
    }
    std::string path = dir + "/" + names[index];
    auto b = inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(path)));
    // After a hard server stop the interim state stands in for the Exit that was never written
    bool interim_is_end = inventory_backup_interim_is_end(b, inventory_backup_boot_id());
    auto from = inventory_backup_restore_source(b, inventory_backup_boot_id());
    const auto& source = *from.inventory;
    uint32_t source_meseta = from.meseta;
    auto plan = inventory_restore_missing(source, source_meseta, p->inventory, p->disp.stats.meseta, InventoryRemovals());
    auto verdict = inventory_loss_verdict(b, backup_session_alive(path), plan.items.size(), plan.meseta, interim_is_end);
    if (verdict == InventoryLossVerdict::NONE) {
      return;
    }
    std::string name = p->disp.visual.name.decode(c->language());
    c->log.warning_f("Inventory backup: login 61 of {} ({}) misses {} items and {} Meseta of {} ({} items, {} Meseta{}): {}",
        name, c->is_spire_protected() ? "protected character" : "character", plan.items.size(), plan.meseta, path,
        source.num_items, source_meseta,
        (from.kind == InventoryBackupSource::Kind::EXIT) ? ", Exit" : (interim_is_end ? ", interim state after a hard stop" : ", join"),
        (verdict == InventoryLossVerdict::RESTORABLE) ? "hard end, $restore told" : "server stop without an end state, not restorable");
    if (!b.loss_time_usecs) {
      b.loss_time_usecs = phosg::now();
      b.loss_items = plan.items.size();
      b.loss_meseta = plan.meseta;
      inventory_backup_write_file(path, b); // the Loss mark pins it against rotation and later join backups
    }
    c->inventory_loss_told = true;
    asio::co_spawn(*c->require_server_state()->io_context,
        send_loss_text(c, inventory_loss_text(verdict, plan.items.size(), plan.meseta)), asio::detached);
  } catch (const std::exception& e) {
    c->log.warning_f("Inventory backup: login check failed: {}", e.what());
  }
}

size_t inventory_backup_on_server_stop(std::shared_ptr<ServerState> s) {
  size_t count = 0;
  if (!s->game_server) {
    return 0;
  }
  for (const auto& c : s->game_server->all_clients()) {
    auto l = c->lobby.lock();
    if (c->inventory_backup_current.empty() || !l || !l->is_game()) {
      continue;
    }
    inventory_backup_on_game_end(c, l, false);
    count++;
  }
  return count;
}

void inventory_backup_note_removed(std::shared_ptr<Client> c, const ItemData& item) {
  inventory_removals_add(c->inventory_backup_removals, item);
  // Something left the inventory: that goes to disk at once, never with the next bundle (B-REGIE-20-4). Where the
  // caller notes before it removes, the write follows at the end of the command (inventory_backup_after_command)
  c->inventory_backup_interim_urgent = true;
  inventory_backup_flush_interim(c);
}

void inventory_backup_note_meseta_removed(std::shared_ptr<Client> c, uint64_t meseta) {
  c->inventory_backup_removals.meseta += meseta;
  c->inventory_backup_interim_urgent = true;
  inventory_backup_flush_interim(c);
}

std::vector<std::string> inventory_restore_command(std::shared_ptr<Client> c, bool check_only) {
  auto s = c->require_server_state();
  auto l = c->lobby.lock();
  bool in_game = l && l->is_game();
  bool spire_game = in_game && lobby_is_spire(c, l);
  uint32_t serial = backup_serial_for_client(c);
  auto p = c->character_file(false, false);
  if (!serial || !p) {
    return {"Geen personage."};
  }
  std::string character_id = spire_guard_character_id(p->disp);
  std::string name = p->disp.visual.name.decode(c->language());
  bool protected_character = c->is_spire_protected();
  // Only the typing player's own character: the directory comes from his own serial and character
  std::string dir = inventory_backup_character_directory(INVENTORY_BACKUP_DIRECTORY, serial, p->disp);
  auto names = inventory_backup_list(dir);
  std::string current = std::filesystem::path(c->inventory_backup_current).filename().string();
  // Read every backup (at most INVENTORY_BACKUP_KEEP) for its Imported and Restored marks; an unreadable file has
  // neither, so it can only be picked as the newest one, and is then refused as missing
  std::vector<std::unique_ptr<InventoryBackup>> loaded;
  std::vector<InventoryBackupPickFlags> flags;
  for (const auto& backup_name : names) {
    std::unique_ptr<InventoryBackup> b;
    try {
      b = std::make_unique<InventoryBackup>(inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(dir + "/" + backup_name))));
    } catch (const std::exception& e) {
      c->log.warning_f("Restore: cannot read {}/{}: {}", dir, backup_name, e.what());
    }
    flags.emplace_back(b ? backup_pick_flags(*b) : InventoryBackupPickFlags());
    loaded.emplace_back(std::move(b));
  }
  ssize_t index = inventory_backup_pick_restore(names, flags, current);

  std::unique_ptr<InventoryBackup> backup;
  std::string path;
  if (index >= 0) {
    path = dir + "/" + names[index];
    backup = std::move(loaded[index]);
  }

  // A backup without an end whose interim state was written by a server process that is gone: a hard stop, and the
  // interim state is the end state (B-REGIE-20-4)
  bool interim_is_end = backup && inventory_backup_interim_is_end(*backup, inventory_backup_boot_id());
  auto refusal = inventory_restore_refusal(in_game, spire_game, protected_character, serial, character_id, backup.get(),
      interim_is_end);
  if (refusal != InventoryRestoreRefusal::NONE) {
    c->log.info_f("Restore{}: refused for {} ({}): {}{}", check_only ? " check" : "", name,
        protected_character ? "protected character" : "character", inventory_restore_refusal_name(refusal),
        path.empty() ? "" : (" (" + path + ")"));
    return {inventory_restore_refusal_text(refusal)};
  }

  auto from = inventory_backup_restore_source(*backup, inventory_backup_boot_id());
  bool from_interim = (from.kind == InventoryBackupSource::Kind::INTERIM);
  const auto& source = *from.inventory;
  uint32_t source_meseta = from.meseta;
  auto plan = inventory_restore_missing(source, source_meseta, p->inventory, p->disp.stats.meseta,
      c->inventory_backup_removals);
  if (plan.empty()) {
    c->log.info_f("Restore{}: nothing missing for {} against {}{}", check_only ? " check" : "", name, path,
        from_interim ? " (interim state after a hard stop)" : "");
    return {from_interim ? INVENTORY_RESTORE_NOTHING_INTERIM_TEXT : "Niets kwijt ten opzichte van\nde back-up."};
  }

  std::vector<std::string> lines;
  if (from_interim) {
    lines.emplace_back(INVENTORY_RESTORE_INTERIM_TEXT);
    c->log.info_f("Restore{}: {} ended in a hard server stop; using its interim state {} of {}",
        check_only ? " check" : "", path, backup->interim_count, phosg::format_time(backup->interim_time_usecs));
  }
  lines.emplace_back(check_only ? "Zou terugleggen:" : "Teruggelegd:");
  for (const auto& item : plan.items) {
    lines.emplace_back(s->data->describe_item(c->version(), item));
  }
  if (plan.meseta) {
    lines.emplace_back(std::format("{} Meseta", plan.meseta));
  }
  if (check_only) {
    c->log.info_f("Restore check for {} against {}: {} items, {} Meseta (nothing dropped, nothing marked)",
        name, path, plan.items.size(), plan.meseta);
    return lines;
  }

  // Mark the backup first: a second $restore on it must never drop anything again, also if the drop below fails
  plan = inventory_restore_take(*backup, plan, check_only, phosg::now());
  inventory_backup_write_file(path, *backup);

  auto drop = [&](ItemData item) {
    item.id = l->generate_item_id(0xFF);
    l->add_item(c->floor, item, c->pos, nullptr, nullptr, 0x1000 | (1 << c->lobby_client_id));
    send_drop_stacked_item_to_channel(s, c->channel, item, c->floor, c->pos);
    c->log.info_f("Restore: dropped {:08X} {} ({}) for {}", item.id.load(), item_code(item),
        s->data->describe_item(c->version(), item), name);
  };
  for (const auto& item : plan.items) {
    drop(item);
  }
  if (plan.meseta) {
    ItemData meseta;
    meseta.data1[0] = 0x04;
    meseta.data2d = plan.meseta;
    drop(meseta);
  }
  c->log.info_f("Restore: {} ({}) got {} items and {} Meseta back from {}", name,
      protected_character ? "protected character" : "character", plan.items.size(), plan.meseta, path);
  return lines;
}

std::string inventory_backup_import_snapshot(const std::string& snapshot_filename, const std::string& root, uint64_t now_usecs) {
  auto json = phosg::JSON::parse(phosg::load_file(snapshot_filename));
  if (!json.contains("Inventory") || !json.contains("Disp")) {
    throw std::runtime_error("snapshot has no character (Inventory and Disp)");
  }
  InventoryBackup b;
  b.serial_number = json.get_int("SerialNumber");
  spire_guard_hex_decode(json.get_string("Inventory"), &b.inventory, sizeof(b.inventory));
  spire_guard_hex_decode(json.get_string("Disp"), &b.disp, sizeof(b.disp));
  b.character_id = spire_guard_character_id(b.disp);
  b.time_usecs = now_usecs;
  b.quest_number = json.get_int("QuestNumber", 0);
  b.imported_from = std::format("{} (snapshot time {})", snapshot_filename,
      phosg::format_time(json.get_int("SnapshotTime", 0)));
  std::string dir = inventory_backup_character_directory(root, b.serial_number, b.disp);
  return inventory_backup_save(dir, b, INVENTORY_BACKUP_KEEP);
}
