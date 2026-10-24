#include "SpireDraw.hh"

#include <phosg/Filesystem.hh>
#include <phosg/JSON.hh>
#include <format>
#include <stdexcept>

// Mirrors rewards.py (hook draw_rewards, draw_one, valid, unpack_tags, weights_syn/var) step by step, including the
// order of the get_random calls, so a draw on the server follows the same rules as the quest's own.

static constexpr uint32_t ID_BITS = 6;
static constexpr uint32_t ID_MASK = 0x3F;
static constexpr uint32_t CURSE_BASE_SHIFT = 24;
static constexpr uint32_t CURSE_R12 = 1;
static constexpr uint32_t CURSE_R13 = 2;
static constexpr uint32_t CURSE_GLASS = 3;
static constexpr uint32_t NUM_CURSE_CODES = 3;
static constexpr uint32_t ARCHETYPE_FO = 2;
static constexpr uint32_t RACE_ANDROID = 2;
static constexpr uint32_t BONUS_SHIFT = 8;

SpireDrawTable SpireDrawTable::from_json_file(const std::string& filename) {
  auto json = phosg::JSON::parse(phosg::load_file(filename));
  SpireDrawTable t;
  t.num_tags = json.get_int("num_tags");
  t.rare_base = json.get_int("rare_base");
  t.rare_per_k = json.get_int("rare_per_k");
  t.uncommon_below = json.get_int("uncommon_below");
  t.curse_pct = json.get_int("curse_pct");
  t.max_attempts = json.get_int("max_attempts");
  t.blood_pact_bonus = json.get_int("blood_pact_bonus");
  t.relic_bit_r3 = json.get_int("relic_bit_r3");
  t.relic_bit_r13 = json.get_int("relic_bit_r13");
  t.relic_bit_r14 = json.get_int("relic_bit_r14");
  for (const auto& a_json : json.get_list("archetypes")) {
    SpireDrawArchetype a;
    for (const auto& e_json : a_json->get_list("entries")) {
      SpireDrawEntry e;
      e.cap = e_json->get_int("cap");
      e.vanaf_k = e_json->get_int("vanaf_k");
      e.android_ok = e_json->get_bool("android_ok");
      e.rare = e_json->get_bool("rare");
      e.own_curse = e_json->get_bool("own_curse");
      e.glass_ok = e_json->get_bool("glass_ok");
      a.entries.emplace_back(e);
    }
    for (const auto& b : a_json->get_list("kbands")) {
      a.kbands.emplace_back(b->as_int());
    }
    for (const auto& [band_str, rows_json] : a_json->get_dict("rows")) {
      auto& rows = a.rows[std::stoul(band_str)];
      for (const auto& row_json : rows_json->as_list()) {
        auto& row = rows.emplace_back();
        for (const auto& m : row_json->as_list()) {
          row.emplace_back(m->as_int());
        }
      }
    }
    for (const auto& z : a_json->get_list("slot_c_zero_tags")) {
      a.slot_c_zero_tags.emplace_back(z->as_int());
    }
    // Audit Q2-2: the picks registers hold SPIRE_DRAW_MAX_ENTRIES pool indexes, so a larger pool would be cut off
    // silently; refuse it here instead
    if (a.entries.size() > SPIRE_DRAW_MAX_ENTRIES) {
      throw std::runtime_error(std::format("archetype has {} entries; at most {} fit in the picks registers",
          a.entries.size(), SPIRE_DRAW_MAX_ENTRIES));
    }
    for (uint8_t z : a.slot_c_zero_tags) {
      if (z >= t.num_tags) {
        throw std::runtime_error("slot_c_zero_tags entry out of range");
      }
    }
    if (a.kbands.empty() || (a.kbands[0] != 1)) {
      throw std::runtime_error("archetype without k band 1");
    }
    for (uint8_t b : a.kbands) {
      if (!a.rows.count(b) || (a.rows.at(b).size() != 3 * t.num_tags + 1u)) {
        throw std::runtime_error("k band rows incomplete");
      }
      for (const auto& row : a.rows.at(b)) {
        if (row.empty()) {
          throw std::runtime_error("empty member row");
        }
        for (uint8_t m : row) {
          if (m >= a.entries.size()) {
            throw std::runtime_error("member out of range");
          }
        }
      }
    }
    t.archetypes.emplace_back(std::move(a));
  }
  if (t.archetypes.size() != 3 || t.num_tags == 0 || t.num_tags > 10) {
    throw std::runtime_error("invalid draw table");
  }
  return t;
}

SpireDrawInput spire_draw_input_from_values(const std::vector<uint32_t>& v) {
  if (v.size() != SPIRE_DRAW_NUM_INPUTS) {
    throw std::runtime_error("wrong number of draw inputs");
  }
  SpireDrawInput in;
  in.archetype = v[0];
  in.race = v[1];
  in.k = v[2];
  in.path = v[3];
  in.choice = v[4];
  in.tag_a = v[5];
  in.tag_b = v[6];
  in.lives = v[7];
  in.glass_item_id = v[8];
  in.relics = v[9];
  in.tags = v[10];
  in.state = v[11];
  for (size_t z = 0; z < 4; z++) {
    in.picks[z] = v[12 + z];
  }
  return in;
}

namespace {

struct Draw {
  const SpireDrawTable& t;
  const SpireDrawArchetype& a;
  const SpireDrawInput& in;
  const std::function<uint32_t(uint32_t)>& rnd;
  uint32_t drawn = 0;

  // valid: 1 when the entry is already on the board or at its cap (r71)
  bool already_or_capped(uint32_t id, uint32_t cap) const {
    for (uint32_t shift = 0; shift < 4 * ID_BITS; shift += ID_BITS) {
      if (((drawn >> shift) & ID_MASK) == id) {
        return true;
      }
    }
    uint32_t index = id - 1;
    uint32_t count = (in.picks[index / 10] >> ((index % 10) * 3)) & 7;
    return count >= cap;
  }

  void record(uint32_t slot, uint32_t id, uint32_t curse) {
    drawn |= id << ((slot - 2) * ID_BITS);
    drawn |= curse << ((slot - 2) * 2 + CURSE_BASE_SHIFT);
  }

  uint8_t band() const {
    uint8_t ret = a.kbands[0];
    for (uint8_t b : a.kbands) {
      if (in.k >= b) {
        ret = b;
      }
    }
    return ret;
  }

  void draw_one(uint32_t slot, uint32_t direction, bool force_rare) {
    uint32_t row; // r64
    uint32_t forced_tag = 0;
    if (slot == 3) {
      if (in.choice == 1) {
        forced_tag = in.tag_a;
      } else if (in.choice == 2) {
        forced_tag = in.tag_b;
      }
    }
    if (forced_tag != 0) {
      row = (forced_tag - 1) * 3;
    } else {
      std::vector<uint32_t> w(t.num_tags);
      for (size_t i = 0; i < t.num_tags; i++) {
        uint32_t c = (in.tags >> (3 * i)) & 7;
        w[i] = (direction == 1) ? (7 - c - c) : (c * 2 + 1);
      }
      if (direction == 1) {
        for (uint8_t z : a.slot_c_zero_tags) {
          w[z] = 0;
        }
      }
      uint32_t total = 0;
      for (uint32_t x : w) {
        total += x;
      }
      uint32_t r = rnd(total);
      row = 3 * (t.num_tags - 1);
      for (size_t i = 0; i + 1 < t.num_tags; i++) {
        if (r < w[i]) {
          row = 3 * i;
          break;
        }
        r -= w[i];
      }
    }

    uint32_t r70 = rnd(100);
    uint32_t threshold = in.k * t.rare_per_k + t.rare_base + ((in.state >> BONUS_SHIFT) & 0xFF) +
        ((in.relics & t.relic_bit_r3) >> 2) * t.blood_pact_bonus;
    uint32_t z;
    if (force_rare || (r70 < threshold)) {
      z = 2;
    } else if (r70 < t.uncommon_below) {
      z = 1;
    } else {
      z = 0;
    }
    row += z;

    uint32_t curse = 0; // r65
    if (z == 2) {
      uint32_t c = rnd(100);
      if ((c < t.curse_pct) && !(in.relics & t.relic_bit_r13) && (in.glass_item_id == 0)) {
        curse = rnd(NUM_CURSE_CODES) + 1;
        if ((curse == CURSE_R12) && (in.lives < 2)) {
          curse = 0; // R12 costs a life: never offered at 1 life
        } else if ((curse == CURSE_R13) && (in.archetype == ARCHETYPE_FO)) {
          curse = 0; // R13 does nothing to a Force
        }
      }
    }

    const auto& rows = a.rows.at(band());
    uint32_t attempts = 0; // r61
    bool sequential = false; // r66
    uint32_t seq = 0; // r67
    uint32_t id; // r60
    auto pick = [&]() -> uint32_t {
      const auto& members = rows.at(row);
      if (members.size() == 1) {
        return members[0] + 1;
      }
      return members[rnd(members.size())] + 1;
    };
    id = pick();
    for (;;) {
      uint32_t index = id - 1;
      if (index >= a.entries.size()) {
        record(slot, id, curse); // sequential mode ran past the table: the quest records it like this too
        return;
      }
      const auto& e = a.entries[index];
      bool invalid = (in.k < e.vanaf_k) || (!e.android_ok && (in.race == RACE_ANDROID)) ||
          already_or_capped(id, e.cap);
      if (!invalid) {
        if (e.rare) {
          if (e.own_curse) {
            curse = 0;
          } else if (!e.glass_ok && (curse == CURSE_GLASS)) {
            curse = 0;
          }
        }
        record(slot, id, curse);
        return;
      }
      if (sequential) {
        seq++;
        id = seq + 1;
        continue;
      }
      attempts++;
      if (attempts < t.max_attempts) {
        id = pick();
        continue;
      }
      row = 3 * t.num_tags;
      if (attempts < 2 * t.max_attempts) {
        id = pick();
        continue;
      }
      sequential = true;
      seq = 0;
      id = 1;
    }
  }
};

} // namespace

uint32_t spire_draw(const SpireDrawTable& table, const SpireDrawInput& in, const std::function<uint32_t(uint32_t)>& rnd) {
  if (in.archetype >= table.archetypes.size()) {
    throw std::runtime_error("archetype out of range");
  }
  Draw d{table, table.archetypes[in.archetype], in, rnd};
  bool force_rare = (in.path == 1); // R15 Elite Hunter: slot A after an elite room
  d.draw_one(3, 0, force_rare);
  d.draw_one(4, 0, false);
  d.draw_one(5, 1, false);
  if (in.relics & table.relic_bit_r14) { // R14 More Eyes: a fourth reward on slot 2
    d.draw_one(2, 0, false);
  }
  return d.drawn;
}
