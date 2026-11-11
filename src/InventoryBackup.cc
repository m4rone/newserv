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

// Own and OwnMeseta of an end state (InventoryBackup.hh, Provenance), added to its dict
static void own_to_json(phosg::JSON& json, const InventoryOwnership& own, bool always) {
  if (always || !own.units.empty()) {
    auto list = phosg::JSON::list();
    for (const auto& [id, units] : own.units) {
      list.emplace_back(phosg::JSON::dict({{"ID", id}, {"Units", units}}));
    }
    json.emplace("Own", std::move(list));
  }
  if (own.has_meseta) {
    json.emplace("OwnMeseta", own.meseta);
  }
}

static InventoryOwnership own_from_json(const phosg::JSON& json) {
  InventoryOwnership own;
  if (json.contains("Own")) {
    own.present = true;
    for (const auto& z : json.at("Own").as_list()) {
      own.units.emplace_back(z->get_int("ID"), z->get_int("Units"));
    }
  }
  if (json.contains("OwnMeseta")) {
    own.present = true;
    own.has_meseta = true;
    own.meseta = json.get_int("OwnMeseta");
  }
  return own;
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
    auto exit = phosg::JSON::dict({
        {"Meseta", b.exit_meseta},
        {"Items", items_json(b.exit_inventory)},
        {"Inventory", spire_guard_hex_encode(&b.exit_inventory, sizeof(b.exit_inventory))},
    });
    own_to_json(exit, b.exit_own, false); // only what differs from the default rule: nothing, for a game without gains
    json.emplace("Exit", std::move(exit));
  }
  // Added last and only when present: a backup without an interim state serializes byte for byte as it did before
  if (b.has_interim) {
    auto interim = phosg::JSON::dict({
        {"Time", b.interim_time_usecs},
        {"TimeText", phosg::format_time(b.interim_time_usecs)},
        {"Boot", b.interim_boot},
        {"Count", b.interim_count},
        {"Meseta", b.interim_meseta},
        {"Items", items_json(b.interim_inventory)},
        {"Inventory", spire_guard_hex_encode(&b.interim_inventory, sizeof(b.interim_inventory))},
    });
    if (b.interim_own.present) {
      own_to_json(interim, b.interim_own, true);
    }
    json.emplace("Interim", std::move(interim));
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
    b.exit_own = own_from_json(exit);
  }
  if (json.contains("Interim")) {
    const auto& interim = json.at("Interim");
    b.has_interim = true;
    b.interim_time_usecs = interim.get_int("Time");
    b.interim_boot = interim.get_int("Boot");
    b.interim_count = interim.get_int("Count", 0);
    b.interim_meseta = interim.get_int("Meseta");
    spire_guard_hex_decode(interim.get_string("Inventory"), &b.interim_inventory, sizeof(b.interim_inventory));
    b.interim_own = own_from_json(interim);
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

InventoryBackupSetAside inventory_backup_set_aside_kind(const std::string& name) {
  if (name.ends_with(INVENTORY_BACKUP_LOST_SUFFIX)) {
    return InventoryBackupSetAside::LOST;
  }
  if (name.ends_with(INVENTORY_BACKUP_UNMARKED_SUFFIX)) {
    return InventoryBackupSetAside::UNMARKED;
  }
  return InventoryBackupSetAside::NONE;
}

std::string inventory_backup_set_aside_as(const std::string& path, const char* suffix) {
  if (!path.ends_with(".json") || (inventory_backup_set_aside_kind(path) != InventoryBackupSetAside::NONE)) {
    return "";
  }
  std::string aside = path.substr(0, path.size() - 5) + suffix;
  std::error_code ec;
  std::filesystem::rename(path, aside, ec);
  return ec ? "" : aside;
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
        inventory_backup_pinned(flags[z]) && (!flags[z].superseded || flags[z].imported)) {
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

// Provenance (InventoryBackup.hh)

namespace {
// item id -> units, for the items of an inventory
std::unordered_map<uint32_t, uint32_t> units_by_id(const PlayerInventory& inventory) {
  std::unordered_map<uint32_t, uint32_t> ret;
  for (size_t z = 0; z < std::min<size_t>(inventory.num_items, inventory.items.size()); z++) {
    const auto& item = inventory.items[z].data;
    if (!item.empty()) {
      ret[item.id.load()] += entry_count(item);
    }
  }
  return ret;
}

// Whether an item of an end state can be the join item that had the same id: the same type (data1[0-2], a mag
// data1[0-1]). An id alone is not enough for a file written without provenance
bool same_kind(const ItemData& a, const ItemData& b) {
  size_t len = (a.data1[0] == 0x02) ? 2 : 3;
  return !memcmp(&a.data1[0], &b.data1[0], len);
}
} // namespace

uint32_t inventory_provenance_units(const ItemData& item) {
  return (item.data1[0] == 0x04) ? item.data2d.load() : entry_count(item);
}

void inventory_provenance_begin(InventoryProvenance& prov, const PlayerInventory& inventory, uint32_t meseta) {
  prov = InventoryProvenance();
  prov.seen = units_by_id(inventory);
  prov.seen_meseta = meseta;
}

void inventory_provenance_credit(InventoryProvenance& prov, const PlayerInventory& inventory, const ItemData& item,
    uint32_t units) {
  if (item.data1[0] == 0x04) {
    prov.credit_meseta += units;
    return;
  }
  // The slot the units landed in: the item's own id, or for a tool that joined a stack, that stack
  const ItemData* landed = nullptr;
  for (size_t z = 0; z < std::min<size_t>(inventory.num_items, inventory.items.size()); z++) {
    const auto& slot = inventory.items[z].data;
    if (slot.id == item.id) {
      landed = &slot;
      break;
    }
    if (!landed && is_tool(item) && !memcmp(&slot.data1[0], &item.data1[0], 3)) {
      landed = &slot;
    }
  }
  if (landed) {
    prov.credit[landed->id.load()] += units;
  }
}

void inventory_provenance_reconcile(InventoryProvenance& prov, const PlayerInventory& inventory, uint32_t meseta) {
  auto now = units_by_id(inventory);
  for (const auto& [id, units] : now) {
    auto seen_it = prov.seen.find(id);
    uint32_t before = (seen_it == prov.seen.end()) ? 0 : seen_it->second;
    if (units > before) {
      uint32_t gain = units - before;
      auto credit_it = prov.credit.find(id);
      uint32_t proven = std::min(gain, (credit_it == prov.credit.end()) ? 0 : credit_it->second);
      if (gain > proven) {
        prov.foreign[id] += gain - proven;
      }
    }
  }
  // What left a slot is counted as its proven units first: the foreign units stay, up to what the slot still holds
  for (auto it = prov.foreign.begin(); it != prov.foreign.end();) {
    auto now_it = now.find(it->first);
    if (now_it == now.end()) {
      it = prov.foreign.erase(it);
    } else {
      it->second = std::min(it->second, now_it->second);
      ++it;
    }
  }
  if (meseta > prov.seen_meseta) {
    uint32_t gain = meseta - prov.seen_meseta;
    prov.foreign_meseta += gain - std::min(gain, prov.credit_meseta);
  }
  prov.foreign_meseta = std::min(prov.foreign_meseta, meseta);
  prov.seen = std::move(now);
  prov.seen_meseta = meseta;
  prov.credit.clear();
  prov.credit_meseta = 0;
}

uint32_t inventory_provenance_own_units(const InventoryProvenance& prov, const ItemData& item) {
  auto seen_it = prov.seen.find(item.id.load());
  if ((item.data1[0] == 0x04) || (seen_it == prov.seen.end())) {
    return 0;
  }
  auto foreign_it = prov.foreign.find(item.id.load());
  uint32_t foreign = (foreign_it == prov.foreign.end()) ? 0 : foreign_it->second;
  return std::min<uint32_t>(seen_it->second - std::min(seen_it->second, foreign), entry_count(item));
}

namespace {
struct JoinSlot {
  uint32_t units = 0;
  const ItemData* item = nullptr;
};
std::unordered_map<uint32_t, JoinSlot> join_slots(const PlayerInventory& join) {
  std::unordered_map<uint32_t, JoinSlot> ret;
  for (size_t z = 0; z < std::min<size_t>(join.num_items, join.items.size()); z++) {
    const auto& item = join.items[z].data;
    if (!item.empty()) {
      auto& slot = ret[item.id.load()];
      slot.units += entry_count(item);
      if (!slot.item) {
        slot.item = &item;
      }
    }
  }
  return ret;
}
} // namespace

InventoryOwnership inventory_provenance_ownership(const InventoryProvenance& prov, const PlayerInventory& join,
    uint32_t join_meseta, const PlayerInventory& end, uint32_t end_meseta) {
  InventoryOwnership own;
  own.present = true;
  auto in_join = join_slots(join);
  auto held = units_by_id(end);
  for (size_t z = 0; z < std::min<size_t>(end.num_items, end.items.size()); z++) {
    const auto& item = end.items[z].data;
    auto held_it = held.find(item.id.load());
    if (item.empty() || (held_it == held.end())) {
      continue;
    }
    uint32_t units = held_it->second;
    held.erase(held_it); // an id is handled once, at its first slot
    auto foreign_it = prov.foreign.find(item.id.load());
    uint32_t proven = units - std::min(units, (foreign_it == prov.foreign.end()) ? 0 : foreign_it->second);
    auto join_it = in_join.find(item.id.load());
    uint32_t by_default = ((join_it != in_join.end()) && same_kind(*join_it->second.item, item))
        ? std::min(join_it->second.units, units)
        : 0;
    if (proven != by_default) {
      own.units.emplace_back(item.id.load(), proven);
    }
  }
  std::sort(own.units.begin(), own.units.end());
  uint32_t proven_meseta = end_meseta - std::min(end_meseta, prov.foreign_meseta);
  if (proven_meseta != std::min(join_meseta, end_meseta)) {
    own.has_meseta = true;
    own.meseta = proven_meseta;
  }
  return own;
}

PlayerInventory inventory_backup_own_state(const PlayerInventory& join, uint32_t join_meseta, const PlayerInventory& end,
    uint32_t end_meseta, const InventoryOwnership& own, uint32_t& own_meseta) {
  // Per item id the units that may still be handed out: what own names, else what the join inventory held under that id
  struct Budget {
    uint32_t units = 0;
    const ItemData* join_item = nullptr; // null: named by own (no type check needed, the server followed the id)
  };
  std::unordered_map<uint32_t, Budget> budget;
  for (const auto& [id, slot] : join_slots(join)) {
    budget[id] = Budget{slot.units, slot.item};
  }
  for (const auto& [id, units] : own.units) {
    budget[id] = Budget{units, nullptr};
  }
  PlayerInventory ret = end;
  ret.num_items = 0;
  for (size_t z = 0; z < ret.items.size(); z++) {
    ret.items[z] = PlayerInventoryItem();
  }
  for (size_t z = 0; z < std::min<size_t>(end.num_items, end.items.size()); z++) {
    const auto& item = end.items[z].data;
    auto it = budget.find(item.id.load());
    if (item.empty() || (it == budget.end()) || (it->second.join_item && !same_kind(*it->second.join_item, item))) {
      continue;
    }
    uint32_t take = std::min(entry_count(item), it->second.units);
    if (!take) {
      continue;
    }
    it->second.units -= take;
    auto& slot = ret.items[ret.num_items++];
    slot = end.items[z];
    if (is_tool(item) && item.data1[5]) {
      slot.data.data1[5] = take;
    }
  }
  own_meseta = std::min(end_meseta, own.has_meseta ? own.meseta : join_meseta);
  return ret;
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
  if (backup->set_aside == InventoryBackupSetAside::LOST) {
    return R::NOT_UPDATED; // before anything its content says: the content is what could not be written
  }
  if (backup->set_aside == InventoryBackupSetAside::UNMARKED) {
    return R::SPIRE_BACKUP; // its name is the Spire mark, also if its content cannot be read
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

namespace {
bool same_content(const PlayerInventory& a, uint32_t a_meseta, const PlayerInventory& b, uint32_t b_meseta) {
  return (a_meseta == b_meseta) &&
      inventory_restore_missing(a, 0, b, 0, InventoryRemovals()).items.empty() &&
      inventory_restore_missing(b, 0, a, 0, InventoryRemovals()).items.empty();
}
} // namespace

bool inventory_backup_played_on(const InventoryBackup* b) {
  if (!b || (b->set_aside != InventoryBackupSetAside::NONE)) {
    // unreadable, or set aside (LOST, UNMARKED): what is on disk is not how that game ended, and nothing was written
    // into it after it got its name. Nobody knows what he did in that game
    return true;
  }
  // A game the Spire guard covered counts like every other game (critic round 2, B1). The guard puts back the run, not
  // the game: before the run starts and after the return to the base ('O') the real character plays in that same game,
  // with bank, shop and partner. So its 98 counts as played on, and without a 98 the server's own view of the real
  // character at the end (Exit or interim state; never the run copy) is compared with the join, as for any game
  if (b->left_normally) {
    return true;
  }
  if (b->has_exit) {
    return !same_content(b->inventory, b->disp.stats.meseta, b->exit_inventory, b->exit_meseta);
  }
  if (b->has_interim) {
    return !same_content(b->inventory, b->disp.stats.meseta, b->interim_inventory, b->interim_meseta);
  }
  return true;
}

InventoryBackupListing inventory_backup_read_all(const std::string& dir, const std::string& current_basename) {
  InventoryBackupListing ret;
  ret.names = inventory_backup_list(dir);
  for (const auto& name : ret.names) {
    std::unique_ptr<InventoryBackup> b;
    auto set_aside = inventory_backup_set_aside_kind(name);
    try {
      b = std::make_unique<InventoryBackup>(inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(dir + "/" + name))));
    } catch (const std::exception& e) {
      ret.unreadable.emplace_back(name + ": " + e.what());
      if (set_aside != InventoryBackupSetAside::NONE) {
        b = std::make_unique<InventoryBackup>(); // its name says enough: it is that game, and it is refused
      }
    }
    if (b) {
      b->set_aside = set_aside;
      // The name carries the Spire mark its content could not take: $restore refuses it as a Spire backup and the
      // login check leaves it to the guard (critic M1). It does count as a game he played on in (round 2, B1)
      b->spire = b->spire || (set_aside == InventoryBackupSetAside::UNMARKED);
    }
    ret.flags.emplace_back(b ? backup_pick_flags(*b) : InventoryBackupPickFlags());
    ret.backups.emplace_back(std::move(b));
  }
  bool played_on_later = false;
  for (ssize_t z = static_cast<ssize_t>(ret.names.size()) - 1; z >= 0; z--) {
    ret.flags[z].superseded = played_on_later;
    if ((ret.names[z] != current_basename) && inventory_backup_played_on(ret.backups[z].get())) {
      played_on_later = true;
    }
  }
  return ret;
}

InventoryRestoreDecision inventory_restore_decide(const std::string& dir, const std::string& current_basename,
    uint64_t boot_id, bool in_game, bool spire_game, bool protected_character, uint32_t serial_number,
    const std::string& character_id, const PlayerInventory& current, uint32_t current_meseta,
    const InventoryRemovals& removed) {
  using R = InventoryRestoreRefusal;
  InventoryRestoreDecision ret;
  // Every backup is read (at most INVENTORY_BACKUP_KEEP plus the pinned ones) for its marks; an unreadable file has
  // none, so it can only be picked as the newest one, and is then refused as missing
  auto listing = inventory_backup_read_all(dir, current_basename);
  ret.unreadable = std::move(listing.unreadable);
  ssize_t index = inventory_backup_pick_restore(listing.names, listing.flags, current_basename);
  // A hard-end backup with a Loss mark that the player played on after: not picked any more (critic B2)
  bool expired = false;
  for (size_t z = 0; z < listing.names.size(); z++) {
    const auto& f = listing.flags[z];
    expired |= (listing.names[z] != current_basename) && inventory_backup_pinned(f) && f.superseded && !f.imported;
  }
  if (index >= 0) {
    ret.path = dir + "/" + listing.names[index];
    ret.backup = std::move(listing.backups[index]);
  }
  // A backup without an end whose interim state was written by a server process that is gone: a hard stop, and the
  // proven part of the interim state is the end state (B-REGIE-20-4, critic B1)
  ret.interim_is_end = ret.backup && inventory_backup_interim_is_end(*ret.backup, boot_id);
  ret.refusal = inventory_restore_refusal(in_game, spire_game, protected_character, serial_number, character_id,
      ret.backup.get(), ret.interim_is_end);
  if (ret.refusal == R::NONE) {
    auto from = inventory_backup_restore_source(*ret.backup, boot_id);
    ret.source_kind = static_cast<int>(from.kind);
    ret.plan = inventory_restore_missing(from.inventory, from.meseta, current, current_meseta, removed);
  }
  // He lost something at a hard end and played on: say that, instead of the refusal or the "nothing missing" of the
  // later game that stands in its place now
  if (expired && (((ret.refusal != R::NONE) && (ret.refusal != R::NOT_IN_GAME) && (ret.refusal != R::SPIRE_GAME)) ||
                     ((ret.refusal == R::NONE) && ret.plan.empty()))) {
    ret.refusal = R::SUPERSEDED;
    ret.plan = InventoryRestorePlan();
  }
  return ret;
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
    case InventoryRestoreRefusal::SUPERSEDED:
      return "Na dat verlies speelde je verder:\nniet meer herstelbaar.";
    case InventoryRestoreRefusal::NOT_UPDATED:
      return "Die back-up kon niet worden\nbijgewerkt: niet herstelbaar.";
  }
  return "";
}

void inventory_backup_record_end(InventoryBackup& b, bool normal, bool guard_covered, const PlayerInventory* inventory,
    uint32_t meseta, const InventoryOwnership* own) {
  b.spire = b.spire || guard_covered;
  if (normal) {
    b.left_normally = true;
  } else if (inventory) {
    b.has_exit = true;
    b.exit_inventory = *inventory;
    b.exit_meseta = meseta;
    b.exit_own = own ? *own : InventoryOwnership();
  }
  if (b.left_normally || b.has_exit) {
    // The game has its end: the interim state is over, and the file is what it was before interim states existed
    b.has_interim = false;
    b.interim_inventory = PlayerInventory();
    b.interim_meseta = 0;
    b.interim_time_usecs = 0;
    b.interim_boot = 0;
    b.interim_count = 0;
    b.interim_own = InventoryOwnership();
  }
}

uint64_t inventory_backup_boot_id() {
  static const uint64_t boot_id = std::max<uint64_t>(phosg::now(), 1);
  return boot_id;
}

bool inventory_backup_interim_is_end(const InventoryBackup& b, uint64_t boot_id) {
  // An interim state that does not say what of it is proven the player's own (written before provenance existed) is
  // never an end state: it would give back what another player's card still holds (critic B1)
  return b.has_interim && b.interim_own.present && !b.has_exit && !b.left_normally && (b.interim_boot != boot_id) &&
      (b.set_aside == InventoryBackupSetAside::NONE);
}

InventoryBackupSource inventory_backup_restore_source(const InventoryBackup& b, uint64_t boot_id) {
  using K = InventoryBackupSource::Kind;
  if (b.has_exit) {
    // Not the Exit itself either: what the receiver got from another player is in it, and on the giver's card (M4)
    InventoryBackupSource from;
    from.kind = K::EXIT;
    from.inventory = inventory_backup_own_state(b.inventory, b.disp.stats.meseta, b.exit_inventory, b.exit_meseta,
        b.exit_own, from.meseta);
    return from;
  }
  if (inventory_backup_interim_is_end(b, boot_id)) {
    // Not the interim state itself: only what of it is proven the player's own (critic B1)
    InventoryBackupSource from;
    from.kind = K::INTERIM;
    from.inventory = inventory_backup_own_state(b.inventory, b.disp.stats.meseta, b.interim_inventory, b.interim_meseta,
        b.interim_own, from.meseta);
    return from;
  }
  return InventoryBackupSource{K::JOIN, b.inventory, b.disp.stats.meseta.load()};
}

void inventory_backup_record_interim(InventoryBackup& b, const PlayerInventory& inventory, uint32_t meseta,
    bool guard_covered, uint64_t now_usecs, uint64_t boot_id, uint32_t count, const InventoryOwnership& own) {
  b.spire = b.spire || guard_covered;
  b.has_interim = true;
  b.interim_inventory = inventory;
  b.interim_meseta = meseta;
  b.interim_time_usecs = now_usecs;
  b.interim_boot = boot_id;
  b.interim_count = count;
  b.interim_own = own;
  b.interim_own.present = true;
}

bool inventory_backup_interim_changed(const InventoryInterimState& state, const PlayerInventory& inventory,
    uint32_t meseta, bool guard_covered) {
  return !state.written || (state.meseta != meseta) || (state.guard_covered != guard_covered) ||
      (memcmp(&state.inventory, &inventory, sizeof(inventory)) != 0);
}

bool inventory_backup_write_interim(const std::string& path, InventoryInterimState& state,
    const PlayerInventory& inventory, uint32_t meseta, bool guard_covered, uint64_t now_usecs, uint64_t boot_id,
    const InventoryProvenance* prov) {
  if (!inventory_backup_interim_changed(state, inventory, meseta, guard_covered)) {
    return false;
  }
  // Read first: the Loss mark of a later login of the same character lives in this file too
  auto b = inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(path)));
  if (b.has_exit || b.left_normally) {
    return false; // the game has its end; an interim state never goes on top of it
  }
  inventory_backup_record_interim(b, inventory, meseta, guard_covered, now_usecs, boot_id, state.count + 1,
      prov ? inventory_provenance_ownership(*prov, b.inventory, b.disp.stats.meseta, inventory, meseta)
           : InventoryOwnership());
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
    case InventoryRestoreRefusal::SUPERSEDED:
      return "played on after the hard end";
    case InventoryRestoreRefusal::NOT_UPDATED:
      return "backup set aside: a removal could not be written";
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

// The backup at path is given up by this living process without an end in it: set it aside as LOST, so no later
// process reads its game as a hard stop (critic M2). Returns the line to log; error is set if the rename failed
static std::string abandon_backup(const std::string& path, const char* why, bool& error) {
  error = false;
  std::error_code ec;
  if (path.empty() || !std::filesystem::exists(path, ec)) {
    return "";
  }
  std::string aside = inventory_backup_set_aside_as(path, INVENTORY_BACKUP_LOST_SUFFIX);
  if (!aside.empty()) {
    return std::format("Inventory backup: {} in {}; set aside as {}, $restore will refuse it", why, path, aside);
  }
  error = true;
  return std::format("Inventory backup: {} in {} and the backup could not be set aside either: after a restart it reads as a hard server stop",
      why, path);
}

static void abandon_backup(std::shared_ptr<Client> c, const std::string& path, const char* why) {
  bool error = false;
  std::string line = abandon_backup(path, why, error);
  if (!line.empty()) {
    if (error) {
      c->log.error_f("{}", line);
    } else {
      c->log.warning_f("{}", line);
    }
  }
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
  // A backup that is still open here never got its end (no 98 and no disconnect reached it): critic M2
  abandon_backup(c, c->inventory_backup_current, "the next join came before the end of the game");
  open_backups.erase(c->inventory_backup_current);
  c->inventory_backup_current.clear();
  c->inventory_backup_removals = InventoryRemovals();
  c->inventory_backup_interim = InventoryInterimState();
  c->inventory_backup_interim_urgent = false;
  c->inventory_backup_provenance = InventoryProvenance();
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
    inventory_provenance_begin(c->inventory_backup_provenance, p->inventory, p->disp.stats.meseta);
    inventory_backup_record_interim(b, p->inventory, p->disp.stats.meseta, false, b.time_usecs,
        inventory_backup_boot_id(), 1, InventoryOwnership());
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
  if (c->inventory_backup_current.empty()) {
    return;
  }
  bool in_game = l && l->is_game();
  std::string path = c->inventory_backup_current;
  c->inventory_backup_current.clear();
  open_backups.erase(path);
  bool guarded = c->inventory_backup_guarded;
  c->inventory_backup_guarded = false;
  c->inventory_backup_interim = InventoryInterimState();
  c->inventory_backup_interim_urgent = false;
  // The end is recorded for a client in its game, and for a 98 wherever the server has it (a 98 is the end of the only
  // game this backup can belong to). Anything else, and an end that cannot be read or written, leaves a backup this
  // living process gives up without an end: set aside (critic M2)
  bool recorded = false;
  if (in_game || normal) try {
    auto b = inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(path)));
    auto p = normal ? nullptr : c->character_file(false, false);
    InventoryOwnership own;
    if (p) {
      inventory_provenance_reconcile(c->inventory_backup_provenance, p->inventory, p->disp.stats.meseta);
      own = inventory_provenance_ownership(c->inventory_backup_provenance, b.inventory, b.disp.stats.meseta, p->inventory,
          p->disp.stats.meseta);
    }
    inventory_backup_record_end(b, normal, guarded, p ? &p->inventory : nullptr, p ? p->disp.stats.meseta.load() : 0,
        p ? &own : nullptr);
    if (b.left_normally || b.has_exit) {
      inventory_backup_write_file(path, b);
      recorded = true;
      c->log.info_f("Inventory backup: game ended {} ({}{})", normal ? "normally (98)" : "without a 98 (exit state saved)",
          path, b.spire ? ", Spire run" : "");
    }
  } catch (const std::exception& e) {
    c->log.warning_f("Inventory backup: cannot record the end of the game in {}: {}", path, e.what());
  }
  if (!recorded) {
    abandon_backup(c, path, "the end of the game could not be recorded");
  }
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
    inventory_provenance_reconcile(c->inventory_backup_provenance, p->inventory, p->disp.stats.meseta);
    if (!inventory_backup_write_interim(c->inventory_backup_current, state, p->inventory, p->disp.stats.meseta,
            c->inventory_backup_guarded, start, inventory_backup_boot_id(), &c->inventory_backup_provenance)) {
      return;
    }
    uint64_t usecs = phosg::now() - start;
    state.failed = false;
    state.removal_unwritten = false;
    state.set_aside_failed = false;
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
    // Critic B3: after a removal the state on disk still holds what left. Never restore from it
    if (c->inventory_backup_interim_urgent || state.removal_unwritten) {
      state.removal_unwritten = true;
      std::string path = c->inventory_backup_current;
      std::string aside = inventory_backup_set_aside_as(path, INVENTORY_BACKUP_LOST_SUFFIX);
      if (!aside.empty()) {
        c->log.error_f("Inventory backup: a removal could not be written into {}; set aside as {}, $restore will refuse it and this game has no backup any more",
            path, aside);
        open_backups.erase(path);
        c->inventory_backup_current.clear();
        c->inventory_backup_interim = InventoryInterimState();
      } else if (!state.set_aside_failed) {
        state.set_aside_failed = true;
        c->log.error_f("Inventory backup: a removal could not be written into {} and the backup could not be set aside either: after a hard stop now $restore would give back what left the inventory",
            path);
      }
    }
  }
}

void inventory_backup_after_command(std::shared_ptr<Client> c) {
  if (c->inventory_backup_interim_urgent) {
    // the mark stays up during the write: a write that fails now fails after a removal (critic B3)
    inventory_backup_flush_interim(c);
    c->inventory_backup_interim_urgent = false;
  }
}

void inventory_backup_after_subcommand(std::shared_ptr<Client> c) {
  if (c->inventory_backup_current.empty()) {
    return;
  }
  try {
    auto p = c->character_file(false, false);
    if (p) {
      inventory_provenance_reconcile(c->inventory_backup_provenance, p->inventory, p->disp.stats.meseta);
    }
  } catch (const std::exception&) {
  }
}

static void set_floor_item_world(std::shared_ptr<Lobby> l, uint8_t floor, uint32_t item_id) {
  try {
    if (l && l->item_exists(floor, item_id)) {
      l->find_item(floor, item_id)->backup_origin.world = true;
    }
  } catch (const std::exception&) {
  }
}

void inventory_backup_note_floor_world(std::shared_ptr<Lobby> l, uint8_t floor, uint32_t item_id) {
  set_floor_item_world(l, floor, item_id);
}

void inventory_backup_note_leader_drop(std::shared_ptr<Lobby> l, uint8_t floor, uint32_t item_id, const void* entity,
    bool request_seen) {
  // One item per enemy or box, and only for one whose drop request the server itself marked: a second item for the
  // same entity, or one for an entity nobody asked a drop for, is of unknown origin
  if (l && entity && request_seen && l->inventory_backup_drop_sources.emplace(entity).second) {
    set_floor_item_world(l, floor, item_id);
  }
}

void inventory_backup_note_dropped(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l, uint8_t floor, const ItemData& item) {
  if (c->inventory_backup_current.empty()) {
    return;
  }
  try {
    if (l && l->item_exists(floor, item.id)) {
      auto fi = l->find_item(floor, item.id);
      fi->backup_origin.world = false;
      fi->backup_origin.dropper = c;
      fi->backup_origin.dropper_own = inventory_provenance_own_units(c->inventory_backup_provenance, item);
    }
  } catch (const std::exception&) {
  }
}

void inventory_backup_note_picked_up(std::shared_ptr<Client> c, const ItemData& item, const InventoryFloorOrigin& origin) {
  if (c->inventory_backup_current.empty()) {
    return;
  }
  try {
    auto p = c->character_file(false, false);
    if (!p) {
      return;
    }
    uint32_t units = inventory_provenance_units(item);
    // Proven: an item the server vouches for, or the units of his own drop that were proven his when he dropped it.
    // An item another player dropped, or one of unknown origin, gets no credit and is foreign after the reconcile
    uint32_t proven = origin.world ? units : ((origin.dropper.lock() == c) ? std::min(units, origin.dropper_own) : 0);
    auto& prov = c->inventory_backup_provenance;
    if (proven) {
      inventory_provenance_credit(prov, p->inventory, item, proven);
    }
    inventory_provenance_reconcile(prov, p->inventory, p->disp.stats.meseta);
  } catch (const std::exception&) {
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
    // Not written. Give the backup a name that says it instead: it stays in the list as this game, refused as a Spire
    // backup, so $restore never works from an older game in its place (critic M1)
    std::string path = c->inventory_backup_current;
    std::string aside = inventory_backup_set_aside_as(path, INVENTORY_BACKUP_UNMARKED_SUFFIX);
    bool set_aside = !aside.empty();
    c->log.error_f("Inventory backup: the Spire mark could not be written into {}; {}", path,
        set_aside ? "set aside as " + aside + ", $restore refuses it as a Spire backup"
                  : "and it could not be set aside either (the mark is retried with every interim write)");
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
    // The backup $restore would use; a hard-end backup the player played on after is not it any more (critic B2)
    auto listing = inventory_backup_read_all(dir, "");
    ssize_t index = inventory_backup_pick_restore(listing.names, listing.flags, "");
    if (index < 0) {
      return;
    }
    std::string path = dir + "/" + listing.names[index];
    if (!listing.backups[index]) {
      c->log.warning_f("Inventory backup: login check cannot read {}", path);
      return;
    }
    auto& b = *listing.backups[index];
    // After a hard server stop the interim state stands in for the Exit that was never written
    bool interim_is_end = inventory_backup_interim_is_end(b, inventory_backup_boot_id());
    auto from = inventory_backup_restore_source(b, inventory_backup_boot_id());
    const auto& source = from.inventory;
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
  if (s->game_server) {
    for (const auto& c : s->game_server->all_clients()) {
      auto l = c->lobby.lock();
      if (c->inventory_backup_current.empty() || !l || !l->is_game()) {
        continue;
      }
      inventory_backup_on_game_end(c, l, false);
      count++;
    }
  }
  // Every backup that is still open now belongs to no client in a game: its end never came in this process, and after
  // this stop its interim state would read as a hard stop (critic M2)
  std::vector<std::pair<std::string, std::weak_ptr<Client>>> left(open_backups.begin(), open_backups.end());
  open_backups.clear();
  for (const auto& [path, weak_c] : left) {
    bool error = false;
    std::string line = abandon_backup(path, "the server stops and the end of the game never came", error);
    if (!line.empty()) {
      if (error) {
        phosg::log_error_f("{}", line);
      } else {
        phosg::log_warning_f("{}", line);
      }
    }
    auto c = weak_c.lock();
    if (c && (c->inventory_backup_current == path)) {
      c->inventory_backup_current.clear();
    }
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
  std::string current = std::filesystem::path(c->inventory_backup_current).filename().string();
  auto decision = inventory_restore_decide(dir, current, inventory_backup_boot_id(), in_game, spire_game,
      protected_character, serial, character_id, p->inventory, p->disp.stats.meseta, c->inventory_backup_removals);
  for (const auto& what : decision.unreadable) {
    c->log.warning_f("Restore: cannot read {}/{}", dir, what);
  }
  auto& backup = decision.backup;
  const std::string& path = decision.path;
  auto refusal = decision.refusal;
  if (refusal != InventoryRestoreRefusal::NONE) {
    c->log.info_f("Restore{}: refused for {} ({}): {}{}", check_only ? " check" : "", name,
        protected_character ? "protected character" : "character", inventory_restore_refusal_name(refusal),
        path.empty() ? "" : (" (" + path + ")"));
    return {inventory_restore_refusal_text(refusal)};
  }

  bool from_interim = (decision.source_kind == static_cast<int>(InventoryBackupSource::Kind::INTERIM));
  auto plan = decision.plan;
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
    inventory_backup_note_floor_world(l, c->floor, item.id); // the server's own drop, visible to him alone
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
