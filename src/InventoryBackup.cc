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

#include "Client.hh"
#include "Lobby.hh"
#include "SendCommands.hh"
#include "ServerState.hh"
#include "SpireFlagBridge.hh"

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
  if (b.has_exit) {
    json.emplace("Exit", phosg::JSON::dict({
        {"Meseta", b.exit_meseta},
        {"Items", items_json(b.exit_inventory)},
        {"Inventory", spire_guard_hex_encode(&b.exit_inventory, sizeof(b.exit_inventory))},
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
  if (json.contains("Exit")) {
    const auto& exit = json.at("Exit");
    b.has_exit = true;
    b.exit_meseta = exit.get_int("Meseta");
    spire_guard_hex_decode(exit.get_string("Inventory"), &b.exit_inventory, sizeof(b.exit_inventory));
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

std::vector<std::string> inventory_backup_files_to_delete(const std::vector<std::string>& sorted_names, size_t keep) {
  if (sorted_names.size() <= keep) {
    return {};
  }
  return std::vector<std::string>(sorted_names.begin(), sorted_names.end() - keep);
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
  ::fsync(fd);
  ::close(fd);
  std::filesystem::rename(temp, path);
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
  for (const auto& name : inventory_backup_files_to_delete(inventory_backup_list(dir), keep)) {
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
        flags[z].imported && !flags[z].restored) {
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
    uint32_t serial_number, const std::string& character_id, const InventoryBackup* backup) {
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
  if (!backup->has_exit && backup->imported_from.empty()) {
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

void inventory_backup_on_game_join(std::shared_ptr<Client> c, std::shared_ptr<Lobby> l) {
  c->inventory_backup_current.clear();
  c->inventory_backup_removals = InventoryRemovals();
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
    b.spire = lobby_is_spire(c, l);
    b.inventory = p->inventory;
    b.disp = p->disp;
    std::string dir = inventory_backup_character_directory(INVENTORY_BACKUP_DIRECTORY, serial, p->disp);
    c->inventory_backup_current = inventory_backup_save(dir, b, INVENTORY_BACKUP_KEEP);
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
  try {
    auto b = inventory_backup_from_json(phosg::JSON::parse(phosg::load_file(path)));
    b.spire = b.spire || lobby_is_spire(c, l);
    if (normal) {
      b.left_normally = true;
    } else {
      auto p = c->character_file(false, false);
      if (p) {
        b.has_exit = true;
        b.exit_inventory = p->inventory;
        b.exit_meseta = p->disp.stats.meseta;
      }
    }
    inventory_backup_write_file(path, b);
    c->log.info_f("Inventory backup: game ended {} ({}{})", normal ? "normally (98)" : "without a 98 (exit state saved)",
        path, b.spire ? ", Spire run" : "");
  } catch (const std::exception& e) {
    c->log.warning_f("Inventory backup: cannot record the end of the game in {}: {}", path, e.what());
  }
}

void inventory_backup_note_removed(std::shared_ptr<Client> c, const ItemData& item) {
  inventory_removals_add(c->inventory_backup_removals, item);
}

void inventory_backup_note_meseta_removed(std::shared_ptr<Client> c, uint64_t meseta) {
  c->inventory_backup_removals.meseta += meseta;
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
    flags.emplace_back(InventoryBackupPickFlags{b && !b->imported_from.empty(), b && b->restored});
    loaded.emplace_back(std::move(b));
  }
  ssize_t index = inventory_backup_pick_restore(names, flags, current);

  std::unique_ptr<InventoryBackup> backup;
  std::string path;
  if (index >= 0) {
    path = dir + "/" + names[index];
    backup = std::move(loaded[index]);
  }

  auto refusal = inventory_restore_refusal(in_game, spire_game, protected_character, serial, character_id, backup.get());
  if (refusal != InventoryRestoreRefusal::NONE) {
    c->log.info_f("Restore{}: refused for {} ({}): {}{}", check_only ? " check" : "", name,
        protected_character ? "protected character" : "character", inventory_restore_refusal_name(refusal),
        path.empty() ? "" : (" (" + path + ")"));
    return {inventory_restore_refusal_text(refusal)};
  }

  const auto& source = backup->has_exit ? backup->exit_inventory : backup->inventory;
  uint32_t source_meseta = backup->has_exit ? backup->exit_meseta : backup->disp.stats.meseta.load();
  auto plan = inventory_restore_missing(source, source_meseta, p->inventory, p->disp.stats.meseta,
      c->inventory_backup_removals);
  if (plan.empty()) {
    c->log.info_f("Restore{}: nothing missing for {} against {}", check_only ? " check" : "", name, path);
    return {"Niets kwijt ten opzichte van\nde back-up."};
  }

  std::vector<std::string> lines;
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
