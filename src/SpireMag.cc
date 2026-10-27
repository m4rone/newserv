#include "SpireMag.hh"

#include <algorithm>
#include <format>
#include <phosg/JSON.hh>
#include <stdexcept>

static SpireMagRow spire_mag_parse_row(const phosg::JSON& json, const std::string& where) {
  SpireMagRow row;
  int64_t id = json.get_int("Id");
  if ((id < 0) || (id > 0xFF)) {
    throw std::runtime_error(std::format("{}: Id {} is no data1[1] value", where, id));
  }
  row.id = id;
  uint16_t* stats[4] = {&row.def, &row.pow, &row.dex, &row.mind};
  const char* names[4] = {"Def", "Pow", "Dex", "Mind"};
  uint32_t sum = 0;
  for (size_t z = 0; z < 4; z++) {
    int64_t v = json.get_int(names[z]);
    if ((v < 0) || (v > 200)) {
      throw std::runtime_error(std::format("{}: {} {} outside 0-200", where, names[z], v));
    }
    *stats[z] = v;
    sum += v;
  }
  if (sum > 200) {
    throw std::runtime_error(std::format("{}: level {} above 200", where, sum));
  }
  return row;
}

SpireMagTable spire_mag_parse(const std::string& json_text) {
  auto json = phosg::JSON::parse(json_text);
  SpireMagTable ret;
  ret.enabled = json.get_bool("Enabled");
  for (const auto& [name, line_json] : json.get_dict("Classes")) {
    if ((name != "HU") && (name != "RA") && (name != "FO")) {
      throw std::runtime_error(std::format("class {} is not HU, RA or FO", name));
    }
    SpireMagLine line;
    size_t z = 0;
    for (const auto& row_json : line_json->get_list("Steps")) {
      line.steps.emplace_back(spire_mag_parse_row(*row_json, std::format("{} step {}", name, z++)));
    }
    if (line.steps.empty()) {
      throw std::runtime_error(std::format("class {} has no steps", name));
    }
    if (line_json->contains("StartFloors")) {
      for (const auto& floor_json : line_json->get_list("StartFloors")) {
        SpireMagFloor floor;
        floor.min_bonus = floor_json->get_int("MinBonus");
        floor.row = spire_mag_parse_row(*floor_json, std::format("{} floor {}", name, floor.min_bonus));
        if (!line.floors.empty() && (floor.min_bonus <= line.floors.back().min_bonus)) {
          throw std::runtime_error(std::format("class {}: StartFloors not ascending at {}", name, floor.min_bonus));
        }
        line.floors.emplace_back(floor);
      }
    }
    ret.classes.emplace(name, std::move(line));
  }
  for (const auto& k : json.get_list("BossRooms")) {
    int64_t v = k->as_int();
    if ((v < 1) || (v > 0xFF)) {
      throw std::runtime_error(std::format("boss room {} outside 1-255", v));
    }
    ret.boss_rooms.emplace(v);
  }
  return ret;
}

const char* spire_mag_archetype(uint8_t char_class) {
  switch (char_class) {
    case 0: // HUmar
    case 1: // HUnewearl
    case 2: // HUcast
    case 9: // HUcaseal
      return "HU";
    case 3: // RAmar
    case 4: // RAcast
    case 5: // RAcaseal
    case 11: // RAmarl
      return "RA";
    case 6: // FOmarl
    case 7: // FOnewm
    case 8: // FOnewearl
    case 10: // FOmar
      return "FO";
    default:
      return "";
  }
}

static uint16_t spire_mag_word(const SpireMagBytes& b, size_t which) { // which 0-3: DEF, POW, DEX, MIND
  return b[4 + which * 2] | (b[5 + which * 2] << 8);
}

static void spire_mag_set_word(SpireMagBytes& b, size_t which, uint16_t v) {
  v &= 0x7FFE;
  b[4 + which * 2] = v & 0xFF;
  b[5 + which * 2] = v >> 8;
}

uint32_t spire_mag_level_of(const SpireMagBytes& bytes) {
  uint32_t ret = 0;
  for (size_t z = 0; z < 4; z++) {
    ret += spire_mag_word(bytes, z) / 100;
  }
  return ret;
}

SpireMagBytes spire_mag_row_bytes(const SpireMagRow& row) {
  SpireMagBytes ret{};
  ret[0] = 0x02;
  ret[1] = row.id;
  ret[3] = 0; // photon blasts
  spire_mag_set_word(ret, 0, row.def * 100);
  spire_mag_set_word(ret, 1, row.pow * 100);
  spire_mag_set_word(ret, 2, row.dex * 100);
  spire_mag_set_word(ret, 3, row.mind * 100);
  ret[2] = spire_mag_level_of(ret);
  return ret;
}

uint32_t spire_mag_expected_step(size_t num_steps, uint32_t start_step, size_t stored_bosses, bool exam_new_boss) {
  if (!num_steps) {
    return 0;
  }
  uint64_t ret = static_cast<uint64_t>(start_step) + stored_bosses + (exam_new_boss ? 1 : 0);
  return std::min<uint64_t>(ret, num_steps - 1);
}

uint32_t spire_mag_start_step(const SpireMagTable& table, uint8_t k) {
  return std::count_if(table.boss_rooms.begin(), table.boss_rooms.end(), [&](uint8_t b) { return b < k; });
}

SpireMagBytes spire_mag_step_bytes(const SpireMagLine& line, uint32_t step, uint32_t start_bonus,
    const std::optional<SpireMagBytes>& last_mirrored) {
  if (line.steps.empty()) {
    throw std::logic_error("run mag line has no steps");
  }
  const auto& step_row = line.steps[std::min<size_t>(step, line.steps.size() - 1)];
  auto ret = spire_mag_row_bytes(step_row);
  // the highest floor the start bonus reaches
  const SpireMagFloor* floor = nullptr;
  for (const auto& f : line.floors) {
    if (start_bonus >= f.min_bonus) {
      floor = &f;
    }
  }
  if (floor) {
    auto floor_bytes = spire_mag_row_bytes(floor->row);
    for (size_t z = 0; z < 4; z++) {
      spire_mag_set_word(ret, z, std::max(spire_mag_word(ret, z), spire_mag_word(floor_bytes, z)));
    }
    if (spire_mag_level_of(floor_bytes) > spire_mag_level_of(spire_mag_row_bytes(step_row))) {
      ret[1] = floor->row.id;
    }
  }
  if (last_mirrored) {
    auto merged = ret;
    for (size_t z = 0; z < 4; z++) {
      spire_mag_set_word(merged, z, std::max(spire_mag_word(ret, z), spire_mag_word(*last_mirrored, z)));
    }
    if (spire_mag_level_of(merged) <= 200) {
      ret = merged;
    } else {
      for (size_t z = 0; z < 4; z++) {
        spire_mag_set_word(ret, z, spire_mag_word(*last_mirrored, z));
      }
    }
  }
  ret[2] = spire_mag_level_of(ret);
  ret[3] = 0;
  return ret;
}

SpireRunMagRefusal spire_run_mag_refusal(const SpireRunMagState& st) {
  using R = SpireRunMagRefusal;
  if (!st.gc_v3) {
    return R::NOT_GC_V3;
  }
  if (!st.spire_game) {
    return R::NO_SPIRE_GAME;
  }
  if (!st.has_overlay) {
    return R::NO_OVERLAY;
  }
  if (st.restore_pending) {
    return R::RESTORE_PENDING;
  }
  if (!st.run_in_progress) {
    return R::MARKER;
  }
  if (st.base_returned) {
    return R::BASE_RETURNED;
  }
  if (st.run_again) {
    return R::RUN_AGAIN;
  }
  if (st.refreshed_by_61) {
    return R::REFRESHED_BY_61;
  }
  if (!st.enabled) {
    return R::DISABLED;
  }
  if (!st.has_table) {
    return R::NO_TABLE;
  }
  if (st.overlay_mags > 1) {
    return R::TWO_MAGS;
  }
  return R::NONE;
}

const char* spire_run_mag_refusal_name(SpireRunMagRefusal r) {
  switch (r) {
    case SpireRunMagRefusal::NONE:
      return "NONE";
    case SpireRunMagRefusal::NOT_GC_V3:
      return "NOT_GC_V3";
    case SpireRunMagRefusal::NO_SPIRE_GAME:
      return "NO_SPIRE_GAME";
    case SpireRunMagRefusal::NO_OVERLAY:
      return "NO_OVERLAY";
    case SpireRunMagRefusal::RESTORE_PENDING:
      return "RESTORE_PENDING";
    case SpireRunMagRefusal::MARKER:
      return "MARKER";
    case SpireRunMagRefusal::BASE_RETURNED:
      return "BASE_RETURNED";
    case SpireRunMagRefusal::RUN_AGAIN:
      return "RUN_AGAIN";
    case SpireRunMagRefusal::REFRESHED_BY_61:
      return "REFRESHED_BY_61";
    case SpireRunMagRefusal::DISABLED:
      return "DISABLED";
    case SpireRunMagRefusal::NO_TABLE:
      return "NO_TABLE";
    case SpireRunMagRefusal::TWO_MAGS:
      return "TWO_MAGS";
  }
  return "?";
}

uint32_t spire_run_mag_answer(uint32_t tag, uint8_t old_type, bool result) {
  return tag | (static_cast<uint32_t>(old_type) << 8) | (result ? 1 : 0);
}

SpireRunMagDecision spire_run_mag_decide(SpireRunMagLedger& ledger, const SpireMagLine& line, uint8_t sub, uint8_t k,
    const std::set<uint8_t>& server_boss_rooms, uint32_t start_bonus) {
  SpireRunMagDecision ret;
  ledger.boss_rooms.insert(server_boss_rooms.begin(), server_boss_rooms.end());
  bool exam_new_boss = (sub == SPIRE_RUN_MAG_EXAM) && k && !ledger.boss_rooms.count(k);
  ret.expected_step = spire_mag_expected_step(line.steps.size(), ledger.start_step, ledger.boss_rooms.size(),
      exam_new_boss);
  if ((sub == SPIRE_RUN_MAG_EXAM) && k) {
    ledger.boss_rooms.emplace(k);
  }
  if (ledger.issued_step < static_cast<int32_t>(ret.expected_step)) {
    ret.result = true;
    ret.bytes = spire_mag_step_bytes(line, ret.expected_step, start_bonus, ledger.last_mirrored);
    // B-REGIE-14-10: the answer issues the step; a RESULT 0 gives it back. An earlier offer whose RESULT never came
    // stays issued (the client holds that mag), so it is what a RESULT 0 of this answer returns to.
    ledger.step_before_offer = ledger.issued_step;
    ledger.issued_step = ret.expected_step;
    ledger.offered_step = ret.expected_step;
    ledger.offered = ret.bytes;
  }
  return ret;
}

SpireRunMagHandled spire_run_mag_handle(SpireRunMagLedger& ledger, const SpireMagTable& table, const SpireMagLine& line,
    uint8_t sub, uint8_t k, const std::vector<SpireMagBytes>& overlay_mags, const std::set<uint8_t>& server_boss_rooms,
    uint32_t start_bonus) {
  SpireRunMagHandled ret;
  if (ledger.issued_step < 0 && ledger.boss_rooms.empty() && !ledger.offered) {
    ledger.start_step = spire_mag_start_step(table, k);
  }
  ret.followed_mirror = spire_run_mag_follow_mirror(ledger, overlay_mags);
  if (overlay_mags.size() == 1) {
    ret.old_type = overlay_mags[0][1];
  }
  ret.decision = spire_run_mag_decide(ledger, line, sub, k, server_boss_rooms, start_bonus);
  ret.answer = spire_run_mag_answer(SPIRE_RUN_MAG_TAG, ret.old_type, ret.decision.result);
  return ret;
}

bool spire_run_mag_savestate_follow(
    SpireRunMagLedger* ledger, bool refreshed_by_61, const std::vector<SpireMagBytes>& overlay_mags) {
  return ledger && !refreshed_by_61 && spire_run_mag_follow_mirror(*ledger, overlay_mags);
}

bool spire_run_mag_result(SpireRunMagLedger& ledger, bool created) {
  if (!ledger.offered) {
    return false;
  }
  if (created) {
    ledger.last_mirrored = ledger.offered;
  } else {
    ledger.issued_step = ledger.step_before_offer;
  }
  ledger.offered.reset();
  ledger.offered_step = -1;
  ledger.step_before_offer = -1;
  return true;
}

bool spire_run_mag_follow_mirror(SpireRunMagLedger& ledger, const std::vector<SpireMagBytes>& overlay_mags) {
  if ((overlay_mags.size() == 1) && ledger.offered && (overlay_mags[0] == *ledger.offered)) {
    // the offered mag was created and its RESULT lost: confirmed as by a RESULT 1
    ledger.last_mirrored = ledger.offered;
    ledger.offered.reset();
    ledger.offered_step = -1;
    ledger.step_before_offer = -1;
    return true;
  }
  if ((overlay_mags.size() != 1) || (ledger.issued_step < 0) || !ledger.last_mirrored) {
    return false;
  }
  const auto& mag = overlay_mags[0];
  if ((mag == *ledger.last_mirrored) || (spire_mag_level_of(mag) < spire_mag_level_of(*ledger.last_mirrored))) {
    return false;
  }
  ledger.last_mirrored = mag;
  return true;
}

std::string spire_run_mag_bytes_hex(const SpireMagBytes& bytes) {
  std::string ret;
  for (uint8_t b : bytes) {
    ret += std::format("{:02X}", b);
  }
  return ret;
}

SpireMagBytes spire_run_mag_bytes_from_hex(const std::string& hex) {
  if (hex.size() != 24) {
    throw std::runtime_error(std::format("run mag bytes {} are not 24 hex digits", hex));
  }
  SpireMagBytes ret{};
  for (size_t z = 0; z < 12; z++) {
    ret[z] = std::stoul(hex.substr(z * 2, 2), nullptr, 16);
  }
  return ret;
}

std::string spire_run_mag_evolve_text(const std::string& sega_name) {
  return std::format("Your mag evolves: {}. Equip it: Item Pack > Mag.", sega_name);
}

bool spire_kit_use_text_due(bool start, uint32_t hp_mat_level, uint32_t atp_grind_level, bool resumed, bool already_sent) {
  return start && (hp_mat_level || atp_grind_level) && !resumed && !already_sent;
}

bool spire_boss_burst_text_due(uint8_t room_k, const std::set<uint8_t>& boss_rooms, std::set<uint8_t>& sent_rooms) {
  if ((room_k < SPIRE_BOSS_BURST_FROM_K) || !boss_rooms.count(room_k)) {
    return false;
  }
  return sent_rooms.emplace(room_k).second;
}

bool spire_run_mag_command_refreshes(uint16_t command) {
  return command == 0x61;
}
