#include "CrashValueFilter.hh"

#include <format>

#include "Client.hh"

const std::vector<CrashValueRule>& crash_value_rules() {
  // Offsets follow G_NPCControl_6x69 (CommandFormats.hh): header 0, param1 4, param2 6, command 8, param3 10
  static const std::vector<CrashValueRule> rules = {
      // Commands 0 (create follower NPC) and 3 (create attacker NPC): param2 is npc_entity_id. An NPC takes one of the
      // four player slots; q903 sent entity 13 (template and slot swapped in npc_crp_id) and both Wiis froze.
      {0x69, "npc_entity_id", 6, 2, 4, 0xFFFF, 8, 2, (1 << 0) | (1 << 3),
          "NPC created outside the four player slots",
          "pso-roguelike new-features/roguelike/handovers/2026-10-25-regie-handover-1115.md:44 (q903 freeze 13:35)"},
      // Command 1 (stop acting): param1 is npc_entity_id, the same slot range
      {0x69, "npc_entity_id", 4, 2, 4, 0xFFFF, 8, 2, (1 << 1),
          "NPC slot outside the four player slots",
          "pso-roguelike new-features/roguelike/handovers/2026-10-25-regie-handover-1115.md:44 (same field, command 1)"},
  };
  return rules;
}

static uint32_t read_le(const uint8_t* bytes, uint8_t width) {
  uint32_t ret = 0;
  for (uint8_t z = 0; z < width; z++) {
    ret |= static_cast<uint32_t>(bytes[z]) << (8 * z);
  }
  return ret;
}

ssize_t crash_value_rule_violated(const std::vector<CrashValueRule>& rules, uint8_t final_subcommand, const void* data,
    size_t size, uint32_t* value) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  for (size_t z = 0; z < rules.size(); z++) {
    const auto& rule = rules[z];
    if (rule.subcommand != final_subcommand) {
      continue;
    }
    if ((rule.field_offset + rule.field_width > size) ||
        (rule.selector_width && (rule.selector_offset + rule.selector_width > size))) {
      continue;
    }
    if (rule.selector_width) {
      uint32_t selector = read_le(bytes + rule.selector_offset, rule.selector_width);
      if ((selector >= 32) || !(rule.selector_values & (1u << selector))) {
        continue;
      }
    }
    uint32_t v = read_le(bytes + rule.field_offset, rule.field_width);
    if ((v >= rule.bad_min) && (v <= rule.bad_max)) {
      if (value) {
        *value = v;
      }
      return z;
    }
  }
  return -1;
}

std::string crash_value_refusal_message(const CrashValueRule& rule, uint8_t client_id, uint32_t value) {
  uint32_t width_max = (rule.field_width >= 4) ? 0xFFFFFFFF : ((1u << (8 * rule.field_width)) - 1);
  std::string condition = (rule.bad_max >= width_max)
      ? std::format(">= {}", rule.bad_min)
      : std::format("in [{}, {}]", rule.bad_min, rule.bad_max);
  return std::format("Security: 6x{:02X} from C-{:X} refused: {} {} {} (client crash value)",
      rule.subcommand, client_id, rule.field_name, value, condition);
}

bool crash_value_should_log(uint64_t& logged_rules, size_t rule_index) {
  uint64_t bit = (rule_index < 64) ? (1ULL << rule_index) : (1ULL << 63);
  if (logged_rules & bit) {
    return false;
  }
  logged_rules |= bit;
  return true;
}

bool crash_value_filter_refuses(std::shared_ptr<Client> c, uint8_t final_subcommand, const void* data, size_t size) {
  const auto& rules = crash_value_rules();
  uint32_t value = 0;
  ssize_t index = crash_value_rule_violated(rules, final_subcommand, data, size, &value);
  if (index < 0) {
    return false;
  }
  if (crash_value_should_log(c->crash_value_rules_logged, index)) {
    c->log.warning_f("{} [{}; source {}]", crash_value_refusal_message(rules[index], c->lobby_client_id, value),
        rules[index].reason, rules[index].source);
  }
  return true;
}
